# Fonts

The text engine at 0x00893D2C to 0x008AE76D: a Maxis framework layer (a COM director, the font system, the font, a glyph blitter) on top of **T2K**, the font scaler Sampo Kaasila wrote and Bitstream sold as Font Fusion. It is the "Font engine" line of [uncharted-code.md](uncharted-code.md), which gave it as 0x00894000 to 0x008A7FFF; the code runs a little lower and higher than that.

Everything here was read from the disassembly with Ghidra (no game runs). Names are ours unless they come from T2K. The addresses the doc relies on are in [data/addresses/font.toml](../data/addresses/font.toml) (none hooked) and [data/names-641.txt](../data/names-641.txt). "Ran" counts come from the 17 coverage files on TurboToaster ([dev-environment.md](dev-environment.md), "Coverage"); code that runs only while the game starts is never recorded there.

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
| 0x08CDA30B | 8 | 0x00AC55B4 (8 slots) | 0x00897722 | **Glyph blitter.** Locks the picture, composites a list of placed glyphs into it, unlocks it. The first one builds blend tables at 0x00B54418 |

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
| f4 | 0x00896053 to 0x00896243, 0x00896E63, 0x00897246, 0x00897722 | 11 | 4 | The glyph blitter and its blend tables |
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
- **The blitter's DrawGlyphs (slot 7 of 0x00AC55B4, 0x00897246).** The last step before text is pixels: a list of placed glyphs (0x24 bytes each: box, sheet pixels, pitch, width, height, character) and the picture. A replacement blitter could send glyphs to the driver instead of the picture, which is also a vtable swap rather than a detour.
- GetFont (0x00894ADF) is where fonts are made, if the larger font objects should be made alongside the game's.

None of these is started.
