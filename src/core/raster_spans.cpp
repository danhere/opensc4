// Our spans of the GZ framework's software rasterizer, 0x0082B780 to
// 0x00864B5F (docs/renderer.md, "The software rasterizer"). A drawing context
// (vtable 0x00AC16C8) hands its draws to a rasterizer (0x00AC17D0), which
// owns one span renderer per pixel format and about 770 span routines. Only
// the 32-bit renderer (vtable 0x00AC1874) ever ran, and of its routines only
// four, all detours here, off by default:
//
//   renderer.raster.spans  FillSpans 0x00854E10 (slot 1), CopySpans
//                          0x008551D0 (slot 5, picks a copy from a table),
//                          and the two copies it was seen to pick: plain
//                          0x0085B030 and tiled 0x0085B550
//
// Spans are pairs of words, (y << 16 | x) for the first pixel and for the
// pixel past the last; the length is their difference, as the game takes it.
// A target or source is passed as words: its bits, its stride, then (the
// source) its width and height. The game computes addresses in 32 bits
// (wrapping) and copies a pixel at a time, forward; ours does the same, so
// a source overlapping its target comes out the same.
//
// None of the four keeps ECX or EDX (the fill calls 0x00910003, the others
// use both), so no caller can rely on them and the detours need no thunks.
//
// Compare mode: the three that write pixels run ours first, what it wrote is
// kept aside and the memory put back, then the game's runs, and the target's
// bytes are compared. CopySpans only forwards to the table; it runs ours.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace opensc4::raster {
namespace {

using Address = uint32_t;

template <typename T>
inline T* Ptr(Address at)
{
    return reinterpret_cast<T*>(static_cast<uintptr_t>(at));
}

// A target or source as the copies get it, by value: bits, stride, width,
// height and two words not read by the copies that ran.
struct Surface {
    Address bits;
    uint32_t stride;
    uint32_t width;
    uint32_t height;
    uint32_t unread[2];
};

// The copies' last argument, by value: a shift for the tiled copy, and the
// table's index (word 4 times two plus the byte at 0x1C).
struct Pattern {
    uint32_t shiftX;
    uint32_t shiftY;
    uint32_t words[5];
    uint32_t last; // low byte: part of the table index
};
static_assert(sizeof(Surface) == 24 && sizeof(Pattern) == 32, "the game passes 6 and 8 words");

using CopyFn = void(__cdecl*)(const uint32_t* spans, int32_t count, Surface target, Surface source,
                              Pattern pattern);
using FillFn = void(__fastcall*)(void* self, void*, const uint32_t* spans, int32_t count, const uint32_t* target,
                                 uint32_t colour);
using SpansFn = void(__fastcall*)(void* self, void*, const uint32_t* spans, int32_t count, const uint32_t* target,
                                  const uint32_t* source, const uint32_t* pattern, uint32_t op);

FillFn g_fill = nullptr;
SpansFn g_spans = nullptr;
CopyFn g_plain = nullptr;
CopyFn g_tiled = nullptr;
bool g_compare = false;

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("raster: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

inline Address PixelAt(Address bits, uint32_t stride, uint32_t span)
{
    return (span >> 16) * stride + (span & 0xFFFF) * 4 + bits;
}

// ====================================================================================
// The spans

void FillSpans(const uint32_t* spans, int32_t count, const uint32_t* target, uint32_t colour)
{
    do {
        const uint32_t start = spans[0], end = spans[1];
        spans += 2;
        uint32_t* to = Ptr<uint32_t>(PixelAt(target[0], target[1], start));
        for (uint32_t n = end - start; n != 0; --n)
            *to++ = colour;
    } while (--count != 0);
}

void CopyPlain(const uint32_t* spans, int32_t count, const Surface& target, const Surface& source)
{
    do {
        const uint32_t start = spans[0], end = spans[1];
        spans += 2;
        uint32_t* to = Ptr<uint32_t>(PixelAt(target.bits, target.stride, start));
        const uint32_t* from = Ptr<const uint32_t>(PixelAt(source.bits, source.stride, start));
        for (uint32_t n = end - start; n != 0; --n)
            *to++ = *from++;
    } while (--count != 0);
}

void CopyTiled(const uint32_t* spans, int32_t count, const Surface& target, const Surface& source,
               const Pattern& pattern)
{
    do {
        const uint32_t start = spans[0], end = spans[1];
        spans += 2;
        const uint32_t y = start >> 16, x = start & 0xFFFF;
        uint32_t* to = Ptr<uint32_t>(PixelAt(target.bits, target.stride, start));
        uint32_t column = (x + pattern.shiftX) % source.width;
        const Address row = ((pattern.shiftY + y) % source.height) * source.stride + source.bits;
        const uint32_t* from = Ptr<const uint32_t>(row + column * 4);
        for (uint32_t n = end - start; n != 0; --n) {
            *to++ = *from++;
            if (++column >= source.width) {
                column = 0;
                from = Ptr<const uint32_t>(row);
            }
        }
    } while (--count != 0);
}

CopyFn TableEntry(const Pattern& pattern, uint32_t op)
{
    const uint32_t index = (pattern.last & 0xFF) + pattern.words[2] * 2;
    const Address table = Ptr<const Address>(addr::GZRaster_CopyTables)[index];
    return Ptr<const CopyFn>(table)[op];
}

// ====================================================================================
// Compare mode

// The bytes from the first span's first pixel to the last pixel any span
// writes (the game's spans are in row order, but nothing relies on it).
struct Region {
    Address at = 0;
    uint32_t size = 0;
};

Region Covered(const uint32_t* spans, int32_t count, Address bits, uint32_t stride)
{
    Address low = 0xFFFFFFFFu, high = 0;
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t start = spans[2 * i], end = spans[2 * i + 1];
        if (end == start)
            continue;
        const Address first = PixelAt(bits, stride, start);
        const Address past = first + (end - start) * 4;
        low = std::min(low, first);
        high = std::max(high, past);
    }
    return low < high ? Region{low, high - low} : Region{};
}

// Ours, then the memory put back and the game's; the target's bytes compared.
template <typename Original, typename Ours>
void Compared(const char* id, Region region, Original&& original, Ours&& ours)
{
    uint8_t* const p = Ptr<uint8_t>(region.at);
    const std::vector<uint8_t> before(p, p + region.size);
    ours();
    const std::vector<uint8_t> mine(p, p + region.size);
    if (region.size != 0)
        std::memcpy(p, before.data(), region.size);
    original();
    uint32_t at = 0;
    while (at < region.size && p[at] == mine[at])
        ++at;
    const bool same = at == region.size;
    char game[48] = {}, text[48] = {};
    if (!same) {
        std::snprintf(game, sizeof game, "+%u of %u: %02X", at, region.size, p[at]);
        std::snprintf(text, sizeof text, "+%u of %u: %02X", at, region.size, mine[at]);
    }
    compare::Record(id, same, game, text);
}

// ====================================================================================
// Detours

void __fastcall DFillSpans(void* self, void*, const uint32_t* spans, int32_t count, const uint32_t* target,
                           uint32_t colour)
{
    OPENSC4_FIRST("FillSpans 0x00854E10");
    // A count of 0 runs the do-while 2^32 times, in the game's as in ours.
    if (!g_compare || count <= 0) {
        if (g_compare)
            g_fill(self, nullptr, spans, count, target, colour);
        else
            FillSpans(spans, count, target, colour);
        return;
    }
    Compared(
        "raster.fill", Covered(spans, count, target[0], target[1]),
        [&] { g_fill(self, nullptr, spans, count, target, colour); },
        [&] { FillSpans(spans, count, target, colour); });
}

void __cdecl DCopyPlain(const uint32_t* spans, int32_t count, Surface target, Surface source, Pattern pattern)
{
    OPENSC4_FIRST("CopyPlain 0x0085B030");
    if (!g_compare || count <= 0) {
        if (g_compare)
            g_plain(spans, count, target, source, pattern);
        else
            CopyPlain(spans, count, target, source);
        return;
    }
    Compared(
        "raster.copy", Covered(spans, count, target.bits, target.stride),
        [&] { g_plain(spans, count, target, source, pattern); }, [&] { CopyPlain(spans, count, target, source); });
}

void __cdecl DCopyTiled(const uint32_t* spans, int32_t count, Surface target, Surface source, Pattern pattern)
{
    OPENSC4_FIRST("CopyTiled 0x0085B550");
    if (!g_compare || count <= 0) {
        if (g_compare)
            g_tiled(spans, count, target, source, pattern);
        else
            CopyTiled(spans, count, target, source, pattern);
        return;
    }
    Compared(
        "raster.tiled", Covered(spans, count, target.bits, target.stride),
        [&] { g_tiled(spans, count, target, source, pattern); },
        [&] { CopyTiled(spans, count, target, source, pattern); });
}

void __fastcall DCopySpans(void*, void*, const uint32_t* spans, int32_t count, const uint32_t* target,
                           const uint32_t* source, const uint32_t* pattern, uint32_t op)
{
    OPENSC4_FIRST("CopySpans 0x008551D0");
    Surface to, from;
    Pattern with;
    std::memcpy(&to, target, sizeof to);
    std::memcpy(&from, source, sizeof from);
    std::memcpy(&with, pattern, sizeof with);
    TableEntry(with, op)(spans, count, to, from, with);
}

// ====================================================================================
// Installation

bool Install(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compare = mode == registry::Mode::Compare;
    struct Detour {
        std::uintptr_t address;
        void* detour;
        void** original;
    };
    const Detour detours[] = {
        {addr::GZRaster32_FillSpans, reinterpret_cast<void*>(&DFillSpans), reinterpret_cast<void**>(&g_fill)},
        {addr::GZRaster32_CopySpans, reinterpret_cast<void*>(&DCopySpans), reinterpret_cast<void**>(&g_spans)},
        {addr::GZRaster32_CopyPlain, reinterpret_cast<void*>(&DCopyPlain), reinterpret_cast<void**>(&g_plain)},
        {addr::GZRaster32_CopyTiled, reinterpret_cast<void*>(&DCopyTiled), reinterpret_cast<void**>(&g_tiled)},
    };
    for (const Detour& d : detours) {
        const addr::Entry* entry = addr_check::Find(d.address);
        if (!entry || !hook::Install(*entry, d.detour, d.original))
            return false;
    }
    log::Write("raster: spans: 4 functions %s", g_compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registered = registry::Register(
    {"renderer.raster.spans", "renderer/raster", registry::Kind::Detour, registry::Phase::Attach,
     addr::GZRaster32_FillSpans,
     "Our spans of the software rasterizer's 32-bit renderer: the fill, the copy dispatch and the plain and tiled "
     "copies, the only four of its ~770 that run (4 detours; docs/renderer.md)",
     true, Install});

} // namespace
} // namespace opensc4::raster
