// Our font's measuring and fitting, the sixth slice of f3 of docs/fonts.md
// ("f3f: the font's measuring and fitting are ours"): eighteen more
// functions of class 0x1AA404AC (vtable 0x00AC55D8), detours in one group,
// off by default and installed at Attach:
//
//   framework.font.font.measure   MeasureString (slot 48, 0x00899F71), its
//                                 trimmed form (slot 49, 0x00899FF4),
//                                 FitString (slot 41, 0x00899D81), the
//                                 count that fits (slot 42, 0x00896808),
//                                 the lines (slot 43, 0x00896886), the
//                                 width for a height (slot 44, 0x008968EB),
//                                 the rectangle that fits (slot 45,
//                                 0x008973D4), the measured rectangle (slot
//                                 46, 0x00896957), one line's rectangle
//                                 (slot 47, 0x008969F2), the shortener
//                                 (slot 50, 0x00896A2D), a character's
//                                 width, bearing and advance (slots 36, 58
//                                 and 40: 0x008972BD, 0x00897304,
//                                 0x00897348), whether every character has
//                                 a glyph (slot 27, 0x00899CA7), the glyphs
//                                 got ahead (slot 28, 0x00899D2A), the
//                                 first character of a text (0x0089671F),
//                                 a character found in a text (0x0089738F)
//                                 and the spaces lines break at
//                                 (0x00896254, the character in EAX)
//
// ToUnicode (f3d, 0x00899BCA), GetGlyph (f3e, slot 60), the text code of
// RZText (the conversion 0x00910E18, a character's units 0x009106A9, the
// units of a count of characters 0x00910855, a word's length 0x0091227B,
// a cluster's 0x00912588, Thai 0x009100DE and its cluster 0x00911CDE),
// T2K's glyph lookup 0x0089F538 and the string objects' own slots stay the
// game's, called at their addresses, as do these eighteen when they call
// one another (through the font's slots, or at their addresses where the
// game calls them so).
//
// The font's fields are listed in font_object.cpp; these use +0x04 (set
// while loaded), +0x74 the spacing every advance adds, +0x7C the encoding
// (set to 0x10 around the work on a text made UTF-16, as the game does),
// +0xD8 the offsets the Thai pass leaves (for each word of its text, the
// index of the input's word it came from) and +0xEC the T2K holder.
//
// Compare mode: each runs ours first, then the game's on what it was
// called with, and compares what each answered and wrote: the out words
// (ours into words of its own), a rectangle (ours on a copy), the string
// the shortener shortens (ours on a string object of our own with the same
// text). While the game's side runs, our detours inside it pass through to
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

namespace opensc4::fontmeasure {
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
    log::Write("fontmeasure: first %s", name);
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
constexpr std::uintptr_t kConvert = 0x00910E18;    // cdecl (from, count, encoding, to, count*, encoding)
constexpr std::uintptr_t kCharUnits = 0x009106A9;  // cdecl (text, count, encoding): the first character's units
constexpr std::uintptr_t kUnitsOf = 0x00910855;    // cdecl (text, characters, encoding): their units
constexpr std::uintptr_t kWordLength = 0x0091227B; // cdecl (words, count): the words up to a break
constexpr std::uintptr_t kCluster = 0x00912588;    // cdecl (text, count, encoding): a cluster's characters
constexpr std::uintptr_t kIsThai = 0x009100DE;     // cdecl (character): 0x0E00 to 0x0E7F
constexpr std::uintptr_t kThaiCluster = 0x00911CDE; // cdecl (words, count): a Thai cluster's words
constexpr std::uintptr_t kT2kHasGlyph = 0x0089F538; // cdecl (scaler, character, 0, error*)

// These functions, at their addresses (ours when the group is on).
constexpr std::uintptr_t kIsSpace = 0x00896254;
constexpr std::uintptr_t kDecode = 0x0089671F;
constexpr std::uintptr_t kFind = 0x0089738F;

constexpr uint32_t kUtf16 = 0x10;

namespace font {
constexpr Address kLoaded = 4;
constexpr Address kSpacing = 0x74;
constexpr Address kEncoding = 0x7C;
constexpr Address kOffsets = 0xD8, kOffsetsEnd = 0xDC;
constexpr Address kHolder = 0xEC;
} // namespace font

namespace slot {
constexpr std::size_t kLineHeight = 0x8C;
constexpr std::size_t kWidth = 0x90;
constexpr std::size_t kHeight = 0x94;
constexpr std::size_t kAdvance = 0xA0;
constexpr std::size_t kFit = 0xA4;
constexpr std::size_t kFitCount = 0xA8;
constexpr std::size_t kLines = 0xAC;
constexpr std::size_t kWidthFor = 0xB0;
constexpr std::size_t kMeasure = 0xC0;
constexpr std::size_t kTrimmed = 0xC4;
constexpr std::size_t kGetGlyph = 0xF0;
} // namespace slot

// The string objects' slots the shortener uses.
namespace text {
constexpr std::size_t kData = 0x18, kSize = 0x1C, kResize = 0x3C, kAppend = 0x44;
}

// A glyph's entry, as GetGlyph answers it (f3e): the box (left, top,
// right, bottom), the left bearing, the top, the advance.
namespace glyph {
constexpr Address kLeft = 0, kRight = 8, kBearing = 0x10, kAdvance = 0x14;
}

uint32_t Encoding(const void* self) { return Load<uint32_t>(At(self) + font::kEncoding); }
void SetEncoding(void* self, uint32_t encoding) { Store(At(self) + font::kEncoding, encoding); }

// ToUnicode through the font's vector `which`.
void ToUnicode(void* self, Address text, uint32_t count, Address& out, uint32_t& outCount, uint32_t which,
               uint32_t keep)
{
    This<void>(kToUnicode, self, text, count, &out, &outCount, which, keep);
}

uint16_t Word(Address p) { return Load<uint16_t>(p); }

// The breaking space at its address (the character in EAX; ECX and EDX
// kept).
__declspec(naked) uint32_t __cdecl CallIsSpace(std::uintptr_t /*fn*/, uint32_t /*character*/)
{
    __asm {
        mov eax, dword ptr [esp + 8]
        call dword ptr [esp + 4]
        ret
    }
}

bool IsSpaceAt(uint32_t character) { return (CallIsSpace(kIsSpace, character) & 0xFF) != 0; }

// ====================================================================================
// The functions

// The game's 0x00896254 (the character in EAX, plain ret, only EAX
// changed): 1 for the space and 0x2002 to 0x200B, else 0.
uint32_t IsSpace(uint32_t c) { return c == 0x20 || (c >= 0x2002 && c <= 0x200B) ? 1 : 0; }

// The game's 0x0089671F (ret 0xC): the first character of a text in the
// font's encoding and, in `units` when given, how many units it takes. 0
// for no text (`units` untouched). UTF-16: the word, 2. A byte below 0x80:
// itself, 1. ASCII above it: 0x00910E18 converts one byte, 1. Others: the
// character's units (0x009106A9) converted, and `units` what the
// conversion answers. The word converted into is the upper half of the
// argument that held the text, so a conversion that writes nothing
// answers that half.
uint32_t Decode(const void* self, Address text, uint32_t count, Address units)
{
    if (count == 0)
        return 0;
    const uint32_t encoding = Encoding(self);
    if (encoding == kUtf16) {
        if (units)
            Store<uint32_t>(units, 2);
        return Word(text);
    }
    if (Load<uint8_t>(text) <= 0x7F) {
        if (units)
            Store<uint32_t>(units, 1);
        return Load<uint8_t>(text);
    }
    uint32_t slotText = text;
    uint32_t room = 1;
    const Address word = At(&slotText) + 2;
    if (encoding == 0) {
        Cdecl<uint32_t>(kConvert, text, 1u, 0u, word, &room, kUtf16);
        if (units)
            Store<uint32_t>(units, 1);
        return Load<uint16_t>(word);
    }
    const uint32_t length = Cdecl<uint32_t>(kCharUnits, text, count, encoding);
    const uint32_t converted = Cdecl<uint32_t>(kConvert, text, length, Encoding(self), word, &room, kUtf16);
    if (units)
        Store<uint32_t>(units, converted);
    return Load<uint16_t>(word);
}

// The first character for slots 36, 58 and 40: the word in UTF-16, else
// 0x0089671F.
uint32_t FirstOf(void* self, Address text, uint32_t count)
{
    if (Encoding(self) == kUtf16)
        return Word(text);
    return This<uint32_t>(kDecode, self, text, count, 0u);
}

// The game's 0x008972BD (slot 36, ret 8): the width of the first
// character's glyph box (right less left); 0 when not loaded or without a
// glyph.
uint32_t GlyphWidth(void* self, Address text, uint32_t count)
{
    if (!Load<uint8_t>(At(self) + font::kLoaded))
        return 0;
    const Address g = Virtual<Address>(self, slot::kGetGlyph, FirstOf(self, text, count));
    if (!g)
        return 0;
    return Load<uint32_t>(g + glyph::kRight) - Load<uint32_t>(g + glyph::kLeft);
}

// The game's 0x00897304 (slot 58, ret 8): the first character's left
// bearing.
uint32_t Bearing(void* self, Address text, uint32_t count)
{
    if (!Load<uint8_t>(At(self) + font::kLoaded))
        return 0;
    const Address g = Virtual<Address>(self, slot::kGetGlyph, FirstOf(self, text, count));
    if (!g)
        return 0;
    return static_cast<uint32_t>(static_cast<int32_t>(Load<int16_t>(g + glyph::kBearing)));
}

// The game's 0x00897348 (slot 40, ret 8): the first character's advance
// plus the font's spacing (+0x74).
uint32_t Advance(void* self, Address text, uint32_t count)
{
    if (!Load<uint8_t>(At(self) + font::kLoaded))
        return 0;
    const Address g = Virtual<Address>(self, slot::kGetGlyph, FirstOf(self, text, count));
    if (!g)
        return 0;
    return static_cast<uint32_t>(static_cast<int32_t>(Load<int16_t>(g + glyph::kAdvance))) +
           Load<uint32_t>(At(self) + font::kSpacing);
}

// The game's 0x0089738F (ret 0xC): where the character is in the text, in
// units, or -1. The end is the text plus the count in bytes, whatever the
// encoding, and each step is 0x0089671F's on the whole count.
uint32_t Find(void* self, Address text, uint32_t count, uint32_t character)
{
    const Address end = text + count;
    uint32_t step = text;
    for (Address p = text; p < end; p += step) {
        if (This<uint32_t>(kDecode, self, p, count, &step) == character)
            return p - text;
    }
    return 0xFFFFFFFF;
}

// The game's 0x00899F71 (slot 48, ret 8): the text made UTF-16 (vector 4)
// and each cluster's first character's advance (slot 40) summed; clusters
// are 0x00912588's in the font's encoding (0x10 while it runs).
uint32_t Measure(void* self, Address text, uint32_t count)
{
    uint32_t sum = 0;
    if (count == 0)
        return 0;
    Address u = 0;
    uint32_t n = 0;
    ToUnicode(self, text, count, u, n, 4, 0);
    const uint32_t saved = Encoding(self);
    const Address end = u + n * 2;
    SetEncoding(self, kUtf16);
    for (Address p = u; p < end;) {
        sum += Virtual<uint32_t>(self, slot::kAdvance, p, 1u);
        const uint32_t step = Cdecl<uint32_t>(kCluster, p, static_cast<uint32_t>(static_cast<int32_t>(end - p) >> 1),
                                              Encoding(self));
        p += step * 2;
    }
    SetEncoding(self, saved);
    return sum;
}

// The game's 0x00899FF4 (slot 49, ret 0xC): MeasureString without the
// trailing breaking spaces (vector 3). `length` is the words measured in
// UTF-16 and ASCII; in other encodings 0x00910855 of the text and of
// `length`'s 0, so 0.
uint32_t MeasureTrimmed(void* self, Address text, uint32_t count, Address length)
{
    uint32_t width = 0;
    Store<uint32_t>(length, 0);
    if (count == 0)
        return 0;
    Address u = 0;
    ToUnicode(self, text, count, u, count, 3, 0);
    for (Address p = u + count * 2 - 2; p >= u; p -= 2) {
        if (!IsSpaceAt(Word(p)))
            break;
        --count;
    }
    if (count == 0)
        return 0;
    const uint32_t saved = Encoding(self);
    SetEncoding(self, kUtf16);
    width = Virtual<uint32_t>(self, slot::kMeasure, u, count);
    SetEncoding(self, saved);
    if (saved == kUtf16 || saved == 0)
        Store<uint32_t>(length, count);
    else
        Store<uint32_t>(length, Cdecl<uint32_t>(kUnitsOf, text, Load<uint32_t>(length), saved));
    return width;
}

// FitString's first character it may stop after: past one breaking space
// that ends a word (more than one character in, the one before not a
// space).
uint32_t PastSpace(Address u, uint32_t at, uint32_t count)
{
    if (static_cast<int32_t>(at) > 1 && !IsSpaceAt(Word(u + at * 2 - 2)) && at < count && IsSpaceAt(Word(u + at * 2)))
        return at + 1;
    return at;
}

// The game's 0x00899D81 (slot 41, ret 0x14): how much of the text fits the
// width, in the font's units, with its width (MeasureString) in `out`. The
// text is made UTF-16 (vector 2, the offsets kept); words (0x0091227B) are
// taken while their trimmed width (slot 49) still fits, up to a line break
// (taken). When not even the first fits: mode 1 takes the first word
// anyway (to a 0, a line break or a breaking space), mode 2 characters
// (Thai clusters) while what is taken measures below the width, less the
// last unless it is the first, one more where the Thai pass made two words
// of one; other modes nothing. A breaking space after the last word goes
// with it. Back through the offsets, then 0x00910855 to units.
uint32_t FitString(void* self, Address text, uint32_t count, uint32_t width, Address out, uint32_t mode)
{
    if (width == 0 || count == 0) {
        if (out)
            Store<uint32_t>(out, 0);
        return 0;
    }
    Address u = 0;
    ToUnicode(self, text, count, u, count, 2, 1);
    const uint32_t saved = Encoding(self);
    Address p = u;
    SetEncoding(self, kUtf16);
    uint32_t used = 0;
    uint32_t at = 0;
    bool tail = true;
    while (at < count) {
        const uint16_t c = Word(p);
        if (c == 10 || c == 13) {
            ++at;
            break;
        }
        const uint32_t n = Cdecl<uint32_t>(kWordLength, p, count - at);
        uint32_t trimmedLength = 0;
        const uint32_t trimmed = Virtual<uint32_t>(self, slot::kTrimmed, p, n, &trimmedLength);
        if (trimmed + used > width)
            break;
        used += Virtual<uint32_t>(self, slot::kMeasure, p, n);
        at += n;
        p += n * 2;
        if (trimmed > width)
            break;
    }
    if (at == 0) {
        if (mode == 1) {
            if (count == 0) {
                tail = false;
            } else {
                do {
                    const uint16_t c = Word(u + at * 2);
                    if (c == 0 || c == 10 || c == 13 || IsSpaceAt(c))
                        break;
                    at += Cdecl<uint32_t>(kWordLength, u + at * 2, count - at);
                } while (at < count);
            }
        } else if (mode == 2) {
            if (width > at) {
                uint32_t before;
                do {
                    before = at;
                    uint32_t step = 1;
                    if (Cdecl<uint32_t>(kIsThai, uint32_t{Word(u + at * 2)}) & 0xFF)
                        step = Cdecl<uint32_t>(kThaiCluster, u + at * 2, count - at);
                    at += step;
                } while (Virtual<uint32_t>(self, slot::kMeasure, u, at) < width);
                if (before != 0)
                    at = before;
            }
            const Address offsets = Load<Address>(At(self) + font::kOffsets);
            if (at < count && offsets != Load<Address>(At(self) + font::kOffsetsEnd) &&
                Load<uint32_t>(offsets + at * 4 - 4) == Load<uint32_t>(offsets + at * 4))
                ++at;
        } else {
            tail = false;
        }
    }
    if (tail)
        at = PastSpace(u, at, count);
    SetEncoding(self, saved);
    const Address offsets = Load<Address>(At(self) + font::kOffsets);
    if (offsets != Load<Address>(At(self) + font::kOffsetsEnd))
        at = Load<uint32_t>(offsets + at * 4);
    const uint32_t units = Cdecl<uint32_t>(kUnitsOf, text, at, saved);
    if (out)
        Store<uint32_t>(out, Virtual<uint32_t>(self, slot::kMeasure, text, units));
    return units;
}

// The game's 0x00896808 (slot 42, ret 0x10): how many units of the text fit
// the width, a character (0x009106A9) at a time, measuring (slot 48) what
// is taken; one past is given back. `out` gets the width of what is kept.
uint32_t FitCount(void* self, Address text, uint32_t count, uint32_t width, Address out)
{
    uint32_t measured = 0;
    uint32_t at = 0;
    if (count != 0) {
        uint32_t beforeAt = 0, beforeMeasured = 0;
        while (measured < width) {
            beforeMeasured = measured;
            beforeAt = at;
            const uint32_t encoding = Encoding(self);
            const Address p = encoding == kUtf16 ? text + at * 2 : text + at;
            at += Cdecl<uint32_t>(kCharUnits, p, count - at, encoding);
            measured = Virtual<uint32_t>(self, slot::kMeasure, text, at);
            if (at >= count)
                break;
        }
        if (measured > width) {
            at = beforeAt;
            measured = beforeMeasured;
        }
    }
    if (out)
        Store<uint32_t>(out, measured);
    return at;
}

// The game's 0x00896886 (slot 43, ret 0xC): the lines FitString (slot 41,
// mode 2) breaks the text into at the width (0 taken as 1).
uint32_t Lines(void* self, Address text, uint32_t count, uint32_t width)
{
    uint32_t lines = 0;
    if (width == 0)
        width = 1;
    const Address end = Encoding(self) == kUtf16 ? text + count * 2 : text + count;
    Address p = text;
    while (p < end) {
        const uint32_t n = Virtual<uint32_t>(self, slot::kFit, p, count, width, 0u, 2u);
        p += Encoding(self) == kUtf16 ? n * 2 : n;
        count -= n;
        ++lines;
    }
    return lines;
}

// The game's 0x008968EB (slot 44, ret 0xC): the narrowest width, in tens
// from 20 to 4000, at which the text takes no more lines (slot 43) than the
// height holds (by the line height, slot 35); 0 when none does.
uint32_t WidthFor(void* self, Address text, uint32_t count, uint32_t height)
{
    const uint32_t lines = height / Virtual<uint32_t>(self, slot::kLineHeight);
    uint32_t low = 2;
    uint32_t high = 400;
    do {
        const uint32_t middle = (high + low) >> 1;
        if (Virtual<uint32_t>(self, slot::kLines, text, count, middle * 10) > lines) {
            low = middle + 1;
        } else {
            if (low == high)
                return low * 10;
            high = middle;
        }
    } while (low <= high);
    return 0;
}

// The game's 0x008973D4 (slot 45, ret 0x18): a rectangle for the text from
// the rectangle's corner. The height at least the line height (slot 35);
// widths of -1 taken from the width of 'a' (slot 36; the text it is given
// is the byte 'a' and, in UTF-16, the lowest byte of the widest width
// above it): the narrowest 12 of them, the widest 32 and at least twice the
// narrowest. Text without a line break (0x0089738F) measuring (slot 48)
// below the narrowest takes one line; below twice that, two lines at the
// width they need (slot 44) if narrower than the widest and the height
// allows; else below the widest one line. Otherwise the lines from the
// measure over the widest plus one (one, with a line break) up to what the
// height holds, each at the width it needs, the first that is not 0,
// narrower than the widest and within the height. AL 1, or 0 and an empty
// rectangle.
uint32_t FitRect(void* self, Address text, uint32_t count, Address rect, int32_t narrowest, int32_t widest,
                 int32_t height)
{
    int32_t measured = 1;
    const int32_t line = static_cast<int32_t>(Virtual<uint32_t>(self, slot::kLineHeight));
    if (height < line)
        height = line;
    int32_t narrow = narrowest;
    if (narrowest == -1 || widest == -1) {
        const uint8_t a[2] = {0x61, static_cast<uint8_t>(widest)};
        const int32_t w = static_cast<int32_t>(Virtual<uint32_t>(self, slot::kWidth, a, 1u));
        if (narrow == -1)
            narrow = w * 12;
        if (widest == -1)
            widest = static_cast<int32_t>(static_cast<uint32_t>(w) << 5);
        if (widest <= narrow)
            widest = narrow * 2;
    }
    const auto set = [&](int32_t across, int32_t down) {
        Store<int32_t>(rect + 8, Load<int32_t>(rect) + across);
        Store<int32_t>(rect + 0xC, Load<int32_t>(rect + 4) + down);
    };
    if (static_cast<int32_t>(This<uint32_t>(kFind, self, text, count, 10u)) == -1) {
        measured = static_cast<int32_t>(Virtual<uint32_t>(self, slot::kMeasure, text, count));
        if (measured < narrow && line <= height) {
            set(measured, line);
            return 1;
        }
        if (measured < narrow * 2) {
            const int32_t two = line * 2;
            const uint32_t w = Virtual<uint32_t>(self, slot::kWidthFor, text, count, two);
            if (w < static_cast<uint32_t>(widest) && two <= height) {
                set(static_cast<int32_t>(w), two);
                return 1;
            }
        }
        if (measured < widest && line <= height) {
            set(measured, line);
            return 1;
        }
    }
    const int32_t most = height / line;
    for (int32_t lines = measured / widest + 1; lines < most; ++lines) {
        const int32_t down = lines * line;
        const uint32_t w = Virtual<uint32_t>(self, slot::kWidthFor, text, count, down);
        if (w != 0 && w < static_cast<uint32_t>(widest) && down <= height) {
            set(static_cast<int32_t>(w), down);
            return 1;
        }
    }
    Store<int32_t>(rect + 8, Load<int32_t>(rect));
    Store<int32_t>(rect + 0xC, Load<int32_t>(rect + 4));
    return 0;
}

// The game's 0x00896957 (slot 46, ret 0x14): the rectangle's right and
// bottom for the text from its corner. No text: empty across, a line
// down. Not wrapped: MeasureString across, a line down. Wrapped to the
// rectangle's height (`fixed` 0): the width that needs (slot 44). Wrapped
// to its width: the lines (slot 43) down.
void MeasureRect(void* self, Address text, uint32_t count, Address rect, uint8_t wrapped, uint8_t fixed)
{
    if (count == 0 || text == 0) {
        Store<int32_t>(rect + 8, Load<int32_t>(rect));
    } else if (wrapped) {
        if (!fixed) {
            const uint32_t w = Virtual<uint32_t>(self, slot::kWidthFor, text, count,
                                                 Load<uint32_t>(rect + 0xC) - Load<uint32_t>(rect + 4));
            Store<uint32_t>(rect + 8, Load<uint32_t>(rect) + w);
            return;
        }
        const uint32_t line = Virtual<uint32_t>(self, slot::kLineHeight);
        const uint32_t lines =
            Virtual<uint32_t>(self, slot::kLines, text, count, Load<uint32_t>(rect + 8) - Load<uint32_t>(rect));
        Store<uint32_t>(rect + 0xC, lines * line + Load<uint32_t>(rect + 4));
        return;
    } else {
        const uint32_t w = Virtual<uint32_t>(self, slot::kMeasure, text, count);
        Store<uint32_t>(rect + 8, w + Load<uint32_t>(rect));
    }
    const uint32_t line = Virtual<uint32_t>(self, slot::kLineHeight);
    Store<uint32_t>(rect + 0xC, line + Load<uint32_t>(rect + 4));
}

// The game's 0x008969F2 (slot 47, ret 0xC): the rectangle of one line of
// the text at 0, 0: slot 37's height down, MeasureString across.
void LineRect(void* self, Address text, uint32_t count, Address rect)
{
    const uint32_t height = Virtual<uint32_t>(self, slot::kHeight, 0u, 0u);
    Store<uint32_t>(rect, 0);
    Store<uint32_t>(rect + 4, 0);
    Store<uint32_t>(rect + 8, 0);
    Store<uint32_t>(rect + 0xC, height);
    Store<uint32_t>(rect + 8, Virtual<uint32_t>(self, slot::kMeasure, text, count));
}

// The game's 0x00896A2D (slot 50, ret 0x10): a string object shortened to
// the rectangle's width, unless `keep`: when not all of it fits (slot
// 42), what fits beside the suffix's width (MeasureString), trailing
// spaces dropped, and the suffix appended; nothing at all when the suffix
// alone is not narrower. Answers the length it had.
uint32_t Shorten(void* self, void* string, void* suffix, Address rect, uint8_t keep)
{
    const uint32_t length = Virtual<uint32_t>(string, text::kSize);
    if (keep)
        return length;
    const int32_t width = Load<int32_t>(rect + 8) - Load<int32_t>(rect);
    const Address data = Virtual<Address>(string, text::kData);
    if (Virtual<uint32_t>(self, slot::kFitCount, data, length, width, 0u) >= length)
        return length;
    const uint32_t suffixSize = Virtual<uint32_t>(suffix, text::kSize);
    const Address suffixData = Virtual<Address>(suffix, text::kData);
    const int32_t suffixWidth = static_cast<int32_t>(Virtual<uint32_t>(self, slot::kMeasure, suffixData, suffixSize));
    if (suffixWidth >= width) {
        Virtual<uint32_t>(string, text::kResize, 0u);
        return length;
    }
    const Address again = Virtual<Address>(string, text::kData);
    const uint32_t fits = Virtual<uint32_t>(self, slot::kFitCount, again, length, width - suffixWidth, 0u);
    Virtual<uint32_t>(string, text::kResize, fits);
    while (Virtual<uint32_t>(string, text::kSize) != 0) {
        const Address d = Virtual<Address>(string, text::kData);
        const uint32_t size = Virtual<uint32_t>(string, text::kSize);
        if (Load<uint8_t>(d + size - 1) != ' ')
            break;
        Virtual<uint32_t>(string, text::kResize, Virtual<uint32_t>(string, text::kSize) - 1);
    }
    const uint32_t size = Virtual<uint32_t>(suffix, text::kSize);
    Virtual<uint32_t>(string, text::kAppend, Virtual<Address>(suffix, text::kData), size);
    return length;
}

// The game's 0x00899CA7 (slot 27, ret 8): whether T2K (0x0089F538) has a
// glyph for every word of the text made UTF-16 (vector 0). AL.
uint32_t HasGlyphs(void* self, Address text, uint32_t count)
{
    uint32_t all = 1;
    if (count == 0)
        return all;
    Address u = 0;
    uint32_t n = 0;
    ToUnicode(self, text, count, u, n, 0, 0);
    const uint32_t saved = Encoding(self);
    const Address end = u + n * 2;
    SetEncoding(self, kUtf16);
    for (Address p = u; p < end; p += 2) {
        uint32_t error;
        const Address scaler = Load<Address>(Load<Address>(At(self) + font::kHolder) + 8);
        if (!Cdecl<uint32_t>(kT2kHasGlyph, scaler, uint32_t{Word(p)}, 0u, &error)) {
            all = 0;
            break;
        }
    }
    SetEncoding(self, saved);
    return all;
}

// The game's 0x00899D2A (slot 28, ret 8): when loaded, every word of the
// text made UTF-16 (vector 0) got (slot 60). AL whether loaded.
uint32_t Preload(void* self, Address text, uint32_t count)
{
    const uint8_t loaded = Load<uint8_t>(At(self) + font::kLoaded);
    if (loaded && count != 0) {
        Address u = 0;
        uint32_t n = 0;
        ToUnicode(self, text, count, u, n, 0, 0);
        for (uint32_t i = 0; i < n; ++i)
            Virtual<Address>(self, slot::kGetGlyph, uint32_t{Word(u + i * 2)});
    }
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
// (+0xD8) at every rebuild, whatever `keep` is, so the two runs would push
// twice. What ours pushed is taken off again (the vector keeps what it
// grew to, which the game's run then needs no more than).
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

std::string Rect(Address rect)
{
    return Words({Load<uint32_t>(rect), Load<uint32_t>(rect + 4), Load<uint32_t>(rect + 8),
                  Load<uint32_t>(rect + 0xC)});
}

void Report(const char* id, const std::string& game, const std::string& ours)
{
    compare::Record(id, game == ours, game == ours ? std::string() : game, game == ours ? std::string() : ours);
}

// A string object of our own for ours' side of the shortener: the four
// slots it uses over a std::string, the others never called.
struct OwnString {
    void** vtable;
    std::string text;
};

Address __fastcall OwnData(OwnString* s, void*) { return At(s->text.data()); }
uint32_t __fastcall OwnSize(OwnString* s, void*) { return static_cast<uint32_t>(s->text.size()); }
uint32_t __fastcall OwnResize(OwnString* s, void*, uint32_t size)
{
    s->text.resize(size);
    return 1;
}
uint32_t __fastcall OwnAppend(OwnString* s, void*, Address data, uint32_t size)
{
    s->text.append(static_cast<const char*>(Ptr(data)), size);
    return 1;
}

void* g_ownVtable[text::kAppend / 4 + 1] = {};

void FillOwnVtable()
{
    g_ownVtable[text::kData / 4] = reinterpret_cast<void*>(&OwnData);
    g_ownVtable[text::kSize / 4] = reinterpret_cast<void*>(&OwnSize);
    g_ownVtable[text::kResize / 4] = reinterpret_cast<void*>(&OwnResize);
    g_ownVtable[text::kAppend / 4] = reinterpret_cast<void*>(&OwnAppend);
}

std::string StringOf(void* string)
{
    const uint32_t size = Virtual<uint32_t>(string, text::kSize);
    std::string text = Words({size});
    const Address data = Virtual<Address>(string, text::kData);
    if (data)
        text.append(static_cast<const char*>(Ptr(data)), size);
    return text;
}

using IsSpaceFn = void*;
using DecodeFn = uint32_t(__fastcall*)(void*, void*, Address, uint32_t, Address);
using TwoFn = uint32_t(__fastcall*)(void*, void*, Address, uint32_t);
using ThreeFn = uint32_t(__fastcall*)(void*, void*, Address, uint32_t, uint32_t);
using FourFn = uint32_t(__fastcall*)(void*, void*, Address, uint32_t, uint32_t, Address);
using FitStringFn = uint32_t(__fastcall*)(void*, void*, Address, uint32_t, uint32_t, Address, uint32_t);
using FitRectFn = uint32_t(__fastcall*)(void*, void*, Address, uint32_t, Address, int32_t, int32_t, int32_t);
using MeasureRectFn = void(__fastcall*)(void*, void*, Address, uint32_t, Address, uint32_t, uint32_t);
using LineRectFn = void(__fastcall*)(void*, void*, Address, uint32_t, Address);
using ShortenFn = uint32_t(__fastcall*)(void*, void*, void*, void*, Address, uint32_t);

IsSpaceFn g_isSpace = nullptr;
DecodeFn g_decode = nullptr;
TwoFn g_glyphWidth = nullptr;
TwoFn g_bearing = nullptr;
TwoFn g_advance = nullptr;
ThreeFn g_find = nullptr;
TwoFn g_measure = nullptr;
ThreeFn g_trimmed = nullptr;
FitStringFn g_fitString = nullptr;
FourFn g_fitCount = nullptr;
ThreeFn g_lines = nullptr;
ThreeFn g_widthFor = nullptr;
FitRectFn g_fitRect = nullptr;
MeasureRectFn g_measureRect = nullptr;
LineRectFn g_lineRect = nullptr;
ShortenFn g_shorten = nullptr;
TwoFn g_hasGlyphs = nullptr;
TwoFn g_preload = nullptr;

// A font's offsets for IsSpace, which has none.
uint8_t g_noFont[0x15C] = {};

// One that answers a word and writes nothing: ours, then the game's,
// compared (`mask` the answer's bits that count).
template <typename OursFn, typename GameFn>
uint32_t Compared(const char* id, const void* self, uint32_t mask, OursFn ours, GameFn game)
{
    uint32_t mine;
    {
        OffsetsKept kept(self ? self : &g_noFont);
        OursSide side;
        mine = ours();
    }
    uint32_t result;
    {
        GameSide side;
        result = game();
    }
    Report(id, Words({result & mask}), Words({mine & mask}));
    return result;
}

uint32_t __cdecl DIsSpace(uint32_t c)
{
    OPENSC4_FIRST("IsSpace 0x00896254");
    if (g_inGame)
        return CallIsSpace(reinterpret_cast<std::uintptr_t>(g_isSpace), c);
    if (Ours())
        return IsSpace(c);
    return Compared("fontmeasure.isspace", nullptr, 0xFFFFFFFF, [&] { return IsSpace(c); },
                    [&] { return CallIsSpace(reinterpret_cast<std::uintptr_t>(g_isSpace), c); });
}

uint32_t __fastcall DDecode(void* self, void*, Address text, uint32_t count, Address units)
{
    OPENSC4_FIRST("Decode 0x0089671F");
    if (g_inGame)
        return g_decode(self, nullptr, text, count, units);
    if (Ours())
        return Decode(self, text, count, units);
    uint32_t mine = 0xA5A5A5A5;
    uint32_t ours;
    {
        OffsetsKept kept(self);
        OursSide side;
        ours = Decode(self, text, count, units ? At(&mine) : 0);
    }
    const uint32_t before = units ? Load<uint32_t>(units) : 0;
    uint32_t result;
    {
        GameSide side;
        result = g_decode(self, nullptr, text, count, units);
    }
    const uint32_t after = units ? Load<uint32_t>(units) : 0;
    // A units word ours did not write is the game's as it was.
    const uint32_t oursUnits = units ? (mine == 0xA5A5A5A5 ? before : mine) : 0;
    Report("fontmeasure.decode", Words({result, after}), Words({ours, oursUnits}));
    return result;
}

// The three of slots 36, 58 and 40.
template <TwoFn* Original, uint32_t (*OursFn)(void*, Address, uint32_t)>
uint32_t GlyphMetric(const char* id, void* self, Address text, uint32_t count)
{
    if (g_inGame)
        return (*Original)(self, nullptr, text, count);
    if (Ours())
        return OursFn(self, text, count);
    return Compared(id, self, 0xFFFFFFFF, [&] { return OursFn(self, text, count); },
                    [&] { return (*Original)(self, nullptr, text, count); });
}

uint32_t __fastcall DGlyphWidth(void* self, void*, Address text, uint32_t count)
{
    OPENSC4_FIRST("GlyphWidth 0x008972BD");
    return GlyphMetric<&g_glyphWidth, &GlyphWidth>("fontmeasure.width", self, text, count);
}

uint32_t __fastcall DBearing(void* self, void*, Address text, uint32_t count)
{
    OPENSC4_FIRST("Bearing 0x00897304");
    return GlyphMetric<&g_bearing, &Bearing>("fontmeasure.bearing", self, text, count);
}

uint32_t __fastcall DAdvance(void* self, void*, Address text, uint32_t count)
{
    OPENSC4_FIRST("Advance 0x00897348");
    return GlyphMetric<&g_advance, &Advance>("fontmeasure.advance", self, text, count);
}

uint32_t __fastcall DFind(void* self, void*, Address text, uint32_t count, uint32_t character)
{
    OPENSC4_FIRST("Find 0x0089738F");
    if (g_inGame)
        return g_find(self, nullptr, text, count, character);
    if (Ours())
        return Find(self, text, count, character);
    return Compared("fontmeasure.find", self, 0xFFFFFFFF, [&] { return Find(self, text, count, character); },
                    [&] { return g_find(self, nullptr, text, count, character); });
}

uint32_t __fastcall DMeasure(void* self, void*, Address text, uint32_t count)
{
    OPENSC4_FIRST("MeasureString 0x00899F71");
    if (g_inGame)
        return g_measure(self, nullptr, text, count);
    if (Ours())
        return Measure(self, text, count);
    return Compared("fontmeasure.measure", self, 0xFFFFFFFF, [&] { return Measure(self, text, count); },
                    [&] { return g_measure(self, nullptr, text, count); });
}

uint32_t __fastcall DTrimmed(void* self, void*, Address text, uint32_t count, Address length)
{
    OPENSC4_FIRST("MeasureTrimmed 0x00899FF4");
    if (g_inGame)
        return g_trimmed(self, nullptr, text, count, length);
    if (Ours())
        return MeasureTrimmed(self, text, count, length);
    uint32_t mine = 0;
    uint32_t ours;
    {
        OffsetsKept kept(self);
        OursSide side;
        ours = MeasureTrimmed(self, text, count, At(&mine));
    }
    uint32_t result;
    {
        GameSide side;
        result = g_trimmed(self, nullptr, text, count, length);
    }
    Report("fontmeasure.trimmed", Words({result, Load<uint32_t>(length)}), Words({ours, mine}));
    return result;
}

uint32_t __fastcall DFitString(void* self, void*, Address text, uint32_t count, uint32_t width, Address out,
                               uint32_t mode)
{
    OPENSC4_FIRST("FitString 0x00899D81");
    if (g_inGame)
        return g_fitString(self, nullptr, text, count, width, out, mode);
    if (Ours())
        return FitString(self, text, count, width, out, mode);
    uint32_t mine = 0;
    uint32_t ours;
    {
        OffsetsKept kept(self);
        OursSide side;
        ours = FitString(self, text, count, width, out ? At(&mine) : 0, mode);
    }
    uint32_t result;
    {
        GameSide side;
        result = g_fitString(self, nullptr, text, count, width, out, mode);
    }
    Report("fontmeasure.fitstring", Words({result, out ? Load<uint32_t>(out) : 0}), Words({ours, mine}));
    return result;
}

uint32_t __fastcall DFitCount(void* self, void*, Address text, uint32_t count, uint32_t width, Address out)
{
    OPENSC4_FIRST("FitCount 0x00896808");
    if (g_inGame)
        return g_fitCount(self, nullptr, text, count, width, out);
    if (Ours())
        return FitCount(self, text, count, width, out);
    uint32_t mine = 0;
    uint32_t ours;
    {
        OffsetsKept kept(self);
        OursSide side;
        ours = FitCount(self, text, count, width, out ? At(&mine) : 0);
    }
    uint32_t result;
    {
        GameSide side;
        result = g_fitCount(self, nullptr, text, count, width, out);
    }
    Report("fontmeasure.fitcount", Words({result, out ? Load<uint32_t>(out) : 0}), Words({ours, mine}));
    return result;
}

uint32_t __fastcall DLines(void* self, void*, Address text, uint32_t count, uint32_t width)
{
    OPENSC4_FIRST("Lines 0x00896886");
    if (g_inGame)
        return g_lines(self, nullptr, text, count, width);
    if (Ours())
        return Lines(self, text, count, width);
    return Compared("fontmeasure.lines", self, 0xFFFFFFFF, [&] { return Lines(self, text, count, width); },
                    [&] { return g_lines(self, nullptr, text, count, width); });
}

uint32_t __fastcall DWidthFor(void* self, void*, Address text, uint32_t count, uint32_t height)
{
    OPENSC4_FIRST("WidthFor 0x008968EB");
    if (g_inGame)
        return g_widthFor(self, nullptr, text, count, height);
    if (Ours())
        return WidthFor(self, text, count, height);
    return Compared("fontmeasure.widthfor", self, 0xFFFFFFFF, [&] { return WidthFor(self, text, count, height); },
                    [&] { return g_widthFor(self, nullptr, text, count, height); });
}

uint32_t __fastcall DFitRect(void* self, void*, Address text, uint32_t count, Address rect, int32_t narrowest,
                             int32_t widest, int32_t height)
{
    OPENSC4_FIRST("FitRect 0x008973D4");
    if (g_inGame)
        return g_fitRect(self, nullptr, text, count, rect, narrowest, widest, height);
    if (Ours())
        return FitRect(self, text, count, rect, narrowest, widest, height);
    uint32_t copy[4];
    std::memcpy(copy, Ptr(rect), sizeof copy);
    uint32_t ours;
    {
        OffsetsKept kept(self);
        OursSide side;
        ours = FitRect(self, text, count, At(copy), narrowest, widest, height);
    }
    uint32_t result;
    {
        GameSide side;
        result = g_fitRect(self, nullptr, text, count, rect, narrowest, widest, height);
    }
    Report("fontmeasure.fitrect", Words({result & 0xFF}) + Rect(rect), Words({ours & 0xFF}) + Rect(At(copy)));
    return result;
}

void __fastcall DMeasureRect(void* self, void*, Address text, uint32_t count, Address rect, uint32_t wrapped,
                             uint32_t fixed)
{
    OPENSC4_FIRST("MeasureRect 0x00896957");
    if (g_inGame)
        return g_measureRect(self, nullptr, text, count, rect, wrapped, fixed);
    if (Ours())
        return MeasureRect(self, text, count, rect, static_cast<uint8_t>(wrapped), static_cast<uint8_t>(fixed));
    uint32_t copy[4];
    std::memcpy(copy, Ptr(rect), sizeof copy);
    {
        OffsetsKept kept(self);
        OursSide side;
        MeasureRect(self, text, count, At(copy), static_cast<uint8_t>(wrapped), static_cast<uint8_t>(fixed));
    }
    {
        GameSide side;
        g_measureRect(self, nullptr, text, count, rect, wrapped, fixed);
    }
    Report("fontmeasure.measurerect", Rect(rect), Rect(At(copy)));
}

void __fastcall DLineRect(void* self, void*, Address text, uint32_t count, Address rect)
{
    OPENSC4_FIRST("LineRect 0x008969F2");
    if (g_inGame)
        return g_lineRect(self, nullptr, text, count, rect);
    if (Ours())
        return LineRect(self, text, count, rect);
    uint32_t copy[4];
    std::memcpy(copy, Ptr(rect), sizeof copy);
    {
        OffsetsKept kept(self);
        OursSide side;
        LineRect(self, text, count, At(copy));
    }
    {
        GameSide side;
        g_lineRect(self, nullptr, text, count, rect);
    }
    Report("fontmeasure.linerect", Rect(rect), Rect(At(copy)));
}

uint32_t __fastcall DShorten(void* self, void*, void* string, void* suffix, Address rect, uint32_t keep)
{
    OPENSC4_FIRST("Shorten 0x00896A2D");
    if (g_inGame)
        return g_shorten(self, nullptr, string, suffix, rect, keep);
    if (Ours())
        return Shorten(self, string, suffix, rect, static_cast<uint8_t>(keep));
    std::string ours;
    {
        OwnString own{g_ownVtable, {}};
        const uint32_t size = Virtual<uint32_t>(string, text::kSize);
        const Address data = Virtual<Address>(string, text::kData);
        if (data)
            own.text.assign(static_cast<const char*>(Ptr(data)), size);
        OffsetsKept kept(self);
        OursSide side;
        const uint32_t answer = Shorten(self, &own, suffix, rect, static_cast<uint8_t>(keep));
        ours = Words({answer}) + StringOf(&own);
    }
    uint32_t result;
    {
        GameSide side;
        result = g_shorten(self, nullptr, string, suffix, rect, keep);
    }
    Report("fontmeasure.shorten", Words({result}) + StringOf(string), ours);
    return result;
}

uint32_t __fastcall DHasGlyphs(void* self, void*, Address text, uint32_t count)
{
    OPENSC4_FIRST("HasGlyphs 0x00899CA7");
    if (g_inGame)
        return g_hasGlyphs(self, nullptr, text, count);
    if (Ours())
        return HasGlyphs(self, text, count);
    return Compared("fontmeasure.hasglyphs", self, 0xFF, [&] { return HasGlyphs(self, text, count); },
                    [&] { return g_hasGlyphs(self, nullptr, text, count); });
}

uint32_t __fastcall DPreload(void* self, void*, Address text, uint32_t count)
{
    OPENSC4_FIRST("Preload 0x00899D2A");
    if (g_inGame)
        return g_preload(self, nullptr, text, count);
    if (Ours())
        return Preload(self, text, count);
    return Compared("fontmeasure.preload", self, 0xFF, [&] { return Preload(self, text, count); },
                    [&] { return g_preload(self, nullptr, text, count); });
}

OPENSC4_THUNK(TDecode, DDecode, 12)
OPENSC4_THUNK(TGlyphWidth, DGlyphWidth, 8)
OPENSC4_THUNK(TBearing, DBearing, 8)
OPENSC4_THUNK(TAdvance, DAdvance, 8)
OPENSC4_THUNK(TFind, DFind, 12)
OPENSC4_THUNK(TMeasure, DMeasure, 8)
OPENSC4_THUNK(TTrimmed, DTrimmed, 12)
OPENSC4_THUNK(TFitString, DFitString, 20)
OPENSC4_THUNK(TFitCount, DFitCount, 16)
OPENSC4_THUNK(TLines, DLines, 12)
OPENSC4_THUNK(TWidthFor, DWidthFor, 12)
OPENSC4_THUNK(TFitRect, DFitRect, 24)
OPENSC4_THUNK(TMeasureRect, DMeasureRect, 20)
OPENSC4_THUNK(TLineRect, DLineRect, 12)
OPENSC4_THUNK(TShorten, DShorten, 16)
OPENSC4_THUNK(THasGlyphs, DHasGlyphs, 8)
OPENSC4_THUNK(TPreload, DPreload, 8)

// The breaking space's entry: the character in EAX, the answer in EAX,
// every other register kept (FitString and the trimmed measure keep
// pointers in ECX and EDX across it).
__declspec(naked) void TIsSpace()
{
    __asm {
        push ecx
        push edx
        push eax
        call DIsSpace
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

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

bool InstallMeasure(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compare = mode == registry::Mode::Compare;
    FillOwnVtable();
    const Detour detours[] = {
        {addr::GZFont_MeasureString, &TMeasure, Slot(&g_measure)},
        {addr::GZFont_MeasureTrimmed, &TTrimmed, Slot(&g_trimmed)},
        {addr::GZFont_FitString, &TFitString, Slot(&g_fitString)},
        {addr::GZFont_FitCount, &TFitCount, Slot(&g_fitCount)},
        {addr::GZFont_Lines, &TLines, Slot(&g_lines)},
        {addr::GZFont_WidthFor, &TWidthFor, Slot(&g_widthFor)},
        {addr::GZFont_FitRect, &TFitRect, Slot(&g_fitRect)},
        {addr::GZFont_MeasureRect, &TMeasureRect, Slot(&g_measureRect)},
        {addr::GZFont_LineRect, &TLineRect, Slot(&g_lineRect)},
        {addr::GZFont_Shorten, &TShorten, Slot(&g_shorten)},
        {addr::GZFont_GlyphWidth, &TGlyphWidth, Slot(&g_glyphWidth)},
        {addr::GZFont_Bearing, &TBearing, Slot(&g_bearing)},
        {addr::GZFont_Advance, &TAdvance, Slot(&g_advance)},
        {addr::GZFont_HasGlyphs, &THasGlyphs, Slot(&g_hasGlyphs)},
        {addr::GZFont_Preload, &TPreload, Slot(&g_preload)},
        {addr::GZFont_Decode, &TDecode, Slot(&g_decode)},
        {addr::GZFont_Find, &TFind, Slot(&g_find)},
        {addr::GZFont_IsSpace, &TIsSpace, &g_isSpace},
    };
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.thunk), detour.original))
            return false;
    }
    log::Write("fontmeasure: measure: %zu functions %s", std::size(detours),
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registered[] = {
    registry::Register({"framework.font.font.measure", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFont_MeasureString,
                        "Our font's measuring and fitting: MeasureString and its trimmed form, FitString, the count "
                        "that fits, the lines, the width for a height, the rectangles, the shortener, a character's "
                        "width, bearing and advance, the glyph checks and the text helpers (18 detours; "
                        "docs/fonts.md)",
                        true, InstallMeasure}),
};

} // namespace
} // namespace opensc4::fontmeasure
