// Our font's glyphs and glyph sheet, the fifth slice of f3 of docs/fonts.md
// ("f3e: the font's glyphs and glyph sheet are ours"): seven more functions
// of class 0x1AA404AC (vtable 0x00AC55D8), detours in one group, off by
// default and installed at Attach:
//
//   framework.font.font.glyphs   GetGlyph (slot 60, 0x0089834C), RenderGlyph
//                                (slot 61, 0x00898FDC), StoreGlyph (slot 62,
//                                0x00897748), GrowSheet (slot 63,
//                                0x0089632D), the box of a character
//                                without a glyph (0x008962D7) and the
//                                underline's and strike-through's
//                                thickness (0x00896618, 0x00896636)
//
// T2K (its glyph lookup 0x0089F4B2, render 0x0089F23B and the purge of its
// glyph 0x0089E29E), the T2K holder's glyph by name (f5, 0x00898EB2), the
// glyph table's find (0x00968CD9) and insert (f3b, 0x008986BF), operator
// new and delete and the CRT's ftol stay the game's, called at their
// addresses, as do these seven when they call one another (GetGlyph and
// RenderGlyph through the font's slots, as the game does).
//
// The advance's arithmetic on the x87 is the game's own instruction
// sequence in naked functions below, so the result is the FPU's at its
// precision, as the game's is.
//
// The font's fields are listed in font_object.cpp; these also use the
// glyph caches (+0x104 and +0x130, slot 59 picks the mode's): +0x00 the
// block, +0x04 the pixels (two rows into the block), +0x08 the width, +0x0C
// the height, +0x10 the pitch, +0x14 the next free column, +0x18 the table
// of glyphs. A glyph's entry in the table is the character, its box in the
// sheet (left, top, right, bottom) and four words: the left bearing, the
// top (ascent less the glyph's top), the advance and one RenderGlyph never
// sets (the game's is whatever its stack held; ours is 0). GetGlyph answers
// the entry less its character (the node's +8).
//
// Compare mode: the thicknesses compare their answer; the box, GrowSheet
// and StoreGlyph run ours first on copies (ours' own block, a copy of the
// sheet and its pixels, a copy of the box), then the game's, and compare
// what each made or left. RenderGlyph runs ours on a copy of the sheet
// without touching the table (the entries it would put there are kept
// aside), then the game's, and compares every StoreGlyph call (its
// arguments, the box and the bitmap it was given), the sheet left and the
// entry put in the table. GetGlyph compares a hit; on a miss it runs the
// game's with the render comparing itself and compares what it answered
// with the table. While the game's side runs, our detours inside it pass
// through to the game's.
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

namespace opensc4::fontglyph {
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
    log::Write("fontglyph: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// ====================================================================================
// The game's functions and constants we use

constexpr std::uintptr_t kTableFind = 0x00968CD9;     // thiscall (const uint32_t* key): the node, or 0
constexpr std::uintptr_t kTableInsert = 0x008986BF;   // thiscall (result*, const entry*), ret 8
constexpr std::uintptr_t kT2kGlyphIndex = 0x0089F4B2; // cdecl (scaler, character, error*): the glyph in AX
constexpr std::uintptr_t kGlyphByName = 0x00898EB2;   // thiscall on the holder (character): the glyph or 0
constexpr std::uintptr_t kT2kRender = 0x0089F23B;     // cdecl (scaler, glyph, 0, 0, grey, word, error*)
constexpr std::uintptr_t kT2kPurge = 0x0089E29E;      // cdecl (scaler, 1, error*)

// These functions, at their addresses (ours when the group is on).
constexpr std::uintptr_t kBox = 0x008962D7;
constexpr std::uintptr_t kUnderlineRows = 0x00896618;
constexpr std::uintptr_t kStrikeRows = 0x00896636;

constexpr Address kOne = 0x00A81228;    // 1.0f
constexpr Address kNearOne = 0x00AC56E0; // 0.98f

namespace font {
constexpr Address kSize = 0x38;
constexpr Address kHeight = 0x3C;
constexpr Address kStyle = 0x40;
constexpr Address kMode = 0x4C;
constexpr Address kLevels = 0x5C, kLevelsEnd = 0x60;
constexpr Address kExtra = 0x70;
constexpr Address kAscent = 0x84;
constexpr Address kFallback = 0xE8;
constexpr Address kHolder = 0xEC;
} // namespace font

namespace slot {
constexpr std::size_t kCache = 0xEC;
constexpr std::size_t kGetGlyph = 0xF0;
constexpr std::size_t kRenderGlyph = 0xF4;
constexpr std::size_t kStoreGlyph = 0xF8;
constexpr std::size_t kGrowSheet = 0xFC;
} // namespace slot

namespace sheet {
constexpr Address kBlock = 0, kPixels = 4, kWidth = 8, kHeight = 0xC, kPitch = 0x10, kNext = 0x14, kTable = 0x18;
constexpr uint32_t kHeader = 0x18;
} // namespace sheet

// The T2K holder (f5): +0x08 the scaler, +0x20 the word T2K renders with,
// +0x24 the font's type (2 and 3 look glyphs up by name). The scaler, after
// a render: +0x6C and +0x7C the metrics SetSize reads, +0x80 and +0xD0 the
// advance (16.16; +0x80 when +0x5C), +0xE0 and +0xE4 the glyph's top and
// left (26.6), +0x10C width, +0x110 height, +0x114 pitch, +0x118 the bits.
namespace holder {
constexpr Address kScaler = 8, kRenderWord = 0x20, kType = 0x24;
}
namespace scaler {
constexpr Address kVertical = 0x5C, kMetricA = 0x6C, kMetricB = 0x7C, kAdvanceV = 0x80, kAdvance = 0xD0, kTop = 0xE0,
                  kLeft = 0xE4, kWidth = 0x10C, kHeight = 0x110, kPitch = 0x114, kBits = 0x118;
}

// The entry the table keeps (0x1C bytes, after the node's link): what
// RenderGlyph builds on its stack and hands to the insert.
struct Entry {
    uint32_t character;
    int32_t box[4];
    int16_t words[4];
};
static_assert(sizeof(Entry) == 0x1C, "a glyph's entry");
// What compare mode looks at of an entry: all but the last word.
constexpr uint32_t kEntryCompared = 0x1A;

constexpr uint32_t kFormatBits = 2, kFormatBytes = 0x100;

Address New(uint32_t bytes) { return At(Cdecl<void*>(addr::SC4_operator_new, bytes)); }
void Delete(Address p) { Cdecl<void>(addr::SC4_operator_delete, Ptr(p)); }

// ====================================================================================
// The game's x87 arithmetic

// RenderGlyph's scaled advance: the advance (16.16) as the x87 loads it,
// times the font's extra scale (+0x70), times 1/65536 (0x00AC276C),
// truncated by the CRT's ftol. Answers what ftol leaves in EAX.
__declspec(naked) uint32_t __cdecl ScaledAdvance(int32_t /*advance*/, const void* /*font*/)
{
    __asm {
        mov ecx, dword ptr [esp + 8]
        fild dword ptr [esp + 4]
        fmul dword ptr [ecx + 0x70]
        mov eax, 0x00AC276C
        fmul dword ptr [eax]
        mov eax, 0x009EEF04
        call eax
        ret
    }
}

// The box's advance: its width times the extra scale, truncated by ftol.
__declspec(naked) uint32_t __cdecl BoxAdvance(int32_t /*width*/, const void* /*font*/)
{
    __asm {
        mov ecx, dword ptr [esp + 8]
        fild dword ptr [esp + 4]
        fmul dword ptr [ecx + 0x70]
        mov eax, 0x009EEF04
        call eax
        ret
    }
}

// The box function at an address: the width in ESI, the height in EBX.
__declspec(naked) Address __cdecl CallBox(std::uintptr_t /*fn*/, int32_t /*width*/, int32_t /*height*/)
{
    __asm {
        push ebx
        push esi
        mov eax, dword ptr [esp + 12]
        mov esi, dword ptr [esp + 16]
        mov ebx, dword ptr [esp + 20]
        call eax
        pop esi
        pop ebx
        ret
    }
}

// ====================================================================================
// The functions

// The game's 0x00896618 (plain ret): the underline's rows by the size.
uint32_t UnderlineRows(const void* self)
{
    const uint32_t size = Load<uint32_t>(At(self) + font::kSize);
    if (size < 0x10)
        return 1;
    if (size < 0x1A)
        return 2;
    return size < 0x24 ? 3 : 4;
}

// The game's 0x00896636 (plain ret): the strike-through's rows by the size.
uint32_t StrikeRows(const void* self)
{
    const uint32_t size = Load<uint32_t>(At(self) + font::kSize);
    if (size < 0x18)
        return 1;
    if (size < 0x30)
        return 2;
    return size < 0x48 ? 3 : 4;
}

// The game's 0x008962D7 (width in ESI, height in EBX): a new block of width
// x height bytes, rows width apart, 0xFF on the edge and 0 inside. The
// product and the places are 32-bit.
Address Box(int32_t width, int32_t height)
{
    const Address block = New(static_cast<uint32_t>(width) * static_cast<uint32_t>(height));
    for (int32_t column = 0; column < width; ++column) {
        Address p = block + static_cast<uint32_t>(column);
        for (int32_t row = 0; row < height; ++row) {
            const bool edge = column == 0 || row == 0 || column == width - 1 || row == height - 1;
            Store<uint8_t>(p, edge ? 0xFF : 0);
            p += static_cast<uint32_t>(width);
        }
    }
    return block;
}

// The game's 0x0089632D (slot 63, ret 0xC, this unread): if the width or
// the height is above the sheet's, a new block of (height + 4) x width
// bytes, zeroed, the pixels two rows in; the old rows (the old width, at
// the old pitch) copied over and the old block freed. The pitch becomes the
// width. AL 1.
uint32_t GrowSheet(Address s, uint32_t width, uint32_t height)
{
    if (width > Load<uint32_t>(s + sheet::kWidth) || height > Load<uint32_t>(s + sheet::kHeight)) {
        const uint32_t bytes = (height + 4) * width;
        const Address block = New(bytes);
        std::memset(Ptr(block), 0, bytes);
        const Address pixels = block + width * 2;
        if (Load<Address>(s + sheet::kPixels) != 0) {
            Address to = pixels;
            for (uint32_t row = 0; row < Load<uint32_t>(s + sheet::kHeight); ++row) {
                std::memcpy(Ptr(to),
                            Ptr(Load<uint32_t>(s + sheet::kPitch) * row + Load<Address>(s + sheet::kPixels)),
                            Load<uint32_t>(s + sheet::kWidth));
                to += width;
            }
            Delete(Load<Address>(s + sheet::kBlock));
        }
        Store(s + sheet::kBlock, block);
        Store(s + sheet::kPixels, pixels);
        Store(s + sheet::kWidth, width);
        Store(s + sheet::kHeight, height);
        Store(s + sheet::kPitch, width);
    }
    return 1;
}

// The rows of a line (underline or strike-through) from `row` on: `rows` of
// them, each `span` bytes of 0xFF from column x, stopping at the font's
// line height; the box's bottom pushed down to the last.
void FillLine(const void* self, Address s, Address box, int32_t x, uint32_t span, uint32_t row, int32_t rows)
{
    int32_t done = 0;
    if (rows > 0) {
        uint32_t r = row;
        do {
            if (r >= Load<uint32_t>(At(self) + font::kHeight))
                break;
            std::memset(Ptr(Load<uint32_t>(s + sheet::kPitch) * r + Load<Address>(s + sheet::kPixels) +
                            static_cast<uint32_t>(x)),
                        0xFF, span);
            ++done;
            ++r;
        } while (done < rows);
    }
    const uint32_t bottom = row + static_cast<uint32_t>(done);
    if (Load<int32_t>(box + 0xC) < static_cast<int32_t>(bottom))
        Store(box + 0xC, bottom);
}

// The game's 0x00897748 (slot 62, ret 0x24): a glyph's bitmap into the
// sheet at (x, y). With an underline or a strike-through (style bits 4 and
// 8) the span is the advance (the box's +0x14) when that is wider, and a
// positive left bearing (+0x10) moves the box right and becomes 0. The
// sheet's next column is x plus the span; the sheet grows (slot 63) to 32
// spans more when that reaches its width, and to y plus the height when
// that is below it (the height cut to what it has). Format 2 is 1 bit a
// pixel, expanded to 0 and 0xFF first; rows are copied and, in grey (mode
// not 1) with levels, mapped through them. The lines' rows are filled from
// x, and then the box starts at x (and ends at the next column when x is
// not 0). AL 1.
uint32_t StoreGlyph(void* self, Address s, Address box, int32_t x, uint32_t y, Address bits, uint32_t width,
                    uint32_t height, uint32_t pitch, uint32_t format)
{
    const Address f = At(self);
    Address temp = 0;
    Address from = bits;
    uint32_t span = width;
    if (Load<uint8_t>(f + font::kStyle) & 0xC) {
        const int16_t advance = Load<int16_t>(box + 0x14);
        if (advance > 0 && width <= static_cast<uint32_t>(static_cast<int32_t>(advance)))
            span = static_cast<uint32_t>(static_cast<int32_t>(advance));
        const int16_t bearing = Load<int16_t>(box + 0x10);
        if (bearing > 0) {
            span += static_cast<uint32_t>(static_cast<int32_t>(bearing));
            Store(box, Load<uint32_t>(box) + static_cast<uint32_t>(static_cast<int32_t>(bearing)));
            Store(box + 8, Load<uint32_t>(box + 8) + static_cast<uint32_t>(static_cast<int32_t>(bearing)));
            Store<int16_t>(box + 0x10, 0);
        }
    }
    const uint32_t end = span + static_cast<uint32_t>(x);
    Store(s + sheet::kNext, end);
    if (end >= Load<uint32_t>(s + sheet::kWidth)) {
        const uint32_t sheetHeight = Load<uint32_t>(s + sheet::kHeight);
        const uint32_t lineHeight = Load<uint32_t>(f + font::kHeight);
        Virtual<uint32_t>(self, slot::kGrowSheet, s, span * 32 + static_cast<uint32_t>(x),
                          sheetHeight < lineHeight ? lineHeight : sheetHeight);
    }
    if (y + height > Load<uint32_t>(s + sheet::kHeight)) {
        Virtual<uint32_t>(self, slot::kGrowSheet, s, Load<uint32_t>(s + sheet::kWidth), y + height);
        const uint32_t now = Load<uint32_t>(s + sheet::kHeight);
        height = now < y ? 0 : now - y;
    }
    if (width != 0) {
        if (format == kFormatBits) {
            temp = New(width * height);
            Address to = temp;
            uint32_t offset = 0;
            for (uint32_t n = height; n != 0; --n) {
                for (uint32_t i = 0; i < width; ++i) {
                    const uint8_t mask = static_cast<uint8_t>(0x80 >> (i & 7));
                    Store<uint8_t>(to + i, (Load<uint8_t>(bits + offset + (i >> 3)) & mask) ? 0xFF : 0);
                }
                offset += pitch;
                to += width;
            }
            from = temp;
            pitch = width;
        }
        Address to = Load<uint32_t>(s + sheet::kPitch) * y + Load<Address>(s + sheet::kPixels) + Load<uint32_t>(box);
        for (uint32_t n = height; n != 0; --n) {
            std::memcpy(Ptr(to), Ptr(from), width);
            if (Load<uint32_t>(f + font::kMode) != 1 &&
                Load<Address>(f + font::kLevels) != Load<Address>(f + font::kLevelsEnd)) {
                const Address stop = to + width;
                for (Address p = to; p < stop; ++p)
                    Store<uint8_t>(p, Load<uint8_t>(Load<uint8_t>(p) + Load<Address>(f + font::kLevels)));
            }
            to += Load<uint32_t>(s + sheet::kPitch);
            from += pitch;
        }
        Delete(temp);
    }
    const uint32_t style = Load<uint32_t>(f + font::kStyle);
    if ((style & 4) || (style & 8)) {
        if (style & 4) {
            const int32_t rows = static_cast<int32_t>(This<uint32_t>(kUnderlineRows, self));
            const uint32_t row = Load<uint32_t>(f + font::kAscent) -
                                 static_cast<uint32_t>(static_cast<int32_t>(Load<int16_t>(box + 0x12))) +
                                 static_cast<uint32_t>(rows);
            if (static_cast<int32_t>(row) >= 0 && row < Load<uint32_t>(f + font::kHeight))
                FillLine(self, s, box, x, span, row, rows);
        }
        if (Load<uint8_t>(f + font::kStyle) & 8) {
            const int32_t rows = static_cast<int32_t>(This<uint32_t>(kStrikeRows, self));
            const uint32_t lineHeight = Load<uint32_t>(f + font::kHeight);
            const uint32_t row = Load<uint32_t>(f + font::kAscent) - (lineHeight >> 2) -
                                 static_cast<uint32_t>(static_cast<int32_t>(Load<int16_t>(box + 0x12)));
            if (static_cast<int32_t>(row) >= 0 && row < lineHeight)
                FillLine(self, s, box, x, span, row, rows);
        }
        Store(box, x);
        if (x != 0)
            Store(box + 8, end);
    }
    return 1;
}

// What T2K left after rendering a glyph, as RenderGlyph reads it.
struct Rendered {
    Address bits;
    int32_t width, height, pitch;
    int32_t left, top, metricA, metricB, vertical, advanceV, advance;
};

Rendered ReadRendered(Address sc)
{
    return {Load<Address>(sc + scaler::kBits),     Load<int32_t>(sc + scaler::kWidth),
            Load<int32_t>(sc + scaler::kHeight),   Load<int32_t>(sc + scaler::kPitch),
            Load<int32_t>(sc + scaler::kLeft),     Load<int32_t>(sc + scaler::kTop),
            Load<int32_t>(sc + scaler::kMetricA),  Load<int32_t>(sc + scaler::kMetricB),
            Load<int32_t>(sc + scaler::kVertical), Load<int32_t>(sc + scaler::kAdvanceV),
            Load<int32_t>(sc + scaler::kAdvance)};
}

// RenderGlyph's StoreGlyph call: everything but the font, the sheet and the
// box, which is the entry's.
struct StoreArgs {
    int32_t x;
    uint32_t y;
    Address bits;
    uint32_t width, height, pitch, format;
};

// The glyph RenderGlyph renders: T2K's for the character (not for one at
// 0x80 or above of a type 2 or 3 face), else by its name (types 2 and 3,
// from 0x20), else the fallback (+0xE8); 0 (the box) without a face.
// T2K's lookup sets `error` (which starts at 1), and the render after it
// is handed the same word.
uint32_t ChooseGlyph(const void* self, uint32_t character, uint32_t& error)
{
    const Address h = Load<Address>(At(self) + font::kHolder);
    if (!h || !Load<Address>(h + holder::kScaler))
        return 0;
    const uint32_t type = Load<uint32_t>(h + holder::kType);
    const bool named = type == 2 || type == 3;
    uint32_t glyph = 0;
    if (character < 0x80 || !named) {
        glyph = Cdecl<uint32_t>(kT2kGlyphIndex, Load<Address>(h + holder::kScaler), character, &error) & 0xFFFF;
        if (error)
            glyph = 0;
    }
    if (!glyph) {
        if (character >= 0x20 && named)
            glyph = This<uint32_t>(kGlyphByName, Ptr(h), character);
        if (!glyph)
            glyph = Load<uint32_t>(At(self) + font::kFallback);
    }
    return glyph;
}

// A rendered glyph's entry and StoreGlyph call at the sheet's next column
// `next`: its box (empty without bits), the left bearing and the top (26.6
// to pixels), the advance: whole pixels at an extra scale of 1.0, scaled
// (ftol) at one up to 0.98, from 1.0 up or NaN; just under 1.0 whole for
// all but the space, which is scaled and a pixel narrower where that does
// not fall short of the whole one. 0x200B and 0xFEFF have none.
void GlyphLayout(const void* self, uint32_t character, int32_t next, const Rendered& r, uint32_t format, Entry& e,
                 StoreArgs& a)
{
    const bool empty = r.bits == 0;
    e = Entry{};
    e.character = character;
    const int32_t width = empty ? 0 : r.width;
    const int32_t height = empty ? 0 : r.height;
    e.box[0] = next;
    e.box[1] = 0;
    e.box[2] = static_cast<int32_t>(static_cast<uint32_t>(width) + static_cast<uint32_t>(next));
    e.box[3] = height;
    e.words[0] = static_cast<int16_t>(r.left / 0x40);
    const int32_t metric = static_cast<int32_t>(static_cast<uint32_t>(r.metricB) + static_cast<uint32_t>(r.metricA));
    e.words[1] = static_cast<int16_t>(static_cast<uint32_t>(metric / 0x10000) - static_cast<uint32_t>(r.top / 0x40));
    const int32_t advance = r.vertical != 0 ? r.advanceV : r.advance;
    const float extra = Load<float>(At(self) + font::kExtra);
    if (extra == Load<float>(kOne)) {
        e.words[2] = static_cast<int16_t>(advance / 0x10000);
    } else if (!(extra > Load<float>(kNearOne)) || !(extra < Load<float>(kOne))) {
        e.words[2] = static_cast<int16_t>(ScaledAdvance(advance, self));
    } else if (character != 0x20) {
        e.words[2] = static_cast<int16_t>(advance / 0x10000);
    } else {
        e.words[2] = static_cast<int16_t>(ScaledAdvance(advance, self));
        if (static_cast<int32_t>(e.words[2]) + 1 > advance / 0x10000)
            e.words[2] = static_cast<int16_t>(e.words[2] - 1);
    }
    if (character == 0x200B || character == 0xFEFF)
        e.words[2] = 0;
    if (empty) {
        a = {next, 0, 0, 0, 0, 0, kFormatBits};
    } else {
        a = {next, 0, r.bits, static_cast<uint32_t>(r.width), static_cast<uint32_t>(r.height),
             static_cast<uint32_t>(r.pitch), format};
    }
}

bool NoBox(uint32_t character) { return character == 0x200B || character == 0xFEFF; }

// The box's entry and StoreGlyph call at `next`: half the line's height
// square, advancing its width times the extra scale (ftol); for 0x200B and
// 0xFEFF nothing stored and no advance. The caller makes the box's bits.
void BoxLayout(const void* self, uint32_t character, int32_t next, Entry& e, StoreArgs& a)
{
    const uint32_t half = Load<uint32_t>(At(self) + font::kHeight) >> 1;
    e = Entry{};
    e.character = character;
    e.box[0] = next;
    e.box[1] = 0;
    e.box[2] = static_cast<int32_t>(half + static_cast<uint32_t>(next));
    e.box[3] = static_cast<int32_t>(half);
    const int32_t width = static_cast<int32_t>(static_cast<uint32_t>(e.box[2]) - static_cast<uint32_t>(next));
    e.words[2] = static_cast<int16_t>(BoxAdvance(width, self));
    if (NoBox(character)) {
        e.words[2] = 0;
        a = {next, 0, 0, 0, 0, 0, kFormatBits};
        return;
    }
    const uint32_t across = static_cast<uint32_t>(e.box[2]) - static_cast<uint32_t>(e.box[0]);
    a = {e.box[0],
         static_cast<uint32_t>(e.box[1]),
         0,
         across,
         static_cast<uint32_t>(e.box[3]) - static_cast<uint32_t>(e.box[1]),
         across,
         kFormatBytes};
}

// The entry stored (slot 62), put in the table and the glyph got from it
// (slot 60).
Address StoreAndKeep(void* self, Address s, Entry& e, const StoreArgs& a)
{
    Virtual<uint32_t>(self, slot::kStoreGlyph, s, At(&e) + 4, a.x, a.y, a.bits, a.width, a.height, a.pitch,
                      a.format);
    uint32_t result[4] = {};
    This<Address>(kTableInsert, Ptr(s + sheet::kTable), At(result), At(&e));
    return Virtual<Address>(self, slot::kGetGlyph, e.character);
}

// The game's 0x00898FDC (slot 61, ret 8). With a glyph (ChooseGlyph) T2K
// renders it (8-bit grey but in mode 1), it is stored at the sheet's next
// column with its box and metrics (GlyphLayout), the entry goes in the
// table, the answer is GetGlyph's and T2K's glyph is purged. Without one,
// or on any T2K error, the box (BoxLayout) instead.
Address RenderGlyph(void* self, uint32_t character, Address s)
{
    const Address f = At(self);
    uint32_t error = 1;
    if (const uint32_t glyph = ChooseGlyph(self, character, error)) {
        const Address h = Load<Address>(f + font::kHolder);
        const bool grey = Load<uint32_t>(f + font::kMode) != 1;
        Cdecl<void>(kT2kRender, Load<Address>(h + holder::kScaler), glyph, 0u, 0u, grey ? 3u : 0u,
                    uint32_t{Load<uint16_t>(h + holder::kRenderWord)}, &error);
        if (!error) {
            Entry e;
            StoreArgs a;
            GlyphLayout(self, character, Load<int32_t>(s + sheet::kNext),
                        ReadRendered(Load<Address>(h + holder::kScaler)), grey ? kFormatBytes : kFormatBits, e, a);
            const Address result = StoreAndKeep(self, s, e, a);
            Cdecl<void>(kT2kPurge, Load<Address>(h + holder::kScaler), 1u, &error);
            if (!error)
                return result;
        }
    }
    Entry e;
    StoreArgs a;
    BoxLayout(self, character, Load<int32_t>(s + sheet::kNext), e, a);
    if (NoBox(character))
        return StoreAndKeep(self, s, e, a);
    a.bits = CallBox(kBox, static_cast<int32_t>(a.width), static_cast<int32_t>(a.height));
    const Address result = StoreAndKeep(self, s, e, a);
    Delete(a.bits);
    return result;
}

// The game's 0x0089834C (slot 60, ret 4): the glyph cache of the mode (slot
// 59), its table's entry for the character (the node's +8), else
// RenderGlyph (slot 61); 0 without a cache.
Address GetGlyph(void* self, uint32_t character)
{
    const Address cache = Virtual<Address>(self, slot::kCache);
    if (!cache)
        return 0;
    const Address node = This<Address>(kTableFind, Ptr(cache + sheet::kTable), &character);
    if (node)
        return node + 8;
    return Virtual<Address>(self, slot::kRenderGlyph, character, cache);
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

// Above 0 while ours runs on a scratch copy in a comparison: our detours
// inside it run ours, uncompared.
int g_oursSide = 0;

struct OursSide {
    OursSide() { ++g_oursSide; }
    ~OursSide() { --g_oursSide; }
    OursSide(const OursSide&) = delete;
    OursSide& operator=(const OursSide&) = delete;
};

bool Ours() { return g_oursSide > 0 || !g_compare; }


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

std::string Hex(Address bytes, uint32_t count)
{
    std::string text;
    char two[4];
    for (uint32_t i = 0; i < count; ++i) {
        std::snprintf(two, sizeof two, "%02X", Load<uint8_t>(bytes + i));
        text += two;
    }
    return text;
}

void Report(const char* id, const std::string& game, const std::string& ours)
{
    compare::Record(id, game == ours, game == ours ? std::string() : game, game == ours ? std::string() : ours);
}

// FNV-1a over rows of bytes.
uint32_t RowsHash(Address first, uint32_t rows, uint32_t bytes, uint32_t pitch)
{
    uint32_t hash = 2166136261u;
    for (uint32_t r = 0; r < rows; ++r) {
        const Address row = first + r * pitch;
        for (uint32_t i = 0; i < bytes; ++i)
            hash = (hash ^ Load<uint8_t>(row + i)) * 16777619u;
    }
    return hash;
}

// A StoreGlyph call: its arguments but the pointers, the box (and words but
// the last) and the hash of the bitmap it is given.
std::string StoreText(Address box, int32_t x, uint32_t y, Address bits, uint32_t width, uint32_t height,
                      uint32_t pitch, uint32_t format, uint32_t hash)
{
    std::string s = Words({static_cast<uint32_t>(x), y, width, height, pitch, format, bits ? 1u : 0u}) +
                    Hex(box, kEntryCompared - 4);
    if (bits && width)
        s += " " + Words({hash});
    return s;
}

uint32_t BitsHash(Address bits, uint32_t width, uint32_t height, uint32_t pitch, uint32_t format)
{
    if (!bits || !width)
        return 0;
    return RowsHash(bits, height, format == kFormatBits ? (width + 7) / 8 : width, pitch);
}

// While the game's RenderGlyph runs in a comparison: each StoreGlyph call
// it makes (as text), with the sheet's next column, what T2K had left, the
// bits given and their hash.
struct Capture {
    struct Call {
        std::string text;
        int32_t next;
        Rendered rendered;
        Address bits;
        uint32_t hash;
    };
    const void* font;
    std::vector<Call> calls;
};

Capture* g_capture = nullptr;

void Note(const void* self, Address s, Address box, int32_t x, uint32_t y, Address bits, uint32_t width,
          uint32_t height, uint32_t pitch, uint32_t format)
{
    Capture::Call call;
    call.hash = BitsHash(bits, width, height, pitch, format);
    call.text = StoreText(box, x, y, bits, width, height, pitch, format, call.hash);
    call.next = Load<int32_t>(s + sheet::kNext);
    const Address h = Load<Address>(At(self) + font::kHolder);
    call.rendered = h && Load<Address>(h + holder::kScaler) ? ReadRendered(Load<Address>(h + holder::kScaler))
                                                              : Rendered{};
    call.bits = bits;
    g_capture->calls.push_back(call);
}

// A sheet: its sizes, the next column, where the pixels are in the block,
// and its rows.
std::string SheetOf(Address s)
{
    const Address block = Load<Address>(s + sheet::kBlock);
    const Address pixels = Load<Address>(s + sheet::kPixels);
    const uint32_t width = Load<uint32_t>(s + sheet::kWidth);
    const uint32_t height = Load<uint32_t>(s + sheet::kHeight);
    const uint32_t pitch = Load<uint32_t>(s + sheet::kPitch);
    std::string text = Words({width, height, pitch, Load<uint32_t>(s + sheet::kNext), block ? 1u : 0u,
                              pixels ? pixels - block : 0u});
    if (pixels)
        text += Words({RowsHash(pixels, height, width, pitch)});
    return text;
}

// A sheet's copy for ours to run on: its header and a block of its own with
// its rows (and the two above them); the block left at the end is freed.
struct ScratchSheet {
    uint8_t header[sheet::kHeader];
    explicit ScratchSheet(Address s)
    {
        std::memcpy(header, Ptr(s), sizeof header);
        const Address block = Load<Address>(s + sheet::kBlock);
        const Address pixels = Load<Address>(s + sheet::kPixels);
        if (!block || pixels < block)
            return;
        const uint32_t pitch = Load<uint32_t>(s + sheet::kPitch);
        const uint32_t used = (pixels - block) + pitch * Load<uint32_t>(s + sheet::kHeight);
        const Address copy = New(used + 2 * pitch);
        std::memset(Ptr(copy), 0, used + 2 * pitch);
        std::memcpy(Ptr(copy), Ptr(block), used);
        Store(At(header) + sheet::kBlock, copy);
        Store(At(header) + sheet::kPixels, copy + (pixels - block));
    }
    ~ScratchSheet()
    {
        const Address block = Load<Address>(At(header) + sheet::kBlock);
        if (block)
            Delete(block);
    }
    Address Self() { return At(header); }
    ScratchSheet(const ScratchSheet&) = delete;
    ScratchSheet& operator=(const ScratchSheet&) = delete;
};

using RowsFn = uint32_t(__fastcall*)(void*, void*);
using GetGlyphFn = Address(__fastcall*)(void*, void*, uint32_t);
using RenderGlyphFn = Address(__fastcall*)(void*, void*, uint32_t, Address);
using StoreGlyphFn = uint32_t(__fastcall*)(void*, void*, Address, Address, int32_t, uint32_t, Address, uint32_t,
                                           uint32_t, uint32_t, uint32_t);
using GrowSheetFn = uint32_t(__fastcall*)(void*, void*, Address, uint32_t, uint32_t);

RowsFn g_underlineRows = nullptr;
RowsFn g_strikeRows = nullptr;
void* g_box = nullptr;
GrowSheetFn g_growSheet = nullptr;
StoreGlyphFn g_storeGlyph = nullptr;
RenderGlyphFn g_renderGlyph = nullptr;
GetGlyphFn g_getGlyph = nullptr;

uint32_t __fastcall DUnderlineRows(void* self, void*)
{
    OPENSC4_FIRST("UnderlineRows 0x00896618");
    if (g_inGame)
        return g_underlineRows(self, nullptr);
    const uint32_t ours = UnderlineRows(self);
    if (Ours())
        return ours;
    uint32_t result;
    {
        GameSide side;
        result = g_underlineRows(self, nullptr);
    }
    Report("fontglyph.underline", Words({result}), Words({ours}));
    return result;
}

uint32_t __fastcall DStrikeRows(void* self, void*)
{
    OPENSC4_FIRST("StrikeRows 0x00896636");
    if (g_inGame)
        return g_strikeRows(self, nullptr);
    const uint32_t ours = StrikeRows(self);
    if (Ours())
        return ours;
    uint32_t result;
    {
        GameSide side;
        result = g_strikeRows(self, nullptr);
    }
    Report("fontglyph.strike", Words({result}), Words({ours}));
    return result;
}

Address __cdecl DBox(int32_t width, int32_t height)
{
    OPENSC4_FIRST("Box 0x008962D7");
    if (g_inGame)
        return CallBox(reinterpret_cast<std::uintptr_t>(g_box), width, height);
    if (Ours())
        return Box(width, height);
    const uint32_t bytes = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
    std::string ours;
    {
        OursSide side;
        const Address block = Box(width, height);
        ours = Words({RowsHash(block, 1, bytes, 0)});
        Delete(block);
    }
    Address result;
    {
        GameSide side;
        result = CallBox(reinterpret_cast<std::uintptr_t>(g_box), width, height);
    }
    Report("fontglyph.box", Words({RowsHash(result, 1, bytes, 0)}), ours);
    return result;
}

uint32_t __fastcall DGrowSheet(void* self, void*, Address s, uint32_t width, uint32_t height)
{
    OPENSC4_FIRST("GrowSheet 0x0089632D");
    if (g_inGame)
        return g_growSheet(self, nullptr, s, width, height);
    if (Ours())
        return GrowSheet(s, width, height);
    std::string ours;
    {
        ScratchSheet scratch(s);
        OursSide side;
        const uint32_t answer = GrowSheet(scratch.Self(), width, height);
        ours = Words({answer & 0xFF}) + SheetOf(scratch.Self());
    }
    uint32_t result;
    {
        GameSide side;
        result = g_growSheet(self, nullptr, s, width, height);
    }
    Report("fontglyph.grow", Words({result & 0xFF}) + SheetOf(s), ours);
    return result;
}

uint32_t __fastcall DStoreGlyph(void* self, void*, Address s, Address box, int32_t x, uint32_t y, Address bits,
                                uint32_t width, uint32_t height, uint32_t pitch, uint32_t format)
{
    OPENSC4_FIRST("StoreGlyph 0x00897748");
    if (g_inGame) {
        if (g_capture && g_capture->font == self)
            Note(self, s, box, x, y, bits, width, height, pitch, format);
        return g_storeGlyph(self, nullptr, s, box, x, y, bits, width, height, pitch, format);
    }
    if (Ours())
        return StoreGlyph(self, s, box, x, y, bits, width, height, pitch, format);
    // Ours on a copy of the sheet and of the box, then the game's.
    std::string ours;
    {
        uint8_t copy[0x18];
        std::memcpy(copy, Ptr(box), sizeof copy);
        ScratchSheet scratch(s);
        OursSide side;
        const uint32_t answer = StoreGlyph(self, scratch.Self(), At(copy), x, y, bits, width, height, pitch, format);
        ours = Words({answer & 0xFF}) + SheetOf(scratch.Self()) + Hex(At(copy), sizeof copy);
    }
    uint32_t result;
    {
        GameSide side;
        result = g_storeGlyph(self, nullptr, s, box, x, y, bits, width, height, pitch, format);
    }
    Report("fontglyph.store", Words({result & 0xFF}) + SheetOf(s) + Hex(box, 0x18), ours);
    return result;
}

Address FindIn(Address cache, uint32_t character)
{
    return This<Address>(kTableFind, Ptr(cache + sheet::kTable), &character);
}

Address __fastcall DRenderGlyph(void* self, void*, uint32_t character, Address s)
{
    OPENSC4_FIRST("RenderGlyph 0x00898FDC");
    if (g_inGame)
        return g_renderGlyph(self, nullptr, character, s);
    if (Ours())
        return RenderGlyph(self, character, s);
    // The game's alone (T2K does not render a glyph the same way twice),
    // noting each StoreGlyph call with what T2K had left and the sheet's
    // next column; then ours works out each call from those.
    Capture capture{self, {}};
    Address result;
    {
        GameSide side;
        g_capture = &capture;
        result = g_renderGlyph(self, nullptr, character, s);
        g_capture = nullptr;
    }
    uint32_t glyph;
    {
        OursSide side;
        uint32_t error = 1;
        glyph = ChooseGlyph(self, character, error);
    }
    std::string game, ours;
    for (std::size_t i = 0; i < capture.calls.size(); ++i) {
        const Capture::Call& call = capture.calls[i];
        game += call.text + "; ";
        Entry e;
        StoreArgs a;
        uint32_t hash = call.hash;
        if (i == 0 && glyph) {
            GlyphLayout(self, character, call.next, call.rendered,
                        Load<uint32_t>(At(self) + font::kMode) != 1 ? kFormatBytes : kFormatBits, e, a);
            if (a.bits != call.bits)
                hash = ~hash;
        } else {
            BoxLayout(self, character, call.next, e, a);
            if (!NoBox(character) && a.width) {
                OursSide side;
                const Address bits = Box(static_cast<int32_t>(a.width), static_cast<int32_t>(a.height));
                hash = RowsHash(bits, a.height, a.width, a.pitch);
                Delete(bits);
                a.bits = bits;
            } else if (!NoBox(character)) {
                a.bits = call.bits;
            }
        }
        ours += StoreText(At(&e) + 4, a.x, a.y, a.bits, a.width, a.height, a.pitch, a.format, hash) + "; ";
    }
    // The answer is GetGlyph's: the entry in the mode's cache.
    const Address cache = Virtual<Address>(self, slot::kCache);
    const Address node = cache ? FindIn(cache, character) : 0;
    game += Words({static_cast<uint32_t>(capture.calls.size()), result == (node ? node + 8 : 0) ? 1u : 0u});
    ours += Words({static_cast<uint32_t>(capture.calls.size() ? capture.calls.size() : 1), 1});
    Report("fontglyph.render", game, ours);
    return result;
}

Address __fastcall DGetGlyph(void* self, void*, uint32_t character)
{
    OPENSC4_FIRST("GetGlyph 0x0089834C");
    if (g_inGame)
        return g_getGlyph(self, nullptr, character);
    if (Ours())
        return GetGlyph(self, character);
    Address cache, node;
    {
        OursSide side;
        cache = Virtual<Address>(self, slot::kCache);
        node = cache ? FindIn(cache, character) : 0;
    }
    Address result;
    if (!cache || node) {
        GameSide side;
        result = g_getGlyph(self, nullptr, character);
    } else {
        // A miss: the game's renders, and its RenderGlyph compares itself.
        result = g_getGlyph(self, nullptr, character);
        node = FindIn(cache, character);
    }
    Report("fontglyph.getglyph", Words({result}), Words({node ? node + 8 : 0}));
    return result;
}

OPENSC4_THUNK(TUnderlineRows, DUnderlineRows, 0)
OPENSC4_THUNK(TStrikeRows, DStrikeRows, 0)
OPENSC4_THUNK(TGrowSheet, DGrowSheet, 12)
OPENSC4_THUNK(TStoreGlyph, DStoreGlyph, 36)
OPENSC4_THUNK(TRenderGlyph, DRenderGlyph, 8)
OPENSC4_THUNK(TGetGlyph, DGetGlyph, 4)

// The box's entry: the width in ESI, the height in EBX; EBX, ESI, EDI and
// EBP kept (and ECX and EDX, as the other thunks do).
__declspec(naked) void TBox()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        call DBox
        add esp, 8
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

bool InstallGlyphs(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compare = mode == registry::Mode::Compare;
    const Detour detours[] = {
        {addr::GZFont_GetGlyph, &TGetGlyph, Slot(&g_getGlyph)},
        {addr::GZFont_RenderGlyph, &TRenderGlyph, Slot(&g_renderGlyph)},
        {addr::GZFont_StoreGlyph, &TStoreGlyph, Slot(&g_storeGlyph)},
        {addr::GZFont_GrowSheet, &TGrowSheet, Slot(&g_growSheet)},
        {addr::GZFont_BoxGlyph, &TBox, &g_box},
        {addr::GZFont_UnderlineRows, &TUnderlineRows, Slot(&g_underlineRows)},
        {addr::GZFont_StrikeRows, &TStrikeRows, Slot(&g_strikeRows)},
    };
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.thunk), detour.original))
            return false;
    }
    log::Write("fontglyph: glyphs: %zu functions %s", std::size(detours),
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registered[] = {
    registry::Register({"framework.font.font.glyphs", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFont_GetGlyph,
                        "Our font's glyphs and glyph sheet: GetGlyph, RenderGlyph, StoreGlyph, GrowSheet, the box "
                        "of a character without a glyph and the lines' thickness (7 detours; docs/fonts.md)",
                        true, InstallGlyphs}),
};

} // namespace
} // namespace opensc4::fontglyph
