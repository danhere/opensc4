// Our font's drawing, the seventh and last slice of f3 of docs/fonts.md
// ("f3g: the font's drawing is ours"): the last four functions of class
// 0x1AA404AC (vtable 0x00AC55D8), detours in one group, off by default and
// installed at Attach:
//
//   framework.font.font.draw   DrawString (slot 51, 0x0089A099),
//                              DrawStringInRect (slot 52, 0x0089A35B),
//                              DrawString's colour ramp (0x0089838C) and
//                              the resize of its placed glyphs (0x00899A73,
//                              on the vector in ECX)
//
// ToUnicode (f3d), GetGlyph and the glyph cache (f3e), FitString and
// MeasureString (f3f), the blitter's begin, draw and end (its slots 5, 7
// and 6), the blitter kind of a mode (0x00896B2C), the colour converter
// (0x00913C1A, its slots 0x90 and 0x8C), the ramps' list (erase 0x00949259,
// push_front 0x009D74D9), the placed glyphs' resize (0x00899513) and
// operator new stay the game's, called at their addresses, as do these four
// when they call one another (DrawStringInRect DrawString through slot 51,
// DrawString the ramp and the resize at their addresses).
//
// The font's fields are listed in font_object.cpp; these use +0x04 (set
// while loaded), +0x44 the font's colour, +0x4C the draw mode, +0x50 the
// mode a second colour of 0x08000000 asks for, +0x74 and +0x78 the
// spacings, +0x7C the encoding, +0x80 how deep the blitter's begin is,
// +0x8C the blitter, +0xE4 the fallback character, +0xF0 the vector of
// placed glyphs (0x24 bytes each: the box, the pixels, the pitch, the width,
// the height, the character), +0xFC the current ramp and +0x100 the list of
// ramps (the four last used, newest first). A ramp is 128 colours: 8 of the
// second colour, 15 rows of 8 between it and the first, 8 of the first.
//
// Compare mode: DrawString and DrawStringInRect run the game's first with
// the font's blitter behind a recorder (each begin, draw and end, with what
// the draw was given: the picture, the kind, the colours, the clip and every
// placed glyph), then ours with a blitter that records the same and answers
// each begin as the game's did, and compare the records, the answer and the
// pen. The ramp and the resize run the game's alone and compare what it did
// with what ours works out from the state before: the list's order, the
// current ramp and a new ramp's colours; the vector's size and the glyphs it
// kept. While the game's side runs, our detours inside it pass through to
// the game's; while ours runs, ours.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

namespace opensc4::fontdraw {
namespace {

// ====================================================================================
// Helpers

using Address = uint32_t;

inline Address At(const void* p) { return static_cast<Address>(reinterpret_cast<uintptr_t>(p)); }
inline void* Ptr(Address at) { return reinterpret_cast<void*>(static_cast<uintptr_t>(at)); }

template <typename T>
inline T Load(Address at)
{
    T value;
    std::memcpy(&value, Ptr(at), sizeof value);
    return value;
}

template <typename T>
inline void Store(Address at, T value)
{
    std::memcpy(Ptr(at), &value, sizeof value);
}

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

// A thiscall game function: `self` in ECX, the rest on the stack.
template <typename R, typename... Args>
inline R This(std::uintptr_t fn, void* self, Args... args)
{
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(self, nullptr, args...);
}

template <typename R, typename... Args>
inline R Cdecl(std::uintptr_t fn, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(fn)(args...);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("fontdraw: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// ====================================================================================
// The game's functions and constants we use

constexpr std::uintptr_t kToUnicode = 0x00899BCA;  // thiscall (text, count, out*, outCount*, which, keep), ret 0x18
constexpr std::uintptr_t kKind = 0x00896B2C;       // cdecl (mode): the blitter's kind, 1 to 3
constexpr std::uintptr_t kConverter = 0x00913C1A;  // cdecl (): the colour converter
constexpr std::uintptr_t kListErase = 0x00949259;  // thiscall on the list (next*, node), ret 8
constexpr std::uintptr_t kPushFront = 0x009D74D9;  // thiscall on the list (const value*), ret 4
constexpr std::uintptr_t kGlyphsResize = 0x00899513; // thiscall on the vector (count, const glyph*), ret 8

// These functions, at their addresses (ours when the group is on).
constexpr std::uintptr_t kRamp = 0x0089838C;
constexpr std::uintptr_t kResize = 0x00899A73;

constexpr uint32_t kOwnColour = 0x08000000;
constexpr uint32_t kGlyphBytes = 0x24;
constexpr uint32_t kRampBytes = 0x200;

namespace font {
constexpr Address kLoaded = 4;
constexpr Address kColour = 0x44;
constexpr Address kMode = 0x4C, kColourMode = 0x50;
constexpr Address kSpacing = 0x74, kLineSpacing = 0x78;
constexpr Address kEncoding = 0x7C;
constexpr Address kDepth = 0x80;
constexpr Address kBlitter = 0x8C;
constexpr Address kOffsets = 0xD8, kOffsetsEnd = 0xDC;
constexpr Address kFallback = 0xE4;
constexpr Address kGlyphs = 0xF0;
constexpr Address kRampNow = 0xFC, kRamps = 0x100;
} // namespace font

namespace slot {
constexpr std::size_t kLineHeight = 0x8C;
constexpr std::size_t kFit = 0xA4;
constexpr std::size_t kMeasure = 0xC0;
constexpr std::size_t kDrawString = 0xCC;
constexpr std::size_t kCache = 0xEC;
constexpr std::size_t kGetGlyph = 0xF0;
} // namespace slot

// The blitter's slots.
namespace blit {
constexpr std::size_t kBegin = 0x14, kEnd = 0x18, kDraw = 0x1C;
}

// The colour converter's slots: a 16-bit colour into three bytes, and three
// back into a colour.
namespace convert {
constexpr std::size_t kPack = 0x8C, kUnpack = 0x90;
}

uint32_t Kind(uint32_t mode) { return Cdecl<uint32_t>(kKind, mode); }

void ToUnicode(void* self, Address text, uint32_t count, Address& out, uint32_t& outCount, uint32_t which,
               uint32_t keep)
{
    This<void>(kToUnicode, self, text, count, &out, &outCount, which, keep);
}

void* Blitter(const void* self) { return Ptr(Load<Address>(At(self) + font::kBlitter)); }

// ====================================================================================
// The functions

// A ramp's colours between the second colour (rows 0) and the first (rows
// 15): the converter splits both, and each of the 15 rows between is the
// second's bytes plus (first's less second's) times the row over 16, in the
// low byte of what the division left (as the game adds it), packed back
// and written 8 times.
void RampRows(Address ramp, uint32_t colour, uint32_t colour2)
{
    void* const converter = Ptr(Cdecl<Address>(kConverter));
    uint8_t first[3] = {}, second[3] = {};
    // The third byte of the second colour lands in the top byte of the
    // argument that held it.
    second[2] = static_cast<uint8_t>(colour2 >> 24);
    Virtual<void>(converter, convert::kUnpack, colour & 0xFFFF, &first[0], &first[1], &first[2]);
    Virtual<void>(converter, convert::kUnpack, colour2 & 0xFFFF, &second[0], &second[1], &second[2]);
    for (Address at = 0; at < 0x20; at += 4)
        Store(ramp + at, colour2);
    Address row = 0x20;
    for (int32_t i = 0; i < 15; ++i) {
        uint32_t parts[3];
        for (int k = 0; k < 3; ++k) {
            const int32_t step = (static_cast<int32_t>(first[k]) - static_cast<int32_t>(second[k])) * i / 0x10;
            parts[k] = (static_cast<uint32_t>(step) & ~0xFFu) |
                       ((static_cast<uint32_t>(step) + second[k]) & 0xFF);
        }
        uint32_t packed;
        Virtual<void>(converter, convert::kPack, &packed, parts[0], parts[1], parts[2]);
        for (Address at = 0; at < 0x20; at += 4)
            Store(ramp + row + at, packed);
        row += 0x20;
    }
    for (Address at = 0x1E0; at < kRampBytes; at += 4)
        Store(ramp + at, colour);
}

bool RampIs(Address ramp, uint32_t colour, uint32_t colour2)
{
    return Load<uint32_t>(ramp + 0x1FC) == colour && Load<uint32_t>(ramp) == colour2;
}

// The game's 0x0089838C (ret 8): the ramp from colour2 to colour made
// current (+0xFC). The current one if it is it; else one in the list
// (moved to the front); else a new one (operator new, 0x200 bytes, zeroed,
// RampRows) at the front, the last dropped when the list had four (its
// node freed, the ramp not).
void Ramp(void* self, uint32_t colour, uint32_t colour2)
{
    const Address f = At(self);
    const Address now = Load<Address>(f + font::kRampNow);
    if (now && RampIs(now, colour, colour2))
        return;
    const Address list = f + font::kRamps;
    const Address first = Load<Address>(Load<Address>(list));
    for (Address node = first; node != Load<Address>(list); node = Load<Address>(node)) {
        Address ramp = Load<Address>(node + 8);
        if (RampIs(ramp, colour, colour2)) {
            if (node != first) {
                uint32_t next;
                This<void>(kListErase, Ptr(list), &next, node);
                This<void>(kPushFront, Ptr(list), &ramp);
            }
            Store(f + font::kRampNow, ramp);
            return;
        }
    }
    const Address ramp = At(Cdecl<void*>(addr::SC4_operator_new, kRampBytes));
    Store(f + font::kRampNow, ramp);
    std::memset(Ptr(ramp), 0, kRampBytes);
    RampRows(ramp, colour, colour2);
    const Address header = Load<Address>(list);
    uint32_t count = 0;
    for (Address node = Load<Address>(header); node != header; node = Load<Address>(node))
        ++count;
    if (count >= 4) {
        uint32_t next;
        This<void>(kListErase, Ptr(list), &next, Load<Address>(Load<Address>(list) + 4));
    }
    This<void>(kPushFront, Ptr(list), f + font::kRampNow);
}

// The game's 0x00899A73 (on the vector in ECX, ret 4): the placed glyphs'
// vector resized (0x00899513) to `count`, new ones copies of a glyph on the
// stack the game never sets (ours zeroed).
void Resize(Address vector, uint32_t count)
{
    uint8_t glyph[kGlyphBytes] = {};
    This<void>(kGlyphsResize, Ptr(vector), count, glyph);
}

// The game's 0x0089A099 (slot 51, ret 0x24): one line of text into a
// picture from (x, y), or the pen when given (which gets where it ends).
// When loaded and given text: the text made UTF-16 (vector 1). In mode 2
// the ramp (with a second colour of 0x08000000 the mode +0x50 asks for, and
// if that is 2 too the second colour 0 or -1 by whether there is a first)
// is made current; a first colour of 0x08000000 is the font's (+0x44).
// With a picture and no begin yet, the blitter's begin (AL 0: answers 0).
// The vector of placed glyphs grows to the text; every glyph got first;
// then each character's glyph (else the fallback's; else nothing, no
// advance) placed at the pen plus its bearing and top (and the line
// spacing), kept when it has pixels and meets the clip, the pen moved by
// its advance plus the spacing. With a picture and glyphs kept, the
// blitter draws them (the colours: the ramp in mode 2, else the two
// colours, the second's top byte whether it began). Then the pen, and the
// end if it began. Answers +0x04's byte.
uint32_t DrawString(void* self, Address picture, int32_t x, int32_t y, Address text, uint32_t count,
                    uint32_t colour, uint32_t colour2, Address clip, Address pen)
{
    const Address f = At(self);
    const uint8_t loaded = Load<uint8_t>(f + font::kLoaded);
    if (!loaded || count == 0)
        return loaded;
    Address u = 0;
    ToUnicode(self, text, count, u, count, 1, 0);
    int32_t penX = x, penY = y;
    uint32_t mode = Load<uint32_t>(f + font::kMode);
    const Address cache = Virtual<Address>(self, slot::kCache);
    if (mode == 2) {
        if (colour2 == kOwnColour) {
            mode = Load<uint32_t>(f + font::kColourMode);
            if (mode == 2)
                colour2 = (colour != 0 ? 1u : 0u) - 1;
        }
        This<void>(kRamp, self, colour, colour2);
    }
    if (colour == kOwnColour)
        colour = Load<uint32_t>(f + font::kColour);
    // The two colours as the game's arguments lie: the second's top byte
    // is whether it began.
    uint32_t colours[2] = {colour, colour2 & 0x00FFFFFF};
    bool began = false;
    if (picture && Load<uint32_t>(f + font::kDepth) == 0) {
        if (!(Virtual<uint32_t>(Blitter(self), blit::kBegin, self, picture, Kind(mode)) & 0xFF))
            return 0;
        Store(f + font::kDepth, Load<uint32_t>(f + font::kDepth) + 1);
        colours[1] |= 0x01000000;
        began = true;
    }
    if (pen) {
        penX = Load<int32_t>(pen);
        penY = Load<int32_t>(pen + 4);
    }
    const Address vector = f + font::kGlyphs;
    const int32_t size = static_cast<int32_t>(Load<Address>(vector + 4) - Load<Address>(vector)) /
                         static_cast<int32_t>(kGlyphBytes);
    if (static_cast<uint32_t>(size) < count)
        This<void>(kResize, Ptr(vector), count);
    const Address list = Load<Address>(vector);
    uint32_t placed = 0;
    for (uint32_t i = 0; i < count; ++i)
        Virtual<Address>(self, slot::kGetGlyph, uint32_t{Load<uint16_t>(u + i * 2)});
    Address e = list;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t c = Load<uint16_t>(u + i * 2);
        Address g = Virtual<Address>(self, slot::kGetGlyph, c);
        if (!g) {
            c = Load<uint32_t>(f + font::kFallback);
            g = Virtual<Address>(self, slot::kGetGlyph, c);
            if (!g)
                continue;
        }
        const int32_t h = Load<int32_t>(g + 0xC) - Load<int32_t>(g + 4);
        const int32_t w = Load<int32_t>(g + 8) - Load<int32_t>(g);
        const int32_t left =
            static_cast<int32_t>(static_cast<uint32_t>(Load<int16_t>(g + 0x10)) + static_cast<uint32_t>(penX));
        const int32_t top = static_cast<int32_t>(static_cast<uint32_t>(Load<int16_t>(g + 0x12)) +
                                                 Load<uint32_t>(f + font::kLineSpacing) +
                                                 static_cast<uint32_t>(penY));
        const int32_t right = static_cast<int32_t>(static_cast<uint32_t>(left) + static_cast<uint32_t>(w));
        const int32_t bottom = static_cast<int32_t>(static_cast<uint32_t>(top) + static_cast<uint32_t>(h));
        Store(e, left);
        Store(e + 4, top);
        Store(e + 8, right);
        Store(e + 0xC, bottom);
        const bool seen = !clip || (left < Load<int32_t>(clip + 8) && top < Load<int32_t>(clip + 0xC) &&
                                    right > Load<int32_t>(clip) && bottom > Load<int32_t>(clip + 4));
        if (seen && w != 0 && h != 0) {
            ++placed;
            const uint32_t pitch = Load<uint32_t>(cache + 0x10);
            Store(e + 0x10, pitch * Load<uint32_t>(g + 4) + Load<uint32_t>(cache + 4) + Load<uint32_t>(g));
            Store(e + 0x14, pitch);
            Store(e + 0x18, w);
            Store(e + 0x1C, h);
            Store(e + 0x20, c);
            e += kGlyphBytes;
        }
        penX = static_cast<int32_t>(static_cast<uint32_t>(penX) + static_cast<uint32_t>(Load<int16_t>(g + 0x14)) +
                                    Load<uint32_t>(f + font::kSpacing));
    }
    if (picture && placed) {
        const Address colourAt = mode == 2 ? Load<Address>(f + font::kRampNow) : At(colours);
        Virtual<uint32_t>(Blitter(self), blit::kDraw, self, picture, list, placed, Kind(mode), colourAt, clip);
    }
    if (pen) {
        Store(pen, penX);
        Store(pen + 4, penY);
    }
    if (began) {
        Virtual<uint32_t>(Blitter(self), blit::kEnd, self, picture, Kind(mode));
        Store(f + font::kDepth, Load<uint32_t>(f + font::kDepth) - 1);
    }
    return loaded;
}

// The game's 0x0089A35B (slot 52, ret 0x24): the text wrapped into the
// rectangle (left, top, right, bottom) a line at a time. When loaded and
// given text: the text made UTF-16 (vector 0), the encoding 0x10 while it
// works; with a picture and no begin yet the blitter's begin (AL 0:
// answers 0). With a width: lines from the rectangle's top (the first from
// the pen's x when it is not the left), each FitString's (mode 2), placed
// by `align` (0 left, 1 centred, 2 right, else where the last ended),
// drawn (slot 51) without its trailing line breaks, a line height apart,
// while inside the rectangle and text is left. Each line's DrawString is
// given a pen of its own set to the line's start, which it moves to the
// line's end; the pen gets that at the end. Then the end if it began. Answers +0x04's byte. `wrote` says
// whether a line set the pen (the game's is its stack's otherwise).
uint32_t DrawStringInRect(void* self, Address picture, Address rect, Address text, uint32_t count, uint32_t align,
                          uint32_t colour, uint32_t colour2, Address clip, Address pen, bool* wrote = nullptr)
{
    const Address f = At(self);
    const uint8_t loaded = Load<uint8_t>(f + font::kLoaded);
    if (!loaded || count == 0)
        return loaded;
    Address u = 0;
    uint32_t n = 0;
    ToUnicode(self, text, count, u, n, 0, 0);
    const uint32_t saved = Load<uint32_t>(f + font::kEncoding);
    Store<uint32_t>(f + font::kEncoding, 0x10);
    bool began = false;
    if (picture && Load<uint32_t>(f + font::kDepth) == 0) {
        if (!(Virtual<uint32_t>(Blitter(self), blit::kBegin, self, picture, Kind(Load<uint32_t>(f + font::kMode))) &
              0xFF)) {
            Store(f + font::kEncoding, saved);
            return 0;
        }
        Store(f + font::kDepth, Load<uint32_t>(f + font::kDepth) + 1);
        began = true;
    }
    int32_t y = Load<int32_t>(rect + 4);
    int32_t x = Load<int32_t>(rect);
    const int32_t line = static_cast<int32_t>(Virtual<uint32_t>(self, slot::kLineHeight));
    const int32_t width = Load<int32_t>(rect + 8) - Load<int32_t>(rect);
    const Address end = u + n * 2;
    if (width > 0) {
        int32_t start[2] = {pen ? Load<int32_t>(pen) : 0, pen ? Load<int32_t>(pen + 4) : 0};
        Address startAt = 0;
        if (pen && Load<int32_t>(pen) != Load<int32_t>(rect))
            x = Load<int32_t>(pen);
        if (y < Load<int32_t>(rect + 0xC)) {
            Address p = u;
            do {
                if (p >= end)
                    break;
                const uint32_t k = Virtual<uint32_t>(self, slot::kFit, p, n, static_cast<uint32_t>(width), 0u, 2u);
                if (align == 0) {
                    x = Load<int32_t>(rect);
                } else if (align == 1) {
                    const int32_t m = static_cast<int32_t>(Virtual<uint32_t>(self, slot::kMeasure, p, k));
                    x = width / 2 - m / 2 + Load<int32_t>(rect);
                } else if (align == 2) {
                    x = Load<int32_t>(rect + 8) - static_cast<int32_t>(Virtual<uint32_t>(self, slot::kMeasure, p, k));
                }
                if (pen) {
                    start[0] = x;
                    start[1] = y;
                    startAt = At(start);
                    if (wrote)
                        *wrote = true;
                }
                if (picture) {
                    int32_t e = static_cast<int32_t>(k);
                    if (e > 0) {
                        do {
                            const uint16_t c = Load<uint16_t>(p + static_cast<uint32_t>(e) * 2 - 2);
                            if (c != 10 && c != 13)
                                break;
                            --e;
                        } while (e > 0);
                    }
                    if (e != 0)
                        Virtual<uint32_t>(self, slot::kDrawString, picture, x, y, p, static_cast<uint32_t>(e),
                                          colour, colour2, clip, startAt);
                }
                n -= k;
                p += k * 2;
                y += line;
            } while (y < Load<int32_t>(rect + 0xC));
        }
        if (pen) {
            Store(pen, start[0]);
            Store(pen + 4, start[1]);
        }
    }
    if (began) {
        Virtual<uint32_t>(Blitter(self), blit::kEnd, self, picture, Kind(Load<uint32_t>(f + font::kMode)));
        Store(f + font::kDepth, Load<uint32_t>(f + font::kDepth) - 1);
    }
    Store(f + font::kEncoding, saved);
    return loaded;
}

// ====================================================================================
// Detours

bool g_compare = false;

// Above 0 while the game's side of a comparison runs: our detours inside it
// pass through to the game's.
int g_inGame = 0;

struct GameSide {
    GameSide() { ++g_inGame; }
    ~GameSide() { --g_inGame; }
    GameSide(const GameSide&) = delete;
    GameSide& operator=(const GameSide&) = delete;
};

// Above 0 while ours runs in a comparison: our detours inside it run ours,
// uncompared.
int g_oursSide = 0;

struct OursSide {
    OursSide() { ++g_oursSide; }
    ~OursSide() { --g_oursSide; }
    OursSide(const OursSide&) = delete;
    OursSide& operator=(const OursSide&) = delete;
};

bool Ours() { return g_oursSide > 0 || !g_compare; }

// Around ours' side of a comparison: the Thai pass pushes on the offsets
// (+0xD8) at every rebuild, so the two runs would push twice; what ours
// pushed is taken off again (as in font_measure.cpp).
struct OffsetsKept {
    Address font;
    uint32_t size;
    explicit OffsetsKept(const void* self)
        : font(At(self)), size(Load<Address>(font + font::kOffsetsEnd) - Load<Address>(font + font::kOffsets))
    {
    }
    ~OffsetsKept() { Store<Address>(font + font::kOffsetsEnd, Load<Address>(font + font::kOffsets) + size); }
    OffsetsKept(const OffsetsKept&) = delete;
    OffsetsKept& operator=(const OffsetsKept&) = delete;
};

std::string Words(std::initializer_list<uint32_t> words)
{
    std::string text;
    char one[12];
    for (const uint32_t w : words) {
        std::snprintf(one, sizeof one, "%X ", w);
        text += one;
    }
    return text;
}

void Report(const char* id, const std::string& game, const std::string& ours)
{
    compare::Record(id, game == ours, game == ours ? std::string() : game, game == ours ? std::string() : ours);
}

// FNV-1a over bytes.
uint32_t BytesHash(Address bytes, uint32_t count)
{
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < count; ++i)
        hash = (hash ^ Load<uint8_t>(bytes + i)) * 16777619u;
    return hash;
}

// The blitter's calls in a comparison, as text. The game's side records
// through a blitter that passes each call on to the font's own (with the
// font's +0x8C its own again while it runs); ours' side records through
// one that answers each begin as the game's did.
struct Recorder {
    std::string calls;
    std::vector<uint32_t> begins;
    std::size_t next = 0;
    bool replay = false;
};

struct RecordingBlitter {
    void** vtable;
    void* real;
    Recorder* recorder;
};

std::string ColoursOf(const void* font, Address colours)
{
    if (colours && colours == Load<Address>(At(font) + font::kRampNow))
        return "ramp " + Words({BytesHash(colours, kRampBytes)});
    if (!colours)
        return "none ";
    return Words({Load<uint32_t>(colours), Load<uint32_t>(colours + 4)});
}

// Passes a call on to the font's own blitter with +0x8C its own meanwhile.
struct RealBlitter {
    Address font;
    Address proxy;
    RealBlitter(void* f, RecordingBlitter* b) : font(At(f)), proxy(Load<Address>(font + font::kBlitter))
    {
        Store(font + font::kBlitter, At(b->real));
    }
    ~RealBlitter() { Store(font + font::kBlitter, proxy); }
    RealBlitter(const RealBlitter&) = delete;
    RealBlitter& operator=(const RealBlitter&) = delete;
};

uint32_t __fastcall RecBegin(RecordingBlitter* b, void*, void* f, Address picture, uint32_t kind)
{
    Recorder& r = *b->recorder;
    r.calls += "B " + Words({picture, kind});
    uint32_t answer;
    if (r.replay) {
        answer = r.next < r.begins.size() ? r.begins[r.next++] : 1;
    } else {
        RealBlitter real(f, b);
        answer = Virtual<uint32_t>(b->real, blit::kBegin, f, picture, kind);
        r.begins.push_back(answer & 0xFF);
    }
    r.calls += Words({answer & 0xFF}) + "; ";
    return answer;
}

uint32_t __fastcall RecEnd(RecordingBlitter* b, void*, void* f, Address picture, uint32_t kind)
{
    Recorder& r = *b->recorder;
    r.calls += "E " + Words({picture, kind}) + "; ";
    if (r.replay)
        return 1;
    RealBlitter real(f, b);
    return Virtual<uint32_t>(b->real, blit::kEnd, f, picture, kind);
}

uint32_t __fastcall RecDraw(RecordingBlitter* b, void*, void* f, Address picture, Address list, uint32_t count,
                            uint32_t kind, Address colours, Address clip)
{
    Recorder& r = *b->recorder;
    r.calls += "D " + Words({picture, count, kind, list == Load<Address>(At(f) + font::kGlyphs) ? 1u : 0u}) +
               ColoursOf(f, colours) +
               (clip ? Words({Load<uint32_t>(clip), Load<uint32_t>(clip + 4), Load<uint32_t>(clip + 8),
                              Load<uint32_t>(clip + 0xC)})
                     : std::string("noclip ")) +
               Words({BytesHash(list, count * kGlyphBytes)}) + "; ";
    if (r.replay)
        return 1;
    RealBlitter real(f, b);
    return Virtual<uint32_t>(b->real, blit::kDraw, f, picture, list, count, kind, colours, clip);
}

void* g_recVtable[blit::kDraw / 4 + 1] = {};

void FillRecVtable()
{
    g_recVtable[blit::kBegin / 4] = reinterpret_cast<void*>(&RecBegin);
    g_recVtable[blit::kEnd / 4] = reinterpret_cast<void*>(&RecEnd);
    g_recVtable[blit::kDraw / 4] = reinterpret_cast<void*>(&RecDraw);
}

// The font's blitter behind a recording one while a side runs.
struct Recording {
    Address font;
    Address real;
    RecordingBlitter blitter;
    Recording(void* f, Recorder& r) : font(At(f)), real(Load<Address>(font + font::kBlitter))
    {
        blitter = {g_recVtable, Ptr(real), &r};
        Store(font + font::kBlitter, At(&blitter));
    }
    ~Recording() { Store(font + font::kBlitter, real); }
    Recording(const Recording&) = delete;
    Recording& operator=(const Recording&) = delete;
};

using DrawStringFn = uint32_t(__fastcall*)(void*, void*, Address, int32_t, int32_t, Address, uint32_t, uint32_t,
                                           uint32_t, Address, Address);
using DrawInRectFn = uint32_t(__fastcall*)(void*, void*, Address, Address, Address, uint32_t, uint32_t, uint32_t,
                                           uint32_t, Address, Address);
using RampFn = void(__fastcall*)(void*, void*, uint32_t, uint32_t);
using ResizeFn = void(__fastcall*)(void*, void*, uint32_t);

DrawStringFn g_drawString = nullptr;
DrawInRectFn g_drawInRect = nullptr;
RampFn g_ramp = nullptr;
ResizeFn g_resize = nullptr;

uint32_t __fastcall DDrawString(void* self, void*, Address picture, int32_t x, int32_t y, Address text,
                                uint32_t count, uint32_t colour, uint32_t colour2, Address clip, Address pen)
{
    OPENSC4_FIRST("DrawString 0x0089A099");
    if (g_inGame)
        return g_drawString(self, nullptr, picture, x, y, text, count, colour, colour2, clip, pen);
    if (Ours())
        return DrawString(self, picture, x, y, text, count, colour, colour2, clip, pen);
    if (!Blitter(self))
        return g_drawString(self, nullptr, picture, x, y, text, count, colour, colour2, clip, pen);
    int32_t mine[2] = {pen ? Load<int32_t>(pen) : 0, pen ? Load<int32_t>(pen + 4) : 0};
    Recorder game;
    uint32_t result;
    {
        GameSide side;
        Recording recording(self, game);
        result = g_drawString(self, nullptr, picture, x, y, text, count, colour, colour2, clip, pen);
    }
    Recorder ours;
    ours.replay = true;
    ours.begins = game.begins;
    uint32_t answer;
    {
        OffsetsKept kept(self);
        OursSide side;
        Recording recording(self, ours);
        answer = DrawString(self, picture, x, y, text, count, colour, colour2, clip, pen ? At(mine) : 0);
    }
    const std::string gamePen = pen ? Words({Load<uint32_t>(pen), Load<uint32_t>(pen + 4)}) : std::string();
    const std::string oursPen = pen ? Words({static_cast<uint32_t>(mine[0]), static_cast<uint32_t>(mine[1])})
                                    : std::string();
    Report("fontdraw.drawstring", Words({result & 0xFF}) + gamePen + game.calls,
           Words({answer & 0xFF}) + oursPen + ours.calls);
    return result;
}

uint32_t __fastcall DDrawInRect(void* self, void*, Address picture, Address rect, Address text, uint32_t count,
                                uint32_t align, uint32_t colour, uint32_t colour2, Address clip, Address pen)
{
    OPENSC4_FIRST("DrawStringInRect 0x0089A35B");
    if (g_inGame)
        return g_drawInRect(self, nullptr, picture, rect, text, count, align, colour, colour2, clip, pen);
    if (Ours())
        return DrawStringInRect(self, picture, rect, text, count, align, colour, colour2, clip, pen);
    if (!Blitter(self))
        return g_drawInRect(self, nullptr, picture, rect, text, count, align, colour, colour2, clip, pen);
    int32_t mine[2] = {pen ? Load<int32_t>(pen) : 0, pen ? Load<int32_t>(pen + 4) : 0};
    Recorder game;
    uint32_t result;
    {
        GameSide side;
        Recording recording(self, game);
        result = g_drawInRect(self, nullptr, picture, rect, text, count, align, colour, colour2, clip, pen);
    }
    Recorder ours;
    ours.replay = true;
    ours.begins = game.begins;
    uint32_t answer;
    bool wrote = false;
    {
        OffsetsKept kept(self);
        OursSide side;
        Recording recording(self, ours);
        answer = DrawStringInRect(self, picture, rect, text, count, align, colour, colour2, clip,
                                  pen ? At(mine) : 0, &wrote);
    }
    // A pen no line set holds what the game's stack held: not compared.
    const bool penCompared = pen && wrote;
    const std::string gamePen = penCompared ? Words({Load<uint32_t>(pen), Load<uint32_t>(pen + 4)}) : std::string();
    const std::string oursPen =
        penCompared ? Words({static_cast<uint32_t>(mine[0]), static_cast<uint32_t>(mine[1])}) : std::string();
    Report("fontdraw.drawinrect", Words({result & 0xFF}) + gamePen + game.calls,
           Words({answer & 0xFF}) + oursPen + ours.calls);
    return result;
}

// The ramps' list as the ramps in it, newest first.
std::vector<Address> RampsOf(const void* self)
{
    std::vector<Address> ramps;
    const Address header = Load<Address>(At(self) + font::kRamps);
    if (!header)
        return ramps;
    for (Address node = Load<Address>(header); node != header && ramps.size() < 64; node = Load<Address>(node))
        ramps.push_back(Load<Address>(node + 8));
    return ramps;
}

// Where a ramp was before (its index), or "new".
std::string PlaceOf(Address ramp, const std::vector<Address>& before, Address beforeNow)
{
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (before[i] == ramp)
            return Words({static_cast<uint32_t>(i)});
    }
    if (ramp && ramp == beforeNow)
        return "now ";
    return ramp ? "new " : "0 ";
}

void __fastcall DRamp(void* self, void*, uint32_t colour, uint32_t colour2)
{
    OPENSC4_FIRST("Ramp 0x0089838C");
    if (g_inGame)
        return g_ramp(self, nullptr, colour, colour2);
    if (Ours())
        return Ramp(self, colour, colour2);
    // The game's alone; ours worked out from the state before.
    const Address f = At(self);
    const Address beforeNow = Load<Address>(f + font::kRampNow);
    const std::vector<Address> before = RampsOf(self);
    {
        GameSide side;
        g_ramp(self, nullptr, colour, colour2);
    }
    const Address now = Load<Address>(f + font::kRampNow);
    std::string game = "now " + PlaceOf(now, before, beforeNow) + "list ";
    for (const Address r : RampsOf(self))
        game += PlaceOf(r, before, beforeNow);
    std::string ours;
    if (beforeNow && RampIs(beforeNow, colour, colour2)) {
        ours = "now " + PlaceOf(beforeNow, before, beforeNow) + "list ";
        for (const Address r : before)
            ours += PlaceOf(r, before, beforeNow);
    } else {
        std::size_t found = before.size();
        for (std::size_t i = 0; i < before.size(); ++i) {
            if (RampIs(before[i], colour, colour2)) {
                found = i;
                break;
            }
        }
        std::vector<Address> after;
        if (found < before.size()) {
            after.push_back(before[found]);
            for (std::size_t i = 0; i < before.size(); ++i) {
                if (i != found)
                    after.push_back(before[i]);
            }
            ours = "now " + PlaceOf(before[found], before, beforeNow) + "list ";
            for (const Address r : after)
                ours += PlaceOf(r, before, beforeNow);
        } else {
            ours = "now new list new ";
            const std::size_t kept = before.size() >= 4 ? before.size() - 1 : before.size();
            for (std::size_t i = 0; i < kept; ++i)
                ours += PlaceOf(before[i], before, beforeNow);
            // A new ramp's colours.
            uint8_t rows[kRampBytes] = {};
            {
                OursSide side;
                RampRows(At(rows), colour, colour2);
            }
            ours += Words({BytesHash(At(rows), kRampBytes)});
            game += Words({now ? BytesHash(now, kRampBytes) : 0u});
        }
    }
    Report("fontdraw.ramp", game, ours);
}

void __fastcall DResize(void* vector, void*, uint32_t count)
{
    OPENSC4_FIRST("Resize 0x00899A73");
    if (g_inGame)
        return g_resize(vector, nullptr, count);
    if (Ours())
        return Resize(At(vector), count);
    // The game's alone: the size it leaves, and the glyphs it kept (the new
    // ones are copies of what the game's stack held).
    const Address v = At(vector);
    const uint32_t before = (Load<Address>(v + 4) - Load<Address>(v)) / kGlyphBytes;
    const uint32_t kept = before < count ? before : count;
    const uint32_t keptHash = BytesHash(Load<Address>(v), kept * kGlyphBytes);
    {
        GameSide side;
        g_resize(vector, nullptr, count);
    }
    const uint32_t after = (Load<Address>(v + 4) - Load<Address>(v)) / kGlyphBytes;
    Report("fontdraw.resize", Words({after, BytesHash(Load<Address>(v), kept * kGlyphBytes)}),
           Words({count, keptHash}));
}

OPENSC4_THUNK(TDrawString, DDrawString, 36)
OPENSC4_THUNK(TDrawInRect, DDrawInRect, 36)
OPENSC4_THUNK(TRamp, DRamp, 8)
OPENSC4_THUNK(TResize, DResize, 4)

// ====================================================================================
// Installation

struct Detour {
    std::uintptr_t address;
    void (*thunk)();
    void** original;
};

template <typename Fn>
void** Slot(Fn* fn)
{
    return reinterpret_cast<void**>(fn);
}

bool InstallDraw(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compare = mode == registry::Mode::Compare;
    FillRecVtable();
    const Detour detours[] = {
        {addr::GZFont_DrawString, &TDrawString, Slot(&g_drawString)},
        {addr::GZFont_DrawStringInRect, &TDrawInRect, Slot(&g_drawInRect)},
        {addr::GZFont_Ramp, &TRamp, Slot(&g_ramp)},
        {addr::GZFont_GlyphsResize, &TResize, Slot(&g_resize)},
    };
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.thunk), detour.original))
            return false;
    }
    log::Write("fontdraw: draw: %zu functions %s", std::size(detours),
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registered[] = {
    registry::Register({"framework.font.font.draw", "framework/font", registry::Kind::Detour, registry::Phase::Attach,
                        addr::GZFont_DrawString,
                        "Our font's drawing: DrawString, DrawStringInRect, the colour ramp and the resize of the "
                        "placed glyphs (4 detours; docs/fonts.md)",
                        true, InstallDraw}),
};

} // namespace
} // namespace opensc4::fontdraw
