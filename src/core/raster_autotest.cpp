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
// addresses never go in. Then the drawing context's 36 methods of
// renderer.raster.context, on recording mocks ("The drawing context" below).
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
// detours) with the two draws (44 and 45) recording mocks too. After each
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

// The context's draws: the kind, the count and the points.
template <int Slot>
uint32_t __fastcall MDraw(Mock* m, void*, uint32_t kind, const float* points, uint32_t count)
{
    Rec(m, Slot);
    Mix(kind);
    Mix(count);
    for (uint32_t i = 0; i < 2 * count && i < 16; ++i)
        MixFloat(points[i]);
    return Answer(m, Slot);
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
                         : slot == 19 ? V(&MNone<19>)
                                      : V(&MNone<20>);
        target[2] = V(&MNone<2>);
        target[30] = V(&MNative);
        std::memcpy(context, reinterpret_cast<void*>(addr::GZDrawContext_vftable), sizeof context);
        context[0] = V(&MTrap<0>);
        context[17] = V(&MTrap<17>);
        context[44] = V(&MDraw<44>);
        context[45] = V(&MDraw<45>);
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
    Mixed(addr::GZDrawContext_FillClip, c);
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
        Mixed(addr::GZDrawContext_FillRect, c, &to);
        Mixed(addr::GZDrawContext_FrameRect, c, from.left, from.top, i % 10 == 9 ? 0x7FFFFFFF : from.right,
                     i % 10 == 9 ? 16777217 : from.bottom);
        MixContext(c);
    }
    RsStage("context-draws");

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
    Mix(Method<uint32_t>(addr::GZDrawContext_Slot46, c));
    Mix(Method<uint32_t>(addr::GZDrawContext_Slot47, c));
    Mix(Method<uint32_t>(addr::GZDrawContext_HasTarget, c));
    At<void*>(c, 0x24) = nullptr;
    Mix(Method<uint32_t>(addr::GZDrawContext_HasTarget, c));
    At<void*>(c, 0x24) = &target;
    MixContext(c);
    RsStage("context-batch");

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

// OPENSC4_TEST_RASTER=<day>: the four spans and the context's methods by
// address.
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
