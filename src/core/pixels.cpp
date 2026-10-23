// Our vertex format and pixel helpers of the GZ framework, 0x008D01F0 to
// 0x008D2993 (docs/pixels.md): leaf functions the stock drivers,
// UIPictureTextures::Upload, the effects and the texture loader call. Each
// is a detour on the game's function, in five groups, off by default:
//
//   renderer.pixels.vertex    a vertex format's bit field: the legacy IDs
//                             expanded (0x008D01F0), made from a list of
//                             element types (0x008D02C0), its stride
//                             (0x008D0370), an element's offset (0x008D04A0)
//                             and count (0x008D04F0), and one element of n
//                             vertices gathered (0x008D0520)
//   renderer.pixels.key       copies that clear the pixels matching a colour
//                             key: 16-bit (0x008D0660) and 32-bit (0x008D0750)
//                             with a mask, and their 1555, 4444 and 8888
//                             wrappers (0x008D0820, 0x008D0860, 0x008D08A0)
//   renderer.pixels.modulate  pixels times a colour: 8888, 888, 4444, 1555
//                             (0x008D08E0 to 0x008D0B60) and DXT1 and DXT3
//                             blocks (0x008D0C60, 0x008D0E10)
//   renderer.pixels.dxt       DXT blocks decoded to 8888 (0x008D1030,
//                             0x008D1A30), 1555 (0x008D1240, 0x008D1F80) and
//                             4444 (0x008D1630, 0x008D2140), with the two
//                             block writers (0x008D0FD0, 0x008D1E70)
//   renderer.pixels.format    the bits per channel of a texture source
//                             (0x008D2790)
//
// The arithmetic is the game's, step for step and in 32 bits, its quirks
// included (the 16-bit modulates step their rows by the pitch less twice the
// row's bytes; the DXT3 modulate takes the indices from the alpha). The MMX
// key copies are done a lane at a time in the same 16, 8, 4 and 2-byte
// pieces. Callers keep ECX and EDX across some of these calls (0x008D04A0
// and 0x008D0520 across 0x008D01F0), so every detour is entered through a
// thunk that keeps them (core/thunk.h).
//
// Compare mode runs ours and then the game's on the same inputs: what ours
// wrote is kept aside and the memory put back before the game's runs, so the
// game's result is what the game sees; the two results are compared byte for
// byte. Still the game's: 0x008D2320, the drivers' test that draws 20 frames
// of quads and reads them back (not a pixel helper; it needs a driver).
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

namespace opensc4::pixels {
namespace {

// ====================================================================================
// Helpers

// Addresses are 32 bits and wrap as the game's do.
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

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("pixels: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// ====================================================================================
// Vertex formats

constexpr uint32_t kFormatBit = 0x80000000;
constexpr int32_t kTypes = 10;

// Per element type: bytes (0x00ACC0AC), the bit position (0x00ACC0D4) and
// mask (0x00ACC0FC) of its count, and the mask of the types laid out before
// it (0x00ACC120, read only for types 1 to 9). The game reads past its
// tables for other types; its callers pass 0 to 9.
constexpr uint32_t kSize[kTypes] = {0xC, 4, 4, 0xC, 4, 4, 4, 8, 0xC, 0x10};
constexpr uint32_t kShift[kTypes] = {0, 2, 5, 6, 7, 8, 0xA, 0xE, 0x12, 0x16};
constexpr uint32_t kMask[kTypes] = {3, 7, 1, 1, 1, 3, 0xF, 0xF, 0xF, 0xF};
constexpr uint32_t kBefore[kTypes] = {0xF, 0, 3, 0x1F, 0x3F, 0x7F, 0xFF, 0x3FF, 0x3FFF, 0x3FFFF};

inline uint32_t Entry(const uint32_t (&table)[kTypes], int32_t type)
{
    return type >= 0 && type < kTypes ? table[type] : 0;
}

// The game's 0x008D01F0: a legacy ID to its bit field; a bit field (bit
// 31) as it is; 0 for any other value.
uint32_t Expand(uint32_t format)
{
    if (static_cast<int32_t>(format) < 0)
        return format;
    switch (format) {
    case 0x01: return 0x80000101;
    case 0x02: return 0x80004001;
    case 0x03: return 0x80008001;
    case 0x0A: return 0x80004101;
    case 0x0B: return 0x80008101;
    case 0x20: return 0x80000001;
    case 0x21: return 0x80000041;
    case 0x22: return 0x80000141;
    case 0x23: return 0x80004041;
    case 0x24: return 0x80008041;
    case 0x25: return 0x80004141;
    case 0x26: return 0x80008141;
    default: return 0;
    }
}

// The game's 0x008D02C0: the element types of a list counted (types 0 to
// 9; the game's counters are on its stack and other values write past
// them) and packed unmasked at their bit positions, with bit 31.
uint32_t Make(uint32_t count, const int32_t* types)
{
    uint32_t n[kTypes] = {};
    for (uint32_t i = 0; i < count; ++i)
        if (types[i] >= 0 && types[i] < kTypes)
            ++n[types[i]];
    uint32_t format = n[9];
    format = format << 4 | n[8];
    format = format << 4 | n[7];
    format = format << 4 | n[6];
    format = format << 2 | n[5];
    format = format << 1 | n[4];
    format = format << 1 | n[3];
    format = format << 1 | n[2];
    format = format << 3 | n[1];
    format = format << 2 | n[0];
    return format | kFormatBit;
}

// The game's 0x008D0370: bytes per vertex, a table for the legacy IDs; else
// from the counts, the second group only when the low nibble is non-zero.
uint32_t Stride(uint32_t format)
{
    switch (format) {
    case 0x01: return 0x10;
    case 0x02: return 0x14;
    case 0x03:
    case 0x22: return 0x1C;
    case 0x0A:
    case 0x21: return 0x18;
    case 0x0B:
    case 0x23: return 0x20;
    case 0x20: return 0xC;
    case 0x24: return 0x28;
    case 0x25: return 0x24;
    case 0x26: return 0x2C;
    default: break;
    }
    if (static_cast<int32_t>(format) >= 0)
        format = Expand(format);
    const uint32_t c = format & 0x7FFFFFFF;
    uint32_t stride = (((c >> 10) & 0xF) + ((c >> 14) & 0xF) * 2 + ((c >> 8) & 3) + (c & 3) * 3) * 4;
    if (c & 0xF) {
        uint32_t more = ((c >> 18) & 0xF) + ((c >> 6) & 1);
        more = more * 3 + ((c >> 22) & 0xF) * 4;
        more += (c >> 2) & 7;
        more += (c >> 7) & 1;
        more += (c >> 5) & 1;
        stride += more * 4;
    }
    return stride;
}

// The game's 0x008D04A0: the index-th element of a type, its byte offset.
uint32_t ElementOffset(uint32_t format, int32_t type, uint32_t index)
{
    uint32_t offset = Entry(kSize, type) * index;
    if (static_cast<int32_t>(format) >= 0)
        format = Expand(format);
    if (type > 0)
        offset += Stride((Entry(kBefore, type) | kFormatBit) & format);
    return offset;
}

// The game's 0x008D04F0: how many elements of a type the format has (an
// arithmetic shift, as the game's).
uint32_t ElementCount(uint32_t format, int32_t type)
{
    if (static_cast<int32_t>(format) >= 0)
        format = Expand(format);
    return static_cast<uint32_t>(static_cast<int32_t>(format) >> (Entry(kShift, type) & 31)) & Entry(kMask, type);
}

// The game's 0x008D0520: one element of each of `count` vertices copied to
// a packed array; dword moves for 4, 8 and 12 bytes, else a dword at a time
// then the odd bytes (rep movsd, rep movsb).
void Gather(uint32_t count, const void* vertices, void* out, uint32_t format, int32_t type, uint32_t index)
{
    const uint32_t stride = Stride(format);
    const uint32_t size = Entry(kSize, type);
    Address from = At(vertices) + ElementOffset(format, type, index);
    Address to = At(out);
    if (count == 0)
        return;
    if (size == 4) {
        do {
            Store(to, Load<uint32_t>(from));
            from += stride;
            to += 4;
        } while (--count != 0);
    } else if (size == 8) {
        do {
            Store(to, Load<uint32_t>(from));
            Store(to + 4, Load<uint32_t>(from + 4));
            from += stride;
            to += 8;
        } while (--count != 0);
    } else if (size == 0xC) {
        do {
            Store(to, Load<uint32_t>(from));
            Store(to + 4, Load<uint32_t>(from + 4));
            Store(to + 8, Load<uint32_t>(from + 8));
            from += stride;
            to += 0xC;
        } while (--count != 0);
    } else {
        do {
            Address s = from, d = to;
            for (uint32_t n = size >> 2; n != 0; --n, s += 4, d += 4)
                Store(d, Load<uint32_t>(s));
            for (uint32_t n = size & 3; n != 0; --n, ++s, ++d)
                Store(d, Load<uint8_t>(s));
            from += stride;
            to += size;
        } while (--count != 0);
    }
}

// ====================================================================================
// Colour keys

// PCMPEQW/PCMPEQD then PANDN: each lane whose masked value is the masked key
// becomes 0.
uint64_t Keyed16(uint64_t pixels, uint32_t mask, uint32_t key)
{
    uint64_t out = 0;
    for (int lane = 0; lane < 4; ++lane) {
        const uint32_t p = static_cast<uint32_t>(pixels >> (16 * lane)) & 0xFFFF;
        if ((p & mask) != key)
            out |= static_cast<uint64_t>(p) << (16 * lane);
    }
    return out;
}

uint64_t Keyed32(uint64_t pixels, uint32_t mask, uint32_t key)
{
    uint64_t out = 0;
    for (int lane = 0; lane < 2; ++lane) {
        const uint32_t p = static_cast<uint32_t>(pixels >> (32 * lane));
        if ((p & mask) != key)
            out |= static_cast<uint64_t>(p) << (32 * lane);
    }
    return out;
}

// The game's 0x008D0660: 16-bit pixels copied, each matching the key under
// the mask (their low words) cleared. A row is done from its end backwards
// in index: EBX runs from 15 - 2 * width up, 16 bytes at a time while the
// add does not carry, then a piece of 8, 4 and 2 bytes as EBX allows.
void KeyCopy16(const void* source, int32_t sourcePitch, void* target, int32_t targetPitch, uint32_t width,
               uint32_t height, uint32_t key, uint32_t mask)
{
    mask &= 0xFFFF;
    key = key & mask;
    Address s = At(source) + width * 2 - 15;
    Address d = At(target) + width * 2 - 15;
    const uint32_t start = 0u - width * 2;
    do {
        uint32_t at = start + 15;
        if (at >= start) {
            bool carry;
            do {
                const uint64_t a = Load<uint64_t>(s + at);
                const uint64_t b = Load<uint64_t>(s + at + 8);
                const uint32_t next = at + 16;
                carry = next < at;
                Store(d + at, Keyed16(a, mask, key));
                Store(d + at + 8, Keyed16(b, mask, key));
                at = next;
            } while (!carry);
        }
        if (static_cast<int32_t>(at) <= 7) {
            Store(d + at, Keyed16(Load<uint64_t>(s + at), mask, key));
            at += 8;
        }
        if (static_cast<int32_t>(at) <= 11) {
            Store(d + at, static_cast<uint32_t>(Keyed16(Load<uint32_t>(s + at), mask, key)));
            at += 4;
        }
        if (static_cast<int32_t>(at) <= 13)
            Store(d + at, static_cast<uint16_t>(Keyed16(Load<uint16_t>(s + at), mask, key)));
        s += static_cast<uint32_t>(sourcePitch);
        d += static_cast<uint32_t>(targetPitch);
    } while (--height != 0);
}

// The game's 0x008D0750: the same for 32-bit pixels, pieces of 16, 8 and 4.
void KeyCopy32(const void* source, int32_t sourcePitch, void* target, int32_t targetPitch, uint32_t width,
               uint32_t height, uint32_t key, uint32_t mask)
{
    key = key & mask;
    Address s = At(source) + width * 4 - 15;
    Address d = At(target) + width * 4 - 15;
    const uint32_t start = 0u - width * 4;
    do {
        uint32_t at = start + 15;
        if (at >= start) {
            bool carry;
            do {
                const uint64_t a = Load<uint64_t>(s + at);
                const uint64_t b = Load<uint64_t>(s + at + 8);
                const uint32_t next = at + 16;
                carry = next < at;
                Store(d + at, Keyed32(a, mask, key));
                Store(d + at + 8, Keyed32(b, mask, key));
                at = next;
            } while (!carry);
        }
        if (static_cast<int32_t>(at) <= 7) {
            Store(d + at, Keyed32(Load<uint64_t>(s + at), mask, key));
            at += 8;
        }
        if (static_cast<int32_t>(at) <= 11)
            Store(d + at, static_cast<uint32_t>(Keyed32(Load<uint32_t>(s + at), mask, key)));
        s += static_cast<uint32_t>(sourcePitch);
        d += static_cast<uint32_t>(targetPitch);
    } while (--height != 0);
}

// The game's 0x008D0820, 0x008D0860 and 0x008D08A0: target first, and the
// mask of the format.
void KeyCopy1555(void* target, int32_t targetPitch, const void* source, int32_t sourcePitch, uint32_t width,
                 uint32_t height, uint32_t key)
{
    KeyCopy16(source, sourcePitch, target, targetPitch, width, height, key, 0x7FFF7FFF);
}

void KeyCopy4444(void* target, int32_t targetPitch, const void* source, int32_t sourcePitch, uint32_t width,
                 uint32_t height, uint32_t key)
{
    KeyCopy16(source, sourcePitch, target, targetPitch, width, height, key, 0x0FFF0FFF);
}

void KeyCopy8888(void* target, int32_t targetPitch, const void* source, int32_t sourcePitch, uint32_t width,
                 uint32_t height, uint32_t key)
{
    KeyCopy32(source, sourcePitch, target, targetPitch, width, height, key, 0x00FFFFFF);
}

// ====================================================================================
// Modulation

// The game's 0x008D08E0: each channel times the colour's, two channels per
// multiply, rounded.
void Modulate8888(void* target, int32_t targetPitch, const void* source, int32_t sourcePitch, uint32_t width,
                  uint32_t height, uint32_t colour)
{
    const uint32_t a = colour >> 24, r = (colour >> 16) & 0xFF, g = (colour >> 8) & 0xFF, b = colour & 0xFF;
    const uint32_t targetSkip = static_cast<uint32_t>(targetPitch) - width * 4;
    const uint32_t sourceSkip = static_cast<uint32_t>(sourcePitch) - width * 4;
    Address d = At(target), s = At(source);
    do {
        uint32_t n = width;
        do {
            const uint32_t p = Load<uint32_t>(s);
            const uint32_t high = ((p >> 8) & 0xFF0000) * a + ((p >> 8) & 0xFF) * g;
            const uint32_t low = (p & 0xFF0000) * r + (p & 0xFF) * b + 0x800080;
            Store(d, (((low >> 8) ^ (high + 0xFF800080)) & 0xFF00FF) ^ (high + 0x800080));
            s += 4;
            d += 4;
        } while (--n != 0);
        d += targetSkip;
        s += sourceSkip;
    } while (--height != 0);
}

// The game's 0x008D09C0: 24-bit pixels, a byte at a time.
void Modulate888(void* target, int32_t targetPitch, const void* source, int32_t sourcePitch, uint32_t width,
                 uint32_t height, uint32_t colour)
{
    const uint32_t c0 = colour & 0xFF, c1 = (colour >> 8) & 0xFF, c2 = (colour >> 16) & 0xFF;
    const uint32_t targetSkip = static_cast<uint32_t>(targetPitch) - width * 3;
    const uint32_t sourceSkip = static_cast<uint32_t>(sourcePitch) - width * 3;
    Address d = At(target), s = At(source);
    do {
        uint32_t n = width;
        do {
            Store(d, static_cast<uint8_t>((Load<uint8_t>(s) * c0 + 0x80) >> 8));
            Store(d + 1, static_cast<uint8_t>((Load<uint8_t>(s + 1) * c1 + 0x80) >> 8));
            Store(d + 2, static_cast<uint8_t>((Load<uint8_t>(s + 2) * c2 + 0x80) >> 8));
            d += 3;
            s += 3;
        } while (--n != 0);
        d += targetSkip;
        s += sourceSkip;
    } while (--height != 0);
}

// The game's 0x008D0A60: 4444 pixels, two nibbles per multiply. Rows step by
// the pitch less four bytes a pixel, as the 32-bit one's.
void Modulate4444(void* target, int32_t targetPitch, const void* source, int32_t sourcePitch, uint32_t width,
                  uint32_t height, uint32_t colour)
{
    const uint32_t c3 = colour >> 28, c2 = (colour >> 20) & 0xF, c1 = (colour >> 12) & 0xF, c0 = (colour >> 4) & 0xF;
    const uint32_t targetSkip = static_cast<uint32_t>(targetPitch) - width * 4;
    const uint32_t sourceSkip = static_cast<uint32_t>(sourcePitch) - width * 4;
    Address d = At(target), s = At(source);
    do {
        uint32_t n = width;
        do {
            const uint32_t p = Load<uint16_t>(s);
            const uint32_t odd = ((p & 0xF000) * c3 + (p & 0xF0) * c1 + 0x808) & 0xF0F00;
            const uint32_t even = ((p & 0xF00) * c2 + (p & 0xF) * c0 + 0x808) & 0xF0F0;
            Store(d, static_cast<uint16_t>((odd + even) >> 4));
            s += 2;
            d += 2;
        } while (--n != 0);
        d += targetSkip;
        s += sourceSkip;
    } while (--height != 0);
}

// The game's 0x008D0B60: 1555 pixels, the alpha bit ANDed with the colour's
// top bit. The same row step.
void Modulate1555(void* target, int32_t targetPitch, const void* source, int32_t sourcePitch, uint32_t width,
                  uint32_t height, uint32_t colour)
{
    const uint32_t r = (colour >> 19) & 0x1F, g = (colour >> 11) & 0x1F, b = (colour >> 3) & 0x1F;
    const uint32_t alpha = (colour >> 16) & 0x8000;
    const uint32_t targetSkip = static_cast<uint32_t>(targetPitch) - width * 4;
    const uint32_t sourceSkip = static_cast<uint32_t>(sourcePitch) - width * 4;
    Address d = At(target), s = At(source);
    do {
        uint32_t n = width;
        do {
            const uint32_t p = Load<uint16_t>(s);
            const uint32_t rb = ((p & 0x7C00) * r + (p & 0x1F) * b + 0x4010) & 0xF83E0;
            const uint32_t green = ((p & 0x3E0) * g + 0x200) & 0x7C00;
            Store(d, static_cast<uint16_t>(((rb + green) >> 5) + (alpha & p)));
            s += 2;
            d += 2;
        } while (--n != 0);
        d += targetSkip;
        s += sourceSkip;
    } while (--height != 0);
}

// A 565 colour times the colour's 5-bit channels (red and blue in one
// multiply), as the DXT modulates do it.
struct Scale565 {
    uint32_t r, g, b;
    explicit Scale565(uint32_t colour) : r((colour >> 19) & 0x1F), g((colour >> 11) & 0x1F), b((colour >> 3) & 0x1F) {}
    uint32_t operator()(uint32_t c) const
    {
        const uint32_t rb = ((c & 0xF800) * r + (c & 0x1F) * b + 0x10010) & 0x3E03E0;
        const uint32_t green = ((c & 0x7E0) * g + 0x400) & 0xF800;
        return (rb + green) >> 6;
    }
};

// The game's 0x008D0C60: DXT1 blocks, width and height in pixels (four a
// block). The two colours modulated; where their order changes they are
// swapped and the indices remapped (all inverted for four colours; 0 and 1
// swapped for three), and a four-colour block whose colours become equal
// gets indices 0.
void ModulateDXT1(void* target, int32_t targetPitch, const void* source, int32_t sourcePitch, uint32_t width,
                  uint32_t height, uint32_t colour)
{
    const Scale565 scale(colour);
    uint32_t blocks = width >> 2;
    uint32_t rows = height >> 2;
    const uint32_t targetSkip = static_cast<uint32_t>(targetPitch) - blocks * 8;
    const uint32_t sourceSkip = static_cast<uint32_t>(sourcePitch) - blocks * 8;
    Address d = At(target), s = At(source);
    do {
        uint32_t n = blocks;
        do {
            const uint32_t c1 = Load<uint16_t>(s + 2);
            const uint32_t c0 = Load<uint16_t>(s);
            uint32_t indices = Load<uint32_t>(s + 4);
            const uint32_t m0 = scale(c0);
            const uint32_t m1 = scale(c1);
            if (c0 > c1) {
                if (m0 < m1) {
                    Store(d + 2, static_cast<uint16_t>(m0));
                    Store(d, static_cast<uint16_t>(m1));
                    indices = ~indices;
                } else {
                    Store(d + 2, static_cast<uint16_t>(m1));
                    Store(d, static_cast<uint16_t>(m0));
                    if (m0 == m1)
                        indices = 0;
                }
            } else if (m0 > m1) {
                Store(d + 2, static_cast<uint16_t>(m0));
                Store(d, static_cast<uint16_t>(m1));
                indices ^= (~indices >> 1) & 0x55555555;
            } else {
                Store(d, static_cast<uint16_t>(m0));
                Store(d + 2, static_cast<uint16_t>(m1));
            }
            Store(d + 4, indices);
            s += 8;
            d += 8;
        } while (--n != 0);
        s += sourceSkip;
        d += targetSkip;
    } while (--rows != 0);
}

// The game's 0x008D0E10: DXT3 blocks; the alpha copied. As the game does,
// the indices are taken from the alpha's second dword (+4, not +0xC) and the
// colours are written back in their own order after the branches.
void ModulateDXT3(void* target, int32_t targetPitch, const void* source, int32_t sourcePitch, uint32_t width,
                  uint32_t height, uint32_t colour)
{
    const Scale565 scale(colour);
    uint32_t blocks = width >> 2;
    uint32_t rows = height >> 2;
    const uint32_t targetSkip = static_cast<uint32_t>(targetPitch) - blocks * 16;
    const uint32_t sourceSkip = static_cast<uint32_t>(sourcePitch) - blocks * 16;
    Address d = At(target), s = At(source);
    do {
        uint32_t n = blocks;
        do {
            const uint32_t c0 = Load<uint16_t>(s + 8);
            uint32_t indices = Load<uint32_t>(s + 4);
            const uint32_t c1 = Load<uint16_t>(s + 0xA);
            const uint32_t m0 = scale(c0);
            const uint32_t m1 = scale(c1);
            Store(d, Load<uint32_t>(s));
            Store(d + 4, Load<uint32_t>(s + 4));
            if (c0 > c1) {
                if (m0 < m1) {
                    Store(d + 8, static_cast<uint16_t>(m1));
                    Store(d + 0xA, static_cast<uint16_t>(m0));
                    indices = ~indices;
                } else {
                    Store(d + 0xA, static_cast<uint16_t>(m1));
                    Store(d + 8, static_cast<uint16_t>(m0));
                    if (m0 == m1)
                        indices = 0;
                }
            } else if (m0 > m1) {
                indices ^= (~indices >> 1) & 0x55555555;
                Store(d + 8, static_cast<uint16_t>(m1));
                Store(d + 0xA, static_cast<uint16_t>(m0));
            } else {
                Store(d + 8, static_cast<uint16_t>(m0));
                Store(d + 0xA, static_cast<uint16_t>(m1));
            }
            Store(d + 8, static_cast<uint16_t>(m0));
            Store(d + 0xA, static_cast<uint16_t>(m1));
            Store(d + 0xC, indices);
            s += 16;
            d += 16;
        } while (--n != 0);
        s += sourceSkip;
        d += targetSkip;
    } while (--rows != 0);
}

// ====================================================================================
// DXT decoding

// A 565 colour as 32 bits (the 5 and 6-bit fields widened by repeating
// their top bits), 0x008D1030's way.
uint32_t Widen565(uint32_t c)
{
    uint32_t rb = ((c & 0xF800) << 5) + (c & 0x1F);
    rb <<= 3;
    rb += rb >> 5;
    const uint32_t g = c & 0x7E0;
    const uint32_t green = (g << 5) + (g >> 1);
    return ((green ^ rb) & 0xFF00) ^ rb;
}

// 0x008D1A30's way: the same, fields masked and added.
uint32_t Widen565Masked(uint32_t c)
{
    uint32_t rb = ((c & 0xF800) << 5) + (c & 0x1F);
    rb <<= 3;
    const uint32_t g = c & 0x7E0;
    const uint32_t green = ((g << 5) + (g >> 1)) & 0xFF00;
    return green + ((rb + (rb >> 5)) & 0xFF00FF);
}

// The third and fourth colours of a four-colour block: two thirds of one
// and a third of the other, two channels per multiply (0xAB and 0x55).
void Thirds8888(uint32_t p0, uint32_t p1, uint32_t& c2, uint32_t& c3)
{
    const uint32_t p0High = (p0 >> 8) & 0xFF00FF, p1High = (p1 >> 8) & 0xFF00FF;
    const uint32_t p0Low = p0 & 0xFF00FF, p1Low = p1 & 0xFF00FF;
    const uint32_t high2 = p0High * 0xAB + p1High * 0x55;
    const uint32_t high3 = p1High * 0xAB + p0High * 0x55;
    const uint32_t low2 = p0Low * 0xAB + p1Low * 0x55 + 0x800080;
    const uint32_t low3 = p1Low * 0xAB + p0Low * 0x55 + 0x800080;
    c2 = (((low2 >> 8) ^ (high2 + 0xFF800080)) & 0xFF00FF) ^ (high2 + 0x800080);
    c3 = (((low3 >> 8) ^ (high3 + 0xFF800080)) & 0xFF00FF) ^ (high3 + 0x800080);
}

// The average of a three-colour block (per byte, rounded up).
inline uint32_t Average8888(uint32_t p0, uint32_t p1) { return (p1 | p0) - (((p1 ^ p0) >> 1) & 0x7F7F7F7F); }

// The game's 0x008D0FD0 (stdcall): a 4 by 4 block of 32-bit pixels, rows
// `pitch` bytes apart, from 2-bit indices into a palette of four.
void WriteBlock8888(void* target, int32_t pitch, uint32_t indices, const uint32_t* palette)
{
    Address d = At(target);
    for (int row = 0; row < 4; ++row) {
        const uint32_t next = indices >> 4;
        const uint32_t second = palette[(indices & 0xC) >> 2];
        Store(d, palette[indices & 3]);
        Store(d + 4, second);
        indices >>= 8;
        const uint32_t fourth = palette[(next & 0xC) >> 2];
        Store(d + 8, palette[next & 3]);
        Store(d + 0xC, fourth);
        d += static_cast<uint32_t>(pitch);
    }
}

// The game's 0x008D1E70: a 4 by 4 block of 16-bit pixels. (Registers: the
// target in EAX, the indices in ECX, the palette in EDX; see its thunk.)
void WriteBlock16(void* target, uint32_t indices, const uint16_t* palette, int32_t pitch)
{
    Address d = At(target);
    for (int row = 0; row < 4; ++row) {
        for (int x = 0; x < 4; ++x)
            Store(d + 2 * x, palette[(indices >> (8 * row + 2 * x)) & 3]);
        d += static_cast<uint32_t>(pitch);
    }
}

// The game's 0x008D1030: DXT1 blocks to opaque 32-bit pixels. `blockPitch`
// is in blocks; the target's pitch in bytes.
void DecodeDXT1To8888(void* target, int32_t targetPitch, const void* blocks, uint32_t blockPitch, uint32_t blocksWide,
                      uint32_t blocksHigh)
{
    if (blocksWide == 0 || blocksHigh == 0)
        return;
    const uint32_t targetSkip = (static_cast<uint32_t>(targetPitch) - blocksWide * 4) << 2;
    const uint32_t sourceSkip = (blockPitch - blocksWide) << 3;
    Address d = At(target), s = At(blocks);
    do {
        uint32_t n = blocksWide;
        do {
            const uint16_t c0 = Load<uint16_t>(s), c1 = Load<uint16_t>(s + 2);
            uint32_t palette[4];
            palette[0] = Widen565(c0) | 0xFF000000;
            palette[1] = Widen565(c1) | 0xFF000000;
            if (c0 > c1) {
                Thirds8888(palette[0], palette[1], palette[2], palette[3]);
            } else {
                palette[2] = Average8888(palette[0], palette[1]);
                palette[3] = 0;
            }
            WriteBlock8888(reinterpret_cast<void*>(static_cast<uintptr_t>(d)), targetPitch, Load<uint32_t>(s + 4),
                           palette);
            d += 0x10;
            s += 8;
        } while (--n != 0);
        d += targetSkip;
        s += sourceSkip;
    } while (--blocksHigh != 0);
}

// The thirds of a 16-bit four-colour block: the green field (with what lies
// outside the other mask) and the red and blue fields in one multiply each,
// shifted down and merged.
struct Thirds16 {
    uint32_t greenMask, otherMask, near, far, greenRound, otherRound, shift;
    void operator()(uint32_t p0, uint32_t p1, uint32_t& c2, uint32_t& c3) const
    {
        const uint32_t p0Green = p0 & greenMask, p1Green = p1 & greenMask;
        const uint32_t p0Other = p0 & otherMask, p1Other = p1 & otherMask;
        const uint32_t green2 = (p0Green * near + p1Green * far + greenRound) >> shift;
        const uint32_t other2 = (p0Other * near + p1Other * far + otherRound) >> shift;
        const uint32_t green3 = (p1Green * near + p0Green * far + greenRound) >> shift;
        const uint32_t other3 = (p0Other * far + p1Other * near + otherRound) >> shift;
        c2 = ((other2 ^ green2) & otherMask) ^ green2;
        c3 = ((other3 ^ green3) & otherMask) ^ green3;
    }
};

// 1555 (0x83E0 and 0x7C1F, 21 and 11 thirty-seconds) and 4444 (0xF0F0 and
// 0x0F0F, 11 and 5 sixteenths).
constexpr Thirds16 kThirds555{0x83E0, 0x7C1F, 0x15, 0xB, 0x80200, 0x4010, 5};
constexpr Thirds16 kThirds444{0xF0F0, 0x0F0F, 0xB, 5, 0x8080, 0x808, 4};

// A 565 colour as 555 and as 444 (the low bits of each field dropped).
inline uint32_t To555(uint32_t c) { return ((c >> 1) & 0x7FE0) + (c & 0x1F); }
inline uint32_t To444(uint32_t c) { return ((c >> 4) & 0xF00) + ((c >> 3) & 0xF0) + (((c & 0xFF) >> 1) & 0xF); }

// A 16-bit palette of four from the block's two colours: thirds when the
// first is the greater, else the average (rounded up, `averageMask` the
// fields' bits but their top) and 0.
void Palette16(uint32_t c0, uint32_t c1, uint32_t p0, uint32_t p1, const Thirds16& thirds, uint32_t averageMask,
               uint16_t (&palette)[4])
{
    palette[0] = static_cast<uint16_t>(p0);
    palette[1] = static_cast<uint16_t>(p1);
    p0 = palette[0];
    p1 = palette[1];
    if (c0 > c1) {
        uint32_t c2, c3;
        thirds(p0, p1, c2, c3);
        palette[2] = static_cast<uint16_t>(c2);
        palette[3] = static_cast<uint16_t>(c3);
    } else {
        palette[2] = static_cast<uint16_t>((p1 | p0) - (((p0 >> 1) ^ (p1 >> 1)) & averageMask));
        palette[3] = 0;
    }
}

// The game's 0x008D1240 and 0x008D1630: DXT3 blocks to 16-bit pixels, a
// pixel's alpha (from its 4-bit alpha, `alpha`) added to its palette entry.
template <typename Alpha>
void DecodeDXT3To16(void* target, int32_t targetPitch, const void* blocks, uint32_t blockPitch, uint32_t blocksWide,
                    uint32_t blocksHigh, uint32_t (*to16)(uint32_t), const Thirds16& thirds, uint32_t averageMask,
                    Alpha alpha)
{
    if (blocksWide == 0 || blocksHigh == 0)
        return;
    const uint32_t pitch = static_cast<uint32_t>(targetPitch);
    const uint32_t targetSkip = (pitch - blocksWide * 2) << 2;
    const uint32_t sourceSkip = (blockPitch - blocksWide) << 4;
    Address d = At(target), s = At(blocks);
    do {
        uint32_t n = blocksWide;
        do {
            const uint32_t c0 = Load<uint16_t>(s + 8), c1 = Load<uint16_t>(s + 0xA);
            const uint32_t indices = Load<uint32_t>(s + 0xC);
            uint16_t palette[4];
            Palette16(c0, c1, to16(c0), to16(c1), thirds, averageMask, palette);
            for (int i = 0; i < 16; ++i) {
                // The alpha read again before each store, as the game does
                // (it matters only when the target is the blocks).
                const uint32_t a = static_cast<uint32_t>(Load<uint64_t>(s) >> (4 * i)) & 0xF;
                const uint16_t pixel = static_cast<uint16_t>(alpha(a) + palette[(indices >> (2 * i)) & 3]);
                Store(d + (i >> 2) * pitch + (i & 3) * 2, pixel);
            }
            d += 8;
            s += 16;
        } while (--n != 0);
        d += targetSkip;
        s += sourceSkip;
    } while (--blocksHigh != 0);
}

uint32_t To555Of(uint32_t c) { return To555(c); }
uint32_t To444Of(uint32_t c) { return To444(c); }

void DecodeDXT3To1555(void* target, int32_t targetPitch, const void* blocks, uint32_t blockPitch, uint32_t blocksWide,
                      uint32_t blocksHigh)
{
    DecodeDXT3To16(target, targetPitch, blocks, blockPitch, blocksWide, blocksHigh, &To555Of, kThirds555, 0x3DEF,
                   [](uint32_t a) { return (a & 8) << 12; });
}

void DecodeDXT3To4444(void* target, int32_t targetPitch, const void* blocks, uint32_t blockPitch, uint32_t blocksWide,
                      uint32_t blocksHigh)
{
    DecodeDXT3To16(target, targetPitch, blocks, blockPitch, blocksWide, blocksHigh, &To444Of, kThirds444, 0x7777,
                   [](uint32_t a) { return a << 12; });
}

// The game's 0x008D1A30: DXT3 blocks to 32-bit pixels, each 4-bit alpha
// times 0x11 added to the top byte of its palette entry.
void DecodeDXT3To8888(void* target, int32_t targetPitch, const void* blocks, uint32_t blockPitch, uint32_t blocksWide,
                      uint32_t blocksHigh)
{
    if (blocksWide == 0 || blocksHigh == 0)
        return;
    const uint32_t pitch = static_cast<uint32_t>(targetPitch);
    const uint32_t targetSkip = (pitch - blocksWide * 4) << 2;
    const uint32_t sourceSkip = (blockPitch - blocksWide) << 4;
    Address d = At(target), s = At(blocks);
    do {
        uint32_t n = blocksWide;
        do {
            const uint16_t c0 = Load<uint16_t>(s + 8), c1 = Load<uint16_t>(s + 0xA);
            const uint32_t indices = Load<uint32_t>(s + 0xC);
            uint32_t palette[4];
            palette[0] = Widen565Masked(c0);
            palette[1] = Widen565Masked(c1);
            if (c0 > c1) {
                Thirds8888(palette[0], palette[1], palette[2], palette[3]);
            } else {
                palette[2] = Average8888(palette[0], palette[1]);
                palette[3] = 0;
            }
            const uint64_t alphas = Load<uint64_t>(s);
            for (int i = 0; i < 16; ++i) {
                const uint32_t a = static_cast<uint32_t>(alphas >> (4 * i)) & 0xF;
                Store(d + (i >> 2) * pitch + (i & 3) * 4, (a * 0x11 << 24) + palette[(indices >> (2 * i)) & 3]);
            }
            d += 0x10;
            s += 16;
        } while (--n != 0);
        d += targetSkip;
        s += sourceSkip;
    } while (--blocksHigh != 0);
}

// The game's 0x008D1F80 and 0x008D2140: DXT1 blocks to 16-bit pixels, the
// first two colours opaque (`opaque` added), the fourth of a three-colour
// block 0.
void DecodeDXT1To16(void* target, int32_t targetPitch, const void* blocks, uint32_t blockPitch, uint32_t blocksWide,
                    uint32_t blocksHigh, uint32_t (*to16)(uint32_t), uint32_t opaque, const Thirds16& thirds,
                    uint32_t averageMask)
{
    if (blocksWide == 0 || blocksHigh == 0)
        return;
    const uint32_t targetSkip = (static_cast<uint32_t>(targetPitch) - blocksWide * 2) << 2;
    const uint32_t sourceSkip = (blockPitch - blocksWide) << 3;
    Address d = At(target), s = At(blocks);
    do {
        uint32_t n = blocksWide;
        do {
            const uint32_t c0 = Load<uint16_t>(s), c1 = Load<uint16_t>(s + 2);
            uint16_t palette[4];
            Palette16(c0, c1, to16(c0) + opaque, to16(c1) + opaque, thirds, averageMask, palette);
            WriteBlock16(reinterpret_cast<void*>(static_cast<uintptr_t>(d)), Load<uint32_t>(s + 4), palette,
                         targetPitch);
            d += 8;
            s += 8;
        } while (--n != 0);
        s += sourceSkip;
        d += targetSkip;
    } while (--blocksHigh != 0);
}

void DecodeDXT1To1555(void* target, int32_t targetPitch, const void* blocks, uint32_t blockPitch, uint32_t blocksWide,
                      uint32_t blocksHigh)
{
    DecodeDXT1To16(target, targetPitch, blocks, blockPitch, blocksWide, blocksHigh, &To555Of, 0x8000, kThirds555,
                   0x3DEF);
}

void DecodeDXT1To4444(void* target, int32_t targetPitch, const void* blocks, uint32_t blockPitch, uint32_t blocksWide,
                      uint32_t blocksHigh)
{
    DecodeDXT1To16(target, targetPitch, blocks, blockPitch, blocksWide, blocksHigh, &To444Of, 0xFFFFF000, kThirds444,
                   0x7777);
}

// ====================================================================================
// Format bits

// The game's 0x008D2790: the bits per channel of a texture source (pixel
// format and data type, src/core/gd/gd_types.h). Written in the game's
// order (red, green, blue, alpha); nothing written and false for a pair it
// does not know.
bool FormatBits(uint32_t pixelFormat, uint32_t dataType, uint32_t* red, uint32_t* blue, uint32_t* green,
                uint32_t* alpha)
{
    auto set = [&](uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
        *red = r;
        *green = g;
        *blue = b;
        *alpha = a;
        return true;
    };
    switch (dataType) {
    case 0:
    case 1:
        switch (pixelFormat) {
        case 0:
        case 2: return set(8, 8, 8, 0);
        case 1:
        case 3: return set(8, 8, 8, 8);
        case 7: return set(5, 6, 5, 0);
        case 8: return set(5, 5, 5, 1);
        case 9:
        case 10: return set(5, 6, 5, 4);
        default: return false;
        }
    case 10:
        if (pixelFormat == 1 || pixelFormat == 3)
            return set(5, 5, 5, 1);
        return false;
    case 13:
        if (pixelFormat == 1 || pixelFormat == 3)
            return set(4, 4, 4, 4);
        return false;
    case 14:
        if (pixelFormat == 0 || pixelFormat == 2)
            return set(5, 6, 5, 0);
        return false;
    default: return false;
    }
}

// ====================================================================================
// Detours

// Whether a group runs in compare mode.
enum Group { kVertex, kKey, kModulate, kDxt, kFormat, kGroups };
bool g_compare[kGroups] = {};

// What a function wrote: (start, bytes).
struct Region {
    Address at = 0;
    uint32_t size = 0;
};

// `rows` rows of `rowBytes`, the first at `base`, `stride` bytes apart.
Region Rows(const void* base, uint32_t rows, uint32_t stride, uint32_t rowBytes)
{
    if (rows == 0 || rowBytes == 0)
        return {};
    const Address first = At(base);
    const Address last = first + (rows - 1) * stride;
    const Address low = std::min(first, last), high = std::max(first, last);
    return {low, high - low + rowBytes};
}

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
// game's; the results and the regions compared.
template <typename R, typename Original, typename Ours>
R Compared(const char* id, std::initializer_list<Region> regions, Original&& original, Ours&& ours)
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
    const R result = original();
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

// Vertex formats

using ExpandFn = uint32_t(__cdecl*)(uint32_t);
using MakeFn = uint32_t(__cdecl*)(uint32_t, const int32_t*);
using OffsetFn = uint32_t(__cdecl*)(uint32_t, int32_t, uint32_t);
using CountFn = uint32_t(__cdecl*)(uint32_t, int32_t);
using GatherFn = void(__cdecl*)(uint32_t, const void*, void*, uint32_t, int32_t, uint32_t);
ExpandFn g_expand = nullptr;
MakeFn g_make = nullptr;
ExpandFn g_stride = nullptr;
OffsetFn g_offset = nullptr;
CountFn g_count = nullptr;
GatherFn g_gather = nullptr;

uint32_t __cdecl DExpand(uint32_t format)
{
    OPENSC4_FIRST("Expand 0x008D01F0");
    if (!g_compare[kVertex])
        return Expand(format);
    return compare::Call("pixels.expand", [&] { return g_expand(format); }, [&] { return Expand(format); });
}

uint32_t __cdecl DMake(uint32_t count, const int32_t* types)
{
    OPENSC4_FIRST("Make 0x008D02C0");
    if (!g_compare[kVertex])
        return Make(count, types);
    return compare::Call("pixels.make", [&] { return g_make(count, types); }, [&] { return Make(count, types); });
}

uint32_t __cdecl DStride(uint32_t format)
{
    OPENSC4_FIRST("Stride 0x008D0370");
    if (!g_compare[kVertex])
        return Stride(format);
    return compare::Call("pixels.stride", [&] { return g_stride(format); }, [&] { return Stride(format); });
}

uint32_t __cdecl DElementOffset(uint32_t format, int32_t type, uint32_t index)
{
    OPENSC4_FIRST("ElementOffset 0x008D04A0");
    if (!g_compare[kVertex])
        return ElementOffset(format, type, index);
    return compare::Call("pixels.elementoffset", [&] { return g_offset(format, type, index); },
                         [&] { return ElementOffset(format, type, index); });
}

uint32_t __cdecl DElementCount(uint32_t format, int32_t type)
{
    OPENSC4_FIRST("ElementCount 0x008D04F0");
    if (!g_compare[kVertex])
        return ElementCount(format, type);
    return compare::Call("pixels.elementcount", [&] { return g_count(format, type); },
                         [&] { return ElementCount(format, type); });
}

void __cdecl DGather(uint32_t count, const void* vertices, void* out, uint32_t format, int32_t type, uint32_t index)
{
    OPENSC4_FIRST("Gather 0x008D0520");
    if (!g_compare[kVertex]) {
        Gather(count, vertices, out, format, type, index);
        return;
    }
    Compared<Nothing>(
        "pixels.gather", {Rows(out, 1, 0, Entry(kSize, type) * count)},
        [&] {
            g_gather(count, vertices, out, format, type, index);
            return Nothing{};
        },
        [&] {
            Gather(count, vertices, out, format, type, index);
            return Nothing{};
        });
}

OPENSC4_CDECL_THUNK(TExpand, DExpand, 4)
OPENSC4_CDECL_THUNK(TMake, DMake, 8)
OPENSC4_CDECL_THUNK(TStride, DStride, 4)
OPENSC4_CDECL_THUNK(TElementOffset, DElementOffset, 12)
OPENSC4_CDECL_THUNK(TElementCount, DElementCount, 8)
OPENSC4_CDECL_THUNK(TGather, DGather, 24)

// Colour keys, modulation and the DXT decoders share signatures.

using KeyFn = void(__cdecl*)(const void*, int32_t, void*, int32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using PixelsFn = void(__cdecl*)(void*, int32_t, const void*, int32_t, uint32_t, uint32_t, uint32_t);
using DecodeFn = void(__cdecl*)(void*, int32_t, const void*, uint32_t, uint32_t, uint32_t);
KeyFn g_key16 = nullptr;
KeyFn g_key32 = nullptr;

// Runs a pixel function: ours, or compared on the rows it writes. Counts of
// 0 (which the game's loops take as 2^32) go to the game's alone.
template <typename Call, typename Ours>
void Pixels(Group group, const char* id, bool degenerate, Region written, Call&& game, Ours&& ours)
{
    if (!g_compare[group]) {
        ours();
        return;
    }
    if (degenerate) {
        game();
        return;
    }
    Compared<Nothing>(
        id, {written},
        [&] {
            game();
            return Nothing{};
        },
        [&] {
            ours();
            return Nothing{};
        });
}

void __cdecl DKeyCopy16(const void* source, int32_t sourcePitch, void* target, int32_t targetPitch, uint32_t width,
                        uint32_t height, uint32_t key, uint32_t mask)
{
    OPENSC4_FIRST("KeyCopy16 0x008D0660");
    Pixels(
        kKey, "pixels.keycopy16", width == 0 || height == 0, Rows(target, height, targetPitch, width * 2),
        [&] { g_key16(source, sourcePitch, target, targetPitch, width, height, key, mask); },
        [&] { KeyCopy16(source, sourcePitch, target, targetPitch, width, height, key, mask); });
}

void __cdecl DKeyCopy32(const void* source, int32_t sourcePitch, void* target, int32_t targetPitch, uint32_t width,
                        uint32_t height, uint32_t key, uint32_t mask)
{
    OPENSC4_FIRST("KeyCopy32 0x008D0750");
    Pixels(
        kKey, "pixels.keycopy32", width == 0 || height == 0, Rows(target, height, targetPitch, width * 4),
        [&] { g_key32(source, sourcePitch, target, targetPitch, width, height, key, mask); },
        [&] { KeyCopy32(source, sourcePitch, target, targetPitch, width, height, key, mask); });
}

#define OPENSC4_PIXELS_DETOUR(Name, Group, Id, First, Degenerate, Written)                                     \
    PixelsFn g_##Name = nullptr;                                                                               \
    void __cdecl D##Name(void* target, int32_t targetPitch, const void* source, int32_t sourcePitch,           \
                         uint32_t width, uint32_t height, uint32_t colour)                                     \
    {                                                                                                          \
        OPENSC4_FIRST(First);                                                                                  \
        Pixels(                                                                                                \
            Group, Id, Degenerate, Written,                                                                    \
            [&] { g_##Name(target, targetPitch, source, sourcePitch, width, height, colour); },                \
            [&] { Name(target, targetPitch, source, sourcePitch, width, height, colour); });                   \
    }                                                                                                          \
    OPENSC4_CDECL_THUNK(T##Name, D##Name, 28)

// The wrappers write what the copy they call writes.
OPENSC4_PIXELS_DETOUR(KeyCopy1555, kKey, "pixels.keycopy1555", "KeyCopy1555 0x008D0820", width == 0 || height == 0,
                      Rows(target, height, targetPitch, width * 2))
OPENSC4_PIXELS_DETOUR(KeyCopy4444, kKey, "pixels.keycopy4444", "KeyCopy4444 0x008D0860", width == 0 || height == 0,
                      Rows(target, height, targetPitch, width * 2))
OPENSC4_PIXELS_DETOUR(KeyCopy8888, kKey, "pixels.keycopy8888", "KeyCopy8888 0x008D08A0", width == 0 || height == 0,
                      Rows(target, height, targetPitch, width * 4))
OPENSC4_PIXELS_DETOUR(Modulate8888, kModulate, "pixels.modulate8888", "Modulate8888 0x008D08E0", width == 0 || height == 0,
                      Rows(target, height, targetPitch, width * 4))
OPENSC4_PIXELS_DETOUR(Modulate888, kModulate, "pixels.modulate888", "Modulate888 0x008D09C0", width == 0 || height == 0,
                      Rows(target, height, targetPitch, width * 3))
OPENSC4_PIXELS_DETOUR(Modulate4444, kModulate, "pixels.modulate4444", "Modulate4444 0x008D0A60", width == 0 || height == 0,
                      Rows(target, height, targetPitch - 2 * width, width * 2))
OPENSC4_PIXELS_DETOUR(Modulate1555, kModulate, "pixels.modulate1555", "Modulate1555 0x008D0B60", width == 0 || height == 0,
                      Rows(target, height, targetPitch - 2 * width, width * 2))
OPENSC4_PIXELS_DETOUR(ModulateDXT1, kModulate, "pixels.modulatedxt1", "ModulateDXT1 0x008D0C60",
                      (width >> 2) == 0 || (height >> 2) == 0, Rows(target, height >> 2, targetPitch, (width >> 2) * 8))
OPENSC4_PIXELS_DETOUR(ModulateDXT3, kModulate, "pixels.modulatedxt3", "ModulateDXT3 0x008D0E10",
                      (width >> 2) == 0 || (height >> 2) == 0, Rows(target, height >> 2, targetPitch, (width >> 2) * 16))

OPENSC4_CDECL_THUNK(TKeyCopy16, DKeyCopy16, 32)
OPENSC4_CDECL_THUNK(TKeyCopy32, DKeyCopy32, 32)

// The decoders: `bytes` per pixel, four pixel rows per block row.
#define OPENSC4_DECODE_DETOUR(Name, Id, First, Bytes)                                                          \
    DecodeFn g_##Name = nullptr;                                                                               \
    void __cdecl D##Name(void* target, int32_t targetPitch, const void* blocks, uint32_t blockPitch,           \
                         uint32_t blocksWide, uint32_t blocksHigh)                                             \
    {                                                                                                          \
        OPENSC4_FIRST(First);                                                                                  \
        Pixels(                                                                                                \
            kDxt, Id, false, Rows(target, blocksHigh * 4, targetPitch, blocksWide * 4 * (Bytes)),              \
            [&] { g_##Name(target, targetPitch, blocks, blockPitch, blocksWide, blocksHigh); },                \
            [&] { Name(target, targetPitch, blocks, blockPitch, blocksWide, blocksHigh); });                   \
    }                                                                                                          \
    OPENSC4_CDECL_THUNK(T##Name, D##Name, 24)

OPENSC4_DECODE_DETOUR(DecodeDXT1To8888, "pixels.dxt1to8888", "DecodeDXT1To8888 0x008D1030", 4)
OPENSC4_DECODE_DETOUR(DecodeDXT3To1555, "pixels.dxt3to1555", "DecodeDXT3To1555 0x008D1240", 2)
OPENSC4_DECODE_DETOUR(DecodeDXT3To4444, "pixels.dxt3to4444", "DecodeDXT3To4444 0x008D1630", 2)
OPENSC4_DECODE_DETOUR(DecodeDXT3To8888, "pixels.dxt3to8888", "DecodeDXT3To8888 0x008D1A30", 4)
OPENSC4_DECODE_DETOUR(DecodeDXT1To1555, "pixels.dxt1to1555", "DecodeDXT1To1555 0x008D1F80", 2)
OPENSC4_DECODE_DETOUR(DecodeDXT1To4444, "pixels.dxt1to4444", "DecodeDXT1To4444 0x008D2140", 2)

// The block writers.

using Block8888Fn = void(__stdcall*)(void*, int32_t, uint32_t, const uint32_t*);
Block8888Fn g_block8888 = nullptr;
void* g_block16 = nullptr;

void __stdcall DWriteBlock8888(void* target, int32_t pitch, uint32_t indices, const uint32_t* palette)
{
    OPENSC4_FIRST("WriteBlock8888 0x008D0FD0");
    Pixels(
        kDxt, "pixels.block8888", false, Rows(target, 4, pitch, 16), [&] { g_block8888(target, pitch, indices, palette); },
        [&] { WriteBlock8888(target, pitch, indices, palette); });
}

// The game's 0x008D1E70 called with its registers.
__declspec(naked) void __cdecl CallGameBlock16(void*, void*, uint32_t, const void*, int32_t)
{
    __asm push esi
    __asm mov esi, dword ptr [esp + 8]
    __asm mov eax, dword ptr [esp + 12]
    __asm mov ecx, dword ptr [esp + 16]
    __asm mov edx, dword ptr [esp + 20]
    __asm push dword ptr [esp + 24]
    __asm call esi
    __asm pop esi
    __asm ret
}

void __cdecl DWriteBlock16(void* target, uint32_t indices, const uint16_t* palette, int32_t pitch)
{
    OPENSC4_FIRST("WriteBlock16 0x008D1E70");
    Pixels(
        kDxt, "pixels.block16", false, Rows(target, 4, pitch, 8),
        [&] { CallGameBlock16(g_block16, target, indices, palette, pitch); },
        [&] { WriteBlock16(target, indices, palette, pitch); });
}

OPENSC4_THUNK(TWriteBlock8888, DWriteBlock8888, 16)

// 0x008D1E70's registers: the target in EAX, the indices in ECX, the
// palette in EDX, the pitch on the stack (ret 4). It keeps EDX and leaves
// EAX three rows on and ECX the last pixel's entry, zero-extended.
__declspec(naked) void TWriteBlock16()
{
    __asm push edx
    __asm push ecx
    __asm push eax
    __asm push dword ptr [esp + 16]
    __asm push edx
    __asm push ecx
    __asm push eax
    __asm call DWriteBlock16
    __asm add esp, 16
    __asm pop eax
    __asm pop ecx
    __asm pop edx
    __asm shr ecx, 30
    __asm movzx ecx, word ptr [edx + ecx * 2]
    __asm push ecx
    __asm mov ecx, dword ptr [esp + 8]
    __asm lea ecx, [ecx + ecx * 2]
    __asm add eax, ecx
    __asm pop ecx
    __asm ret 4
}

// Format bits

using FormatBitsFn = bool(__cdecl*)(uint32_t, uint32_t, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
FormatBitsFn g_formatBits = nullptr;

bool __cdecl DFormatBits(uint32_t pixelFormat, uint32_t dataType, uint32_t* red, uint32_t* blue, uint32_t* green,
                         uint32_t* alpha)
{
    OPENSC4_FIRST("FormatBits 0x008D2790");
    if (!g_compare[kFormat])
        return FormatBits(pixelFormat, dataType, red, blue, green, alpha);
    return Compared<bool>(
        "pixels.formatbits", {Rows(red, 1, 0, 4), Rows(blue, 1, 0, 4), Rows(green, 1, 0, 4), Rows(alpha, 1, 0, 4)},
        [&] { return g_formatBits(pixelFormat, dataType, red, blue, green, alpha); },
        [&] { return FormatBits(pixelFormat, dataType, red, blue, green, alpha); });
}

OPENSC4_CDECL_THUNK(TFormatBits, DFormatBits, 24)

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
    log::Write("pixels: %s: %zu functions %s", name, N,
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

template <typename Fn>
void** Slot(Fn* fn)
{
    return reinterpret_cast<void**>(fn);
}

bool InstallVertex(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GDVertexFormat_Expand, &TExpand, Slot(&g_expand)},
        {addr::GDVertexFormat_Make, &TMake, Slot(&g_make)},
        {addr::GDVertexFormat_Stride, &TStride, Slot(&g_stride)},
        {addr::GDVertexFormat_ElementOffset, &TElementOffset, Slot(&g_offset)},
        {addr::GDVertexFormat_ElementCount, &TElementCount, Slot(&g_count)},
        {addr::GDVertexFormat_Gather, &TGather, Slot(&g_gather)},
    };
    return InstallAll(mode, kVertex, "vertex", detours);
}

bool InstallKey(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZPixels_KeyCopy16, &TKeyCopy16, Slot(&g_key16)},
        {addr::GZPixels_KeyCopy32, &TKeyCopy32, Slot(&g_key32)},
        {addr::GZPixels_KeyCopy1555, &TKeyCopy1555, Slot(&g_KeyCopy1555)},
        {addr::GZPixels_KeyCopy4444, &TKeyCopy4444, Slot(&g_KeyCopy4444)},
        {addr::GZPixels_KeyCopy8888, &TKeyCopy8888, Slot(&g_KeyCopy8888)},
    };
    return InstallAll(mode, kKey, "key", detours);
}

bool InstallModulate(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZPixels_Modulate8888, &TModulate8888, Slot(&g_Modulate8888)},
        {addr::GZPixels_Modulate888, &TModulate888, Slot(&g_Modulate888)},
        {addr::GZPixels_Modulate4444, &TModulate4444, Slot(&g_Modulate4444)},
        {addr::GZPixels_Modulate1555, &TModulate1555, Slot(&g_Modulate1555)},
        {addr::GZPixels_ModulateDXT1, &TModulateDXT1, Slot(&g_ModulateDXT1)},
        {addr::GZPixels_ModulateDXT3, &TModulateDXT3, Slot(&g_ModulateDXT3)},
    };
    return InstallAll(mode, kModulate, "modulate", detours);
}

bool InstallDxt(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZPixels_WriteBlock8888, &TWriteBlock8888, Slot(&g_block8888)},
        {addr::GZPixels_DecodeDXT1To8888, &TDecodeDXT1To8888, Slot(&g_DecodeDXT1To8888)},
        {addr::GZPixels_DecodeDXT3To1555, &TDecodeDXT3To1555, Slot(&g_DecodeDXT3To1555)},
        {addr::GZPixels_DecodeDXT3To4444, &TDecodeDXT3To4444, Slot(&g_DecodeDXT3To4444)},
        {addr::GZPixels_DecodeDXT3To8888, &TDecodeDXT3To8888, Slot(&g_DecodeDXT3To8888)},
        {addr::GZPixels_WriteBlock16, &TWriteBlock16, &g_block16},
        {addr::GZPixels_DecodeDXT1To1555, &TDecodeDXT1To1555, Slot(&g_DecodeDXT1To1555)},
        {addr::GZPixels_DecodeDXT1To4444, &TDecodeDXT1To4444, Slot(&g_DecodeDXT1To4444)},
    };
    return InstallAll(mode, kDxt, "dxt", detours);
}

bool InstallFormat(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZPixels_FormatBits, &TFormatBits, Slot(&g_formatBits)},
    };
    return InstallAll(mode, kFormat, "format", detours);
}

const bool g_registered[] = {
    registry::Register({"renderer.pixels.vertex", "renderer/pixels", registry::Kind::Detour, registry::Phase::Attach,
                        addr::GDVertexFormat_Stride,
                        "Our vertex format helpers: the legacy IDs expanded, a format made from a list, its stride, "
                        "an element's offset and count, an element gathered (6 detours; docs/pixels.md)",
                        true, InstallVertex}),
    registry::Register({"renderer.pixels.key", "renderer/pixels", registry::Kind::Detour, registry::Phase::Attach,
                        addr::GZPixels_KeyCopy16,
                        "Our colour key copies: 16 and 32-bit with a mask and the 1555, 4444 and 8888 wrappers "
                        "(5 detours; docs/pixels.md)",
                        true, InstallKey}),
    registry::Register({"renderer.pixels.modulate", "renderer/pixels", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZPixels_Modulate8888,
                        "Our modulates of pixels by a colour: 8888, 888, 4444, 1555 and DXT1 and DXT3 blocks "
                        "(6 detours; docs/pixels.md)",
                        true, InstallModulate}),
    registry::Register({"renderer.pixels.dxt", "renderer/pixels", registry::Kind::Detour, registry::Phase::Attach,
                        addr::GZPixels_DecodeDXT1To8888,
                        "Our DXT decoders: DXT1 to 8888, 1555 and 4444, DXT3 to 8888, 1555 and 4444, and the two "
                        "block writers (8 detours; docs/pixels.md)",
                        true, InstallDxt}),
    registry::Register({"renderer.pixels.format", "renderer/pixels", registry::Kind::Detour, registry::Phase::Attach,
                        addr::GZPixels_FormatBits,
                        "Our bits per channel of a texture source (1 detour; docs/pixels.md)", true, InstallFormat}),
};

} // namespace
} // namespace opensc4::pixels
