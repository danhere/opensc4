# Fonts

The text engine at 0x00893D2C to 0x008AE76D: a Maxis framework layer (a COM director, the font system, the font, a glyph blitter) on top of **T2K**, the font scaler Sampo Kaasila wrote and Bitstream sold as Font Fusion. It is the "Font engine" line of [uncharted-code.md](uncharted-code.md), which gave it as 0x00894000 to 0x008A7FFF; the code runs a little lower and higher than that.

Everything here was read from the disassembly with Ghidra; the glyph blitter (f4), the director (f1), the T2K holder (f5), the font system's own functions (f2a), its tables (f2b) and the font's object and properties (f3a) and its containers (f3b) are ours and were checked in the game ("f4: the glyph blitter is ours", "f1 and f5", "f2a", "f2b", "f3a" and "f3b" below). Names are ours unless they come from T2K. The addresses the doc relies on are in [data/addresses/font.toml](../data/addresses/font.toml) (f1, f2a, f2b, f3a, f3b, f4 and f5 hooked) and [data/names-641.txt](../data/names-641.txt). "Ran" counts come from the 17 coverage files on TurboToaster ([dev-environment.md](dev-environment.md), "Coverage"); code that runs only while the game starts is never recorded there.

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
| 0x20673960 | 0x70 | 0x00AC5488 (25 slots), 0x00AC5454 (+4, 13) | 0x00895268 | **Font system.** The faces: the game gives it its Fonts folder (slot 15 scans a folder for `*.tt?`, `*.pfb`, `*.mxf`, `*.otf`, slot 14 adds a file, trying Windows's Fonts folder for a path that is not there), an alias table (default faces Times New Roman, Arial, Courier New, Lucida Console in slot 3; the game's `FontStyle.ini` reader, 0x0044D810, is outside the range), GetFont (slot 11, 0x00894ADF: face list, size, style, reuse) which picks a face, and slot 10 (0x00895A1C), which makes the font object for it. Interface 0x00673960; the framework caches it at 0x00B628D4 (ui.toml's `SC4::Singleton913D22`) |
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
| f1 | 0x00895F36 to 0x0089604D, less 0x0089603D | 8 | 1 | The director, its three factories, the static getter, slot 4 (the 1 is the font factory). **Ours** (`framework.font.director`, below) |
| f2 | 0x00893D2C to 0x00895F35 | 85 | 7 | The font system: folder scans, face table, aliases, GetFont, and the STLport map and list code for them. Most of it runs while the game starts, before the meter is armed; the 7 are QueryInterface, Release, GetFont, slot 10 and three helpers. **f2a**, the system's own 32, and **f2b**, the 51 STLport functions and two folded setters, are **ours** (`framework.font.system.*`, below) |
| f3 | 0x00896254 to 0x0089A577, less f4 and f5 | 113 | 59 | The font: layout, measuring, line breaking, drawing, glyph cache and sheet, encodings, Thai, sample characters per language (slot 25). **f3a**, 38 of them (the object, its properties, the blitter's hand-over), and **f3b**, 36 (its STLport containers), are **ours** (`framework.font.font.*`, below); the rest is the game's ("f3: what is left") |
| f4 | 0x00896053 to 0x00896243, 0x00896E63, 0x00897246, 0x00897722 | 11 | 4 | The glyph blitter and its blend tables. **Ours** (`framework.font.blitter.draw` and `.object`, below) |
| f5 | 0x0089603D, 0x00896D2F, 0x00896DDD, 0x00896E4A, 0x008976A4, 0x00897FED, 0x0089801E, 0x0089883F, 0x008988F1, 0x00898C6B, 0x00898CD9, 0x00898EB2 | 12 | 11 | The T2K holder: open, close, the file stream and T2K's read callback, names and styles, glyph names. **Ours** (`framework.font.t2k.*`, below; the first map counted 13 over ranges that also held some of the font's functions) |
| f6 | 0x0089A578 to 0x008AE76D | 321 | 142 | **T2K** (third-party) |

Inside f6, by the anchors above: the memory manager and its stream (0x0089A578 to 0x0089AB3F), the sfnt reader and table loaders (0x0089B4C1 to 0x0089D56A), the API the Maxis side calls (0x0089D662 to 0x0089F7C6: new sfnt, new scaler, transform 0x0089DC9A, render glyph 0x0089F23B, char to glyph 0x0089F4B2, unwrap `.pfb` 0x0089F7C6), the Type 1 parser (about 0x0089FAF7 to 0x008A1834; it ran, so some of the game's fonts are Type 1), then the scaler, hinting and rasterizer to 0x008AE76D. The largest function, 0x008A489C (8.5 KB), never ran.

Only the Maxis side calls T2K: every call into 0x0089A578 to 0x008AE76D from outside it comes from f5 or the font's slots 3, 11, 25, 27, 30 and 61.

### Calls from outside

The range has 112 entry points called or referenced from outside 0x00894000 to 0x008A7FFF. 16 of them are T2K functions called from T2K's own code past 0x008A8000; most of the rest are slots of the vtables above (references from 0x00AC54xx to 0x00AC56xx). The direct calls from game code are few and not about text: STLport helpers the linker folded into this range and other code shares (0x0089408D list push_back, 0x0089461C map erase, 0x00897C8D list insert, 0x00894585, 0x00894843, 0x0089508C, 0x00897D22; those of f2 are ours in `framework.font.system.shared`, below), the director getter (0x004600EE) and two static teardowns. Text reaches the fonts through COM: the font system from the framework, fonts from GetFont, then the font's slots.

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

Of these, the f4 blitter, GetFont (f2a) and SetBlitter (f3a) are ours (as translations, not yet as crisp-text hooks); f1 and f5, which open the faces, are ours too but are not drawing.

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

## f1 and f5: the director and the T2K holder are ours

The 8 functions of f1 and the 12 of f5 are translated by hand ([src/core/font_director.cpp](../src/core/font_director.cpp), [src/core/font_t2k_holder.cpp](../src/core/font_t2k_holder.cpp)), each a detour on the game's, in four groups, off by default and installed at Attach (the director is made by a static initializer and the first fonts are opened while the game starts):

| Group | Functions | What |
|---|---:|---|
| `framework.font.director` | 8 | the factories of the font system 0x00895F36, the font 0x00895F4C and the blitter 0x00895F65; the director's constructor 0x00895F7B, its ID (slot 13, 0x00895FC7) and start (slot 4, 0x00895FCD); the static getter 0x00896012; the constructor of the one-byte static next to it, 0x0089604D |
| `framework.font.t2k.stream` | 5 | the file stream's constructor 0x00896D2F, what is left of it 0x0089603D, its read 0x00896DDD, T2K's read callback 0x00896E4A, ReadAll 0x00898C6B |
| `framework.font.t2k.holder` | 3 | the holder's constructor 0x00897FED, Open 0x00898CD9, Close 0x008976A4 |
| `framework.font.t2k.names` | 4 | the face's name 0x0089801E, its style bits 0x0089883F, the table of glyph names 0x008988F1, the glyph by its name 0x00898EB2 |

T2K (f6) stays the game's: every call into it is made at the game's address with the game's arguments, and T2K's entry points set their own `longjmp` target, so its errors come back in the error word and never cross our frames. The callback handed to T2K is the game's address 0x00896E4A, as the game hands it. The classes' constructors, the director base (cRZCOMDllDirector), the game's file (cRZFile), string and STLport map and vector code are the game's too. Addresses: [data/addresses/font.toml](../data/addresses/font.toml) (19 entries are new there; the five names f0 put in [data/names-641.txt](../data/names-641.txt) moved over).

f5 in the first map counted 13 functions over the ranges in the Pieces table, which also hold some of the font's (GetGlyph 0x0089834C, the ramp 0x0089838C, SetScale 0x0089854E and five helpers): the functions that work on the holder or its stream are these 12, and the thirteenth could not be told from the ranges. 11 of the 12 ran in the coverage files (all but the name table, which runs once while the game starts).

### What they do

- **The holder** (0x34 bytes, the font's +0xEC): +0x00 T2K's memory handler, +0x04 the sfnt, +0x08 the scaler, +0x0C the file stream, +0x10 T2K's input stream, +0x14 a byte vector (a Type 1 face unwrapped from its `.pfb`), +0x20 a word of flags (0xA; Open adds 0x80 for TrueType and type 7, 1 for the others), +0x24 the kind (1 TrueType, 2 Type 1, 3 type 0x16, 4 type 7, 0 none), +0x28 a map of characters to glyphs found by name. The constructor clears +0x00 to +0x1F and +0x24, sets the flags and makes the map (+0x22 is not written).
- **Open** closes what is open, then makes the memory handler, the stream and T2K's input stream over it, asks T2K the font type, and for a Type 1 face reads the file whole (ReadAll), unwraps it (0x0089F7C6) and reads it again from memory; then the sfnt and the scaler, whose class words +0x74 and +0x76 become 3 and 1 and its +0x48 1. Each step stops at T2K's first error. True if there is a scaler. **Close** deletes the scaler, the sfnt, the input stream, the file stream and the memory handler, clearing each field.
- **The stream** (0x8C bytes) is the game's file and three fields: where the font starts (+0x80), how many bytes from there are scrambled (+0x84) and the key they are XORed with (+0x88). All the game's fonts are `.mxf` files of this kind: "MXFN", the scrambled length big-endian, the key, then the face. The constructor opens the file (read, existing, shared for reading) and reads that header from files longer than 9 bytes; Read seeks to start plus offset, reads and XORs what falls below the scrambled length; the callback is Read with its AL widened; ReadAll empties a vector, sizes it to the face (at most the limit) and reads it from 0.
- **GetName** asks T2K for a name by ID in American English, then in language 0, then the full name (4) in both, each try once as it is and once with the class's word +0x76 cleared, stopping at the first that gives a name; the name (UTF-16, or Windows-1252) replaces the string's text, or the string is emptied. For the family (1) it is cut at "bold" and then at "italic", for the style (2) it keeps from the first of the two; both are then trimmed. **Styles** is the scaler's two style bits (its table +0x3C, byte +0x30) or'd with "italic" (2) and "bold" (1) found in the lower-cased style name. Load (0x0089955C) calls both.
- **The glyph by name** (faces of kind 2 and 3, from RenderGlyph): the holder's map first; else the glyph name of the character from the static table (479 PostScript names from 0x00B150F0, put in the map 0x00B5C41C by 0x008988F1 the first time), then `uniXXXX`, each through T2K's name lookup and char-to-glyph; a glyph found is kept in the map.
- **The director** registers the three classes with the game's factories; its start makes the font system through its own COM object (slot 12) and gives it to the framework as a system service. The getter makes it the first time and leaves its teardown to `atexit`. The byte constructor sets the byte at 0x00B543C2 to 1.

### Quirks kept

- Close leaves the vector, the flags and the kind: Open on a holder that was open before ORs its flags onto the old ones (a TrueType face reopened over a Type 1 one has 0x8B), and the bytes of a Type 1 face stay until another Type 1 face replaces them or the holder is destroyed.
- The stream's constructor reads the "MXFN" tag over its own argument, so a failed read compares the path's pointer, byte-swapped.
- Open asks the stream its length even when the stream could not be allocated (a null stream: both crash).
- GetName looks for "bold" and "italic" in lower case only: a family name with "Bold" is not cut (Styles lower-cases the name first). For the family it erases from "bold" to the end and then from where "italic" was before that erase.
- The glyph map is keyed by the low 16 bits (0x10041 is 0x41); a character without a glyph is looked up again every time. The error word of these T2K calls is shared and never read.
- The static table's map entries have two bytes of padding the game leaves as its stack had them; ours writes 0. Nothing reads them.
- The director's start ignores the COM object it is handed and asks its own.

### Testing

**Compare mode**: the pure functions (Remaining, Styles, the director's ID, the getter once the director is made) compare their result; Read, the callback and the byte constructor keep what ours wrote aside, put the memory back, run the game's and compare the bytes. Those that make objects run the game's on scratch copies, made and taken down by the game's own constructors and destructors: the stream's constructor on a scratch stream (its three fields, whether the file is open, its length); ReadAll into a scratch vector; the holder's constructor on a scratch holder (the fields and the empty map); Open on a scratch holder given what Open keeps from before (the bytes copied, the flags and kind), compared by which objects there are, the unwrapped bytes, flags, kind, the stream's fields and what Open set in the scaler; GetName into a scratch string of the same class (without a scaler both run on the caller's string, which they only read); the glyph by name on a copy of the holder with an empty map of its own, so it looks the glyph up; the name table built by ours into a map of its own and compared entry by entry with the game's static one. While the game's side runs, our detours inside it pass through to the game's. Close, the factories, the director's constructor and its start (they free, make or register) run the game's alone, as does the getter the first time.

**`OPENSC4_TEST_FONTS=<day>`** has four more stages after f4's (same file, same ` | ft` hash):

| Stage | What |
|---|---|
| holder | the constructor in place between margins; then 31 files: the 19 faces of the game's Fonts folder, `arial`, `times`, `cour` and `consola` from Windows's, two OpenType faces, and files of our own (Courier New behind an MXFN header with 4096 bytes scrambled, MXFN headers of 5 and 10 bytes, Courier New as it is, 3000 bytes of junk, a file that is not there): each opened, its stream read at seven offsets around its ends by four counts (Read and the callback, into buffers between margins) and whole three ways (ReadAll), its style bits, its names 0 to 6 and 0x10, the family into a string that held text, the glyphs of 13 characters by name twice (kind 2 and 3), opened again over itself with another file, closed twice, named closed; destroyed by the game's destructor |
| stream | the stream's constructor alone on our six files: its fields, open or not, the reads |
| names | the name table built again, then every entry looked up and its name hashed |
| director | the three factories (vtable, AddRef, Release), the constructor in a space of our own (vtables, margins, the three classes' factories in its class table, its ID), the base's destructor; the getter, the ID by address and through the real director; the start on a mock director whose COM object makes nothing (the framework is not reached); the byte constructor on four values |

The 31 files open 25 times: the game's faces are TrueType but for the two Arta faces and Stoclet, which are Type 1 (so the `.pfb` unwrapping and the glyph by name run); the OpenType faces are none of T2K's types here.

Results, 2026-10-24:

| Check | Result |
|---|---|
| Test runs, groups off (twice) vs on (all four) | every stage hash the same: f4's six as before (strings 1CFB48AA), holder 8289E001, stream 28FDD633, names 32472A2E, director E4D5A624; `ft` E4D5A624. 25 of 31 files open (kinds `1111221111111111112111100100100`), 200 names |
| Compare mode, test | 0 divergences: readcallback 28309 calls, read 980, getname 365, readall 210, styles 117, open 116, construct 88, glyphbyname 86, remaining 35, constructstream 6, setbyte 5, id 3, get 1, buildnametable 1 |
| Compare mode, 90 s of plain play (Making Money) | 0 divergences: readcallback 6181, construct 51, open 51, getname 51, styles 51, glyphbyname 12, setbyte 1 |
| Screenshots (`run-shots.sh`, Making Money, the four groups on) vs the game | 20 of 20 within `--tolerance 32 --max-differing 1.2` (a first run of ours lost 10 captures to `shot ... FAILED` with the game running on; run again, all 20 taken) |
| Parity `ft2-r1`, 12 saves, 7 days, muted, own folder `p4env-ft2`, two jobs, A `determinism.dispatch_info`, B that and the four groups | 12 same |
| The same with `OPENSC4_TEST_FONTS=2` (`ft2-t1`) | 11 same, 1 partly; `ft` E4D5A624 in all 120 runs, both sides. Rush Hour d0000: only the first B run differed (day 4 of 7), its three retries matched A; Big City d0090 was the same after a second pair, A itself giving two outcomes. Read as the harness's noise ("Running it" in [parity.md](parity.md)); Rush Hour's four saves run again (`ft2-t2`): 4 same |

**Reached** in plain play: the getter (once, which makes the director with our constructor), the byte constructor, the font system's, the blitter's and the font's factories, and all of f5: the holder's constructor, Open, Close, the stream's constructor, Remaining, Read, the callback, ReadAll (the Type 1 faces), GetName, Styles and the glyph by name (a Type 1 face's characters), whose first call builds the name table through ours. Not reached: the director's ID and its start (the font system is made by the framework asking the factory, not by OnStart). The test reaches all 20 but the start's path that hands a system to the framework.

**Audit**: each translation was checked against the disassembly by the author and by an independent audit (every write and its order, memory read again vs kept in registers, signed, unsigned and 16-bit compares, every call's address, convention, arguments and conditions, return values, the thunks' byte counts, every `addr::` name against the address called; then compare mode's safety). No difference reachable by a caller. Left: the upper bytes of EAX where only AL is answered (OnStart, Read, ReadAll, Close, GetName: ours 0, the game's whatever the register held; every caller tests AL or ignores it); the order in which `Length() - base` reads its two sides (Length never touches the base); the pad bytes above. Fixed from the audit, in compare mode only: Open's comparison no longer reads the scaler's class when it is null, the byte constructor's divergence message says what differed, two comments. Compare-mode limits it noted: a comparison of GetName or Styles after a T2K error could report a divergence that is not one (the first run leaves the last name loaded); ours call each other directly, so the compare branches of the stream, Close and the name table run only when the game's code calls them. All checks above ran on the final build.

## f2a: the font system is ours

32 of f2's 85 functions, the font system's own (class 0x20673960), are translated by hand ([src/core/font_system.cpp](../src/core/font_system.cpp)), each a detour on the game's, in four groups, off by default and installed at Attach (the system is made and its faces found while the game starts):

| Group | Functions | What |
|---|---:|---|
| `framework.font.system.object` | 13 | the constructor 0x00895268, the destructor 0x00894F2C and the deleting destructor (slot 24, 0x0089538D), QueryInterface (slot 0, 0x00893D2C), Release (slot 2, 0x00893DD7) and its last reference 0x00893D68, Shutdown (slot 4, 0x00894F8D), SetEncoding (slot 5, 0x00893D7B), SetBlitter (slot 6, 0x00893D91), the version 0x00894F77, and the +4 vtable's thunks to QueryInterface, Release and Shutdown (0x00894F7D, 0x00894F85, 0x00895260) |
| `framework.font.system.faces` | 9 | a file's type 0x0089433C, AddFontFile (slot 14, 0x00895564), AddFontFolder (slot 15, 0x00894864), AddFont (slot 13, 0x008957D6), RemoveFace (slot 17, 0x00894A3A), ReleaseFont (slot 12, 0x00893F22), ListFaces (slot 18, 0x00893E5B), ListFonts (slot 19, 0x00893ECB), HasFace (slot 22, 0x008944B2) |
| `framework.font.system.aliases` | 6 | Init (slot 3, 0x00895E68) and its thunk 0x00895F2E, the default face of a family 0x00895DCF, AddAlias (slot 20, 0x0089595D), GetAliasFaces (slot 21, 0x00894198), ResolveAlias (slot 23, 0x0089423C) |
| `framework.font.system.getfont` | 4 | GetFont (slot 11, 0x00894ADF), MakeFont (slot 10, 0x00895A1C), GetFontFor (slot 9, 0x008942E3), SetMonoRange (slot 8, 0x008954CB) |

The main vtable 0x00AC5488 has 25 slots, not 26: the word after slot 24 is the start of the text "Lucida Console". Slot 1 (AddRef, 0x0094C47C) and slot 16 (0x0093878E) are the framework's shared functions. Slot 7 (0x00893D87, the default draw mode into +0x14) is code the linker folded with two other classes' (vtables 0x00AC2A44 and 0x00AD7310 point at it too), as is the +4 vtable's slot 4 (0x00894F6D, 52 vtables): both are in f2b's shared group (below); the +4 vtable's framework slots stay the game's. The STLport code the system keeps its tables in is f2b (below), called at the game's addresses (ours when its groups are on), as are the game's string, file (cRZFile) and folder (cRZDirectory) classes, the T2K holder (f5), the font class and the COM. Addresses: [data/addresses/font.toml](../data/addresses/font.toml) (30 entries are new there; GZFontSystem::Construct and ::GetFont were there).

### What they do

- **The object** (0x70 bytes): +0x08 a byte Init sets and Shutdown clears, +0x0C the count, +0x10 the class, +0x14 the draw mode fonts get by default, +0x18 and +0x1C the sizes drawn in mode 1, +0x20 the encoding fonts get (RZText's default when made), +0x24 the blitter fonts get, +0x28 a set of the faces' names in lower case, +0x34 a map of a family's code (a signed word) to its default face, +0x40 the faces, +0x4C the aliases, +0x58 the file patterns (`*.tt?`, `*.pfb`, `*.mxf`, `*.otf`, through the pointers at 0x00B13434), +0x64 a map of a face's name to its own sizes in mode 1. A face (a map of a file's path to 0x24 bytes): +0x24 a list of the fonts made of it, each with the style it was asked for, +0x28 its name, +0x3C its style bits, +0x40 its kind (1, or 2 when T2K's scaler has +0x5C), +0x44 the file's type. QueryInterface answers 1 and 0x00673960 with the system and 0x287FB697 with its +4; Release at a count of 1 or below shuts the system down and deletes it.
- **Faces.** At startup the game gives the system its Fonts folder (slot 15): the 19 `.mxf` faces. AddFontFile tries the path as given, then in Windows's Fonts folder (so a bare `arial.ttf` is found there), opens it with a T2K holder and keeps it as a face named by its family, with its style bits, kind and type (by extension, else by the first 32 bytes: `%!PS`, 00 01 00 00, `ttcf`; the game's `.mxf` faces are type 0). HasFace looks a name up in the lower-case names, then for a face of it with the low two style bits.
- **Aliases.** Init puts the default faces of families 5, 9, 6 and 10 (Times New Roman, Arial, Courier New, Lucida Console) in the families map; AddAlias adds a face's name to an alias's list; ResolveAlias gives the first of them HasFace knows.
- **GetFont** lower-cases the list, splits it at commas (at most 32 names), and renames each name the system has no face of to the alias whose list has it. Every face then scores 0 for the list's first name, 2 for a later one, 0x20 for none (with no list, 0, or 4 if its kind is not the one asked), plus 8 if its style bits are fewer than asked (bits 2 to 5 of the asked aside) and 0x10 if it has one more; the first lowest wins. With reuse, a font of that size and style in the face's list is AddRef'd and given; else slot 10 makes one. Slot 9 is GetFont without a list, the font's colours set.
- **MakeFont** (slot 10) with reuse gives a font of that size and style already in a face of that path; else it makes one through the COM (by hand if the COM gives none), gives it the path, size, style, the system's encoding and blitter (the font system's blitter, so most fonts draw through it: "Two blitters" above), draw mode 1 if the size is in the system's range or the face's own (else the default mode), and loads it; with reuse the font is kept in the faces, making a face of its path named by its family if there is none. SetMonoRange sets those ranges.

### Quirks kept

- GetAliasFaces fills `count` strings whatever the alias has: a string past the alias's names is given what lies past its vector (the test passes null there).
- MakeFont's reuse looks at every face whose path equals the asked one, ignoring case, and keeps the last match; GetFont's at the best face only. AddFont tells fonts apart by size alone (a second font of a face at the same size but another style is refused); the lists keep the style they were asked for.
- A face that slot 10 or 13 makes takes its type (+0x44) from the three words of an empty face value the game leaves as its stack had them; ours zeroes them. Nothing in the range reads the type.
- The families' map is keyed by the whole word Init passes, though the codes are small; SetFamily replaces an existing name's text in place.
- SetMonoRange keeps the first range given for a face (insert does not replace) and removes it when low is not below high, signed.
- AddFontFolder counts a file whose name has `.PFB` in capitals as well as each file added, and reads the character before the folder's text even when it is empty; AddFontFile and MakeFont, with a path that is not there and no Windows folder, go on with the path as it was.
- ReleaseFont releases a font twice when the first release leaves it one reference: the list's and the one that was left, so a caller's reference ends with it. Release at 1 or below does not count down.
- AddFontFile answers true for a file it finds in Windows's Fonts folder under a path already kept as a face: the face's insert and the name's find them there and change nothing.
- The return values the game leaves in EAX's upper bytes (QueryInterface, Init, Shutdown, RemoveFace, HasFace, ResolveAlias, AddFontFile answer only AL) are 0 in ours; SetBlitter and SetMonoRange return nothing in either.

### Testing

**Compare mode**: the version, a file's type and HasFace compare their result; QueryInterface and SetEncoding keep what ours wrote (and the count) aside, put it back and compare with the game's; ListFaces and ListFonts compare the bytes they write; GetAliasFaces and ResolveAlias assign the same texts twice and compare them. GetFont runs ours to its decision without AddRef'ing or making anything, then the game's, whose call to slot 10 is noted: a font reused must be the game's result, a font made must be asked of the same face, size, style and reuse. Init and AddFontFile run ours and the game's on two scratch systems made and taken down by the game's constructor and destructor, and compare every table, then run the game's on the system they were called for. The rest (they make or release fonts, or add to the tables) run the game's alone. While the game's side runs, our detours inside it pass through to the game's.

**`OPENSC4_TEST_FONTS=<day>`** has four more stages after the director's (same file, same ` | ft` hash), on systems of our own made by the constructor at its address:

| Stage | What |
|---|---|
| system | QueryInterface for eight IDs by address and through the +4 thunk, the count down by address, thunk and vtables, the version three ways, SetEncoding, SetBlitter with mock blitters that count their references, Init three ways, the families' faces new and replaced, Shutdown with and without Init's byte, the constructor and destructor in place between margins, the deleting destructor without freeing |
| faces | the type of the 44 files (FontPaths and 13 of our own: by extension in any case, by their first bytes, short, empty, missing); FontPaths added (some twice), names only in Windows's folder or nowhere; folders (the game's, ours, one not there); ListFaces into buffers between margins; HasFace by eight names and six styles; fonts made on another system added (to their face, same size again, another size, none, a path that makes a face); ListFonts; ReleaseFont; RemoveFace |
| aliases | nine aliases added; GetAliasFaces for six aliases at counts 0 to 5 (null strings past their names, one skipped); ResolveAlias |
| getfont | the sizes in mode 1 (the system's, a face's twice, removed, never there); GetFont over 12 face lists (commas, aliases, unknown names, an empty list, 41 names) by kinds 0 and 2, sizes 0, 10, 16, styles 0, 1, 3, 0x7E, reuse off and on; slot 9 by kind, style and reuse; slot 10 on every face's path, a bare name, a file that does not load and one not there, by size, style and reuse; GetFont on the game's own system without reuse |

Each font goes into the hash by its path, size, style, kind, draw mode and encoding and by the order it was first seen in (so a font reused is told from one made). The game's `.mxf` faces are type 0, the Windows `.ttf` faces 1 and the OpenType ones 4 (`types 00000000000000000001111440001112140012440000`).

A mutation check (HasFace's style bits cut to one, GetAliasFaces' count not capped, GetFont's two style scores swapped, the version one more) changed the hash at the stage it should each time, and not before.

Results, 2026-10-25:

| Check | Result |
|---|---|
| Test runs, groups off (twice) vs on (the four), and with every font group on | every stage hash the same: the ten before as with f1 and f5 (director E4D5A624), system A4C99A12, faces DECCE56C, aliases 4948FEAA, getfont E7A84ACB; `ft` E7A84ACB. 384 fonts got, 192 none, 324 different |
| Compare mode, test | 0 divergences: getfont 699 calls, filetype 158, addfontfile 152, hasface 54, getaliasfaces 36, queryinterface 23, init 9, version 7, resolvealias 6, listfaces 5, listfonts 4, setencoding 4 |
| Compare mode, 90 s of plain play (Making Money) | 0 divergences: getfont 1109, queryinterface 1022, addfontfile 19, filetype 19, version 4, init 1 |
| Screenshots (`run-shots.sh`, Making Money, the four groups on) vs the game | 20 of 20 within `--tolerance 32 --max-differing 1.2` |
| Parity `ft3-r1`, 12 saves, 7 days, muted, own folder `p4env-ft3`, two jobs, A `determinism.dispatch_info`, B that and the four groups | 9 same, 3 partly (Making Money d0365, Big City d0365, Rush Hour d0090): in each only the first B run (and once the second) differed, on the advisors' hash (token 209), and the retries matched A; Rush Hour d0090's A itself gave two outcomes. The three run again (`ft3-r2`): 3 same |
| The same with `OPENSC4_TEST_FONTS=2` (`ft3-t1`) | 11 same, 1 partly (Big City d0000: the first B run differed on token 209, its three retries matched A); `ft` E7A84ACB in all 30 runs, both sides. Big City d0000 run again (`ft3-t2`): same |

The advisors' hash differing in a first B run only, with the retries matching, is what the f1 and f5 parity runs saw too (token 209 in `ft2-r1` and `ft2-t1`): the harness's noise ("Running it" in [parity.md](parity.md)).

**Reached** in plain play: the constructor, QueryInterface and its thunk, Release and its thunk, the version, Init (through its thunk) and the families, SetBlitter, AddFontFolder (the game's Fonts folder), AddFontFile (19 faces) and the file type, AddAlias, GetFont and MakeFont; and at exit Shutdown (through its thunk), the last reference, the deleting destructor and the destructor. Not reached: SetEncoding, SetMonoRange, slot 9, AddFont, ReleaseFont, RemoveFace, ListFaces, ListFonts, HasFace, GetAliasFaces, ResolveAlias. The test reaches all 32.

**Audit**: each translation was checked against the disassembly by the author and by an independent audit (every write and its order, signed and unsigned compares, byte and word tests, what is read again and what kept, every call's address, convention, arguments and `ret`, the thunks' byte counts, temporaries made and destroyed in the game's order, every `addr::` name; then compare mode's safety). No difference reachable by a caller. Left: the type of a face made by slot 10 or 13 (above); the upper bytes of EAX where only AL is answered and the void slots' EAX; the game's statically linked `memset` and `strncpy` in ListFaces are the CRT's in ours; the import `GetWindowsDirectoryA` called directly (the game calls it through its import table, 0x00A8010C). Fixed from the audit, in compare mode only: QueryInterface no longer reads the caller's pointer for an ID neither side answers. A compare-mode limit it noted: GetAliasFaces and ResolveAlias write the caller's strings twice, so a caller passing the same string as the alias and an answer would see the game's run look up the text ours wrote (no such caller is known). All checks above ran on the final build but the first parity batch (`ft3-r1`, before the QueryInterface fix, which changes only compare mode).

## f2b: the font system's tables are ours

The other 53 of f2's 85 functions, the STLport map, list and vector code the linker put in the font system's range and two one-line setters folded there, are translated by hand ([src/core/font_system_tables.cpp](../src/core/font_system_tables.cpp)), each a detour on the game's, in two groups, off by default and installed at Attach:

| Group | Functions | What |
|---|---:|---|
| `framework.font.system.tables` | 43 | only the font system calls them. The faces' map: its header 0x00893E2F, initialiser 0x00893F9C, constructor 0x0089505A, destructor 0x00894DC7, clear 0x00894784, erase of a subtree 0x00894657 and of a node 0x008945E1, insert 0x008953A9 and the insert under a parent 0x00895136, its node 0x00894EEA; the pair of a path and a face made 0x00894740, copied 0x008946F6, destroyed 0x00894172; the face made 0x00894144, copied 0x008945AA, destroyed 0x0089415F; a face's list copied 0x008940EB. The aliases' map: the same nine (0x00893E45, 0x00893FC6, 0x00895073, 0x00894DDE, 0x008947AD, 0x0089468C, 0x0089543A, 0x008951CB, 0x00894F0B) and its pair (0x00894762, 0x0089471B, 0x00894185). The sizes by face: destructor 0x00894DF5, clear 0x008947D6, erase of a subtree 0x008946C1, of a range 0x008947FF and of a key 0x00894E0C, the bounds 0x00893FF0 and 0x00894028, the equal range 0x0089411A, insert 0x008950A5 and the insert under a parent 0x00894E55, the pair copied 0x0089448D. The byte vector a file's type reads into: made 0x008940A7, allocated 0x00894060, filled 0x00893DB8 |
| `framework.font.system.shared` | 10 | other code calls them too: slot 7 (0x00893D87, `GZFontSystem::SetDrawMode`, in two other classes' vtables), the +4 vtable's slot 4 (0x00894F6D, `SetServiceID`, in 52 vtables), a tree's distance (0x00893DEC; four other callers), a list of word pairs' node (0x00893E0F: the list insert 0x0092119E, which every such list in the game uses, makes its nodes with it), push_back (0x0089408D) and insert of a range (0x008940CD), the erase of a node of a string-keyed map (0x0089461C), the sizes' pair (0x00894585), node (0x00894843) and map's constructor (0x0089508C; GZWinTreeView's handler 0x009586B0 uses those three) |

Four of the shared ones were already named in [gzui.toml](../data/addresses/gzui.toml) (`GZPairList::PushBack`, `SC4StringObjMap::Erase`, `cGZUIScriptWidgets::TreeKeyPair` and `::TreeKeyMapConstruct`) and keep their names; the other 49 are new in font.toml. The balancing (0x0040D5E0, 0x0040DAF0), the tree's steps (0x0040D4C0, 0x0040D500), the allocator, the strings, the vectors of strings (0x0051FE90, 0x004522F0, 0x00419140), the lists' own insert (0x0092119E, 0x009A960E), constructor and destructor (0x009A88C4, 0x0096EDBC) and the sizes' map's initialiser (0x0094CB61) are outside the range and stay the game's, called at their addresses. So are these 53 when they call one another: with a group off its functions are the game's wherever they are called from.

### What they do

- **The trees** are STLport's red-black trees: a map is the header's pointer, the count and an empty comparator at +8 (`SC4String::Less`, 0x004B5E10); a node is its colour (a byte, 0 red), parent, left, right and the value at +0x10; the header's parent is the root, its left and right the first and last nodes. The faces' and aliases' headers are as large as their nodes (0x48 and 0x30 bytes); the sizes' (0x2C) are made outside the range.
- **Insert** (STLport's `insert_unique`) walks down from the root by the key, then either finds the key in the node before the place (stepping back with 0x0040D500 unless the place is the first) or calls the insert under a parent, which puts the node on the parent's left if the parent is the header, x is set or the key is less, else on its right, keeps the first and last, balances and counts one more. The result's byte is written alone, the three bytes after it left as they were.
- **Erase** of a subtree destroys right subtrees first (calling itself) and goes down the left; clear erases the root's subtree only if the count is not 0, then makes the header a new map's; the sizes' erase of a range clears when the range is the whole map, else erases node by node, each next found first; erase of a key is the equal range (the upper bound found first), its distance, then that range.
- **The values**: a face (0x24 bytes) is its list of (font, style) pairs, its name and three words (made empty, the three words are left as they were: the type quirk of f2a); a pair is its key (a string) and the value at +0x14. The byte vector is `count` of a byte and a zero, the fill reading the byte again for each.

### Quirks kept

- The nodes' makers test the value's place (the node plus 0x10) for null, not the node; erase of a node destroys the value before it tests the node.
- The byte vector's fill counts down from a signed count and stops at once for 0 or less; its allocation asks for one byte more than the count.
- What the game leaves in EAX for void functions (push_back and the list's range insert return the list insert's own result pointer, which points into a frame that has returned) is 0 in ours where the game leaves junk; the setters return the value written, as the game's do.

### Testing

**Compare mode**: the bounds, the equal range, the distance and the fill compare their results; the two setters keep what ours wrote aside, put the field back and compare with the game's. The three inserts run ours to its decision without its effects (the node already there, or the x and parent it would insert under) and then the game's, whose call of the insert under a parent is noted and compared, with the byte. The rest (they allocate, free or link nodes) run the game's alone. While the game's side runs, our detours inside it pass through to the game's.

**`OPENSC4_TEST_FONTS=<day>`** has one more stage after the font system's, `stl`, with three partial hashes logged before it (`stl-faces`, `stl-aliases`, `stl-ranges`; the same file, the same ` | ft` hash): maps, lists and vectors of our own made and changed by the 53 at their addresses. A tree goes into the hash whole: its count, its root and last node, then in order each node's colour, the places of its parent and children (by their order, never an address), its key and value. The faces: 64 inserts of 41 keys (most more than once) with names, words and lists of fonts; the root's face and pair copied and a node made of it; nodes put under the last on its right and under the first on its left with x set; every third key erased; cleared; inserted again; destroyed. The aliases: 48 inserts with vectors of 0 to 4 names, the same copies, a node under the last, cleared, inserted, destroyed. The sizes: 80 inserts of 50 keys; the bounds, equal range and distance for ten keys before, between, on and after; keys erased (one twice, one not there), a range, the first node, a node made and one put under the last; the whole range erased, inserted, cleared, destroyed. Then the byte vector made and allocated again for five counts and bytes, filled into a buffer between margins (counts below 0, 0, 1, 20); a list of word pairs pushed, copied, a range inserted twice (one empty), a node; the two setters on a buffer.

A mutation check (the distance one more for one step, the first node not kept on a left insert, clear leaving the count, the fill's byte changed for the last of a count) changed the hash at the stage it should each time and not before (the first node's mutation from f2a's aliases stage on, as the system

Results, 2026-10-25:

| Check | Result |
|---|---|
| Test runs, groups off (twice) vs on, each group alone, and with every font group on | every stage hash the same: the fifteen before as with f2a (getfont E7A84ACB), stl-faces A055EF8F, stl-aliases 2FBC5C0F, stl-ranges 0850E57A, stl 28F25CD5; `ft` 28F25CD5 |
| Compare mode, test | 0 divergences: facesinsert 184 calls, fill 140, rangesinsert 94, aliasesinsert 65, lowerbound 43, upperbound 27, distance 17, equalrange 17, setdrawmode 2, setserviceid 2 |
| Compare mode, 90 s of plain play (Making Money) | 0 divergences: facesinsert 19, fill 19, aliasesinsert 1, setdrawmode 1, setserviceid 1 |
| Screenshots (`run-shots.sh`, Making Money, both groups on) vs the game | 20 of 20 within `--tolerance 32 --max-differing 1.2` |
| Parity `ft4-r1`, 12 saves, 7 days, muted, own folder `p4env-ft4`, two jobs, A `determinism.dispatch_info`, B that and both groups | 12 same |
| The same with `OPENSC4_TEST_FONTS=2` (`ft4-t1`) | 12 same; `ft` 28F25CD5 in all 24 runs, both sides |

**Reached** in plain play (39 of 53): the three maps' constructors, initialisers and headers, the faces' and aliases' inserts, inserts under a parent, nodes and pair copies, the faces' and aliases' pairs, a face made, copied and destroyed, a list's push_back, node, copy and range insert, the byte vector and its fill, both setters; at exit the three maps' destructors and clears, the faces' and aliases' erases of subtrees and pair destructors. Not reached: the sizes' insert, insert under a parent, bounds, equal range, erases, pair, pair copy and node (SetMonoRange is never called), the distance and the faces' erase of a node (RemoveFace). The test reaches all 53.

**Audit**: each translation was checked against the disassembly by the author and by an independent audit (every write and its order, what is read again, signed compares, byte and word stores, every call's address, convention, arguments and `ret`, the thunks' byte counts, what is returned, every `addr::` name; then compare mode's safety). No difference a caller could see. Left: the EAX of void functions (push_back and the range insert return the list insert's result pointer, which in the game points at its own argument slot and in ours into a returned frame). In compare mode the fill writes its bytes twice and the setters their field three times, ending in the game's state.

## f3a: the font's object and properties are ours

38 of f3's functions, the font's object, its properties and the blitter's hand-over (class 0x1AA404AC), are translated by hand ([src/core/font_object.cpp](../src/core/font_object.cpp)), in three groups, off by default and installed at Attach (fonts are made while the game starts):

| Group | Functions | What |
|---|---:|---|
| `framework.font.font.object` | 10 | the constructor 0x00899AAB, the destructor 0x0089893F and the deleting destructor (slot 57, 0x00898FC0), QueryInterface (slot 0, 0x0089626E), Release (slot 2, 0x008972A8) and its last reference 0x00896293, Unload (slot 4, 0x008982E1), and the glyph cache's constructor 0x00899A89, clear 0x008982BA and destructor 0x0089892E |
| `framework.font.font.properties` | 24 | GetFamily (slot 5, 0x008989E5), HasStyle (6, 0x00898A00), SetStyle (8, 0x008962A9), SetColours (14, 0x00898A13), SetMode (16, 0x00896654), the grey ramp's power and bias (17 to 20: 0x00896662, 0x00899987, 0x00896666, 0x008999CE), GetPath and SetPath (21, 22: 0x00898A24, 0x00898A3F), GetEncoding, SetEncoding and SupportsEncoding (23, 24, 26: 0x0089666A, 0x00896678, 0x00896684), GetScale (29, 0x00896692), the extra scale (31, 32: 0x008966A7, 0x0089865F), the spacing (33, 34: 0x008966BB, 0x008966D0), LineHeight (35, 0x008966EB), TextHeight (37, 0x008966F2), Baseline (38, 0x008966F8), BelowBaseline (39, 0x00896702), the glyph cache of the mode (59, 0x008962C4) |
| `framework.font.font.blitter` | 4 | the blitter's mode of a draw mode 0x00896B2C, SetBlitter (slot 54, 0x00896B45), Begin and End (55, 56: 0x00896B8E, 0x00896BC2) |

Slots 17 and 19 are four bytes each (`FLD`, `RET`), back to back, too short for a detour: ours takes their slots of the vtable 0x00AC55D8 instead (nothing else points at them). The other 36 are detours. Slots 1, 7, 9, 10, 12, 13, 15 and 53 are a few bytes the linker folded with other classes' (AddRef 0x0094C4B1 among them) and stay the game's, as do Load, SetSize, SetScale, the layout, measuring and drawing, the glyph cache's lookups and the sheet (the other 76, listed below), the T2K holder (f5) and the containers' code, all called at the game's addresses. Addresses: [data/addresses/font.toml](../data/addresses/font.toml) (37 entries are new there; GZFont::Construct was there).

### What they do

- **The object** (0x15C bytes): +0x04 the loaded byte, +0x08 the count, +0x0C the path and +0x20 the family (the game's strings), +0x34 the kind, +0x38 the size, +0x3C the glyphs' height, +0x40 the style bits, +0x44 and +0x48 two colours, +0x4C the draw mode (1 when made) and +0x50 a 2, +0x54 and +0x58 the grey ramp's power (1.0) and bias (0.0) with +0x5C the vector of 256 levels rebuilt from them (0x008993BB), +0x68 and +0x6C the scale, +0x70 an extra scale, +0x74 and +0x78 two spacings, +0x7C the encoding, +0x80 Begin's depth, +0x84 and +0x88 two metrics SetSize sets, +0x8C the blitter, +0x90 six objects of 0xC bytes, +0xD8 and +0xF0 two vectors, +0xE4 the fallback character 0x20, +0xEC the T2K holder (made by the constructor, 0x34 bytes), +0xFC a word, +0x100 a list of blocks, +0x104 and +0x130 the monochrome and grey glyph caches. A glyph cache (0x2C bytes) is a glyph sheet (the block, its pixels, width, height, pitch and next free column) and the table of its glyphs at +0x18.
- **QueryInterface** answers 0x10673960 and 1, AddRef'd through slot 1; **Release** counts down while above 1 and at 1 or below calls Unload and the deleting destructor. **Unload**, if loaded, gives the blitter up (slot 54 with null), clears both caches, frees the blocks of +0x100 and clears the list, closes the face (f5's Close) and clears the loaded byte. The **destructor** unloads, destroys and frees the holder, then takes down the caches, the list, the vectors, the six objects, the levels and the strings.
- **Properties**: SetStyle sets or clears bits 2 to 5 (the flag a byte); HasStyle is whether all the asked bits are there; SetMode ignores 0. The ramp's power is kept in 0 to 10 and its bias in -1 to 1, the extra scale in 0.1 to 10; each is stored only if it differs from the field (a NaN always differs), the first two then rebuilding the levels, the third clearing both caches. LineHeight is the glyphs' height and the second spacing, Baseline +0x84 and the second spacing, BelowBaseline the first less the second (asked through the vtable, Baseline first). The glyph cache of the mode is the monochrome one in mode 1, else the grey one.
- **The blitter**: SetBlitter AddRefs a new blitter and hands it the font (its slot 3), hands the old one's slot 4 the font and releases it. Begin calls the blitter's Begin (slot 5: the font, the picture, the blitter's mode, which is the draw mode for 2 and 3 and 1 for anything else) at depth 0, End its End (slot 6) at depth 1; both move the depth.

### Quirks kept

- End goes on counting below 0, and Begin from below 0 calls nothing until the depth is back at 0.
- Release at a count of 0 or below destroys the font too (a signed compare); the count is not touched.
- The ramp's power below 0 becomes +0, so -0 is kept as -0 only when the field was not 0 already (0 and -0 compare equal and nothing is stored); a NaN is stored and rebuilds the levels every time.
- SetExtra's second argument is unused, and GetExtra's second answer is always 1.0; TextHeight ignores the text it is given.
- GetEncoding and SetEncoding answer AL 1 over the encoding's upper three bytes, as the game loads them into EAX; SupportsEncoding answers only AL (true for encoding 2 alone), ours 0 above it. Unload answers AL 1 over whatever EAX held (ours 0 above it). The void slots' EAX is the game's last value in each (SetStyle's mask, SetColours' and SetSpacing's second argument) and ours gives the same.
- The constructor passes the list's constructor a byte of its stack as an allocator, which it never reads; ours passes a 0.

### Testing

**Compare mode**: what only reads compares its result (HasStyle, SupportsEncoding, the line metrics, the glyph cache, the blitter's mode; the ramp's getters by their bits); what writes only the font or its caller's answers (QueryInterface, SetStyle, SetColours, SetMode, the encoding, the spacing, GetScale, GetExtra) keeps what ours wrote aside with the font's 0x15C bytes and the answers, puts them back, runs the game's and compares; GetFamily, GetPath and SetPath assign the same text twice and compare it. The three setters that rebuild or clear run the game's and compare the field it left with what ours would leave. The constructor runs the game's on the font and ours on a scratch font, compares every field but where pointers point (whether they are set), and takes the scratch down with the game's destructor. The rest (they free, release or call the blitter) run the game's alone. While the game's side runs, our detours inside it pass through to the game's.

**`OPENSC4_TEST_FONTS=<day>`** has three more stages after the font system's tables (same file, same ` | ft` hash):

| Stage | What |
|---|---|
| fontobject | the constructor and destructor in place between margins; QueryInterface for eight IDs, the count down by address and the vtable, Unload of a font never loaded; Release at 0, at 1 and down from three, the last reference's function, the deleting destructor with and without freeing; a glyph cache made, cleared empty and with a block, taken down, between margins; three fonts the font system made and loaded (Arial MT at 12 and 30, Times New Roman at 17), measured in modes 1 and 2 (filling both caches), the extra scale changed, unloaded twice, loaded again and measured, released to the last |
| fontprops | on a font of our own: SetStyle by seven masks and flags and HasStyle by nine; the colours; the mode by eight values with the cache of each; the ramp's power by 14 values and its bias by 12 (past both ends, -0, infinities, NaN twice) with the levels they build; the path and the family into strings that held text; the encoding by four and SupportsEncoding by six; the scales and the extra scale by 11; the spacing by five pairs with the line metrics |
| fontblitter | the blitter's mode of seven values; SetBlitter over two mock blitters that note every call (the same one twice, another, none); Begin and End nested, past 0 and back, in four modes |

Every font goes into the hash by its fields, texts and levels and by whether its pointers are set. A mutation check (Unload leaving the loaded byte, the extra scale's low clamp at 0, End at depth 2, the blitter's mode losing 3) changed the hash at the stage it should each time: fontobject, fontprops, fontblitter, and strings for the last (the first stage that draws in mode 3).

Results, 2026-10-25:

| Check | Result |
|---|---|
| Test runs, groups off (twice) vs on (the three), and with every font group on | every stage hash the same: the 18 before as with f2b (stl 28F25CD5), fontobject F81D0D6F, fontprops CC037FDD, fontblitter 7EEE16FF; `ft` 7EEE16FF. All 38 reached. (Before f2b's stage was added: E8435DED, 31C1F067, BDD0D65D, the same off and on) |
| Compare mode, test | 0 divergences: glyphcache 210667 calls, blitmode 22890, textheight 2205, getpath 507, setmode 487, setstyle 413, construct 387, queryinterface 386, setencoding 382, setpath 381, getfamily 275, lineheight 156, setcolours 105, setspacing 88, setextra 74, hasstyle 72, baseline 55, getpower 28, getbias 28, setpower 15, setbias 13, getscale 12, getextra 12, belowbaseline 8, supportsencoding 6, getspacing 5, getencoding 4 |
| Compare mode, 90 s of plain play (Making Money) | 0 divergences: glyphcache 1654756, blitmode 32987, baseline 16542, textheight 14006, lineheight 11933, setstyle 3316, setmode 115, setcolours 90, setspacing 83, setextra 60, construct 32, getfamily 32, queryinterface 32, setencoding 32, setpath 32 |
| Screenshots (`run-shots.sh`, Making Money, the three groups on) vs the game | 20 of 20 within `--tolerance 32 --max-differing 1.2` |
| Parity `ft5-r1`, 12 saves, 7 days, muted, own folder `p4env-ft5`, two jobs, A `determinism.dispatch_info`, B that and the three groups (before the rebase on f2b) | 12 same |
| The same with `OPENSC4_TEST_FONTS=2` on the rebased build (`ft5-t1`) | 12 same (Rush Hour d0000 after a second pair, A itself giving two outcomes: the harness's noise); `ft` 0DF2E70F in all 130 runs, both sides. Under `determinism.fixed_ticks` (one of the parity's settings) fontobject hashes BC3BBF4F rather than F81D0D6F, off and on alike: the fonts the font system loads differ there, not the translations |
| Compare mode, test, on the rebased build | 0 divergences in all 27 comparisons |

**Reached** in plain play: the constructor, QueryInterface, SetPath, SetEncoding, SetStyle, SetMode, SetColours, SetSpacing, SetExtra, GetFamily, SetBlitter, Begin, End, the blitter's mode, the glyph cache of the mode (1.65 million times in 90 s), LineHeight, TextHeight, Baseline, the caches' three functions, and at exit Release, its last reference, Unload and the destructors. Not reached: HasStyle, the ramp's four, GetPath, GetEncoding, SupportsEncoding, GetScale, GetExtra, GetSpacing, BelowBaseline. The test reaches all 38.

**Audit**: each translation was checked against the disassembly by the author and by an independent audit (every write and its order, signed and byte compares, what is read again, every call's address, convention and arguments, the x87 compares with NaN, -0 and equality, the thunks' byte counts, the expect bytes and that no patch covers a branch target, every `addr::` name; then compare mode's safety). No difference reachable by a caller. Left: the upper bytes of EAX where only AL is answered and the void slots' EAX (above); a signalling NaN, which the game's `FLD`/`FSTP` copies quiet and ours copies as it is (GetScale, GetExtra and the three setters' stores; nothing makes one). Changed from the audit: the bias's high is read from 0x00A81228 as the game reads it (the same 1.0). A compare-mode limit it noted: GetFamily and GetPath assign the caller's string twice.

## f3b: the font's containers are ours

36 more of f3's functions, the STLport container code the linker put in the font's range, are translated by hand ([src/core/font_containers.cpp](../src/core/font_containers.cpp)), in two groups, off by default and installed at Attach:

| Group | Functions | What |
|---|---:|---|
| `framework.font.font.containers` | 34 | the maps of characters (the holder's at +0x28 and the static map of glyph names 0x00B5C41C): Find 0x0089762F, Insert 0x00897E7C, InsertAt 0x00897DAC, the node 0x00897A91 and its pair 0x0089768B, and each map's own erase of a subtree, clear and destructor (0x0089753F, 0x00897A1C, 0x00897C5F; 0x0089756C, 0x00897A45, 0x00897C76); the glyph table of a glyph cache: Insert 0x008986BF, InsertNoResize 0x00897CB2, the node 0x00897A6E and its value 0x0089766B; DrawString's vector of placed glyphs: Resize 0x00899513, Erase 0x00897E56, the fill insert 0x0089874D, the insert overflow 0x00897F04, and under them the fill 0x00896BF6, the copies 0x00896C90 and 0x008975B4, the copies back 0x00896CFD and 0x008975F7, the construct 0x00896CC2, the fills 0x00896CD8 and 0x00897614, the uninitialised copy 0x008975D1; the Thai pass's vector of UTF-16 text: Insert 0x00897BDC, Reserve 0x008986E3, the copy into a new block 0x00897FC2, the finds 0x00896C19 and 0x00897599; the T2K holder's destructor 0x00897E36 |
| `framework.font.font.shared` | 2 | a list of words' insert before a node (0x00897C8D; seven call sites outside the font, the packed files' among them) and a vector of pointers' insert overflow (0x00897D22, called by 0x00997959) |

All 36 are detours. DrawString's resize of its placed glyphs (0x00899A73) stays the game's: it hands Resize an uninitialised 0x24-byte value from its stack. The balancing (0x0040D5E0), the tree's step back (0x0040D500), the hash table's growth (0x009978AF), the allocators (0x0090CF54, 0x0049CAF0, 0x009B3553, 0x009DB998, 0x0094B33A), the copies and fills of words and pointers (0x004622A0, 0x00413D00, 0x00407A50, 0x004186F0), the text's own insert overflow (0x004658B0) and f5's Close stay the game's, called at their addresses, as do these functions when they call one another. Addresses: [data/addresses/font.toml](../data/addresses/font.toml) (35 entries are new there; the list's insert was in resources.toml).

### What they do

- **The maps of characters** are STLport red-black trees: the map is its header's pointer and the count; a node (0x18 bytes) is its colour, parent, left, right, the character (a word) at +0x10 and a double word at +0x14 (a glyph in the holder's map, a glyph name in the static one). Find and Insert read the character once for the descent and again for the last test; Insert tests the node before the last one it reached (unless that was the first) and inserts under the last; InsertAt puts the node left when the parent is the header, right when asked, left when x is set or the character is below the parent's. The two maps are two copies of the same code for erase, clear and destructor and share the rest.
- **The glyph table** (+0x18 of a glyph cache) is a hash table: the buckets' vector at +4, the count at +0x10, nodes of 0x20 bytes (the next node, the character, six words). Insert grows the table for one more first; InsertNoResize finds the character's bucket (unsigned remainder by the number of buckets) and either answers the node there or puts a new one at the bucket's front.
- **The placed glyphs** (0x24 bytes: box, pixels, pitch, width, height, character) are copied as nine words, forwards or backwards. Counts are signed divisions by 0x24, compared unsigned where the game does (room against the count, the size against the count asked).
- **The text** is a vector of 16-bit words. Its find compares each word with the double word at the value, four at a time and then the last three, so a value with bits above the word is never found.
- **The holder's destructor** closes the holder (f5's Close), takes down its map at +0x28 and frees its block at +0x14.

### Quirks kept

- The text's find reads the value once for the fours and again for each of the last three words.
- The placed glyphs' construct leaves EAX as it came (ours answers the place); the maps' clear answers the header only when it cleared. The void functions' EAX is otherwise what the game leaves where a caller could see it (the overflows' end of room, the fill insert's fill).
- Resize counts the size again from the end read again but the begin read first.

### Testing

**Compare mode**: the finds (the maps' and the text's two) compare their results; the maps' insert runs ours to its decision without its effects (the node there, or the parent it would insert under) and then the game's, whose call of InsertAt is noted and compared; the glyph table's InsertNoResize decides with ours (the node there, or the bucket's first node the new one goes before) and compares it with what the game's did. The rest run the game's alone. While the game's side runs, our detours inside it pass through to the game's.

**`OPENSC4_TEST_FONTS=<day>`** has one more stage after the font's blitter (same file, same ` | ft` hash):

| Stage | What |
|---|---|
| fontcontainers | the placed glyphs' helpers in a buffer between margins (fills, copies forwards, backwards and overlapping, counts of 0, a part of a glyph and backwards, the construct at null); a glyph's value and a map's pair, and at null; a vector of placed glyphs resized up from nothing, down and past its room, copies put in at the front, middle and end with more and fewer glyphs after the place, ranges erased, the overflow at both ends; the text reserved from nothing, words put in at the end, front and middle until past its room, reserved again, copied into a new block and searched over every length for words there, not there and with bits above the word; both maps filled with 70 random characters and the ends, searched, InsertAt at both ends, a node made, a subtree erased, cleared, filled again and taken down; a glyph cache's table filled with 90 characters and four more without growing, a node made, taken down; a list of words, a vector of pointers out of room four ways, a holder with characters in its map and a block taken down |

Maps go into the hash by order (each node's colour, the places of its parent and children, its pair), tables by bucket, vectors by contents, places by offsets. A mutation check (the maps' clear leaving the count, the text's find skipping its third last word, the glyph table's insert not counting, the pointers' overflow answering one word more room) changed the hash at fontcontainers each time.

Results, 2026-10-25:

| Check | Result |
|---|---|
| Test runs, groups off vs on (the two), with every font group on (17), and in compare mode | every stage hash the same: the 21 before as with f3a (fontblitter 7EEE16FF), fontcontainers BCF8FA6E; `ft` BCF8FA6E. All 36 reached |
| Compare mode, test | 0 divergences: glyphinsert 1277 calls, mapinsert 1148, mapfind 640, textfind 155, textfindtagged 75 |
| Compare mode, 90 s of plain play (Making Money) | 0 divergences: glyphinsert 509, mapinsert 489, mapfind 20 |
| Screenshots (`run-shots.sh`, Making Money, the two groups on) vs the game | 20 of 20 within `--tolerance 32 --max-differing 1.2` |
| Parity `ft6-t1`, 12 saves, 7 days, muted, `OPENSC4_TEST_FONTS=2`, own folder `p4env-ft6`, two jobs, A `determinism.dispatch_info`, B that and the two groups | 12 same at the first try; `ft` FCB199D3 in all 120 daily fingerprints, both sides |

**Reached** in plain play: 25 of the 36, the maps (Find, Insert, InsertAt, the node and pair, both maps' erase, clear and destructor), the glyph table's four, Resize, the fill insert, the overflow and the fills, construct and uninitialised copy under them, the list's insert and the holder's destructor. Not reached: the text's five (only Thai text uses them), Erase and the four copies, the pointers' overflow. The test reaches all 36.

**Audit**: each translation was checked against the disassembly by the author and by an independent audit (every write and its order, what is read once and what again, signed and unsigned compares and divisions, byte, word and double word widths, every call's address, convention, ECX and arguments, the thunks' byte counts against each `ret`, the expect bytes and that no patch covers a branch target, every `addr::` name; then compare mode's safety). No difference reachable by a caller. Left: EAX where the game's void functions leave something no caller reads (the erases, clears of an empty map, destructors, the fill insert of 0, the construct); the tags, which ours points at a byte of its own where the game points at one of its argument slots (no callee reads them); in compare mode the tagged find and the glyph table's Insert also compare the find and InsertNoResize they call, reads only.

### f3: what is left

The other 40 functions of f3 stay the game's: Load (slot 3), SetSize (11), SetScale (30), the sample characters (25), the encodings' conversion (0x00899BCA, 0x00896425 and their helpers), Thai (0x00899665, 0x0089645A), FitString, MeasureString and its trimmed form, the string shortener, DrawString and DrawStringInRect with DrawString's resize of its placed glyphs (0x00899A73), GetGlyph, RenderGlyph, the sheet's store and growth, the levels' rebuild (0x008993BB) and DrawString's colour ramp (0x0089838C): 0x00896254 0x008962D7 0x0089632D 0x008963C9 0x00896425 0x0089645A 0x00896618 0x00896636 0x0089671F 0x00896808 0x00896886 0x008968EB 0x00896957 0x008969F2 0x00896A2D 0x008972BD 0x00897304 0x00897348 0x0089738F 0x008973D4 0x00897748 0x00897AB1 0x0089834C 0x0089838C 0x0089854E 0x00898A5A 0x00898FDC 0x008993BB 0x0089955C 0x00899665 0x00899A15 0x00899A73 0x00899BCA 0x00899CA7 0x00899D2A 0x00899D81 0x00899F71 0x00899FF4 0x0089A099 0x0089A35B. Ghidra now finds 114 functions in f3's range less f4 and f5 (the first map counted 113).
