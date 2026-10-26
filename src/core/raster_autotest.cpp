// The software rasterizer's part of the autotest (docs/renderer.md, "The
// software rasterizer"): OPENSC4_TEST_RASTER=<day> calls the four spans of
// the 32-bit renderer that run (0x00854E10, 0x008551D0, 0x0085B030,
// 0x0085B550) by address, so a run with renderer.raster.spans off drives the
// game's and a run with it on drives ours. Inputs: spans of every length
// from 0 up, padded and zero strides, a copy onto itself shifted (the
// forward order shows), tiled sources of odd sizes with shifts near 2^32
// (the unsigned wrap), and CopySpans through the table to both copies.
// Targets are canvases with a margin of known bytes all round, hashed whole,
// so a stray write counts too. The hashes go into the fingerprint's `rs`
// part and are logged per stage as `autotest: raster: <stage> <hash>`;
// addresses never go in.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace opensc4::autotest {
namespace {

bool g_rsOn = false;
uint32_t g_rsHash = 2166136261u;

void MixByte(uint8_t value) { g_rsHash = (g_rsHash ^ value) * 16777619u; }

void MixBytes(const std::vector<uint8_t>& bytes)
{
    for (const uint8_t b : bytes)
        MixByte(b);
}

void RsStage(const char* name) { log::Write("autotest: raster: %s %08X", name, g_rsHash); }

struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state ^ (state >> 15);
    }
    uint32_t Below(uint32_t n) { return Next() % n; }
};

// A 32-bit picture with 64 bytes of 0xA5 before and after.
struct Canvas {
    std::vector<uint8_t> bytes;
    uint32_t stride;
    uint32_t width, height;
    Canvas(uint32_t w, uint32_t h, uint32_t pad)
        : bytes((w * 4 + pad) * h + 128, 0xA5), stride(w * 4 + pad), width(w), height(h)
    {
    }
    uint32_t Bits() { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bytes.data() + 64)); }
    void Fill(Random& random)
    {
        for (std::size_t i = 64; i + 64 < bytes.size(); ++i)
            bytes[i] = static_cast<uint8_t>(random.Next() >> 8);
    }
};

// Spans inside a `width` by `height` area: rows in order, lengths 0 to the
// width (all of them for the first rows).
std::vector<uint32_t> Spans(Random& random, uint32_t width, uint32_t height)
{
    std::vector<uint32_t> spans;
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t length = y <= width ? y : random.Below(width + 1);
        const uint32_t x = random.Below(width - length + 1);
        spans.push_back(y << 16 | x);
        spans.push_back(y << 16 | (x + length));
    }
    return spans;
}

struct Pattern {
    uint32_t words[8];
};

using FillFn = void(__fastcall*)(void*, void*, const uint32_t*, int32_t, const uint32_t*, uint32_t);
using SpansFn = void(__fastcall*)(void*, void*, const uint32_t*, int32_t, const uint32_t*, const uint32_t*,
                                  const uint32_t*, uint32_t);
using CopyFn = void(__cdecl*)(const uint32_t*, int32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, Pattern);

template <typename Fn>
Fn Game(std::uintptr_t address)
{
    return reinterpret_cast<Fn>(address);
}

// The copies' arguments: the target's six words, the source's, a pattern
// whose table index (word 4 times two plus the byte at 0x1C) is 8, the
// table of the copies that ran.
void CallCopy(std::uintptr_t address, const std::vector<uint32_t>& spans, Canvas& target, uint32_t targetBits,
              Canvas& source, uint32_t sourceBits, uint32_t shiftX, uint32_t shiftY)
{
    const Pattern pattern{{shiftX, shiftY, 0, 0, 4, 0, 0, 0}};
    Game<CopyFn>(address)(spans.data(), static_cast<int32_t>(spans.size() / 2), targetBits, target.stride,
                          target.width, target.height, 0, 0, sourceBits, source.stride, source.width, source.height,
                          0, 0, pattern);
}

void CallCopySpans(const std::vector<uint32_t>& spans, Canvas& target, Canvas& source, uint32_t op, uint32_t shiftX,
                   uint32_t shiftY)
{
    const uint32_t to[6] = {target.Bits(), target.stride, target.width, target.height, 0, 0};
    const uint32_t from[6] = {source.Bits(), source.stride, source.width, source.height, 0, 0};
    const uint32_t pattern[8] = {shiftX, shiftY, 0, 0, 3, 0, 0, 2}; // 2 + 3 * 2 = 8
    Game<SpansFn>(addr::GZRaster32_CopySpans)(nullptr, nullptr, spans.data(), static_cast<int32_t>(spans.size() / 2),
                                              to, from, pattern, op);
}

void DriveFill()
{
    Random random{0x52415331u};
    for (const uint32_t pad : {0u, 12u}) {
        for (const uint32_t width : {1u, 7u, 33u}) {
            Canvas target(width, 40, pad);
            target.Fill(random);
            const std::vector<uint32_t> spans = Spans(random, width, 40);
            const uint32_t to[2] = {target.Bits(), target.stride};
            Game<FillFn>(addr::GZRaster32_FillSpans)(nullptr, nullptr, spans.data(),
                                                     static_cast<int32_t>(spans.size() / 2), to, random.Next());
            MixBytes(target.bytes);
        }
    }
    // A zero stride: every row on the first.
    Canvas target(16, 1, 0);
    const std::vector<uint32_t> spans = Spans(random, 16, 12);
    const uint32_t to[2] = {target.Bits(), 0};
    Game<FillFn>(addr::GZRaster32_FillSpans)(nullptr, nullptr, spans.data(), static_cast<int32_t>(spans.size() / 2),
                                             to, 0x12345678u);
    MixBytes(target.bytes);
    RsStage("fill");
}

void DrivePlain()
{
    Random random{0x52415332u};
    for (const uint32_t pad : {0u, 20u}) {
        Canvas target(29, 36, pad), source(29, 36, 8);
        target.Fill(random);
        source.Fill(random);
        const std::vector<uint32_t> spans = Spans(random, 29, 36);
        CallCopy(addr::GZRaster32_CopyPlain, spans, target, target.Bits(), source, source.Bits(), 0, 0);
        MixBytes(target.bytes);
    }
    // Onto itself: the source a pixel and a row past the target, then the
    // target past the source (where the forward order shows).
    for (const bool sourceFurther : {true, false}) {
        Canvas both(24, 30, 4);
        both.Fill(random);
        const std::vector<uint32_t> spans = Spans(random, 23, 28);
        const uint32_t near = both.Bits(), far = both.Bits() + 4 + both.stride;
        CallCopy(addr::GZRaster32_CopyPlain, spans, both, sourceFurther ? near : far, both,
                 sourceFurther ? far : near, 0, 0);
        MixBytes(both.bytes);
    }
    RsStage("plain");
}

void DriveTiled()
{
    Random random{0x52415333u};
    struct Case {
        uint32_t width, height, shiftX, shiftY;
    };
    const Case cases[] = {
        {1, 1, 0, 0}, {3, 5, 0, 0}, {7, 3, 2, 1}, {5, 7, 0xFFFFFFFFu, 0xFFFFFFFEu}, {13, 2, 0xFFFFFFF0u, 3},
        {40, 40, 17, 39},
    };
    for (const Case& c : cases) {
        Canvas target(37, 30, 8), source(c.width, c.height, 4);
        target.Fill(random);
        source.Fill(random);
        const std::vector<uint32_t> spans = Spans(random, 37, 30);
        CallCopy(addr::GZRaster32_CopyTiled, spans, target, target.Bits(), source, source.Bits(), c.shiftX,
                 c.shiftY);
        MixBytes(target.bytes);
    }
    RsStage("tiled");
}

void DriveDispatch()
{
    Random random{0x52415334u};
    for (const uint32_t op : {0u, 8u}) {
        Canvas target(21, 25, 0), source(9, 11, 12);
        target.Fill(random);
        source.Fill(random);
        const std::vector<uint32_t> spans = Spans(random, 21, 11);
        CallCopySpans(spans, target, source, op, 5, 0xFFFFFFFDu);
        MixBytes(target.bytes);
    }
    RsStage("dispatch");
}

// OPENSC4_TEST_RASTER=<day>: the four by address.
void DriveRasterDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_RASTER") || !game::IsBuild641())
        return;
    g_rsOn = true;
    DriveFill();
    DrivePlain();
    DriveTiled();
    DriveDispatch();
    log::Write("autotest: raster driven (hash %08X)", g_rsHash);
}

void RsFingerprint(cISC4City*, std::string& line)
{
    if (!g_rsOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | rs %08X", g_rsHash);
    line += text;
}

const FingerprintPart kRsFingerprint{"raster", 4, &RsFingerprint};
const DayHook kRsDrive{"raster", 4, &DriveRasterDay};

} // namespace
} // namespace opensc4::autotest
