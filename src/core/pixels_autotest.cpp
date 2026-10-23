// The vertex format and pixel helpers' part of the autotest (docs/pixels.md):
// OPENSC4_TEST_PIXELS=<day> calls the 26 helpers of 0x008D01F0 to 0x008D2993
// by address, so a run with renderer.pixels.* off drives the game's and a
// run with them on drives ours, on inputs chosen for their edge cases: odd
// widths (the key copies' 16, 8, 4 and 2-byte pieces), padded, zero and
// negative pitches, in place, every branch of the DXT colour code, every
// pixel format and data type pair. Targets are canvases with a margin of
// known bytes all round, hashed whole, so a stray write counts too. Every
// result goes into the fingerprint's `px` part, one hash per stage logged as
// `autotest: pixels: <stage> <hash>`; addresses never go in.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace opensc4::autotest {
namespace {

bool g_pxOn = false;
uint32_t g_pxHash = 2166136261u;

void MixByte(uint8_t value) { g_pxHash = (g_pxHash ^ value) * 16777619u; }

void Mix(uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        MixByte(static_cast<uint8_t>(value >> (8 * i)));
}

void MixBytes(const std::vector<uint8_t>& bytes)
{
    for (const uint8_t b : bytes)
        MixByte(b);
}

void PxStage(const char* name) { log::Write("autotest: pixels: %s %08X", name, g_pxHash); }

// A fixed sequence of numbers (the same in every run).
struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state ^ (state >> 15);
    }
    uint8_t Byte() { return static_cast<uint8_t>(Next() >> 8); }
};

// A buffer with `margin` bytes of 0xA5 all round; `origin` is where rows
// start, at least `before` bytes in.
struct Canvas {
    std::vector<uint8_t> bytes;
    std::size_t origin;
    Canvas(std::size_t size, std::size_t before) : bytes(size + before + 64, 0xA5), origin(before + 32) {}
    uint8_t* At() { return bytes.data() + origin; }
    void Fill(Random& random, std::size_t from, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
            bytes[origin + from + i] = random.Byte();
    }
};

template <typename Fn>
Fn Game(std::uintptr_t address)
{
    return reinterpret_cast<Fn>(address);
}

// ====================================================================================
// Vertex formats

using ExpandFn = uint32_t(__cdecl*)(uint32_t);
using MakeFn = uint32_t(__cdecl*)(uint32_t, const int32_t*);
using OffsetFn = uint32_t(__cdecl*)(uint32_t, int32_t, uint32_t);
using CountFn = uint32_t(__cdecl*)(uint32_t, int32_t);
using GatherFn = void(__cdecl*)(uint32_t, const void*, void*, uint32_t, int32_t, uint32_t);

struct Regs {
    uint32_t eax, ecx, edx;
};

// 0x008D01F0 called with marks in ECX and EDX, which its callers rely on it
// keeping; the registers after the call written to `out`.
__declspec(naked) void __cdecl CallExpand(std::uintptr_t, uint32_t, Regs*)
{
    __asm push ebx
    __asm mov ecx, 0x11223344
    __asm mov edx, 0x55667788
    __asm push dword ptr [esp + 12]
    __asm call dword ptr [esp + 12]
    __asm add esp, 4
    __asm mov ebx, dword ptr [esp + 16]
    __asm mov dword ptr [ebx], eax
    __asm mov dword ptr [ebx + 4], ecx
    __asm mov dword ptr [ebx + 8], edx
    __asm pop ebx
    __asm ret
}

// The formats the vertex stage runs through: every legacy ID and its
// neighbours, bit fields with every count, and odd ones.
std::vector<uint32_t> Formats(Random& random)
{
    std::vector<uint32_t> formats;
    for (uint32_t f = 0; f <= 0x30; ++f)
        formats.push_back(f);
    for (const uint32_t f : {0x80000000u, 0x80000001u, 0x80004101u, 0x8000810Fu, 0x80FFFFFFu, 0xFFFFFFFFu,
                             0x7FFFFFFFu, 0x12345678u, 0x80000F00u, 0x803C0000u, 0x80000004u, 0x800000E0u})
        formats.push_back(f);
    for (int i = 0; i < 24; ++i)
        formats.push_back(random.Next() | 0x80000000);
    for (int i = 0; i < 4; ++i)
        formats.push_back(random.Next() & 0x7FFFFFFF);
    return formats;
}

void DriveVertex()
{
    Random random{0x5EED0001};
    const std::vector<uint32_t> formats = Formats(random);
    const auto expand = Game<ExpandFn>(addr::GDVertexFormat_Expand);
    const auto make = Game<MakeFn>(addr::GDVertexFormat_Make);
    const auto stride = Game<ExpandFn>(addr::GDVertexFormat_Stride);
    const auto offset = Game<OffsetFn>(addr::GDVertexFormat_ElementOffset);
    const auto count = Game<CountFn>(addr::GDVertexFormat_ElementCount);
    const auto gather = Game<GatherFn>(addr::GDVertexFormat_Gather);

    for (const uint32_t f : formats) {
        Mix(expand(f));
        Regs regs{};
        CallExpand(addr::GDVertexFormat_Expand, f, &regs);
        Mix(regs.eax);
        MixByte(regs.ecx == 0x11223344);
        MixByte(regs.edx == 0x55667788);
        Mix(stride(f));
        for (int32_t type = 0; type < 10; ++type) {
            Mix(count(f, type));
            for (uint32_t index = 0; index < 4; ++index)
                Mix(offset(f, type, index));
        }
    }
    PxStage("format");

    // Lists of element types: empty, one of each, counts past their fields'
    // masks, random.
    std::vector<std::vector<int32_t>> lists = {{}, {0}, {0, 5}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, {0, 0, 0, 0, 0},
                                               {1, 1, 1, 1, 1, 1, 1, 1, 1}, {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9},
                                               {5, 5, 5, 5}, {2, 2}, {7, 6, 5, 0}};
    for (int i = 0; i < 12; ++i) {
        std::vector<int32_t> list;
        const uint32_t n = random.Next() % 24;
        for (uint32_t k = 0; k < n; ++k)
            list.push_back(static_cast<int32_t>(random.Next() % 10));
        lists.push_back(list);
    }
    for (const auto& list : lists)
        Mix(make(static_cast<uint32_t>(list.size()), list.data()));
    PxStage("make");

    // Gather: 9 vertices of random bytes; every type and index the format
    // has (and one past), counts 0, 1, 2 and 9.
    for (const uint32_t f : {0x01u, 0x0Bu, 0x26u, 0x80004101u, 0x8000C1C7u, 0x80FFFFFFu, 0x8044C3E7u}) {
        const uint32_t s = stride(f);
        std::vector<uint8_t> vertices(s * 9 + 64);
        for (uint8_t& b : vertices)
            b = random.Byte();
        for (int32_t type = 0; type < 10; ++type) {
            const uint32_t n = count(f, type);
            for (uint32_t index = 0; index <= n && index < 4; ++index) {
                for (const uint32_t vertexCount : {0u, 1u, 2u, 9u}) {
                    if (offset(f, type, index) + 16 > s + 16)
                        continue;
                    Canvas out(16 * 9, 0);
                    gather(vertexCount, vertices.data(), out.At(), f, type, index);
                    MixBytes(out.bytes);
                }
            }
        }
    }
    PxStage("gather");
}

// ====================================================================================
// Colour keys

using KeyFn = void(__cdecl*)(const void*, int32_t, void*, int32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using PixelsFn = void(__cdecl*)(void*, int32_t, const void*, int32_t, uint32_t, uint32_t, uint32_t);

// A source of `height` rows of `width` pixels of `bytes`, `pitch` apart
// (negative: bottom up), every third pixel `key` with random bits outside
// `mask`.
struct Picture {
    Canvas canvas;
    int32_t pitch;
    Picture(Random& random, uint32_t width, uint32_t height, uint32_t bytes, int32_t pitch, uint32_t key,
            uint32_t mask)
        : canvas(static_cast<std::size_t>(height) * (pitch < 0 ? -pitch : pitch) + width * bytes + 32,
                 pitch < 0 ? static_cast<std::size_t>(height - 1) * -pitch : 0),
          pitch(pitch)
    {
        for (uint32_t y = 0; y < height; ++y) {
            uint8_t* row = canvas.At() + static_cast<std::ptrdiff_t>(y) * pitch;
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t value = random.Next();
                if ((x + 2 * y) % 3 == 0)
                    value = (key & mask) | (value & ~mask);
                std::memcpy(row + x * bytes, &value, bytes);
            }
        }
    }
    uint8_t* At() { return canvas.At(); }
};

void DriveKeys()
{
    Random random{0x5EED0002};
    const auto key16 = Game<KeyFn>(addr::GZPixels_KeyCopy16);
    const auto key32 = Game<KeyFn>(addr::GZPixels_KeyCopy32);
    const auto key1555 = Game<PixelsFn>(addr::GZPixels_KeyCopy1555);
    const auto key4444 = Game<PixelsFn>(addr::GZPixels_KeyCopy4444);
    const auto key8888 = Game<PixelsFn>(addr::GZPixels_KeyCopy8888);

    const uint32_t widths[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 15, 16, 17, 31, 33};
    for (const uint32_t width : widths) {
        for (const uint32_t height : {1u, 2u, 3u}) {
            for (int layout = 0; layout < 4; ++layout) {
                // 0 tight, 1 padded, 2 bottom up, 3 in place.
                for (const uint32_t bytes : {2u, 4u}) {
                    const int32_t tight = static_cast<int32_t>(width * bytes);
                    const int32_t pitch = layout == 0 || layout == 3 ? tight : layout == 1 ? tight + 6 : -tight - 2;
                    const uint32_t key = random.Next();
                    const uint32_t masks16[] = {0x7FFF7FFF, 0x0FFF0FFF, 0xFFFF, 0};
                    const uint32_t masks32[] = {0xFFFFFF, 0xFFFFFFFF, 0};
                    const int n = bytes == 2 ? 4 : 3;
                    for (int m = 0; m < n; ++m) {
                        const uint32_t mask = bytes == 2 ? masks16[m] : masks32[m];
                        Picture source(random, width, height, bytes, pitch, key, bytes == 2 ? mask & 0xFFFF : mask);
                        Picture target(random, width, height, bytes, pitch, 0, 0);
                        uint8_t* to = layout == 3 ? source.At() : target.At();
                        const KeyFn fn = bytes == 2 ? key16 : key32;
                        fn(source.At(), pitch, to, pitch, width, height, key, mask);
                        MixBytes(source.canvas.bytes);
                        MixBytes(target.canvas.bytes);
                    }
                    // The wrappers, their mask fixed.
                    Picture source(random, width, height, bytes, pitch, key, bytes == 2 ? 0x7FFF : 0xFFFFFF);
                    Picture target(random, width, height, bytes, pitch, 0, 0);
                    if (bytes == 2) {
                        key1555(target.At(), pitch, source.At(), pitch, width, height, key);
                        MixBytes(target.canvas.bytes);
                        Picture other(random, width, height, bytes, pitch, key, 0x0FFF);
                        key4444(other.At(), pitch, other.At(), pitch, width, height, key);
                        MixBytes(other.canvas.bytes);
                    } else {
                        key8888(target.At(), pitch, source.At(), pitch, width, height, key);
                        MixBytes(target.canvas.bytes);
                    }
                }
            }
        }
    }
    PxStage("key");
}

// ====================================================================================
// Modulation

// A DXT block's colours: the first greater, smaller or the same.
void SetBlockColours(Random& random, uint8_t* block, int kind)
{
    uint16_t c0 = static_cast<uint16_t>(random.Next()), c1 = static_cast<uint16_t>(random.Next());
    if (kind == 0 && c0 <= c1)
        std::swap(c0, c1);
    if (kind == 1 && c0 > c1)
        std::swap(c0, c1);
    if (kind == 2)
        c1 = c0;
    if (kind == 0 && c0 == c1)
        c0 = static_cast<uint16_t>(c1 + 1);
    std::memcpy(block, &c0, 2);
    std::memcpy(block + 2, &c1, 2);
}

void DriveModulate()
{
    Random random{0x5EED0003};
    const PixelsFn plain[] = {Game<PixelsFn>(addr::GZPixels_Modulate8888), Game<PixelsFn>(addr::GZPixels_Modulate888),
                              Game<PixelsFn>(addr::GZPixels_Modulate4444), Game<PixelsFn>(addr::GZPixels_Modulate1555)};
    const uint32_t plainBytes[] = {4, 3, 2, 2};
    const uint32_t colours[] = {0, 0xFFFFFFFF, 0x80808080, 0x7F00FF01, 0x01020304, 0xFFFF0000};

    for (int f = 0; f < 4; ++f) {
        const uint32_t bytes = plainBytes[f];
        for (const uint32_t width : {1u, 2u, 3u, 5u, 8u, 13u}) {
            for (const uint32_t height : {1u, 2u, 3u}) {
                // Pitches: four bytes a pixel (the 16-bit ones step by the
                // pitch less four bytes a pixel), padded, and 0 (rows over
                // each other, as 0x00492CC0 calls them), and in place.
                for (int layout = 0; layout < 4; ++layout) {
                    const int32_t pitch = layout == 0 ? static_cast<int32_t>(width * 4)
                                          : layout == 1 ? static_cast<int32_t>(width * 4 + 12)
                                          : layout == 2 ? 0
                                                        : static_cast<int32_t>(width * 4);
                    if (layout == 2 && bytes == 2)
                        continue; // rows would run backwards out of the canvas
                    for (const uint32_t colour : colours) {
                        const std::size_t size = static_cast<std::size_t>(height) * (width * 4 + 12) + 16;
                        Canvas source(size, 0), target(size, 0);
                        source.Fill(random, 0, size);
                        target.Fill(random, 0, size);
                        void* to = layout == 3 ? source.At() : target.At();
                        plain[f](to, pitch, source.At(), pitch, width, height, colour ^ random.Next() % 2);
                        MixBytes(source.bytes);
                        MixBytes(target.bytes);
                    }
                }
            }
        }
    }
    PxStage("modulate");

    const PixelsFn dxt[] = {Game<PixelsFn>(addr::GZPixels_ModulateDXT1), Game<PixelsFn>(addr::GZPixels_ModulateDXT3)};
    for (int f = 0; f < 2; ++f) {
        const uint32_t blockBytes = f == 0 ? 8 : 16;
        const uint32_t colourAt = f == 0 ? 0 : 8;
        for (const uint32_t width : {4u, 5u, 8u, 12u, 16u}) {
            for (const uint32_t height : {4u, 7u, 8u}) {
                const uint32_t blocks = width / 4, rows = height / 4;
                for (int layout = 0; layout < 3; ++layout) {
                    // tight, padded, in place
                    const int32_t pitch = static_cast<int32_t>(blocks * blockBytes + (layout == 1 ? 8 : 0));
                    for (const uint32_t colour : colours) {
                        const std::size_t size = static_cast<std::size_t>(rows) * pitch + 16;
                        Canvas source(size, 0), target(size, 0);
                        source.Fill(random, 0, size);
                        target.Fill(random, 0, size);
                        for (uint32_t r = 0; r < rows; ++r)
                            for (uint32_t b = 0; b < blocks; ++b)
                                SetBlockColours(random, source.At() + r * pitch + b * blockBytes + colourAt,
                                                static_cast<int>((r * blocks + b) % 3));
                        void* to = layout == 2 ? source.At() : target.At();
                        dxt[f](to, pitch, source.At(), pitch, width, height, colour);
                        MixBytes(source.bytes);
                        MixBytes(target.bytes);
                    }
                }
            }
        }
    }
    PxStage("modulatedxt");
}

// ====================================================================================
// DXT decoding

using DecodeFn = void(__cdecl*)(void*, int32_t, const void*, uint32_t, uint32_t, uint32_t);
using Block8888Fn = void(__stdcall*)(void*, int32_t, uint32_t, const uint32_t*);

// 0x008D1E70 called with its registers; what it leaves in EAX (less the
// target), ECX and EDX (whether it is the palette) written to `out`.
__declspec(naked) void __cdecl CallBlock16(std::uintptr_t, void*, uint32_t, const void*, int32_t, Regs*)
{
    __asm push esi
    __asm push edi
    __asm mov esi, dword ptr [esp + 12]
    __asm mov eax, dword ptr [esp + 16]
    __asm mov ecx, dword ptr [esp + 20]
    __asm mov edx, dword ptr [esp + 24]
    __asm push dword ptr [esp + 28]
    __asm call esi
    __asm mov edi, dword ptr [esp + 32]
    __asm sub eax, dword ptr [esp + 16]
    __asm mov dword ptr [edi], eax
    __asm mov dword ptr [edi + 4], ecx
    __asm sub edx, dword ptr [esp + 24]
    __asm mov dword ptr [edi + 8], edx
    __asm pop edi
    __asm pop esi
    __asm ret
}

void DriveDxt()
{
    Random random{0x5EED0004};
    struct Decoder {
        std::uintptr_t address;
        uint32_t blockBytes, pixelBytes, colourAt;
    };
    const Decoder decoders[] = {
        {addr::GZPixels_DecodeDXT1To8888, 8, 4, 0}, {addr::GZPixels_DecodeDXT3To1555, 16, 2, 8},
        {addr::GZPixels_DecodeDXT3To4444, 16, 2, 8}, {addr::GZPixels_DecodeDXT3To8888, 16, 4, 8},
        {addr::GZPixels_DecodeDXT1To1555, 8, 2, 0}, {addr::GZPixels_DecodeDXT1To4444, 8, 2, 0},
    };
    for (const Decoder& decoder : decoders) {
        const auto decode = Game<DecodeFn>(decoder.address);
        for (const uint32_t wide : {0u, 1u, 2u, 3u}) {
            for (const uint32_t high : {0u, 1u, 2u}) {
                for (int layout = 0; layout < 3; ++layout) {
                    // tight, padded (target and blocks), bottom up
                    const uint32_t blockPitch = wide + (layout == 1 ? 1 : 0);
                    const int32_t rowBytes = static_cast<int32_t>(wide * 4 * decoder.pixelBytes);
                    const int32_t pitch = layout == 0 ? rowBytes : layout == 1 ? rowBytes + 6 : -rowBytes - 4;
                    const int32_t step = pitch < 0 ? -pitch : pitch;
                    Canvas blocks(static_cast<std::size_t>(high) * blockPitch * decoder.blockBytes + 16, 0);
                    blocks.Fill(random, 0, blocks.bytes.size() - blocks.origin);
                    for (uint32_t i = 0; i < high * blockPitch; ++i)
                        SetBlockColours(random, blocks.At() + i * decoder.blockBytes + decoder.colourAt,
                                        static_cast<int>(i % 3));
                    const std::size_t rows = static_cast<std::size_t>(high) * 4;
                    Canvas target(rows * step + 16, pitch < 0 && rows > 0 ? (rows - 1) * step : 0);
                    decode(target.At(), pitch, blocks.At(), blockPitch, wide, high);
                    MixBytes(target.bytes);
                }
            }
        }
    }
    PxStage("decode");

    // The block writers on their own.
    const auto block8888 = Game<Block8888Fn>(addr::GZPixels_WriteBlock8888);
    for (int i = 0; i < 8; ++i) {
        const uint32_t palette[4] = {random.Next(), random.Next(), random.Next(), random.Next()};
        const int32_t pitch = i % 2 ? 16 : -24;
        Canvas target(4 * 24 + 16, pitch < 0 ? 3 * 24 : 0);
        block8888(target.At(), pitch, random.Next(), palette);
        MixBytes(target.bytes);
    }
    for (int i = 0; i < 8; ++i) {
        const uint16_t palette[4] = {static_cast<uint16_t>(random.Next()), static_cast<uint16_t>(random.Next()),
                                     static_cast<uint16_t>(random.Next()), static_cast<uint16_t>(random.Next())};
        const int32_t pitch = i % 2 ? 8 : -12;
        Canvas target(4 * 12 + 16, pitch < 0 ? 3 * 12 : 0);
        Regs regs{};
        CallBlock16(addr::GZPixels_WriteBlock16, target.At(), random.Next(), palette, pitch, &regs);
        MixBytes(target.bytes);
        Mix(regs.eax);
        Mix(regs.ecx);
        Mix(regs.edx);
    }
    PxStage("blocks");
}

// ====================================================================================
// Format bits

using FormatBitsFn = bool(__cdecl*)(uint32_t, uint32_t, uint32_t*, uint32_t*, uint32_t*, uint32_t*);

void DriveFormat()
{
    const auto bits = Game<FormatBitsFn>(addr::GZPixels_FormatBits);
    for (uint32_t pixelFormat = 0; pixelFormat <= 12; ++pixelFormat) {
        for (uint32_t dataType = 0; dataType <= 16; ++dataType) {
            uint32_t out[4] = {0xA1, 0xA2, 0xA3, 0xA4};
            MixByte(bits(pixelFormat, dataType, &out[0], &out[1], &out[2], &out[3]) ? 1 : 0);
            for (const uint32_t v : out)
                Mix(v);
        }
    }
    for (const uint32_t odd : {0xFFFFFFFFu, 0x80000000u, 0x100u}) {
        uint32_t out[4] = {0xB1, 0xB2, 0xB3, 0xB4};
        MixByte(bits(odd, 0, &out[0], &out[1], &out[2], &out[3]) ? 1 : 0);
        MixByte(bits(1, odd, &out[0], &out[1], &out[2], &out[3]) ? 1 : 0);
        for (const uint32_t v : out)
            Mix(v);
    }
    PxStage("bits");
}

// OPENSC4_TEST_PIXELS=<day>: the 26 by address. Every result goes into the
// fingerprint's `px` part.
void DrivePixelsDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PIXELS") || !game::IsBuild641())
        return;
    g_pxOn = true;
    DriveVertex();
    DriveKeys();
    DriveModulate();
    DriveDxt();
    DriveFormat();
    log::Write("autotest: pixels driven (hash %08X)", g_pxHash);
}

void PxFingerprint(cISC4City*, std::string& line)
{
    if (!g_pxOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | px %08X", g_pxHash);
    line += text;
}

const FingerprintPart kPxFingerprint{"pixels", 4, &PxFingerprint};
const DayHook kPxDrive{"pixels", 4, &DrivePixelsDay};

} // namespace
} // namespace opensc4::autotest
