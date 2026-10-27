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
// addresses never go in. Then the drawing context's methods on recording
// mocks ("The drawing context" below), the rasterizer's on recording buffers
// and renderer, and a drawing through a context onto real pixels ("The
// rasterizer"), and the buffer with a context's on a recording context and
// plain buffer, and drawing through one onto real pixels ("The buffer").
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
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

// ====================================================================================
// The drawing context
//
// A context made by the game's constructor over operator new's memory
// (filled with a pattern first, so the padding the settings stack copies is
// the same in every run), with a rasterizer and a target that are mocks of
// our own: each of their slots records its id, slot and arguments into the
// hash and answers from a fixed rule. The context's vtable is a copy of the
// game's (so its slots reach the game's methods, or ours through the
// detours), the draws (44 and 45) included: the rasterizer's mock answers
// how to draw a shape (slot 21) and records the spans (22). After each
// call the context's state goes into the hash: everything but its pointers,
// and the two stacks' records.

struct Rect {
    int32_t left, top, right, bottom;
};

void Mix(uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        MixByte(static_cast<uint8_t>(value >> (8 * i)));
}

void MixFloat(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    Mix(bits);
}

void MixRect(const Rect* r)
{
    Mix(static_cast<uint32_t>(r->left));
    Mix(static_cast<uint32_t>(r->top));
    Mix(static_cast<uint32_t>(r->right));
    Mix(static_cast<uint32_t>(r->bottom));
}

float FloatOf(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

constexpr int kSlots = 48;

struct Mock {
    void** vtable;
    uint32_t id;
    uint32_t calls = 0;
    uint8_t answer = 1; // the rasterizer's answer to SetSource
    uint32_t how = 2;   // its answer to how to draw a shape (slot 21)
    uint32_t spanCalls = 0;
};

void Rec(Mock* m, uint32_t slot)
{
    Mix(m->id);
    Mix(slot);
    ++m->calls;
}

// What a slot answers: its id, slot and how many calls it has had.
uint32_t Answer(Mock* m, uint32_t slot) { return m->id << 16 ^ slot << 8 ^ m->calls * 0x9E3779B1u; }

template <int Slot>
uint32_t __fastcall MTrap(Mock* m, void*)
{
    Rec(m, 0xEE00 + Slot);
    return 0;
}

template <int Slot>
uint32_t __fastcall MNone(Mock* m, void*)
{
    Rec(m, Slot);
    return Answer(m, Slot);
}

template <int Slot>
uint32_t __fastcall MWord(Mock* m, void*, uint32_t a)
{
    Rec(m, Slot);
    Mix(a);
    return Answer(m, Slot);
}

template <int Slot>
uint32_t __fastcall MRect(Mock* m, void*, const Rect* r)
{
    Rec(m, Slot);
    MixRect(r);
    return Answer(m, Slot);
}

uint32_t __fastcall MColours(Mock* m, void*, uint32_t a, uint32_t b, const uint32_t* colours)
{
    Rec(m, 7);
    Mix(a);
    Mix(b);
    for (int i = 0; i < 4; ++i)
        Mix(colours[i]);
    return Answer(m, 7);
}

uint32_t __fastcall MPen(Mock* m, void*, float width)
{
    Rec(m, 11);
    MixFloat(width);
    return Answer(m, 11);
}

uint32_t __fastcall MThree(Mock* m, void*, uint32_t a, uint32_t b, uint32_t c)
{
    Rec(m, 12);
    Mix(a);
    Mix(b);
    Mix(c);
    return Answer(m, 12);
}

std::vector<Mock*> g_mocks;

void MixObject(const void* p)
{
    for (Mock* m : g_mocks)
        if (m == p) {
            Mix(m->id);
            return;
        }
    Mix(p ? 0xBADu : 0u);
}

// SetSource: the source rectangle from the arguments; the answer in AL, the
// rest of EAX not 0.
uint32_t __fastcall MSource(Mock* m, void*, const void* source, float x, float y, float sx, float sy, Rect* out)
{
    Rec(m, 13);
    MixObject(source);
    MixFloat(x);
    MixFloat(y);
    MixFloat(sx);
    MixFloat(sy);
    const int32_t left = static_cast<int32_t>(m->calls * 7 % 50) - 10;
    const int32_t top = static_cast<int32_t>(m->calls * 11 % 40) - 5;
    *out = Rect{left, top, left + static_cast<int32_t>(m->calls % 60), top + 30};
    return 0x5A5A00u | m->answer;
}

// The target's slot 30: a pixel value from red, green and blue.
uint32_t __fastcall MNative(Mock* m, void*, uint32_t r, uint32_t g, uint32_t b)
{
    Rec(m, 30);
    Mix(r);
    Mix(g);
    Mix(b);
    return 0x5A000000 | (r & 0xFF) << 16 | (g & 0xFF) << 8 | (b & 0xFF);
}

// How to draw a shape: the kind, the count and the points (two floats a
// point, the first eight).
uint32_t __fastcall MHow(Mock* m, void*, uint32_t kind, const float* points, int32_t count)
{
    Rec(m, 21);
    Mix(kind);
    Mix(static_cast<uint32_t>(count));
    for (int32_t i = 0; i < 2 * count && i < 16; ++i)
        MixFloat(points[i]);
    return m->how;
}

// The surface (slot 19, ret 8): six words out, the second argument
// recorded.
uint32_t __fastcall MSurface(Mock* m, void*, uint32_t* out, uint32_t unused)
{
    Rec(m, 19);
    Mix(unused);
    for (uint32_t i = 0; i < 6; ++i)
        out[i] = m->id * 0x1000193u + i;
    return Answer(m, 19);
}

// The spans: every word.
uint32_t __fastcall MSpans(Mock* m, void*, const uint32_t* spans, uint32_t count)
{
    Rec(m, 22);
    Mix(count);
    for (uint32_t i = 0; i < 2 * count; ++i)
        Mix(spans[i]);
    ++m->spanCalls;
    return Answer(m, 22);
}

template <std::size_t... I>
void FillTraps(void** table, std::index_sequence<I...>)
{
    ((table[I] = reinterpret_cast<void*>(&MTrap<static_cast<int>(I)>)), ...);
}

template <typename Fn>
void* V(Fn fn)
{
    return reinterpret_cast<void*>(fn);
}

struct ContextTables {
    void* raster[kSlots];
    void* target[kSlots];
    void* context[kSlots];
    ContextTables()
    {
        FillTraps(raster, std::make_index_sequence<kSlots>{});
        FillTraps(target, std::make_index_sequence<kSlots>{});
        raster[2] = V(&MNone<2>);
        raster[4] = V(&MNone<4>);
        raster[6] = V(&MRect<6>);
        raster[7] = V(&MColours);
        raster[8] = V(&MWord<8>);
        raster[9] = V(&MWord<9>);
        raster[10] = V(&MWord<10>);
        raster[11] = V(&MPen);
        raster[12] = V(&MThree);
        raster[13] = V(&MSource);
        raster[14] = V(&MNone<14>);
        raster[15] = V(&MRect<15>);
        for (int slot = 16; slot <= 20; ++slot)
            raster[slot] = slot == 16 ? V(&MNone<16>)
                         : slot == 17 ? V(&MNone<17>)
                         : slot == 18 ? V(&MNone<18>)
                         : slot == 19 ? V(&MSurface)
                                      : V(&MNone<20>);
        raster[21] = V(&MHow);
        raster[22] = V(&MSpans);
        target[2] = V(&MNone<2>);
        target[30] = V(&MNative);
        std::memcpy(context, reinterpret_cast<void*>(addr::GZDrawContext_vftable), sizeof context);
        context[0] = V(&MTrap<0>);
        context[17] = V(&MTrap<17>);
    }
};

ContextTables& ContextTablesOf()
{
    static ContextTables tables;
    return tables;
}

// The context's words: the offsets ours names (raster_context.cpp).
template <typename T>
T& At(uint8_t* c, uint32_t offset)
{
    return *reinterpret_cast<T*>(c + offset);
}

// Everything but the vtable and the two pointers (+0x24, +0x28), the stacks
// by their records.
void MixContext(uint8_t* c)
{
    for (uint32_t at = 0x04; at < 0x24; ++at)
        MixByte(c[at]);
    for (uint32_t at = 0x2C; at < 0x7C; ++at)
        MixByte(c[at]);
    for (uint32_t at = 0xA0; at < 0xB8; ++at) // DrawShape's swapped words
        MixByte(c[at]);
    for (const uint32_t vector : {0x7Cu, 0x88u}) {
        const uint8_t* begin = At<uint8_t*>(c, vector);
        const uint8_t* end = At<uint8_t*>(c, vector + 4);
        const uint8_t* capacity = At<uint8_t*>(c, vector + 8);
        Mix(static_cast<uint32_t>(end - begin));
        MixByte(capacity >= end);
        for (const uint8_t* p = begin; p < end; ++p)
            MixByte(*p);
    }
    Mix(At<uint32_t>(c, 0xC4));
}

template <typename R, typename... A>
R Method(std::uintptr_t address, uint8_t* c, A... args)
{
    return reinterpret_cast<R(__fastcall*)(void*, void*, A...)>(address)(c, nullptr, args...);
}

// A method whose answer goes into the hash.
template <typename... A>
void Mixed(std::uintptr_t address, uint8_t* c, A... args)
{
    Mix(Method<uint32_t>(address, c, args...));
}

// Pointers answered: whether it is the one expected.
template <typename... A>
void MixedIs(const void* expected, std::uintptr_t address, uint8_t* c, A... args)
{
    MixByte(Method<uint32_t>(address, c, args...) == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(expected)));
}

// A draw's answer when it handed on spans (the rasterizer's answer) or the
// rasterizer said not to draw; with nothing drawn it is a pointer.
template <typename... A>
void Drawn(Mock& raster, std::uintptr_t address, uint8_t* c, A... args)
{
    const uint32_t before = raster.spanCalls;
    const uint32_t answer = Method<uint32_t>(address, c, args...);
    if (raster.spanCalls != before || raster.how != 2)
        Mix(answer);
}

// The pen width's answer only when it changes (unchanged, the game's is the
// FPU status word).
void SetPen(uint8_t* c, float width)
{
    const bool changed = !(At<float>(c, 0x4C) == width);
    const uint32_t answer = Method<uint32_t>(addr::GZDrawContext_SetPenWidth, c, width);
    if (changed)
        Mix(answer);
}

// A push's answer when it had room: the record's place.
void Pushed(std::uintptr_t address, uint8_t* c, uint32_t vector)
{
    uint8_t* const end = At<uint8_t*>(c, vector + 4);
    const bool room = end != At<uint8_t*>(c, vector + 8);
    const uint32_t answer = Method<uint32_t>(address, c);
    if (room)
        MixByte(answer == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(end)));
}

// A pop's answer when it was empty: the context (transforms) or the
// vector's start (settings); otherwise the last call's, for the transforms
// (the settings' last call is SetPenWidth, whose answer is the FPU status
// word when the width is unchanged).
void Popped(std::uintptr_t address, uint8_t* c, uint32_t vector, const void* emptyAnswer)
{
    const bool empty = At<uint8_t*>(c, vector) == At<uint8_t*>(c, vector + 4);
    const uint32_t answer = Method<uint32_t>(address, c);
    if (empty)
        MixByte(answer == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emptyAnswer)));
    else if (vector == 0x7C)
        Mix(answer);
}
Rect RandomRect(Random& random, int32_t low, int32_t high)
{
    const int32_t x = low + static_cast<int32_t>(random.Below(static_cast<uint32_t>(high - low)));
    const int32_t y = low + static_cast<int32_t>(random.Below(static_cast<uint32_t>(high - low)));
    return {x, y, x + static_cast<int32_t>(random.Below(50)) - 5, y + static_cast<int32_t>(random.Below(50)) - 5};
}

void DriveContext()
{
    Random random{0x52415335u};
    ContextTables& t = ContextTablesOf();
    Mock raster{t.raster, 0x4A};
    Mock target{t.target, 0xB0};
    Mock source{t.target, 0x5C};
    g_mocks = {&raster, &target, &source};

    uint8_t* const c = static_cast<uint8_t*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(200));
    std::memset(c, 0xC3, 200);
    reinterpret_cast<void*(__fastcall*)(void*, void*)>(addr::GZDrawContext_GZDrawContext)(c, nullptr);
    At<void**>(c, 0) = t.context;
    At<void*>(c, 0x24) = &target;
    At<void*>(c, 0x28) = &raster;
    At<Rect>(c, 0x54) = Rect{0, 0, 120, 90};
    Mix(Method<uint32_t>(addr::GZDrawContext_AddRef, c));
    Mixed(addr::GZDrawContext_ResetClip, c);
    MixContext(c);
    RsStage("context-made");

    // The setters, each twice (the second time unchanged), with and without
    // a rasterizer; pen widths at the edges of the rounding.
    const uint32_t pens[] = {0x3F800000, 0x3F800000, 0x7FC00000, 0xFFC00000, 0xBF800000, 0x80000000,
                             0x00000000, 0x00000001, 0x3EFFFFFF, 0x3F000000, 0x3FC00000, 0x40200000,
                             0x4F32D05E, 0x501502F9, 0x60AD78EC, 0x3F7FFFFF, 0x40400001, 0x7F800000};
    for (int pass = 0; pass < 2; ++pass) {
        At<void*>(c, 0x28) = pass == 0 ? static_cast<void*>(&raster) : nullptr;
        for (const uint32_t pen : pens) {
            SetPen(c, FloatOf(pen));
            MixContext(c);
        }
        for (const uint32_t mode : {0u, 3u, 3u, 0xFFFFFFFFu, 0x100u}) {
            Mix(Method<uint32_t>(addr::GZDrawContext_SetMode, c, mode));
            Mix(Method<uint32_t>(addr::GZDrawContext_SetFlag45, c, mode));
            Mix(Method<uint32_t>(addr::GZDrawContext_SetFlag44, c, mode ^ 0x101));
            MixContext(c);
        }
        for (int i = 0; i < 6; ++i) {
            Mixed(addr::GZDrawContext_SetColour, c, i % 4, random.Next());
            Mixed(addr::GZDrawContext_SetForeColour, c, random.Next());
            MixContext(c);
        }
    }
    At<void*>(c, 0x28) = &raster;
    Mixed(addr::GZDrawContext_ResetSettings, c);
    MixContext(c);
    RsStage("context-settings");

    // Origin and clip: origins near and far (the truncation and wrap),
    // rectangles inside, across, outside and inverted, with and without a
    // whole source set.
    const int32_t origins[][2] = {{0, 0}, {5, -7}, {-33, 40}, {16777217, -16777219}, {0x7FFFFFF0, 3},
                                  {-0x7FFFFFF0, -2}};
    for (int round = 0; round < 24; ++round) {
        const int32_t* o = origins[round % 6];
        MixedIs(o, addr::GZDrawContext_SetOrigin, c, o);
        int32_t back[2] = {0x11111111, 0x22222222};
        MixedIs(back, addr::GZDrawContext_GetOrigin, c, back);
        Mix(static_cast<uint32_t>(back[0]));
        Mix(static_cast<uint32_t>(back[1]));
        raster.answer = static_cast<uint8_t>(round % 3 != 0);
        if (round % 4 == 1)
            Mixed(addr::GZDrawContext_SetSource, c, static_cast<void*>(&source), FloatOf(0x3FC00000),
                         FloatOf(0xC1200000), FloatOf(0x3F800000), FloatOf(0x40000000));
        if (round % 4 == 3)
            Mixed(addr::GZDrawContext_UseSourcePart, c, &At<Rect>(c, 0x54));
        if (round % 4 == 0)
            Mixed(addr::GZDrawContext_UseWholeSource, c);
        const Rect r = RandomRect(random, -20, 130);
        Mixed(addr::GZDrawContext_ClipTo, c, &r);
        MixContext(c);
        const Rect a = RandomRect(random, -20, 130);
        Mixed(addr::GZDrawContext_ClipToAbsolute, c, &a);
        MixContext(c);
        if (round % 5 == 4) {
            Mixed(addr::GZDrawContext_ResetClip, c);
            MixContext(c);
        }
    }
    Mixed(addr::GZDrawContext_ClearSource, c);
    MixContext(c);
    RsStage("context-clip");

    // The stacks: pushed past their first allocations (the growing inserts),
    // popped past empty.
    for (int i = 0; i < 40; ++i) {
        const int32_t o[2] = {i * 3 - 50, 17 - i};
        MixedIs(o, addr::GZDrawContext_SetOrigin, c, o);
        const Rect r = RandomRect(random, -20, 130);
        Mixed(addr::GZDrawContext_ClipToAbsolute, c, &r);
        Pushed(addr::GZDrawContext_PushTransform, c, 0x7C);
        SetPen(c, static_cast<float>(i) * 0.75f);
        Mixed(addr::GZDrawContext_SetMode, c, static_cast<uint32_t>(i % 5));
        Mixed(addr::GZDrawContext_SetFlag45, c, static_cast<uint32_t>(i % 3 == 0));
        Mixed(addr::GZDrawContext_SetFlag44, c, static_cast<uint32_t>(i % 2) | 0x7700);
        Pushed(addr::GZDrawContext_PushSettings, c, 0x88);
        Mix(Method<uint32_t>(addr::GZDrawContext_TransformDepth, c));
        MixContext(c);
    }
    for (int i = 0; i < 44; ++i) {
        Popped(addr::GZDrawContext_PopTransform, c, 0x7C, c);
        Popped(addr::GZDrawContext_PopSettings, c, 0x88, At<uint8_t*>(c, 0x88));
        Mix(Method<uint32_t>(addr::GZDrawContext_TransformDepth, c));
        MixContext(c);
    }
    RsStage("context-stacks");

    // Rectangles and blits: empty, inverted and huge rectangles (the
    // differences wrap), scales that do not divide.
    const int32_t origin[2] = {3, -4};
    MixedIs(origin, addr::GZDrawContext_SetOrigin, c, origin);
    Drawn(raster, addr::GZDrawContext_FillClip, c);
    for (int i = 0; i < 40; ++i) {
        Rect from = RandomRect(random, -10, 60), to = RandomRect(random, -30, 120);
        if (i % 10 == 7)
            to = Rect{-0x7FFFFFFF, 5, 0x7FFFFFF0, 9};
        if (i % 10 == 8)
            from = Rect{0x7FFFFFF0, -3, static_cast<int32_t>(0x80000005), 7};
        raster.answer = static_cast<uint8_t>(i % 4 != 2);
        Mixed(addr::GZDrawContext_Blt, c, static_cast<void*>(&source), &from, &to);
        MixContext(c);
        Mixed(addr::GZDrawContext_TiledBlt, c, static_cast<void*>(&source), &from, &to,
                     static_cast<int32_t>(random.Below(41)) - 20, i % 10 == 9 ? 0x7FFFFFFF : -i);
        MixContext(c);
        Drawn(raster, addr::GZDrawContext_FillRect, c, &to);
        Drawn(raster, addr::GZDrawContext_FrameRect, c, from.left, from.top, i % 10 == 9 ? 0x7FFFFFFF : from.right,
                     i % 10 == 9 ? 16777217 : from.bottom);
        MixContext(c);
    }
    RsStage("context-draws");

    // The shapes through Draw and DrawShape: rectangles and lines inside,
    // across and outside clips of several sizes; ends on pixel centres and
    // halves (the rounding and the lines' end rule); every octant, single
    // pixels, steep and shallow, entering and leaving each side; NaN, huge
    // and negative points; odd counts; the rasterizer saying 0, 2 and 7; the
    // span vector grown past its first allocations.
    const Rect clips[] = {{0, 0, 120, 90}, {10, 7, 37, 61}, {-5, -5, 3, 200}, {40, 40, 41, 41}, {0, 0, 0, 0}};
    const float specials[] = {0.5f, 1.5f, 2.0f, 2.49f, 2.51f, -0.5f, 7.75f, 1e6f, -1e6f, FloatOf(0x7FC00000),
                              FloatOf(0xFF800000), 119.5f, 89.5f, 0.0f};
    for (int round = 0; round < 60; ++round) {
        At<Rect>(c, 0x34) = clips[round % 5];
        const int32_t o[2] = {round % 3 - 1, (round % 4) * 2 - 3};
        MixedIs(o, addr::GZDrawContext_SetOrigin, c, o);
        raster.how = round % 11 == 5 ? 0 : round % 13 == 6 ? 7 : 2;
        float points[32];
        for (int i = 0; i < 32; ++i) {
            const uint32_t pick = random.Below(10);
            points[i] = pick == 0   ? specials[random.Below(14)]
                        : pick == 1 ? static_cast<float>(static_cast<int32_t>(random.Below(140)) - 10) + 0.5f
                                    : static_cast<float>(static_cast<int32_t>(random.Below(1600)) - 200) / 8.0f;
        }
        const int32_t count = round % 7 == 3 ? 3 : 2 * static_cast<int32_t>(1 + random.Below(6));
        Drawn(raster, addr::GZDrawContext_Draw, c, uint32_t{3}, static_cast<const float*>(points), count);
        MixContext(c);
        Drawn(raster, addr::GZDrawContext_Draw, c, uint32_t{1}, static_cast<const float*>(points), count);
        MixContext(c);
        Drawn(raster, addr::GZDrawContext_DrawShape, c, uint32_t{1}, static_cast<const float*>(points + 4), count);
        Drawn(raster, addr::GZDrawContext_DrawShape, c, uint32_t{3}, static_cast<const float*>(points + 2), count);
        Mix(Method<uint32_t>(addr::GZDrawContext_DrawShape, c, uint32_t{9}, static_cast<const float*>(points), 2));
        Mix(Method<uint32_t>(addr::GZDrawContext_Draw, c, 0xFFFFFFFFu, static_cast<const float*>(points), 2));
        MixContext(c);
    }
    // Lines from a centre to every direction, ends on centres and halves,
    // through a small clip and a large one; then one long line per row.
    for (const Rect& clip : {Rect{20, 20, 60, 50}, Rect{0, 0, 120, 90}}) {
        At<Rect>(c, 0x34) = clip;
        const int32_t zero[2] = {0, 0};
        MixedIs(zero, addr::GZDrawContext_SetOrigin, c, zero);
        raster.how = 2;
        for (int angle = 0; angle < 48; ++angle) {
            const float r = 10.0f + static_cast<float>(angle % 5) * 13.0f;
            const float dx = static_cast<float>(std::cos(angle * 0.1309)) * r;
            const float dy = static_cast<float>(std::sin(angle * 0.1309)) * r;
            const float half = angle % 2 ? 0.5f : 0.0f;
            const float line[4] = {40.0f + half, 35.0f, 40.0f + dx + half, 35.0f + dy};
            Drawn(raster, addr::GZDrawContext_Draw, c, uint32_t{1}, line, 2);
            const float back[4] = {line[2], line[3], line[0], line[1]};
            Drawn(raster, addr::GZDrawContext_Draw, c, uint32_t{1}, back, 2);
        }
        float many[160];
        for (int i = 0; i < 40; ++i) {
            many[4 * i] = -15.0f + static_cast<float>(i);
            many[4 * i + 1] = static_cast<float>(i) * 2.25f;
            many[4 * i + 2] = 140.0f - static_cast<float>(i) * 3.0f;
            many[4 * i + 3] = 95.0f - static_cast<float>(i);
        }
        Drawn(raster, addr::GZDrawContext_Draw, c, uint32_t{1}, static_cast<const float*>(many), 80);
        Drawn(raster, addr::GZDrawContext_Draw, c, uint32_t{3}, static_cast<const float*>(many), 80);
        MixContext(c);
    }
    At<Rect>(c, 0x34) = Rect{0, 0, 120, 90};
    RsStage("context-shapes");
    // The batch count past zero, the rasterizer's own slots, the target.
    for (int i = 0; i < 6; ++i) {
        Mix(Method<uint32_t>(addr::GZDrawContext_IsBatching, c));
        Mix(Method<uint32_t>(addr::GZDrawContext_BeginBatch, c));
    }
    for (int i = 0; i < 8; ++i) {
        Mix(Method<uint32_t>(addr::GZDrawContext_EndBatch, c));
        Mix(Method<uint32_t>(addr::GZDrawContext_IsBatching, c));
    }
    Mix(Method<uint32_t>(addr::GZDrawContext_Slot26, c));
    Mix(Method<uint32_t>(addr::GZDrawContext_Slot40, c));
    uint32_t surface[6] = {};
    Mix(Method<uint32_t>(addr::GZDrawContext_GetSurface, c, surface, uint32_t{1}));
    for (const uint32_t word : surface)
        Mix(word);
    Mix(Method<uint32_t>(addr::GZDrawContext_Slot47, c));
    Mix(Method<uint32_t>(addr::GZDrawContext_HasTarget, c));
    At<void*>(c, 0x24) = nullptr;
    Mix(Method<uint32_t>(addr::GZDrawContext_HasTarget, c));
    At<void*>(c, 0x24) = &target;
    MixContext(c);
    RsStage("context-batch");

    // SetTarget: the target held (nothing happens; the answer is the
    // caller's EAX), then none, with and without a rasterizer. A new target
    // would make a real rasterizer through the framework, so that path is
    // left to the game runs.
    Method<uint32_t>(addr::GZDrawContext_SetTarget, c, static_cast<void*>(&target));
    MixObject(At<void*>(c, 0x24));
    MixObject(At<void*>(c, 0x28));
    MixContext(c);
    for (int pass = 0; pass < 2; ++pass) {
        Mixed(addr::GZDrawContext_SetTarget, c, static_cast<void*>(nullptr));
        MixObject(At<void*>(c, 0x24));
        MixObject(At<void*>(c, 0x28));
        MixContext(c);
        At<void*>(c, 0x24) = &target;
    }
    At<void*>(c, 0x28) = &raster;
    RsStage("context-target");

    // References: up twice, down three times (the last one destroys it,
    // releasing the rasterizer and the target).
    Mix(Method<uint32_t>(addr::GZDrawContext_AddRef, c));
    Mix(Method<uint32_t>(addr::GZDrawContext_AddRef, c));
    Mix(Method<uint32_t>(addr::GZDrawContext_Release, c));
    Mix(Method<uint32_t>(addr::GZDrawContext_Release, c));
    Mix(Method<uint32_t>(addr::GZDrawContext_Release, c));
    Mix(raster.calls);
    Mix(target.calls);
    g_mocks.clear();
    RsStage("context-life");
}

// ====================================================================================
// The rasterizer
//
// Its methods by address on a rasterizer of the game's constructor over
// memory filled with a pattern, over recording buffers (the target and a
// source, cIGZBuffer mocks whose bits are made-up numbers) and a recording
// span renderer put in place of the one Attach made. Every call is made
// with a known EAX, so the answers that are the caller's EAX are checked
// too; every answer goes into the hash (pointers as whether they are the
// one expected), and so does the rasterizer's whole state after each call
// (its pointers as which mock they are). Then Make, and a drawing through a
// context of the game's onto real pixels.

// A call with EAX set first: some of the rasterizer's answers are the
// caller's EAX.
uint32_t g_eaxIn = 0;
std::uintptr_t g_jumpTo = 0;

__declspec(naked) void EaxJump()
{
    __asm mov eax, g_eaxIn
    __asm jmp dword ptr [g_jumpTo]
}

// Called through a volatile pointer: the compiler takes a naked function
// for one that never returns (its body ends in assembly), and would drop
// everything after a call it can see.
void* volatile g_eaxJump = reinterpret_cast<void*>(&EaxJump);

template <typename... A>
uint32_t WithEax(uint32_t eax, std::uintptr_t address, void* self, A... args)
{
    g_eaxIn = eax;
    g_jumpTo = address;
    using Fn = uint32_t(__fastcall*)(void*, void*, A...);
    return reinterpret_cast<Fn>(g_eaxJump)(self, nullptr, args...);
}

struct Buffer {
    void** vtable;
    uint32_t id;
    uint32_t calls = 0;
    int32_t references = 1;
    uint32_t width = 0, height = 0, format = 9, pixelBytes = 4;
    uint32_t bits = 0, stride = 0;
    Rect rect{};
    uint8_t queried = 1; // whether it has the buffer interface
    uint8_t locks = 1;   // its answer to Lock
    uint8_t keyed = 0;
    uint32_t key = 0;
};

void BRec(Buffer* b, uint32_t slot)
{
    Mix(b->id);
    Mix(slot);
    ++b->calls;
}

uint32_t BAnswer(Buffer* b, uint32_t slot) { return b->id << 16 ^ slot << 8 ^ b->calls * 0x9E3779B1u; }

uint32_t __fastcall BQuery(Buffer* b, void*, uint32_t iid, void** out)
{
    BRec(b, 0);
    Mix(iid);
    if (b->queried && iid == 0x20732180u) {
        *out = b;
        ++b->references;
        return 0x5A5A5A01u;
    }
    return 0x5A5A5A00u;
}

uint32_t __fastcall BAddRef(Buffer* b, void*)
{
    BRec(b, 1);
    return static_cast<uint32_t>(++b->references);
}

uint32_t __fastcall BRelease(Buffer* b, void*)
{
    BRec(b, 2);
    --b->references;
    return BAnswer(b, 2);
}

uint32_t __fastcall BLock(Buffer* b, void*, uint32_t flags)
{
    BRec(b, 6);
    Mix(flags);
    return (BAnswer(b, 6) & ~0xFFu) | b->locks;
}

uint32_t __fastcall BUnlock(Buffer* b, void*, uint32_t flags)
{
    BRec(b, 7);
    Mix(flags);
    return BAnswer(b, 7);
}

template <int Slot, uint32_t Buffer::*Field>
uint32_t __fastcall BWord(Buffer* b, void*)
{
    BRec(b, Slot);
    return b->*Field;
}

uint32_t __fastcall BRect(Buffer* b, void*, Rect* out)
{
    BRec(b, 11);
    *out = b->rect;
    return BAnswer(b, 11);
}

const Rect* __fastcall BRectOf(Buffer* b, void*)
{
    BRec(b, 12);
    return &b->rect;
}

uint32_t* __fastcall BFormat(Buffer* b, void*, uint32_t* out)
{
    BRec(b, 15);
    out[0] = b->format;
    out[1] = b->format * 8; // the game's buffers fill a second word
    return out;
}

uint32_t __fastcall BKey(Buffer* b, void*, uint32_t* out)
{
    BRec(b, 26);
    *out = b->key;
    return (BAnswer(b, 26) & ~0xFFu) | b->keyed;
}

uint32_t __fastcall BNative(Buffer* b, void*, uint32_t r, uint32_t g, uint32_t bl)
{
    BRec(b, 30);
    Mix(r);
    Mix(g);
    Mix(bl);
    return 0x5A000000 | (r & 0xFF) << 16 | (g & 0xFF) << 8 | (bl & 0xFF);
}

// The span renderer: what it is handed. The surfaces' six words, the
// pattern's words but those DrawSpans leaves unset (the shifts and steps of
// a plain copy, the steps of a part's), the key's byte.
struct Renderer {
    void** vtable;
    uint32_t id;
    uint32_t calls = 0;
};

void RRec(Renderer* r, uint32_t slot, const uint32_t* spans, int32_t count, const uint32_t* surface)
{
    Mix(r->id);
    Mix(slot);
    ++r->calls;
    Mix(static_cast<uint32_t>(count));
    for (int32_t i = 0; i < 2 * count && i < 16; ++i)
        Mix(spans[i]);
    for (int i = 0; i < 6; ++i)
        Mix(surface[i]);
}

uint32_t __fastcall RDelete(Renderer* r, void*, uint32_t flags)
{
    Mix(r->id);
    Mix(0xDE00 | flags);
    return 0x0DE1E7E0u;
}

uint32_t __fastcall RFill(Renderer* r, void*, const uint32_t* spans, int32_t count, const uint32_t* surface,
                          uint32_t pixel)
{
    RRec(r, 1, spans, count, surface);
    Mix(pixel);
    return 0x1000 + r->calls;
}

template <int Slot>
uint32_t __fastcall RGradient(Renderer* r, void*, const uint32_t* spans, int32_t count, const uint32_t* surface,
                              const int32_t* gradient)
{
    RRec(r, Slot, spans, count, surface);
    for (int i = 0; i < 12; ++i)
        Mix(static_cast<uint32_t>(gradient[i]));
    return 0x2000 + r->calls;
}

uint32_t __fastcall RBlend(Renderer* r, void*, const uint32_t* spans, int32_t count, const uint32_t* surface,
                           uint32_t pixel, uint32_t colour)
{
    RRec(r, 3, spans, count, surface);
    Mix(pixel);
    Mix(colour);
    return 0x3000 + r->calls;
}

uint32_t __fastcall RCopy(Renderer* r, void*, const uint32_t* spans, int32_t count, const uint32_t* target,
                          const uint32_t* source, const uint32_t* pattern, uint32_t op)
{
    RRec(r, 5, spans, count, target);
    for (int i = 0; i < 6; ++i)
        Mix(source[i]);
    Mix(op);
    for (int i = (op & 0xC) ? 0 : 4; i < 7; ++i)
        if (i < 2 || i >= 4 || (op & 4))
            Mix(pattern[i]);
    MixByte(static_cast<uint8_t>(pattern[7]));
    return 0x5000 + r->calls;
}

struct RasterTables {
    void* buffer[48];
    void* renderer[6];
    RasterTables()
    {
        FillTraps(buffer, std::make_index_sequence<48>{});
        buffer[0] = V(&BQuery);
        buffer[1] = V(&BAddRef);
        buffer[2] = V(&BRelease);
        buffer[6] = V(&BLock);
        buffer[7] = V(&BUnlock);
        buffer[9] = V(&BWord<9, &Buffer::width>);
        buffer[10] = V(&BWord<10, &Buffer::height>);
        buffer[11] = V(&BRect);
        buffer[12] = V(&BRectOf);
        buffer[15] = V(&BFormat);
        buffer[17] = V(&BWord<17, &Buffer::pixelBytes>);
        buffer[26] = V(&BKey);
        buffer[30] = V(&BNative);
        buffer[34] = V(&BWord<34, &Buffer::bits>);
        buffer[35] = V(&BWord<35, &Buffer::stride>);
        renderer[0] = V(&RDelete);
        renderer[1] = V(&RFill);
        renderer[2] = V(&RGradient<2>);
        renderer[3] = V(&RBlend);
        renderer[4] = V(&RGradient<4>);
        renderer[5] = V(&RCopy);
    }
};

RasterTables& RasterTablesOf()
{
    static RasterTables tables;
    return tables;
}

std::vector<std::pair<const void*, uint32_t>> g_known;

void MixWho(const void* p)
{
    for (const auto& [object, id] : g_known)
        if (object == p) {
            Mix(id);
            return;
        }
    Mix(p ? 0xBADu : 0u);
}

// The rasterizer's state: the pointers (+0x08 the target, +0x24 the source,
// +0xB0 the renderer) as which object they are, or a game's renderer by its
// vtable.
void MixRaster(const uint8_t* r)
{
    MixByte(At<void*>(const_cast<uint8_t*>(r), 0) == reinterpret_cast<void*>(addr::GZRasterizer_vftable));
    for (uint32_t at = 0x04; at < 0xD4; ++at) {
        if (at == 0x08 || at == 0x24 || at == 0xB0) {
            const void* p = *reinterpret_cast<void* const*>(r + at);
            bool known = !p;
            for (const auto& entry : g_known)
                known = known || entry.first == p;
            if (at == 0xB0 && !known)
                Mix(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*static_cast<void* const*>(p))));
            else
                MixWho(p);
            at += 3;
            continue;
        }
        MixByte(r[at]);
    }
}

uint32_t g_eaxNext = 0x13579BDFu;

// A rasterizer method with a new EAX; its answer into the hash.
template <typename... A>
void Rm(std::uintptr_t address, uint8_t* r, A... args)
{
    g_eaxNext = g_eaxNext * 1664525u + 1013904223u;
    Mix(WithEax(g_eaxNext, address, r, args...));
}

void DriveRasterizerMethods()
{
    Random random{0x52415336u};
    RasterTables& t = RasterTablesOf();
    Buffer target{t.buffer, 0x7A};
    target.width = 200;
    target.height = 150;
    target.bits = 0x10000000;
    target.stride = 832;
    target.rect = Rect{0, 0, 200, 150};
    Buffer source{t.buffer, 0x50};
    source.width = 37;
    source.height = 23;
    source.bits = 0x20000040;
    source.stride = 160;
    source.rect = Rect{0, 0, 37, 23};
    Renderer renderer{t.renderer, 0x3E};
    g_known = {{&target, target.id}, {&source, source.id}, {&renderer, renderer.id}};

    uint8_t* const r = static_cast<uint8_t*>(
        reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0xD4));
    std::memset(r, 0x6B, 0xD4);
    g_eaxIn = 0x2468ACE0u;
    MixByte(WithEax(0x2468ACE0u, addr::GZRasterizer_GZRasterizer, r) ==
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r)));
    Mix(reinterpret_cast<uint32_t(__fastcall*)(void*, void*)>((*reinterpret_cast<void***>(r))[1])(r, nullptr));
    MixRaster(r);
    RsStage("rasterizer-made");

    // Attach: refused by the query, by the lock, by the format; every
    // format; Detach with and without anything held.
    Rm(addr::GZRasterizer_Detach, r);
    MixRaster(r);
    target.queried = 0;
    Rm(addr::GZRasterizer_Attach, r, static_cast<void*>(&target));
    MixRaster(r);
    target.queried = 1;
    target.locks = 0;
    Rm(addr::GZRasterizer_Attach, r, static_cast<void*>(&target));
    MixRaster(r);
    target.locks = 1;
    for (const uint32_t format : {3u, 0u, 1u, 10u, 2u, 4u, 5u, 6u, 7u, 8u, 9u}) {
        target.format = format;
        Rm(addr::GZRasterizer_Attach, r, static_cast<void*>(&target));
        MixRaster(r);
    }
    Rm(addr::GZRasterizer_Detach, r);
    MixRaster(r);
    Rm(addr::GZRasterizer_Detach, r);
    target.format = 9;
    Rm(addr::GZRasterizer_Attach, r, static_cast<void*>(&target));
    // The game's renderer deleted and ours put in its place.
    if (void* const made = At<void*>(r, 0xB0))
        reinterpret_cast<void(__fastcall*)(void*, void*, uint32_t)>((*static_cast<void***>(made))[0])(made, nullptr,
                                                                                                    1);
    At<void*>(r, 0xB0) = &renderer;
    MixRaster(r);
    RsStage("rasterizer-attach");

    // The setters, the target's lock, the surface.
    for (int round = 0; round < 12; ++round) {
        const Rect clip = RandomRect(random, -40, 220);
        Rm(addr::GZRasterizer_SetClip, r, &clip);
        uint32_t colours[4];
        for (uint32_t& colour : colours)
            colour = random.Next();
        const int32_t first = static_cast<int32_t>(random.Below(4));
        Rm(addr::GZRasterizer_SetColours, r, first, static_cast<int32_t>(random.Below(5 - first)),
           static_cast<const uint32_t*>(colours));
        const uint32_t modes[] = {0, 1, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 0x100u, 0x1FFu, 2};
        Rm(addr::GZRasterizer_SetMode, r, modes[round % 8]);
        Rm(addr::GZRasterizer_SetFlag45, r, round % 3 == 0 ? 0x1200u : 0x34501u);
        Rm(addr::GZRasterizer_SetFlag44, r, round % 2 ? 0u : 0xFFFFFF01u);
        Rm(addr::GZRasterizer_UnlockTarget, r);
        Rm(addr::GZRasterizer_UnlockTarget, r);
        Rm(addr::GZRasterizer_LockTarget, r);
        Rm(addr::GZRasterizer_LockTarget, r);
        uint32_t surface[6] = {};
        g_eaxNext = g_eaxNext * 1664525u + 1013904223u;
        const uint32_t answer = WithEax(g_eaxNext, addr::GZRasterizer_GetSurface, r, surface, uint32_t{7});
        MixByte(answer == ((static_cast<uint32_t>(reinterpret_cast<uintptr_t>(surface)) & ~0xFFu) | 1));
        for (const uint32_t word : surface)
            Mix(word);
        MixRaster(r);
    }
    RsStage("rasterizer-setters");

    // The gradient: none, and planes with halves, NaN, huge and negative
    // values at several points.
    Rm(addr::GZRasterizer_SetGradient, r, static_cast<const float*>(nullptr), 0.0f, 0.0f);
    MixRaster(r);
    const float specials[] = {0.5f, 1.0f, -1.0f, 0.00196078f, 0.0019608f, 1e9f, -1e9f, FloatOf(0x7FC00000),
                              FloatOf(0x7F800000), 0.0f, 128.5f / 255.0f, -0.25f};
    for (int round = 0; round < 10; ++round) {
        float planes[12];
        for (float& p : planes)
            p = random.Below(5) == 0 ? specials[random.Below(12)]
                                     : static_cast<float>(static_cast<int32_t>(random.Below(2000)) - 1000) / 997.0f;
        const float x = round % 3 == 0 ? 0.0f : static_cast<float>(random.Below(400)) / 4.0f - 30.0f;
        const float y = round % 4 == 0 ? 0.5f : static_cast<float>(random.Below(400)) / 8.0f;
        Rm(addr::GZRasterizer_SetGradient, r, static_cast<const float*>(planes), x, y);
        MixRaster(r);
    }
    RsStage("rasterizer-gradient");

    // The source: none with nothing held, refused, unscaled at whole numbers
    // and halves, scaled by numbers that do not divide 65536, at points near
    // the rounding's edges; parts.
    Rect out{};
    Rm(addr::GZRasterizer_SetSource, r, static_cast<void*>(nullptr), 0.0f, 0.0f, 1.0f, 1.0f, &out);
    MixRaster(r);
    source.queried = 0;
    Rm(addr::GZRasterizer_SetSource, r, static_cast<void*>(&source), 3.0f, 4.0f, 1.0f, 1.0f, &out);
    MixRaster(r);
    source.queried = 1;
    const float scales[] = {1.0f, 1.0f, 2.0f, 0.5f, 3.0f, 1.0f / 3.0f, 1.5f, 0.999f, 1.0000001f, 7.25f, 0.1f, 1.0f};
    const float places[] = {0.0f, 0.5f, 1.5f, 2.5f, -0.5f, -1.5f, 10.4999f, 10.5f, 33.75f, -7.25f, 100.0f, 1e7f};
    for (int round = 0; round < 36; ++round) {
        const float sx = scales[round % 12], sy = scales[(round * 5 + 1) % 12];
        const float x = places[round % 12], y = places[(round * 7 + 3) % 12];
        source.rect = RandomRect(random, -10, 50);
        out = Rect{0x11, 0x22, 0x33, 0x44};
        Rm(addr::GZRasterizer_SetSource, r, static_cast<void*>(&source), x, y, round % 9 == 8 ? 1.0f : sx,
           round % 9 == 8 ? 1.0f : sy, &out);
        MixRect(&out);
        MixRaster(r);
        Rm(addr::GZRasterizer_UseWholeSource, r);
        const Rect part = RandomRect(random, 0, 30);
        Rm(addr::GZRasterizer_UseSourcePart, r, &part);
        MixRaster(r);
    }
    Rm(addr::GZRasterizer_SetSource, r, static_cast<void*>(nullptr), 0.0f, 0.0f, 0.0f, 0.0f, &out);
    MixRaster(r);
    RsStage("rasterizer-source");

    // How to draw: the flag clear, then every kind that does not reach the
    // polygons (which never ran).
    for (const uint32_t flag : {0u, 1u}) {
        Rm(addr::GZRasterizer_SetFlag44, r, flag);
        const float points[8] = {};
        for (uint32_t kind = 0; kind < 12; ++kind)
            for (const int32_t count : {0, 1, 2, -3})
                if (kind != 8 || count < 3)
                    Rm(addr::GZRasterizer_HowToDraw, r, kind, static_cast<const float*>(points), count);
        Rm(addr::GZRasterizer_HowToDraw, r, uint32_t{7}, static_cast<const float*>(points), int32_t{3});
    }
    RsStage("rasterizer-how");

    // The spans: fills with and without a gradient and blending; copies from
    // every format, keyed or not, refused by the lock, whole, a part and
    // scaled, with each op.
    std::vector<uint32_t> spans;
    for (uint32_t y = 0; y < 6; ++y) {
        spans.push_back(y << 16 | (y * 3));
        spans.push_back(y << 16 | (y * 3 + 9));
    }
    const float planes[12] = {0.1f, 0.2f, 0.3f, 0.4f, 0.01f, -0.01f, 0.02f, 0.0f, 0.0f, 0.005f, -0.003f, 0.5f};
    for (int round = 0; round < 8; ++round) {
        Rm(addr::GZRasterizer_SetMode, r, uint32_t{round % 2 ? 1u : 0u});
        Rm(addr::GZRasterizer_SetFlag45, r, uint32_t{round % 4 >= 2 ? 1u : 0u});
        if (round >= 4)
            Rm(addr::GZRasterizer_SetGradient, r, static_cast<const float*>(planes), 1.5f, 2.0f);
        else
            Rm(addr::GZRasterizer_SetGradient, r, static_cast<const float*>(nullptr), 0.0f, 0.0f);
        Rm(addr::GZRasterizer_DrawSpans, r, static_cast<const uint32_t*>(spans.data()), int32_t{6});
        MixRaster(r);
    }
    Rm(addr::GZRasterizer_SetGradient, r, static_cast<const float*>(nullptr), 0.0f, 0.0f);
    for (int round = 0; round < 48; ++round) {
        source.format = round < 12 ? static_cast<uint32_t>(round) : 4 + static_cast<uint32_t>(round % 6);
        source.keyed = static_cast<uint8_t>(round % 5 != 0);
        source.key = random.Next();
        source.locks = static_cast<uint8_t>(round % 17 != 16);
        source.rect = Rect{0, 0, 37, 23};
        const float scale = round % 3 == 2 ? 1.5f : 1.0f;
        Rm(addr::GZRasterizer_SetSource, r, static_cast<void*>(&source), static_cast<float>(round % 7) - 2.5f,
           static_cast<float>(round % 5), scale, scale, &out);
        if (round % 4 == 1) {
            const Rect part{1 + round % 3, 2, 9 + round % 5, 7 + round % 2};
            Rm(addr::GZRasterizer_UseSourcePart, r, &part);
        } else {
            Rm(addr::GZRasterizer_UseWholeSource, r);
        }
        Rm(addr::GZRasterizer_SetMode, r, uint32_t{round % 2 ? 1u : 0u});
        Rm(addr::GZRasterizer_SetFlag45, r, uint32_t{round % 8 >= 4 ? 1u : 0u});
        Rm(addr::GZRasterizer_DrawSpans, r, static_cast<const uint32_t*>(spans.data()), int32_t{6});
        MixRaster(r);
    }
    Rm(addr::GZRasterizer_SetSource, r, static_cast<void*>(nullptr), 0.0f, 0.0f, 0.0f, 0.0f, &out);
    RsStage("rasterizer-spans");

    // References: up once, down twice (the last one deletes the renderer,
    // releases the target and frees it).
    Mix(reinterpret_cast<uint32_t(__fastcall*)(void*, void*)>((*reinterpret_cast<void***>(r))[1])(r, nullptr));
    Rm(addr::GZRasterizer_Release, r);
    MixRaster(r);
    Rm(addr::GZRasterizer_Release, r);
    Mix(static_cast<uint32_t>(target.references));
    Mix(static_cast<uint32_t>(source.references));
    Mix(target.calls);
    Mix(source.calls);
    Mix(renderer.calls);

    // Make: the target refusing the interface, then accepting it; the
    // rasterizer made released at once.
    for (const uint8_t queried : {0, 1}) {
        target.queried = queried;
        void* made = reinterpret_cast<void*>(0x1234);
        g_eaxIn = 0x0F0F0F0Fu;
        g_jumpTo = addr::GZRasterizer_Make;
        Mix(reinterpret_cast<uint32_t(__stdcall*)(void*, void**)>(g_eaxJump)(&target, &made));
        if (queried) {
            uint8_t* const m = static_cast<uint8_t*>(made);
            MixByte(At<void*>(m, 0) == reinterpret_cast<void*>(addr::GZRasterizer_vftable));
            Mix(At<uint32_t>(m, 0x04));
            for (const uint32_t at : {0x08u, 0x24u, 0xB0u})
                MixWho(At<void*>(m, at));
            for (const uint32_t at : {0x41u, 0x43u, 0x74u, 0x75u})
                MixByte(m[at]);
            Rm(addr::GZRasterizer_Release, m);
        } else {
            MixByte(made == reinterpret_cast<void*>(0x1234));
        }
    }
    Mix(static_cast<uint32_t>(target.references));
    g_known.clear();
    RsStage("rasterizer-life");
}

// A context of the game's, its vtable the game's own, given a target buffer
// over real pixels through SetTarget (so a real rasterizer from the
// framework and the game's 32-bit span renderer), drawing rectangles, frames
// and blits from a source picture; the target's pixels and every call on the
// buffers go into the hash. Only what ran in the game: no blending, no
// gradient, a source at scale 1, whole or a part.
bool DriveRasterDraw()
{
    Random random{0x52415337u};
    RasterTables& t = RasterTablesOf();
    Canvas pixels(64, 48, 8), picture(13, 9, 4);
    pixels.Fill(random);
    picture.Fill(random);
    Buffer target{t.buffer, 0x7B};
    target.width = pixels.width;
    target.height = pixels.height;
    target.bits = pixels.Bits();
    target.stride = pixels.stride;
    target.rect = Rect{0, 0, 64, 48};
    Buffer source{t.buffer, 0x51};
    source.width = picture.width;
    source.height = picture.height;
    source.bits = picture.Bits();
    source.stride = picture.stride;
    source.rect = Rect{0, 0, 13, 9};
    g_known = {{&target, target.id}, {&source, source.id}};

    // What the framework would make for this target; only a rasterizer of
    // this class is driven.
    void* factory = nullptr;
    if (void* const framework = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetFramework)())
        reinterpret_cast<uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t, void**)>(
            (*static_cast<void***>(framework))[5])(framework, nullptr, 0xEB2EA3CBu, 0x4B2EA3B3u, &factory);
    void* probe = nullptr;
    if (factory)
        reinterpret_cast<uint32_t(__fastcall*)(void*, void*, void*, void**)>((*static_cast<void***>(factory))[4])(
            factory, nullptr, &target, &probe);
    const bool ours = probe && *static_cast<void**>(probe) == reinterpret_cast<void*>(addr::GZRasterizer_vftable);
    if (probe)
        reinterpret_cast<uint32_t(__fastcall*)(void*, void*)>((*static_cast<void***>(probe))[2])(probe, nullptr);
    if (factory)
        reinterpret_cast<uint32_t(__fastcall*)(void*, void*)>((*static_cast<void***>(factory))[2])(factory, nullptr);
    if (!ours) {
        log::Write("autotest: raster: the framework made no rasterizer of 0x00AC17D0 for the test buffer");
        g_known.clear();
        RsStage("draw-skipped");
        return false;
    }

    uint8_t* const c = static_cast<uint8_t*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(200));
    std::memset(c, 0xC3, 200);
    reinterpret_cast<void*(__fastcall*)(void*, void*)>(addr::GZDrawContext_GZDrawContext)(c, nullptr);
    Method<uint32_t>(addr::GZDrawContext_AddRef, c);
    Mix(Method<uint32_t>(addr::GZDrawContext_SetTarget, c, static_cast<void*>(&target)));
    MixContext(c);
    for (int i = 0; i < 4; ++i)
        Mixed(addr::GZDrawContext_SetColour, c, i, random.Next() & 0xFFFFFF);
    RsStage("draw-target");

    for (int round = 0; round < 24; ++round) {
        const int32_t origin[2] = {round % 5 - 2, round % 3 - 1};
        Method<uint32_t>(addr::GZDrawContext_SetOrigin, c, origin);
        if (round % 6 == 5)
            Mixed(addr::GZDrawContext_ResetClip, c);
        else if (round % 6 == 2) {
            const Rect clip = RandomRect(random, -5, 60);
            Mixed(addr::GZDrawContext_ClipTo, c, &clip);
        }
        Mixed(addr::GZDrawContext_SetForeColour, c, random.Next() & 0xFFFFFF);
        const Rect a = RandomRect(random, -10, 70), b = RandomRect(random, -10, 70);
        Method<uint32_t>(addr::GZDrawContext_FillRect, c, &a);
        Method<uint32_t>(addr::GZDrawContext_FrameRect, c, b.left, b.top, b.right, b.bottom);
        const Rect from{static_cast<int32_t>(random.Below(4)), static_cast<int32_t>(random.Below(3)),
                        8 + static_cast<int32_t>(random.Below(6)), 5 + static_cast<int32_t>(random.Below(5))};
        const int32_t x = static_cast<int32_t>(random.Below(60)) - 5, y = static_cast<int32_t>(random.Below(44)) - 5;
        const Rect same{x, y, x + from.right - from.left, y + from.bottom - from.top};
        Mixed(addr::GZDrawContext_Blt, c, static_cast<void*>(&source), &from, &same);
        const Rect wide = RandomRect(random, -5, 60);
        Mixed(addr::GZDrawContext_TiledBlt, c, static_cast<void*>(&source), &from, &wide,
              static_cast<int32_t>(random.Below(9)) - 4, static_cast<int32_t>(random.Below(9)) - 4);
        if (round % 4 == 3) {
            Mixed(addr::GZDrawContext_BeginBatch, c);
            Method<uint32_t>(addr::GZDrawContext_FillClip, c);
            Mixed(addr::GZDrawContext_EndBatch, c);
        }
        MixContext(c);
        MixBytes(pixels.bytes);
    }
    uint32_t surface[6] = {};
    const uint32_t answer = Method<uint32_t>(addr::GZDrawContext_GetSurface, c, surface, uint32_t{1});
    MixByte(answer == ((static_cast<uint32_t>(reinterpret_cast<uintptr_t>(surface)) & ~0xFFu) | 1));
    Mix(surface[0] - pixels.Bits());
    for (int i = 1; i < 6; ++i)
        Mix(surface[i]);
    RsStage("draw-pixels");

    // A second target, then none, then the context let go.
    Mix(Method<uint32_t>(addr::GZDrawContext_SetTarget, c, static_cast<void*>(&source)));
    MixContext(c);
    Mix(Method<uint32_t>(addr::GZDrawContext_SetTarget, c, static_cast<void*>(nullptr)));
    MixContext(c);
    Mix(Method<uint32_t>(addr::GZDrawContext_Release, c));
    Mix(static_cast<uint32_t>(target.references));
    Mix(static_cast<uint32_t>(source.references));
    Mix(target.calls);
    Mix(source.calls);
    MixBytes(picture.bytes);
    g_known.clear();
    RsStage("draw-life");
    return true;
}

// ====================================================================================
// The buffer
//
// The buffer with a drawing context (vtable 0x00AC1550): its methods by
// address, each with a known EAX, on buffers of the game's constructor over
// memory filled with a pattern, over a recording context and plain buffer
// (the side's SetContext hands them over). The context's SetTarget and the
// plain buffer's slots the buffer jumps to answer the EAX they were reached
// with, so the methods that hand on such answers are checked too; answers
// that are pointers go in as which object they are. After each call the
// buffer's state goes into the hash. Then a buffer drawing through a
// context of the game's (and a real rasterizer) onto real pixels.

struct Recorder {
    void** vtable;
    uint32_t id;
    uint32_t calls = 0;
    int32_t references = 1;
    uint8_t hasTarget = 0; // the context's slot 16
    uint8_t batching = 0;  // its slot 41
    uint8_t has = 1;       // a colour source: whether it has the colour interface
    uint8_t ok = 1;        // and whether its slot 4 gives a colour
    uint8_t rgba[4] = {};
    uint32_t format = 9, pixelBytes = 4, key = 0;
    Rect rect{};
};

void KRec(Recorder* k, uint32_t slot)
{
    Mix(k->id);
    Mix(slot);
    ++k->calls;
}

uint32_t KAnswer(Recorder* k, uint32_t slot) { return k->id << 16 ^ slot << 8 ^ k->calls * 0x9E3779B1u; }

uint32_t Address(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// An answer, or which known object (or its table) it points at, AL apart.
std::vector<std::pair<const void*, uint32_t>> g_pointers;

void MixValue(uint32_t value)
{
    for (const auto& [p, id] : g_pointers) {
        const uint32_t at = Address(p);
        if (value == at) {
            Mix(0xC0DE0000u | id);
            return;
        }
        if ((value & ~0xFFu) == (at & ~0xFFu)) {
            Mix(0xC1DE0000u | id);
            MixByte(static_cast<uint8_t>(value));
            return;
        }
    }
    Mix(value);
}

void MixKnown(const void* p)
{
    if (!p) {
        Mix(0);
        return;
    }
    for (const auto& [object, id] : g_pointers)
        if (object == p) {
            Mix(id);
            return;
        }
    Mix(0xBADu);
}

template <int Slot>
uint32_t __fastcall KTrap(Recorder* k, void*)
{
    KRec(k, 0xEE00 + Slot);
    return 0;
}

template <int Slot>
uint32_t __fastcall KNone(Recorder* k, void*)
{
    KRec(k, Slot);
    return KAnswer(k, Slot);
}

template <int Slot>
uint32_t __fastcall KWord(Recorder* k, void*, uint32_t a)
{
    KRec(k, Slot);
    Mix(a);
    return KAnswer(k, Slot);
}

template <int Slot>
uint32_t __fastcall KRect(Recorder* k, void*, const Rect* r)
{
    KRec(k, Slot);
    MixRect(r);
    return KAnswer(k, Slot);
}

uint32_t __fastcall KAddRef(Recorder* k, void*)
{
    KRec(k, 1);
    return static_cast<uint32_t>(++k->references);
}

uint32_t __fastcall KRelease(Recorder* k, void*)
{
    KRec(k, 2);
    --k->references;
    return KAnswer(k, 2);
}

uint32_t __fastcall KHasTarget(Recorder* k, void*)
{
    KRec(k, 16);
    return (KAnswer(k, 16) & ~0xFFu) | k->hasTarget;
}

uint32_t __fastcall KIsBatching(Recorder* k, void*)
{
    KRec(k, 41);
    return (KAnswer(k, 41) & ~0xFFu) | k->batching;
}

uint32_t __fastcall KColour(Recorder* k, void*, uint32_t index, uint32_t rgb)
{
    KRec(k, 20);
    Mix(index);
    Mix(rgb);
    return KAnswer(k, 20);
}

uint32_t __fastcall KBlt(Recorder* k, void*, const void* source, const Rect* from, const Rect* to)
{
    KRec(k, 38);
    MixKnown(source);
    MixRect(from);
    MixRect(to);
    return KAnswer(k, 38);
}

uint32_t __fastcall KSurface(Recorder* k, void*, uint32_t* out, uint32_t whole)
{
    KRec(k, 46);
    Mix(whole);
    for (uint32_t i = 0; i < 6; ++i)
        out[i] = k->id * 0x1000193u + k->calls + i;
    return KAnswer(k, 46);
}

// Recorders that leave EAX as they were reached with: the context's
// SetTarget and the plain buffer's 23, 26, 27, 37 and 38.
void __fastcall KTargetSeen(Recorder* k, void*, const void* target)
{
    KRec(k, 17);
    MixKnown(target);
}

void __fastcall PWordSeen(Recorder* k, void*, uint32_t slot, uint32_t a)
{
    KRec(k, slot);
    Mix(a);
}

void __fastcall PKeySeen(Recorder* k, void*, uint32_t* out)
{
    KRec(k, 26);
    *out = k->key;
}

__declspec(naked) void KSetTarget()
{
    __asm {
        push eax
        push dword ptr [esp + 8]
        call KTargetSeen
        pop eax
        ret 4
    }
}

__declspec(naked) void PSetKey()
{
    __asm {
        push eax
        push dword ptr [esp + 8]
        push 23
        call PWordSeen
        pop eax
        ret 4
    }
}

__declspec(naked) void PGetKey()
{
    __asm {
        push eax
        push dword ptr [esp + 8]
        call PKeySeen
        pop eax
        ret 4
    }
}

__declspec(naked) void PClearKey()
{
    __asm {
        push eax
        push 0
        push 27
        call PWordSeen
        pop eax
        ret
    }
}

__declspec(naked) void PGetCache()
{
    __asm {
        push eax
        push 0
        push 37
        call PWordSeen
        pop eax
        ret
    }
}

__declspec(naked) void PSetCache()
{
    __asm {
        push eax
        push dword ptr [esp + 8]
        push 38
        call PWordSeen
        pop eax
        ret 4
    }
}

uint32_t __fastcall PQuery(Recorder* k, void*, uint32_t iid, void** out)
{
    KRec(k, 0);
    Mix(iid);
    if (iid == 0x20732180u) {
        *out = k;
        ++k->references;
        return 0x5A5A5A01u;
    }
    return 0x5A5A5A00u;
}

const Rect* __fastcall PRectOf(Recorder* k, void*)
{
    KRec(k, 12);
    return &k->rect;
}

uint32_t* __fastcall PFormat(Recorder* k, void*, uint32_t* out)
{
    KRec(k, 15);
    out[0] = k->format;
    out[1] = k->format * 8;
    return out;
}

uint32_t __fastcall PPixelBytes(Recorder* k, void*)
{
    KRec(k, 17);
    return k->pixelBytes;
}

// A source with a colour: its query for 0x86D72B57 answers itself, its slot
// 4 the four bytes.
uint32_t __fastcall CQuery(Recorder* k, void*, uint32_t iid, void** out)
{
    KRec(k, 0);
    Mix(iid);
    if (k->has && iid == 0x86D72B57u) {
        *out = k;
        ++k->references;
        return 0x77777701u;
    }
    return 0x77777700u;
}

uint32_t __fastcall CColour(Recorder* k, void*, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a)
{
    KRec(k, 4);
    if (k->ok) {
        *r = k->rgba[0];
        *g = k->rgba[1];
        *b = k->rgba[2];
        *a = k->rgba[3];
    }
    return (KAnswer(k, 4) & ~0xFFu) | k->ok;
}

struct BufferTables {
    void* context[48];
    void* plain[48];
    void* colour[8];
    BufferTables()
    {
        FillTraps(context, std::make_index_sequence<48>{});
        FillTraps(plain, std::make_index_sequence<48>{});
        FillTraps(colour, std::make_index_sequence<8>{});
        for (void** table : {context, plain, colour}) {
            table[1] = V(&KAddRef);
            table[2] = V(&KRelease);
        }
        context[3] = V(&KNone<3>);
        context[5] = V(&KRect<5>);
        context[9] = V(&KNone<9>);
        context[16] = V(&KHasTarget);
        context[17] = reinterpret_cast<void*>(&KSetTarget);
        context[20] = V(&KColour);
        context[21] = V(&KWord<21>);
        context[35] = V(&KRect<35>);
        context[38] = V(&KBlt);
        context[40] = V(&KNone<40>);
        context[41] = V(&KIsBatching);
        context[42] = V(&KNone<42>);
        context[43] = V(&KNone<43>);
        context[46] = V(&KSurface);
        plain[0] = V(&PQuery);
        plain[12] = V(&PRectOf);
        plain[15] = V(&PFormat);
        plain[17] = V(&PPixelBytes);
        plain[23] = reinterpret_cast<void*>(&PSetKey);
        plain[26] = reinterpret_cast<void*>(&PGetKey);
        plain[27] = reinterpret_cast<void*>(&PClearKey);
        plain[37] = reinterpret_cast<void*>(&PGetCache);
        plain[38] = reinterpret_cast<void*>(&PSetCache);
        colour[0] = V(&CQuery);
        colour[4] = V(&CColour);
    }
};

BufferTables& BufferTablesOf()
{
    static BufferTables tables;
    return tables;
}

// The buffer's state: its three tables as whether they are the class's,
// the counted base's count, the context and plain buffer as which they are,
// the surface's bits less `bits`, the rest byte by byte.
void MixBuffer(const uint8_t* b, uint32_t bits = 0)
{
    MixByte(At<void*>(const_cast<uint8_t*>(b), 0x00) == reinterpret_cast<void*>(addr::GZContextBuffer_vftable));
    MixByte(At<void*>(const_cast<uint8_t*>(b), 0x04) == reinterpret_cast<void*>(addr::GZContextBuffer_vftable_4));
    MixByte(At<void*>(const_cast<uint8_t*>(b), 0x0C) == reinterpret_cast<void*>(addr::GZContextBuffer_vftable_C));
    Mix(At<uint32_t>(const_cast<uint8_t*>(b), 0x08));
    MixKnown(At<void*>(const_cast<uint8_t*>(b), 0x10));
    MixKnown(At<void*>(const_cast<uint8_t*>(b), 0x14));
    for (uint32_t at = 0x18; at < 0x4C; ++at)
        if (at < 0x30 || at >= 0x34)
            MixByte(b[at]);
    Mix(At<uint32_t>(const_cast<uint8_t*>(b), 0x30) - bits);
}

// A method with a new EAX; its answer into the hash.
template <typename... A>
void Bm(std::uintptr_t address, void* self, A... args)
{
    g_eaxNext = g_eaxNext * 1664525u + 1013904223u;
    MixValue(WithEax(g_eaxNext, address, self, args...));
}

uint8_t* NewBuffer()
{
    uint8_t* const b = static_cast<uint8_t*>(
        reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x4C));
    std::memset(b, 0x6B, 0x4C);
    MixByte(WithEax(0x2468ACE0u, addr::GZContextBuffer_GZContextBuffer, b) == Address(b));
    return b;
}

void DriveBufferMethods()
{
    Random random{0x52415338u};
    BufferTables& t = BufferTablesOf();
    Recorder context{t.context, 0xC7};
    Recorder plain{t.plain, 0x91};
    Recorder plain2{t.plain, 0x92};
    Recorder colour{t.colour, 0xC5};
    plain.rect = Rect{3, 4, 203, 154};
    plain2.rect = Rect{-7, 2, 41, 30};
    plain2.format = 5;
    plain2.pixelBytes = 2;

    uint8_t* const b = NewBuffer();
    uint8_t* const side = b + 0x0C;
    g_pointers = {{b, 0xB1},          {side, 0xB2},          {&context, context.id}, {&plain, plain.id},
                  {&plain2, plain2.id}, {&colour, colour.id}, {t.context, 0xA1},      {t.plain, 0xA2},
                  {t.colour, 0xA3}};
    MixBuffer(b);
    // Before it has a plain buffer: an interface it does not have.
    void* out = nullptr;
    Bm(addr::GZContextBuffer_QueryInterface, b, 0x12345678u, &out);
    MixKnown(out);
    RsStage("buffer-made");

    // The context and the plain buffer: new, the same again, a new plain
    // buffer, then the first back.
    Bm(addr::GZContextBuffer_SetContext, side, static_cast<void*>(&context), static_cast<void*>(&plain));
    MixBuffer(b);
    Bm(addr::GZContextBuffer_SetContext, side, static_cast<void*>(&context), static_cast<void*>(&plain));
    MixBuffer(b);
    Bm(addr::GZContextBuffer_SetContext, side, static_cast<void*>(&context), static_cast<void*>(&plain2));
    MixBuffer(b);
    Bm(addr::GZContextBuffer_SetContext, side, static_cast<void*>(&context), static_cast<void*>(&plain));
    MixBuffer(b);
    Mix(static_cast<uint32_t>(context.references));
    Mix(static_cast<uint32_t>(plain.references));
    Mix(static_cast<uint32_t>(plain2.references));
    for (const uint32_t iid : {1u, 0x20732180u, 0xAB300B2Bu, 0x86D72B57u, 0x20732180u + 1}) {
        out = nullptr;
        Bm(addr::GZContextBuffer_QueryInterface, b, iid, &out);
        MixKnown(out);
    }
    RsStage("buffer-context");

    // The lock with the context with and without a target and batching,
    // various flags, nested; the side's lock and unlock, GetContext.
    static const uint32_t kFlags[] = {0x8010, 0x800, 0x8040, 0x18000, 0x7FFF8000, 0, 0xFFFFFFFF};
    for (int round = 0; round < 16; ++round) {
        context.hasTarget = static_cast<uint8_t>(round & 1);
        context.batching = static_cast<uint8_t>(round >> 1 & 1);
        const int depth = 1 + (round >> 2 & 1) + (round >> 3);
        for (int i = 0; i < depth; ++i) {
            Bm(addr::GZContextBuffer_Lock, b, kFlags[random.Below(7)]);
            Bm(addr::GZContextBuffer_IsLocked, b);
            MixBuffer(b);
        }
        for (int i = 0; i < depth; ++i) {
            Bm(addr::GZContextBuffer_Unlock, b, kFlags[random.Below(7)]);
            Bm(addr::GZContextBuffer_IsLocked, b);
            MixBuffer(b);
        }
        Bm(addr::GZContextBuffer_LockContext, side);
        MixBuffer(b);
        Bm(addr::GZContextBuffer_GetContext, side);
        Bm(addr::GZContextBuffer_UnlockContext, side);
        MixBuffer(b);
    }
    RsStage("buffer-lock");

    // Sizes, formats and colours, every format from 0 to 10.
    for (uint32_t format = 0; format <= 10; ++format) {
        plain.format = format;
        plain.pixelBytes = format < 3 ? 1 : format < 8 ? 2 : 4;
        plain.rect = Rect{static_cast<int32_t>(random.Below(20)) - 10, static_cast<int32_t>(random.Below(20)) - 10,
                          static_cast<int32_t>(random.Below(300)), static_cast<int32_t>(random.Below(300))};
        Bm(addr::GZContextBuffer_SetContext, side, static_cast<void*>(&context), static_cast<void*>(&plain));
        MixBuffer(b);
        Bm(addr::GZContextBuffer_Width, b);
        Bm(addr::GZContextBuffer_Height, b);
        Rect r{};
        MixByte(WithEax(0x11111111u, addr::GZContextBuffer_CopyRect, b, &r) == Address(&r));
        MixRect(&r);
        uint32_t record[2] = {};
        Bm(addr::GZContextBuffer_FormatAndDepth, b, record);
        Mix(record[0]);
        Mix(record[1]);
        record[0] = record[1] = 0;
        MixByte(WithEax(0x22222222u, addr::GZContextBuffer_Format, b, record) == Address(record));
        Mix(record[0]);
        Mix(record[1]);
        Bm(addr::GZContextBuffer_BitsPerPixel, b);
        for (int i = 0; i < 24; ++i) {
            const uint32_t pixel = i < 4 ? (i == 0 ? 0u : i == 1 ? 0xFFFFFFFFu : i == 2 ? 0xFFFFu : 0x8000u)
                                         : random.Next();
            Bm(addr::GZContextBuffer_ToArgb, b, pixel);
            uint8_t rgb[3] = {0x5D, 0x5D, 0x5D};
            Bm(addr::GZContextBuffer_ColourOf, b, pixel, &rgb[0], &rgb[1], &rgb[2]);
            MixByte(rgb[0]);
            MixByte(rgb[1]);
            MixByte(rgb[2]);
            Bm(addr::GZContextBuffer_PixelOf, b, random.Next(), random.Next(), random.Next());
        }
    }
    RsStage("buffer-formats");

    // FillRect and Blt with and without a clip; sources without the colour
    // interface, with it and a colour, with it and none; UseColourOf alone.
    plain.format = 9;
    plain.pixelBytes = 4;
    plain.rect = Rect{0, 0, 200, 150};
    Bm(addr::GZContextBuffer_SetContext, side, static_cast<void*>(&context), static_cast<void*>(&plain));
    for (int round = 0; round < 12; ++round) {
        const Rect r = RandomRect(random, -20, 220), clip = RandomRect(random, -20, 220);
        const Rect from = RandomRect(random, -5, 40), to = RandomRect(random, -20, 220);
        Bm(addr::GZContextBuffer_FillRect, b, random.Next(), &r, round & 1 ? &clip : nullptr);
        colour.has = static_cast<uint8_t>(round % 3 != 0);
        colour.ok = static_cast<uint8_t>(round % 3 == 1);
        for (uint8_t& byte : colour.rgba)
            byte = static_cast<uint8_t>(random.Next());
        Bm(addr::GZContextBuffer_Blt, b, static_cast<void*>(&colour), &from, &to, round & 2 ? &clip : nullptr);
        Bm(addr::GZContextBuffer_UseColourOf, side, static_cast<void*>(&colour));
        MixBuffer(b);
    }
    Mix(static_cast<uint32_t>(colour.references));
    RsStage("buffer-draws");

    // The colour key and the cache, on to the plain buffer.
    for (int round = 0; round < 4; ++round) {
        plain.key = random.Next();
        Bm(addr::GZContextBuffer_SetColourKey, b, random.Next());
        MixBuffer(b);
        uint32_t key = 0;
        Bm(addr::GZContextBuffer_GetColourKey, b, &key);
        Mix(key);
        Bm(addr::GZContextBuffer_ClearColourKey, b);
        MixBuffer(b);
        Bm(addr::GZContextBuffer_GetCache, b);
        Bm(addr::GZContextBuffer_SetCache, b, 0x5E7CAC4Eu + static_cast<uint32_t>(round));
    }
    RsStage("buffer-key");

    // Let go: the destructor without freeing (the memory is freed here),
    // then a second buffer released through its own Release (the counted
    // base's, which calls the deleting destructor).
    Bm(addr::GZContextBuffer_Destroy, b, uint32_t{0});
    MixBuffer(b);
    Mix(static_cast<uint32_t>(context.references));
    Mix(static_cast<uint32_t>(plain.references));
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(b);
    uint8_t* const second = NewBuffer();
    g_pointers.push_back({second, 0xB3});
    Bm(addr::GZContextBuffer_SetContext, second + 0x0C, static_cast<void*>(&context), static_cast<void*>(&plain2));
    MixValue(reinterpret_cast<uint32_t(__fastcall*)(void*, void*)>((*reinterpret_cast<void***>(second))[1])(second,
                                                                                                         nullptr));
    MixBuffer(second);
    Mix(reinterpret_cast<uint32_t(__fastcall*)(void*, void*)>((*reinterpret_cast<void***>(second))[2])(second,
                                                                                                    nullptr));
    Mix(static_cast<uint32_t>(context.references));
    Mix(static_cast<uint32_t>(plain2.references));
    Mix(context.calls);
    Mix(plain.calls);
    Mix(plain2.calls);
    Mix(colour.calls);
    g_pointers.clear();
    RsStage("buffer-life");
}

// A buffer of the game's with a context of the game's over a target buffer
// with real pixels (so a real rasterizer from the framework when it locks):
// fills and blits through the buffer, the pixels and every call on the
// buffers into the hash.
void DriveBufferDraw()
{
    Random random{0x52415339u};
    RasterTables& t = RasterTablesOf();
    Canvas pixels(48, 40, 4), picture(11, 7, 0);
    pixels.Fill(random);
    picture.Fill(random);
    Buffer target{t.buffer, 0x7C};
    target.width = pixels.width;
    target.height = pixels.height;
    target.bits = pixels.Bits();
    target.stride = pixels.stride;
    target.rect = Rect{0, 0, 48, 40};
    Buffer source{t.buffer, 0x52};
    source.width = picture.width;
    source.height = picture.height;
    source.bits = picture.Bits();
    source.stride = picture.stride;
    source.rect = Rect{0, 0, 11, 7};
    g_known = {{&target, target.id}, {&source, source.id}};

    uint8_t* const c = static_cast<uint8_t*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(200));
    std::memset(c, 0xC3, 200);
    reinterpret_cast<void*(__fastcall*)(void*, void*)>(addr::GZDrawContext_GZDrawContext)(c, nullptr);
    Method<uint32_t>(addr::GZDrawContext_AddRef, c);
    uint8_t* const b = NewBuffer();
    uint8_t* const side = b + 0x0C;
    g_pointers = {{b, 0xB1}, {side, 0xB2}, {c, 0xCC}, {&target, target.id}, {&source, source.id}};
    MixValue(Method<uint32_t>(addr::GZContextBuffer_SetContext, side, static_cast<void*>(c),
                              static_cast<void*>(&target)));
    MixValue(Method<uint32_t>(addr::GZContextBuffer_LockContext, side));
    MixContext(c);
    MixBuffer(b, pixels.Bits());
    RsStage("buffer-draw-target");

    for (int round = 0; round < 16; ++round) {
        const Rect r = RandomRect(random, -10, 60), clip = RandomRect(random, -5, 50);
        MixValue(Method<uint32_t>(addr::GZContextBuffer_FillRect, b, random.Next() & 0xFFFFFF, &r,
                                  round & 1 ? &clip : nullptr));
        const Rect from{static_cast<int32_t>(random.Below(3)), static_cast<int32_t>(random.Below(2)),
                        6 + static_cast<int32_t>(random.Below(5)), 4 + static_cast<int32_t>(random.Below(3))};
        const int32_t x = static_cast<int32_t>(random.Below(50)) - 5, y = static_cast<int32_t>(random.Below(40)) - 5;
        const Rect to{x, y, x + from.right - from.left, y + from.bottom - from.top};
        MixValue(Method<uint32_t>(addr::GZContextBuffer_Blt, b, static_cast<void*>(&source), &from, &to,
                                  round & 2 ? &clip : nullptr));
        if (round % 5 == 4) {
            MixValue(Method<uint32_t>(addr::GZContextBuffer_UnlockContext, side));
            MixValue(Method<uint32_t>(addr::GZContextBuffer_GetContext, side));
            MixValue(Method<uint32_t>(addr::GZContextBuffer_Lock, b, uint32_t{0x8010}));
        }
        MixContext(c);
        MixBuffer(b, pixels.Bits());
        MixBytes(pixels.bytes);
    }
    RsStage("buffer-draw-pixels");

    MixValue(Method<uint32_t>(addr::GZContextBuffer_UnlockContext, side));
    MixValue(Method<uint32_t>(addr::GZContextBuffer_IsLocked, b));
    MixContext(c);
    MixBuffer(b, pixels.Bits());
    Method<uint32_t>(addr::GZDrawContext_Release, c);
    Mix(reinterpret_cast<uint32_t(__fastcall*)(void*, void*)>((*reinterpret_cast<void***>(b))[2])(b, nullptr));
    Mix(static_cast<uint32_t>(target.references));
    Mix(static_cast<uint32_t>(source.references));
    Mix(target.calls);
    Mix(source.calls);
    MixBytes(picture.bytes);
    g_pointers.clear();
    g_known.clear();
    RsStage("buffer-draw-life");
}

// OPENSC4_TEST_RASTER=<day>: the four spans, the context's methods, the
// rasterizer's and the buffer's by address.
void DriveRasterDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_RASTER") || !game::IsBuild641())
        return;
    g_rsOn = true;
    DriveFill();
    DrivePlain();
    DriveTiled();
    DriveDispatch();
    DriveContext();
    DriveRasterizerMethods();
    if (DriveRasterDraw())
        DriveBufferDraw();
    DriveBufferMethods();
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
