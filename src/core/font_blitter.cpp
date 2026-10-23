// Our glyph blitter of the framework's fonts, 0x00896053 to 0x00896243,
// 0x00896E63, 0x00897246 and 0x00897722 (docs/fonts.md, "f4: the glyph
// blitter is ours"): the 8-byte COM object (class 0x08CDA30B, vtable
// 0x00AC55B4) a font keeps at +0x8C, which locks a picture, composites
// DrawString's list of placed glyphs into its pixels and unlocks it, and the
// blend tables its first instance builds. Each is a detour on the game's
// function, in two groups, off by default and installed at Attach (fonts are
// made while the game starts):
//
//   framework.font.blitter.draw    DrawGlyphs (slot 7, 0x00897246), the
//                                  compositor DrawGlyph (0x00896E63) and its
//                                  three 16-bit blends through the tables:
//                                  4444 (0x00896053), 555 (0x008960C1) and
//                                  565 (0x0089612A)
//   framework.font.blitter.object  the constructor (0x00897722), the blend
//                                  table builder (0x0089618D),
//                                  QueryInterface (slot 0, 0x0089620D),
//                                  Release (slot 2, 0x008961F6), Begin and
//                                  End (slots 5 and 6, 0x00896232,
//                                  0x00896243)
//
// The pixels are the game's, quirks included: a 24-bit picture is drawn as
// 32-bit pixels; the 565 blend keeps only five bits of green and takes red
// unmasked; the 555 blend clears the alpha bit of a 1555 picture and the
// 4444 one sets alpha to 0xF; the 32-bit blend rounds with 0x80 and divides
// by 256, not 255. A glyph's own width and height never clip it: only its
// box, the clip and the picture do.
//
// The three blends take their arguments in registers (the target pixel in
// EAX, the colour in ECX, the alpha 0 to 31 in EDX; the pixel comes back in
// EAX, EDX kept); they are entered through thunks of their own. The others
// keep ECX and EDX for their callers through core/thunk.h.
//
// Compare mode runs ours and then the game's on the same inputs, as the
// pixel helpers do (docs/pixels.md): what ours wrote is kept aside and the
// memory put back before the game's runs, and both are compared byte for
// byte. While the game's side runs, our detours inside it (DrawGlyph and the
// blends under the game's DrawGlyphs, the table builder under the game's
// constructor) pass through to the game's. Begin and End lock a picture and
// cannot run twice: in compare mode they run the game's alone, as Release
// does when it would free the object.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace opensc4::fontblit {
namespace {

// ====================================================================================
// Helpers

// Addresses are 32 bits and wrap as the game's do; so do coordinates added
// and multiplied (compared signed where the game compares them so).
using Address = uint32_t;

inline Address At(const void* p) { return static_cast<Address>(reinterpret_cast<uintptr_t>(p)); }

template <typename T>
inline T Load(Address at)
{
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(static_cast<uintptr_t>(at)), sizeof value);
    return value;
}

template <typename T>
inline void Store(Address at, T value)
{
    std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(at)), &value, sizeof value);
}

inline int32_t Plus(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }
inline int32_t Minus(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("fontblit: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

struct Rect {
    int32_t left, top, right, bottom;
};

// One entry of DrawString's list (0x24 bytes): where the glyph goes in the
// picture, its coverage in the glyph sheet and the character.
struct PlacedGlyph {
    Rect box;
    Address pixels;
    int32_t pitch;
    int32_t width;
    int32_t height;
    uint32_t character;
};
static_assert(sizeof(PlacedGlyph) == 0x24);

// The picture: a cIGZBuffer (gzcom-dll's cIGZBuffer.h; its GetColorType
// overloads in MSVC's reverse order, so +0x38 is the one filling a struct).
namespace buffer {
constexpr std::size_t kLock = 0x18;
constexpr std::size_t kUnlock = 0x1C;
constexpr std::size_t kWidth = 0x24;
constexpr std::size_t kHeight = 0x28;
constexpr std::size_t kColorType = 0x38;
constexpr std::size_t kBitsPerPixel = 0x40;
constexpr std::size_t kBits = 0x88;
constexpr std::size_t kStride = 0x8C;
} // namespace buffer

// cGZBufferColorType: what GetColorType fills. DrawGlyph starts it as
// AutoFromScreenFormat16 and asks only in mode 3.
struct ColorType {
    uint32_t type;
    uint32_t bitsPerPixel;
};
constexpr uint32_t kAutoFromScreenFormat16 = 3;
constexpr uint32_t kR5G5B5 = 4;
constexpr uint32_t kR5G6B5 = 5;
constexpr uint32_t kA1R5G5B5 = 6;
constexpr uint32_t kA4R4G4B4 = 7;

// The flags Begin and End lock and unlock the picture with.
constexpr uint32_t kLockFlags = 0x8080;

// What the blitter answers to QueryInterface besides cIGZUnknown (1).
constexpr uint32_t kUnknownInterface = 1;
constexpr uint32_t kBlitterInterface = 0xA8BC2A75;

// The blitter: its vtable, then a reference count.
constexpr Address kRefCount = 4;

// Characters below a space are not drawn.
constexpr uint32_t kFirstDrawn = 0x20;

// The draw modes DrawString hands down (the font's +0x4C): 1 one colour
// where the coverage is over half, 2 a colour per coverage level from a
// table, anything else the colour blended by coverage.
constexpr uint32_t kMonochrome = 1;
constexpr uint32_t kPalette = 2;
constexpr uint32_t kBlended = 3;

// ====================================================================================
// Blend tables

// 32 levels of a colour channel blended over 32 levels of the target's by
// 32 alphas: entry (colour, target, alpha) at ((colour * 32 + target) * 32
// + alpha), one byte each, 0x8000 in all, then the byte that says they are
// built.
constexpr uint32_t kLevels = 32;
constexpr uint32_t kTableBytes = kLevels * kLevels * kLevels;

Address Table() { return static_cast<Address>(addr::GZFontBlitter_BlendTable); }
Address TablesBuilt() { return static_cast<Address>(addr::GZFontBlitter_TablesBuilt); }

// The game's 0x0089618D: alpha * colour / 32 + (1 - alpha / 32) * target,
// truncated. The game works it out on the x87 with alpha / 32 as a float;
// every step is exact there (small whole numbers times multiples of 1/32),
// so whole numbers give the same bytes at any precision.
bool BuildTables()
{
    Address at = Table();
    for (uint32_t colour = 0; colour < kLevels; ++colour)
        for (uint32_t target = 0; target < kLevels; ++target)
            for (uint32_t alpha = 0; alpha < kLevels; ++alpha)
                Store<uint8_t>(at++, static_cast<uint8_t>((alpha * colour + (kLevels - alpha) * target) / kLevels));
    return true;
}

// A channel of `colour` blended over `target` by `alpha`, from the tables.
// The index wraps as the game's does when a channel is not masked (565 red).
inline uint32_t Level(uint32_t colour, uint32_t target, uint32_t alpha)
{
    return Load<uint8_t>(Table() + (((colour << 5) + target) << 5) + alpha);
}

// The game's 0x00896053: a 4444 pixel; alpha comes out 0xF.
uint32_t Blend4444(uint32_t target, uint32_t colour, uint32_t alpha)
{
    const uint32_t red = Level((colour >> 8) & 0xF, (target >> 8) & 0xF, alpha);
    const uint32_t green = Level((colour >> 4) & 0xF, (target >> 4) & 0xF, alpha);
    const uint32_t blue = Level(colour & 0xF, target & 0xF, alpha);
    return (((0xF0 | red) << 4 | green) << 4) | blue;
}

// The game's 0x008960C1: a 555 pixel; bit 15 comes out clear.
uint32_t Blend555(uint32_t target, uint32_t colour, uint32_t alpha)
{
    const uint32_t red = Level((colour >> 10) & 0x1F, (target >> 10) & 0x1F, alpha);
    const uint32_t green = Level((colour >> 5) & 0x1F, (target >> 5) & 0x1F, alpha);
    const uint32_t blue = Level(colour & 0x1F, target & 0x1F, alpha);
    return ((red << 5 | green) << 5) | blue;
}

// The game's 0x0089612A: a 565 pixel from the five high bits of green (the
// lowest comes out 0); red is everything above bit 10, unmasked.
uint32_t Blend565(uint32_t target, uint32_t colour, uint32_t alpha)
{
    const uint32_t red = Level(colour >> 11, target >> 11, alpha);
    const uint32_t green = Level((colour >> 6) & 0x1F, (target >> 6) & 0x1F, alpha);
    const uint32_t blue = Level(colour & 0x1F, target & 0x1F, alpha);
    return ((red << 5 | green) << 6) | blue;
}

// ====================================================================================
// Drawing

// The 32-bit blend: two channels at a time (0xFF00FF), the target weighted
// by 256 - coverage and the colour by coverage, rounded with 0x80.
inline uint32_t Blend8888(uint32_t target, uint32_t coverage, uint32_t colourRB, uint32_t colourAG)
{
    const uint32_t keep = 0x100 - coverage;
    const uint32_t rb = (target & 0xFF00FF) * keep + coverage * colourRB + 0x800080;
    const uint32_t ag = ((target >> 8) & 0xFF00FF) * keep + coverage * colourAG + 0x800080;
    return ((rb >> 8) & 0xFF00FF) | (ag & 0xFF00FF00);
}

// The game's 0x00896E63 (stdcall, ret 0x28): one glyph's coverage
// composited into the picture. `box` is where the glyph goes, `source` the
// part of its coverage (only its left and top are used), `pitch` the bytes
// between coverage rows. Returns true.
bool DrawGlyph(void* font, void* picture, const Rect* box, uint32_t character, Address pixels, const Rect* source,
               int32_t pitch, uint32_t mode, const uint32_t* colours, const Rect* clip)
{
    (void)font;
    (void)character;
    const int32_t width = Virtual<int32_t>(picture, buffer::kWidth);
    const int32_t height = Virtual<int32_t>(picture, buffer::kHeight);
    Rect from = *source;
    Rect to = *box;
    ColorType type{kAutoFromScreenFormat16, 0x10};
    const uint32_t bits = Virtual<uint32_t>(picture, buffer::kBitsPerPixel);
    if (mode == kBlended)
        Virtual<void>(picture, buffer::kColorType, &type);

    // Cut to the clip, then to the picture; the coverage starts as many
    // pixels in as the left and top moved.
    if (clip) {
        if (clip->left > to.left) {
            const int32_t cut = Minus(clip->left, to.left);
            from.left = Plus(from.left, cut);
            to.left = Plus(to.left, cut);
        }
        if (clip->top > to.top) {
            const int32_t cut = Minus(clip->top, to.top);
            from.top = Plus(from.top, cut);
            to.top = Plus(to.top, cut);
        }
        if (clip->right < to.right)
            to.right = clip->right;
        if (clip->bottom < to.bottom)
            to.bottom = clip->bottom;
    }
    if (to.left < 0) {
        from.left = Minus(from.left, to.left);
        to.left = 0;
    }
    if (to.top < 0) {
        from.top = Minus(from.top, to.top);
        to.top = 0;
    }
    if (to.right > width)
        to.right = width;
    if (to.bottom > height)
        to.bottom = height;
    if (to.right <= to.left || to.bottom <= to.top)
        return true;

    const uint32_t rows = static_cast<uint32_t>(Minus(to.bottom, to.top));
    const uint32_t across = static_cast<uint32_t>(Minus(to.right, to.left));
    Address coverage = static_cast<uint32_t>(pitch) * static_cast<uint32_t>(from.top) +
                       static_cast<uint32_t>(from.left) + pixels;

    if (bits == 16) {
        const uint32_t colour = colours[0] & 0xFFFF;
        const uint32_t stride = Virtual<uint32_t>(picture, buffer::kStride) >> 1;
        Address row = At(Virtual<void*>(picture, buffer::kBits)) +
                      (stride * static_cast<uint32_t>(to.top) + static_cast<uint32_t>(to.left)) * 2;
        const Address end = row + rows * stride * 2;
        Address rowEnd = row + across * 2;
        if (row >= end)
            return true;
        const uint32_t step = stride * 2;
        do {
            Address at = row;
            Address cover = coverage;
            if (mode == kMonochrome) {
                for (; at < rowEnd; at += 2, ++cover)
                    if (Load<uint8_t>(cover) > 0x7F)
                        Store<uint16_t>(at, static_cast<uint16_t>(colour));
            } else if (mode == kPalette) {
                for (; at < rowEnd; at += 2, ++cover) {
                    const uint8_t level = Load<uint8_t>(cover);
                    if (level)
                        Store<uint16_t>(at, Load<uint16_t>(At(colours) + level * 4u));
                }
            } else {
                uint32_t (*blend)(uint32_t, uint32_t, uint32_t) = nullptr;
                switch (type.type) {
                case kR5G5B5:
                case kA1R5G5B5: blend = &Blend555; break;
                case kR5G6B5: blend = &Blend565; break;
                case kA4R4G4B4: blend = &Blend4444; break;
                default: break;
                }
                if (blend)
                    for (; at < rowEnd; at += 2, ++cover) {
                        const uint8_t level = Load<uint8_t>(cover);
                        if (level)
                            Store<uint16_t>(at, static_cast<uint16_t>(blend(Load<uint16_t>(at), colour, level >> 3)));
                    }
            }
            row += step;
            rowEnd += step;
            coverage += static_cast<uint32_t>(pitch);
        } while (row < end);
        return true;
    }

    if (bits == 32 || bits == 24) {
        // 24-bit pictures too: four bytes a pixel, the stride in dwords.
        const uint32_t colour = colours[0];
        const uint32_t stride = Virtual<uint32_t>(picture, buffer::kStride) >> 2;
        Address row = At(Virtual<void*>(picture, buffer::kBits)) +
                      (stride * static_cast<uint32_t>(to.top) + static_cast<uint32_t>(to.left)) * 4;
        const Address end = row + rows * stride * 4;
        Address rowEnd = row + across * 4;
        if (row >= end)
            return true;
        const uint32_t step = stride * 4;
        do {
            Address at = row;
            Address cover = coverage;
            if (mode == kMonochrome) {
                for (; at < rowEnd; at += 4, ++cover)
                    if (Load<uint8_t>(cover) > 0x7F)
                        Store<uint32_t>(at, colour);
            } else if (mode == kPalette) {
                for (; at < rowEnd; at += 4, ++cover) {
                    const uint8_t level = Load<uint8_t>(cover);
                    if (level)
                        Store<uint32_t>(at, colours[level]);
                }
            } else {
                // Every pixel is written, those of no coverage unchanged.
                const uint32_t colourAG = (colour >> 8) & 0xFF00FF;
                const uint32_t colourRB = colour & 0xFF00FF;
                for (; at < rowEnd; at += 4, ++cover)
                    Store<uint32_t>(at, Blend8888(Load<uint32_t>(at), Load<uint8_t>(cover), colourRB, colourAG));
            }
            row += step;
            rowEnd += step;
            coverage += static_cast<uint32_t>(pitch);
        } while (row < end);
    }
    return true;
}

// The game's 0x00897246 (slot 7, ret 0x1C): each glyph of a printable
// character (0x20 and up) drawn with DrawGlyph, its whole coverage from
// (0, 0). Returns true.
bool DrawGlyphs(void* font, void* picture, const PlacedGlyph* glyphs, uint32_t count, uint32_t mode,
                const uint32_t* colours, const Rect* clip)
{
    if (count == 0)
        return true;
    Rect source{0, 0, 0, 0};
    for (const PlacedGlyph* glyph = glyphs; count != 0; ++glyph, --count) {
        source.right = glyph->width;
        source.bottom = glyph->height;
        if (glyph->character >= kFirstDrawn)
            DrawGlyph(font, picture, &glyph->box, glyph->character, glyph->pixels, &source, glyph->pitch, mode, colours,
                      clip);
    }
    return true;
}

// ====================================================================================
// The object

// The game's 0x00897722 (thiscall): no references yet, the vtable, and the
// blend tables built by the first one made.
void* Construct(void* self)
{
    Store<uint32_t>(At(self) + kRefCount, 0);
    Store<uint32_t>(At(self), static_cast<uint32_t>(addr::GZFontBlitter_Vtable));
    if (Load<uint8_t>(TablesBuilt()) == 0) {
        BuildTables();
        Store<uint8_t>(TablesBuilt(), 1);
    }
    return self;
}

// The game's 0x0089620D (slot 0, ret 8): itself, AddRef'd through its own
// slot 1, for cIGZUnknown and the blitter's interface.
bool QueryInterface(void* self, uint32_t iid, void** out)
{
    if (iid != kUnknownInterface && iid != kBlitterInterface)
        return false;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return true;
}

// The game's 0x008961F6 (slot 2): the count less one while above 1; else
// the object is deleted (no destructor) and 0 returned.
uint32_t Release(void* self)
{
    const int32_t count = Load<int32_t>(At(self) + kRefCount);
    if (count > 1) {
        Store<int32_t>(At(self) + kRefCount, count - 1);
        return static_cast<uint32_t>(count - 1);
    }
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(self);
    return 0;
}

// The game's 0x00896232 and 0x00896243 (slots 5 and 6, ret 0xC): the
// picture locked and unlocked; the picture's answer comes back as it is.
uint32_t Begin(void* picture) { return Virtual<uint32_t>(picture, buffer::kLock, kLockFlags); }
uint32_t End(void* picture) { return Virtual<uint32_t>(picture, buffer::kUnlock, kLockFlags); }

// ====================================================================================
// Detours

enum Group { kDraw, kObject, kGroups };
bool g_compare[kGroups] = {};

// Above 0 while the game's side of a comparison runs: our detours inside it
// pass through to the game's.
int g_inGame = 0;

struct GameSide {
    GameSide() { ++g_inGame; }
    ~GameSide() { --g_inGame; }
    GameSide(const GameSide&) = delete;
    GameSide& operator=(const GameSide&) = delete;
};

// What a function wrote: (start, bytes).
struct Region {
    Address at = 0;
    uint32_t size = 0;
};

struct Nothing {
    bool operator==(const Nothing&) const { return true; }
};

std::string Hex(const uint8_t* bytes, std::size_t count)
{
    std::string text;
    char two[4];
    for (std::size_t i = 0; i < count; ++i) {
        std::snprintf(two, sizeof two, "%02X", bytes[i]);
        text += two;
    }
    return text;
}

// Ours first, its writes kept aside and the regions put back, then the
// game's (with our detours inside it passing through); the results and the
// regions compared.
template <typename R, typename Original, typename Ours>
R Compared(const char* id, const std::vector<Region>& regions, Original&& original, Ours&& ours)
{
    std::vector<std::vector<uint8_t>> before, mine;
    for (const Region& r : regions) {
        const auto* p = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(r.at));
        before.emplace_back(p, p + r.size);
    }
    const R ourResult = ours();
    std::size_t k = 0;
    for (const Region& r : regions) {
        auto* p = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(r.at));
        mine.emplace_back(p, p + r.size);
        std::memcpy(p, before[k++].data(), r.size);
    }
    R result;
    {
        GameSide side;
        result = original();
    }
    bool same = result == ourResult;
    std::string game, text;
    if (!same) {
        game = "result";
        text = "result";
    }
    k = 0;
    for (const Region& r : regions) {
        const auto* p = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(r.at));
        if (same && std::memcmp(p, mine[k].data(), r.size) != 0) {
            same = false;
            std::size_t at = 0;
            while (p[at] == mine[k][at])
                ++at;
            const std::size_t n = std::min<std::size_t>(16, r.size - at);
            char where[32];
            std::snprintf(where, sizeof where, "region %zu @%zX ", k, at);
            game = where + Hex(p + at, n);
            text = where + Hex(mine[k].data() + at, n);
        }
        ++k;
    }
    compare::Record(id, same, game, text);
    return result;
}

// The rows of the picture a list of glyphs can write: from the highest top
// to the lowest bottom any glyph is cut to, whole strides, and for 32 and
// 24-bit pictures as far past the last row as the glyphs' rightmost edge
// reaches (a 24-bit picture's rows run past its stride). Asks the picture its
// height, bits, stride and pixels (getters; in compare mode only).
Region RowsWritten(void* picture, const PlacedGlyph* glyphs, uint32_t count, const Rect* clip)
{
    const int32_t width = Virtual<int32_t>(picture, buffer::kWidth);
    const int32_t height = Virtual<int32_t>(picture, buffer::kHeight);
    const uint32_t bits = Virtual<uint32_t>(picture, buffer::kBitsPerPixel);
    uint32_t pixelBytes;
    if (bits == 16)
        pixelBytes = 2;
    else if (bits == 32 || bits == 24)
        pixelBytes = 4;
    else
        return {};
    int32_t top = INT32_MAX, bottom = INT32_MIN, right = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const PlacedGlyph& glyph = glyphs[i];
        if (glyph.character < kFirstDrawn)
            continue;
        int32_t t = glyph.box.top, b = glyph.box.bottom, r = glyph.box.right;
        if (clip) {
            t = std::max(t, clip->top);
            b = std::min(b, clip->bottom);
            r = std::min(r, clip->right);
        }
        t = std::max(t, 0);
        b = std::min(b, height);
        r = std::min(r, width);
        if (b <= t || r <= 0)
            continue;
        top = std::min(top, t);
        bottom = std::max(bottom, b);
        right = std::max(right, r);
    }
    if (bottom <= top)
        return {};
    // Past the last row only as far as the glyphs reach (a 24-bit picture's
    // last row can run past its memory by up to a row of pixels).
    const uint32_t stride = Virtual<uint32_t>(picture, buffer::kStride) / pixelBytes * pixelBytes;
    const Address base = At(Virtual<void*>(picture, buffer::kBits));
    const uint32_t reach = static_cast<uint32_t>(right) * pixelBytes;
    const uint32_t slack = reach > stride ? reach - stride : 0;
    return {base + static_cast<uint32_t>(top) * stride,
            static_cast<uint32_t>(bottom - top) * stride + slack};
}

// --- the three blends: registers in, registers out

using BlendFn = void (*)();
BlendFn g_blend4444 = nullptr;
BlendFn g_blend555 = nullptr;
BlendFn g_blend565 = nullptr;

// Calls a game blend with its registers; EDX kept as the game's keeps it.
__declspec(naked) uint32_t __cdecl CallBlend(BlendFn, uint32_t, uint32_t, uint32_t)
{
    __asm push ebx
    __asm push esi
    __asm push edi
    __asm mov eax, dword ptr [esp + 20]
    __asm mov ecx, dword ptr [esp + 24]
    __asm mov edx, dword ptr [esp + 28]
    __asm call dword ptr [esp + 16]
    __asm pop edi
    __asm pop esi
    __asm pop ebx
    __asm ret
}

template <uint32_t (*Ours)(uint32_t, uint32_t, uint32_t), BlendFn* Original>
uint32_t __cdecl DBlend(const char* id, uint32_t target, uint32_t colour, uint32_t alpha)
{
    if (g_inGame)
        return CallBlend(*Original, target, colour, alpha);
    if (!g_compare[kDraw])
        return Ours(target, colour, alpha);
    return compare::Call(id, [&] { return CallBlend(*Original, target, colour, alpha); },
                         [&] { return Ours(target, colour, alpha); });
}

uint32_t __cdecl DBlend4444(uint32_t target, uint32_t colour, uint32_t alpha)
{
    OPENSC4_FIRST("Blend4444 0x00896053");
    return DBlend<&Blend4444, &g_blend4444>("fontblit.blend4444", target, colour, alpha);
}

uint32_t __cdecl DBlend555(uint32_t target, uint32_t colour, uint32_t alpha)
{
    OPENSC4_FIRST("Blend555 0x008960C1");
    return DBlend<&Blend555, &g_blend555>("fontblit.blend555", target, colour, alpha);
}

uint32_t __cdecl DBlend565(uint32_t target, uint32_t colour, uint32_t alpha)
{
    OPENSC4_FIRST("Blend565 0x0089612A");
    return DBlend<&Blend565, &g_blend565>("fontblit.blend565", target, colour, alpha);
}

// Target pixel in EAX, colour in ECX, alpha in EDX; the pixel back in EAX,
// ECX and EDX as they came.
#define OPENSC4_BLEND_THUNK(Name, Target)                                                                      \
    __declspec(naked) void Name()                                                                              \
    {                                                                                                          \
        __asm push ecx                                                                                         \
        __asm push edx                                                                                         \
        __asm push edx                                                                                         \
        __asm push ecx                                                                                         \
        __asm push eax                                                                                         \
        __asm call Target                                                                                      \
        __asm add esp, 12                                                                                      \
        __asm pop edx                                                                                          \
        __asm pop ecx                                                                                          \
        __asm ret                                                                                              \
    }

OPENSC4_BLEND_THUNK(TBlend4444, DBlend4444)
OPENSC4_BLEND_THUNK(TBlend555, DBlend555)
OPENSC4_BLEND_THUNK(TBlend565, DBlend565)

// --- drawing

using DrawGlyphFn = bool(__stdcall*)(void*, void*, const Rect*, uint32_t, Address, const Rect*, int32_t, uint32_t,
                                     const uint32_t*, const Rect*);
using DrawGlyphsFn = bool(__fastcall*)(void*, void*, void*, void*, const PlacedGlyph*, uint32_t, uint32_t,
                                       const uint32_t*, const Rect*);
DrawGlyphFn g_drawGlyph = nullptr;
DrawGlyphsFn g_drawGlyphs = nullptr;

bool __stdcall DDrawGlyph(void* font, void* picture, const Rect* box, uint32_t character, Address pixels,
                          const Rect* source, int32_t pitch, uint32_t mode, const uint32_t* colours, const Rect* clip)
{
    OPENSC4_FIRST("DrawGlyph 0x00896E63");
    auto game = [&] { return g_drawGlyph(font, picture, box, character, pixels, source, pitch, mode, colours, clip); };
    auto ours = [&] { return DrawGlyph(font, picture, box, character, pixels, source, pitch, mode, colours, clip); };
    if (g_inGame)
        return game();
    if (!g_compare[kDraw])
        return ours();
    const PlacedGlyph one{*box, pixels, pitch, source->right, source->bottom, kFirstDrawn};
    return Compared<bool>("fontblit.drawglyph", {RowsWritten(picture, &one, 1, clip)}, game, ours);
}

bool __fastcall DDrawGlyphs(void* self, void*, void* font, void* picture, const PlacedGlyph* glyphs, uint32_t count,
                            uint32_t mode, const uint32_t* colours, const Rect* clip)
{
    OPENSC4_FIRST("DrawGlyphs 0x00897246");
    auto game = [&] { return g_drawGlyphs(self, nullptr, font, picture, glyphs, count, mode, colours, clip); };
    auto ours = [&] { return DrawGlyphs(font, picture, glyphs, count, mode, colours, clip); };
    if (g_inGame)
        return game();
    if (!g_compare[kDraw])
        return ours();
    return Compared<bool>("fontblit.drawglyphs", {RowsWritten(picture, glyphs, count, clip)}, game, ours);
}

OPENSC4_THUNK(TDrawGlyph, DDrawGlyph, 40)
OPENSC4_THUNK(TDrawGlyphs, DDrawGlyphs, 28)

// --- the object

using BuildFn = bool(__cdecl*)();
using ConstructFn = void*(__fastcall*)(void*, void*);
using QueryFn = bool(__fastcall*)(void*, void*, uint32_t, void**);
using ReleaseFn = uint32_t(__fastcall*)(void*, void*);
using LockFn = uint32_t(__fastcall*)(void*, void*, void*, void*, uint32_t);
BuildFn g_build = nullptr;
ConstructFn g_construct = nullptr;
QueryFn g_query = nullptr;
ReleaseFn g_release = nullptr;
LockFn g_begin = nullptr;
LockFn g_end = nullptr;

Region Tables() { return {Table(), kTableBytes + 1}; }

bool __cdecl DBuildTables()
{
    OPENSC4_FIRST("BuildBlendTables 0x0089618D");
    if (g_inGame)
        return g_build();
    if (!g_compare[kObject])
        return BuildTables();
    return Compared<bool>("fontblit.buildtables", {Tables()}, [&] { return g_build(); }, [&] { return BuildTables(); });
}

void* __fastcall DConstruct(void* self, void*)
{
    OPENSC4_FIRST("Construct 0x00897722");
    if (g_inGame)
        return g_construct(self, nullptr);
    if (!g_compare[kObject])
        return Construct(self);
    return Compared<void*>("fontblit.construct", {{At(self), 8}, Tables()}, [&] { return g_construct(self, nullptr); },
                           [&] { return Construct(self); });
}

bool __fastcall DQueryInterface(void* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("QueryInterface 0x0089620D");
    if (g_inGame)
        return g_query(self, nullptr, iid, out);
    if (!g_compare[kObject])
        return QueryInterface(self, iid, out);
    const Region written = out ? Region{At(out), 4} : Region{};
    return Compared<bool>("fontblit.queryinterface", {written, {At(self) + kRefCount, 4}},
                          [&] { return g_query(self, nullptr, iid, out); },
                          [&] { return QueryInterface(self, iid, out); });
}

uint32_t __fastcall DRelease(void* self, void*)
{
    OPENSC4_FIRST("Release 0x008961F6");
    if (g_inGame)
        return g_release(self, nullptr);
    if (!g_compare[kObject])
        return Release(self);
    if (Load<int32_t>(At(self) + kRefCount) <= 1)
        return g_release(self, nullptr);
    return Compared<uint32_t>("fontblit.release", {{At(self) + kRefCount, 4}},
                              [&] { return g_release(self, nullptr); }, [&] { return Release(self); });
}

uint32_t __fastcall DBegin(void* self, void*, void* font, void* picture, uint32_t mode)
{
    OPENSC4_FIRST("Begin 0x00896232");
    if (g_inGame || g_compare[kObject])
        return g_begin(self, nullptr, font, picture, mode);
    return Begin(picture);
}

uint32_t __fastcall DEnd(void* self, void*, void* font, void* picture, uint32_t mode)
{
    OPENSC4_FIRST("End 0x00896243");
    if (g_inGame || g_compare[kObject])
        return g_end(self, nullptr, font, picture, mode);
    return End(picture);
}

OPENSC4_CDECL_THUNK(TBuildTables, DBuildTables, 0)
OPENSC4_THUNK(TConstruct, DConstruct, 0)
OPENSC4_THUNK(TQueryInterface, DQueryInterface, 8)
OPENSC4_THUNK(TRelease, DRelease, 0)
OPENSC4_THUNK(TBegin, DBegin, 12)
OPENSC4_THUNK(TEnd, DEnd, 12)

// ====================================================================================
// Installation

struct Detour {
    std::uintptr_t address;
    void (*thunk)();
    void** original;
};

template <std::size_t N>
bool InstallAll(registry::Mode mode, Group group, const char* name, const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    g_compare[group] = mode == registry::Mode::Compare;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.thunk), detour.original))
            return false;
    }
    log::Write("fontblit: %s: %zu functions %s", name, N,
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

template <typename Fn>
void** Slot(Fn* fn)
{
    return reinterpret_cast<void**>(fn);
}

bool InstallDraw(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFontBlitter_DrawGlyphs, &TDrawGlyphs, Slot(&g_drawGlyphs)},
        {addr::GZFontBlitter_DrawGlyph, &TDrawGlyph, Slot(&g_drawGlyph)},
        {addr::GZFontBlitter_Blend4444, &TBlend4444, Slot(&g_blend4444)},
        {addr::GZFontBlitter_Blend555, &TBlend555, Slot(&g_blend555)},
        {addr::GZFontBlitter_Blend565, &TBlend565, Slot(&g_blend565)},
    };
    return InstallAll(mode, kDraw, "draw", detours);
}

bool InstallObject(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFontBlitter_Construct, &TConstruct, Slot(&g_construct)},
        {addr::GZFontBlitter_BuildBlendTables, &TBuildTables, Slot(&g_build)},
        {addr::GZFontBlitter_QueryInterface, &TQueryInterface, Slot(&g_query)},
        {addr::GZFontBlitter_Release, &TRelease, Slot(&g_release)},
        {addr::GZFontBlitter_Begin, &TBegin, Slot(&g_begin)},
        {addr::GZFontBlitter_End, &TEnd, Slot(&g_end)},
    };
    return InstallAll(mode, kObject, "object", detours);
}

const bool g_registered[] = {
    registry::Register({"framework.font.blitter.draw", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFontBlitter_DrawGlyphs,
                        "Our glyph compositing: DrawGlyphs, DrawGlyph and its 4444, 555 and 565 blends "
                        "(5 detours; docs/fonts.md)",
                        true, InstallDraw}),
    registry::Register({"framework.font.blitter.object", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFontBlitter_Construct,
                        "Our glyph blitter object: constructor, blend tables, QueryInterface, Release, Begin and "
                        "End (6 detours; docs/fonts.md)",
                        true, InstallObject}),
};

} // namespace
} // namespace opensc4::fontblit
