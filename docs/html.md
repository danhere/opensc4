# HTML viewer

The HTML control the game uses for advice, news and the text pages, at 0x008F673E to 0x0090AD25: a COM director, the `HTML Window` class, a document that tokenizes and parses the page into a tree of nodes, the nodes' layout and paint, and under them an Internet service with a cookie store and a file cache that the game's pages hardly touch. It is the "HTML viewer" line of [uncharted-code.md](uncharted-code.md) and the "HTML parser" line of [framework.md](framework.md); the code starts a little below 0x008F6800 (the director), and 0x0090AD26 to 0x0090B1A0 belongs to the picture codecs, not to the viewer.

Everything here was read from the disassembly with Ghidra (no game runs). Names are ours. The addresses the doc relies on are in [data/addresses/html.toml](../data/addresses/html.toml) (none hooked) and [data/names-641.txt](../data/names-641.txt). "Ran" counts a function that ran in at least one of the 46 coverage files on TurboToaster (`%LOCALAPPDATA%\opensc4\analysis`, the `*coverage*.csv` and `cov.csv` files; [dev-environment.md](dev-environment.md), "Coverage"); code that runs only while the game starts is never recorded there.

## Library or Maxis

Maxis. There are no vendor or copyright strings in the range or in its data (0x00ACCC00 to 0x00ACE5A0), and the code is the framework's: packed (not aligned to 16 bytes), COM classes with `cRZCOMDllDirector` registration, `cRZString`, `cRZFile`, the STL allocator at 0x0090CF54. The parser's debug names (`ParseTABLE`, `ParseIMG`, `ParseFONT` and the rest, one per tag handler) and `Maxis Network Cache\` point the same way. So the whole viewer counts as Maxis code: nothing changes in `tools/progress.py` (`NOT_MAXIS`) or in the third-party line of [uncharted-code.md](uncharted-code.md).

## Class map

The HTML director (0x008F673E, vtables 0x00ACCC58 and 0x00ACCC28, director ID 0x649B0FBE, static getter 0x008F677C) registers one class and adds a second, empty director (0x008FC379, vtables 0x00ACD378 and 0x00ACD34C, getter 0x008FC398) to its list. The Internet service is not registered: the app (0x00450350) first asks COM for class 0x849B190A with interface 0x449B1912, and when nothing answers (no plugin provides it) builds the game's own.

| Class ID | Size | Vtables | Constructor | What |
|---|---:|---|---|---|
| 0xAA12E5F5 | 0x468 | 0x00ACD0D8 (151 slots), 0x00ACD050 at +0xD8 (34), 0x00ACD040 at +0xDC (4) | 0x008FBC38 | **HTML window.** A framework window (most of the 151 slots are the base window's in 0x0099xxxx) with its own interface 0x4A11FD4A at +0xD8 and a message target (0x452294AA) at +0xDC; QueryInterface 0x008FA40C also answers 0x22BA0121 (the window). Made by the factory 0x008FBDBD, which returns the +0xD8 interface. Slot 4 (0x008FC0F6, create) loads `Missing.jpg`, makes a picture reader (class 0xA2FFB5D3) and fetches the Internet service; it titles itself `HTML Window` |
| none | 0x60 | 0x00ACCEC8, 0x00ACCE48 at +4, 0x00ACCE10 at +8, 0x00ACCDF8 at +0xC | 0x008F9BED | **Internet service** (system service 0x449B1939, interface 0x449B1912; also answers 0x287FB697). Requests by URL (slot 16 of the +4 vtable, 0x008F833D): registered handlers first (presumably the game's `sc4` link handler, [ui.md](ui.md), made right after the service at 0x004510FA), then by the URL's scheme to the service's own slots 4 to 7, which use the network layer (0x00810000 to 0x0082B77F). Makes the file cache and the cookie store, and two network objects of 0xEC and 0x4C bytes from that layer |
| none | 0x3C | 0x00ACCF68 (20 slots) | 0x008F9BAD | **Cookie store.** `CookieData.ini`; parses `Set-Cookie` attributes (0x008F94F9: Comment, CommentURL, Domain, Path, Secure, Version, Discard, Expires, Max-Age, Port) and saves cookies as `CookieData%05u` keys |
| none | 0x5C | 0x00ACD418 (28 slots) | 0x008FD389 | **File cache.** `Maxis Network Cache\` with `FileCache.ini` (`Cache Entries`, `CacheEntry%05u` = `%s,%s,%u,%u,%u,%d,%d,%d`), `.cache` files, limits 3,000,000 / 500,000 / 432,000 at +0x48 to +0x50; `Cache-Control` parsing (`no-cache`, `no-store`, `must-revalidate`, `max-age`) is in the service (0x008F7A50). The freshness check (slot 12, 0x008FD0DC) is the only slot that ran |
| none | 0x1C4 | 0x00ACD938 (24 slots), 0x00ACD928 at +4 | 0x00905AC5 | **Document.** The page text, the tokenizer's position, the current tag, the font stack and the node tree. The first one builds the tag table (0x00902D4F) and the block-tag sets (0x008FF1B9) and four colours; each one sets the seven font steps (default 8, 10, 12, 14, 18, 24, 36 at 0x00ACD4A0) and the faces `arial` and `courier new` |

The window's own interface (+0xD8, vtable 0x00ACD050) is what the game's controls call: the advice list ([ui.md](ui.md), class 0xCA1492AC) and the text page dialog among them. Slots that matter:

| Slot | Address | What |
|---:|---|---|
| 7 | 0x008FAD0E | Resize to (width, height), clamped to the parent; tells the scroll bars |
| 17 | 0x008FB168 | Go to a URL (an address object, and whether to reload): asks the service for it (its slot 16) unless it is the page already shown |
| 18 | 0x008FBBF7 | The same from a string |
| 22 | 0x008FBF73 | Show HTML text (0x008FBDE4) with a base address |
| 32 | 0x008FB127 | Set the margins (four values), then lay out again |
| **33** | **0x008FB66F** | **Set the seven font steps** (copied to +0x12C). The text page dialog passes `SC4UI::TextPageFontSizes` (0x00AB4AD0: 8, 10, 12, 16, 19, 24, 48) |

Showing a page (0x008FBDE4, text and base address) makes a document, hands it the window and the service, parses the text (document slot 8) and on success swaps the new document in, sets the scroll area and lays it out (0x008FA6F3); when the document's slot 20 says so it starts a 500 ms timer (callback 0x008FA31D). The service's answer to a URL request comes back as a message to the +0xDC part (0x008FBFE5, message 0x75758483), which shows the text the same way.

### Nodes

The document's tree is one family of small classes with an 11-slot vtable: slot 0 the deleting destructor, slot 3 Layout (with a layout context: pen, line, margins, floating images), slot 4 a second layout pass for containers, slot 5 Draw (with a draw context whose +8 is the window's drawing interface), slot 7 a hit test. Leaves derive from 0x00ACD504 (constructor 0x0090847A), containers from 0x00ACD5E8 (constructor 0x008FECBD, children vector at +0x1C). Which tag makes which node, from the handlers that build them:

| Vtable | Built by | Tag | What it does |
|---|---|---|---|
| 0x00ACD530 | 0x008FF725 | BR | A line break |
| 0x00ACD55C | 0x0090097A | HR | A rule: SIZE, WIDTH (pixels or %), drawn as grey lines 0xA0A0A0 / 0x404040 (0x009093CD) |
| 0x00ACD588 | 0x008FF82E | INPUT | Nothing drawn |
| 0x00ACD614 | 0x0090083A | AREA | A map's area (SHAPE: RECT, CIRCLE, POLY, DEFAULT; HREF) |
| 0x00ACD640 | 0x009079A8 | BLINK | Blinking text |
| 0x00ACD66C | 0x00906C6B | BLOCKQUOTE | 12 pixels of space and indent |
| 0x00ACD698, 0x00ACD6F0 | 0x00905ED9, 0x00905F63 | DT, DD | Definition list parts |
| 0x00ACD6C4 | 0x009065FD | CENTER, DIV | An aligned block |
| 0x00ACD71C | 0x00906D9B | FORM | A container |
| 0x00ACD748 | 0x00906754 | H1 to H6 | Headings |
| 0x00ACD774, 0x00ACD7A0 | 0x00905FED | LI | List items (bullet, or the number as `%3d. `) |
| 0x00ACD7CC | 0x009074D3 | MAP | An image map |
| 0x00ACD7F8, 0x00ACD850 | 0x009060DF, 0x00906AFF | OPTION, SELECT | Containers |
| 0x00ACD824 | 0x0090614F | P | 6 pixels of space before and after |
| 0x00ACD87C | 0x009075FD | SPAN | Inline style: `color` (`#rrggbb`, `rgb()`), `text-decoration`, `font-size`, `font-family` |
| 0x00ACD8A8 | 0x009064B0 | TR | A table row (BGCOLOR); holds TH and TD cells |
| 0x00ACD8D4 | 0x009063E6 | TH | A header cell |
| 0x00ACE188 (+0x00ACE174, +0x00ACE170) | 0x00900A58, 0x008FF8AB | IMG (and list bullets) | SRC, ALT, WIDTH, HEIGHT, ALIGN (left, right floats); drawn by 0x00908FD5 through the drawing interface's blit, or the stretch blit when the size differs |
| 0x00ACE1B4, 0x00ACE1E0 | 0x00900C31, 0x00900D78 | LINK, META | Kept, not drawn |
| 0x00ACE20C | 0x00907D6B, 0x0090689F | UL, OL | Lists |
| 0x00ACE238 | 0x00906F3C | TABLE | BORDER, WIDTH, BGCOLOR, CELLPADDING, CELLSPACING; rows and cells laid out by 0x0090A448 (953 bytes) |
| 0x00ACE264 | 0x009061DC | TD | VALIGN, WIDTH, BGCOLOR, NOWRAP; draws its background, then its children |
| 0x00ACE290 | 0x00907A47 | A | HREF, NAME; the link's hit area and colours |
| 0x00ACE2C0 | 0x00908811 | the body | The root; BACKGROUND, BGCOLOR, LINK, ALINK, VLINK, TEXT (0x00907E42) |
| 0x00ACE300 (+0x00ACE2EC) | 0x00908CA6 | text | A run of text in one font and colour, entities decoded (named ones through 0x00901222: `quot`, `amp`, `nbsp` and the Latin-1 names; `&#n;` and `&#xn;` directly); laid out word by word (0x00909BE1, 0x0090A24D) and drawn with GZFont DrawString (0x00909757, slot 51 of [fonts.md](fonts.md)) |

## The parser

The tag table (0x00902D4F, built once) maps about 160 names to IDs, each start and end tag its own ID (`!DOCTYPE` 2, `HTML` 4, `BODY` 8, `FONT` 0x20, `A` 0x36, `IMG` 0x38, `BR` 0x3B, `P` 0x65, `TABLE` 0x78, `TD` 0x8F, `TR` 0x93, `NOBR` 0xA1). The tokenizer is three functions on the document: 0x008FFAC2 collects text up to the next `<`, skipping `<!-- -->`; 0x00905CF0 reads the next tag and looks its name up (the current ID at +0xE8); 0x009056DA splits its attributes (quoted with `"` or `'`, or bare). Colours go through 0x0090AA54: `#rrggbb` or the 16 HTML colour names.

Parsing is recursive descent. 0x00908122 (document slot 8) starts it: it checks `<!DOCTYPE ... DTD HTML x.y>` (slot 9, 0x008FFF91) and runs 0x009080A2, which makes the body node and calls 0x00907E42 for `BODY`; text that does not start with a tag becomes one plain text run (slot 11, 0x00905985). From there 0x00907135 handles block tags (HR, P, PRE, DL, DIV, BLOCKQUOTE, FORM, TABLE, CENTER, headings, lists; NOSCRIPT, FIELDSET, ADDRESS, DIR and MENU are skipped to their end tag by 0x009065C5) and 0x00907CCA handles text and inline tags: text (ID 3) becomes a text node (0x008FE6E2), and four tag sets go to 0x0090727D (font tags: TT, I, B, U, STRIKE, BIG, SMALL, FONT), 0x00906985 (phrase tags), 0x00907C3E (A, IMG, BR, SCRIPT, MAP, SPAN, BLINK; OBJECT, Q, SUB, SUP, BDO skipped to their end tag) and 0x00906E59 (INPUT, SELECT; TEXTAREA, LABEL, BUTTON skipped).

Images are fetched while parsing: 0x008FFBFC reads the file through `cRZFile` and the picture reader, and the document's slot 16 (0x009059FC) hands an image that arrives later to its node.

## Pieces

| Piece | Range | Functions | Ran | What |
|---|---|---:|---:|---|
| h1 | 0x008F673E to 0x008F67A6, 0x008FA317 to 0x008FC3EE | 74 | 35 | The director (below 0x008F6800), the HTML window class, its factory and slots, the empty second director |
| h2 | 0x008FE5F9 to 0x009010FC | 137 | 85 | The document: node constructors and destructors, the tokenizer, the small tag handlers (INPUT, AREA, HR, IMG, LINK, META), image fetching, the document's slots |
| h3 | 0x009010FD to 0x009081BA | 57 | 32 | The parser: entity table (0x00901222, 6.9 KB), tag table (0x00902D4F, 10.6 KB), next tag, attributes, the block, inline, list, table and body handlers |
| h4 | 0x008FDE3F to 0x008FE5F8 | 28 | 20 | The layout context (pen, line height, margins, floats, rectangle offsets) and STL vector code the linker folded here, which other code calls (0x008FDE8D, 0x008FDFF5, 0x008FE324, 0x008FE3DC, 0x008FE4DE, 0x008FE5AC) |
| h5 | 0x009081BB to 0x0090AD25 | 127 | 91 | The nodes' layout and paint: the node classes' slots, text runs (line breaking, DrawString), tables, cells, images (0x00908FD5), rules, colour names, the multibyte character helpers |
| h6 | 0x008F67A7 to 0x008FA316 | 115 | 12 | The Internet service and the cookie store. The 12 are the service's QueryInterface, Release, request (slot 16 of the +4 vtable) and its helpers, and 0x008F6E80, an STLport list push_back with 13 callers elsewhere (gzui.toml's `SC4PtrList::PushBack`) |
| h7 | 0x008FC3EF to 0x008FDE3E | 54 | 4 | The file cache: `FileCache.ini`, `.cache` files, entries, expiry. The 4 are the freshness check and its map lookup |
| | **0x008F673E to 0x0090AD25** | **592** | **279** | |

The 18 functions from 0x0090AD26 to 0x0090B1A0 (5 ran) are the picture codecs' director (0x0090AD97: classes 0xA2FFB5D3, 0x2A316C02, 0xCA316C55; getter 0x0090ADE3) and 16-bit pixel converters, which go with `cGZImageReader` from 0x0090B1A1 on.

Planned toggle groups, off by default until each piece is ours: `ui.html.window` (h1), `ui.html.document` (h2), `ui.html.parser` (h3), `ui.html.layout` (h4), `ui.html.nodes` (h5), `ui.html.internet` (h6), `ui.html.cache` (h7). h6 and h7 hardly run: the game's pages come through the service's scheme handlers and never reach HTTP, the cache's files or the cookies.

### Calls from outside

Almost everything comes in through vtables. The direct calls from game code are the HTML director's getter (from the director list 0x00460090), the service's constructor (0x00450350), the codec director's getter, and helpers that other code shares, mostly STL container code the linker folded in (h2, h4, h5 and h6: 0x008F6E80, 0x008FDE8D, 0x008FDFF5, 0x008FE324, 0x008FE3DC, 0x008FE4DE, 0x008FE5AC, 0x008FE833, 0x008FE9D9, 0x008FEA1C, 0x008FEB1E, 0x008FEBE9, 0x008FEC00, 0x008FEF47, 0x008FF040, 0x008FF785, 0x0090990F, 0x009098CC, 0x00909B33, 0x009090E0, 0x009091B2, 0x009086AC; 0x008FF785 is music.toml's vector push_back). Those stay the game's when the pieces they sit in are translated. 0x008FB729 and 0x008FB72E are bare jumps to the framework window's AddRef and Release that 39 window vtables share (ui.toml).

## Testing idea

Lay out pages at a fixed width with no window: the advice list's HTML (ui.toml's static strings at 0x00B20964 up, built by 0x0041E4B0) and the news ticker's rows, at 300 and 450 pixels with both font step tables, and hash every node's box (vtable, rectangle at +0xC, font, colour) in tree order. The same document run through the game's code and ours must give the same hashes. The parser alone can be checked the same way earlier, by hashing the tree before layout.

The best first piece is h3 with the tokenizer functions of h2: it is pure text in, a node tree out, deterministic and testable that way without a window or a picture, and every page the game shows runs through it.
