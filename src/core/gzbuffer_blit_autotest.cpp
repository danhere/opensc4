// The blits' part of the autotest (docs/pixels.md, "Blits"): OPENSC4_TEST_BLITS
// =<day> calls the 19 functions of 0x008D8760 to 0x008DA8BF by address (the
// game's with renderer.blits.* off, ours with them on). The buffers, drawing
// contexts and pictures they draw on are mocks of our own whose vtables
// record every call into the hash: the slot, the values, the rectangles'
// contents and the mocks' ids (addresses never), and answer from a fixed
// rule, so the hash says whether two runs made the same calls with the same
// arguments in the same order. The flood fills run on real memory with a
// margin row above and below. Every result goes into the fingerprint's `bl`
// part, one hash per stage logged as `autotest: blits: <stage> <hash>`.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace opensc4::autotest {
namespace {

bool g_blOn = false;
uint32_t g_blHash = 2166136261u;

void MixByte(uint8_t value) { g_blHash = (g_blHash ^ value) * 16777619u; }

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

void BlStage(const char* name) { log::Write("autotest: blits: %s %08X", name, g_blHash); }

struct Rect {
    int32_t left, top, right, bottom;
};

struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state ^ (state >> 15);
    }
    int32_t Range(int32_t low, int32_t high) { return low + static_cast<int32_t>(Next() % static_cast<uint32_t>(high - low + 1)); }
};

// ====================================================================================
// Mocks

constexpr int kSlots = 48;

struct Mock {
    void** vtable;
    uint32_t id;
    Rect area{0, 0, 64, 64};
    Mock* gdi = nullptr;     // a buffer's answer to 0xAB300B2B
    Mock* colours = nullptr; // a buffer's answer to 0x86D72B57
    Mock* context = nullptr; // a drawing context interface's slot 4
    uint8_t ok = 1;          // Lock's answer; a colour source's slot 4 answer
    uint8_t transparent = 0;
    uint8_t bytes[4] = {0x12, 0x34, 0x56, 0x78};
    uint8_t* bits = nullptr;
    int32_t stride = 0, width = 64, height = 64;
    uint32_t bpp = 32;
};

std::vector<Mock*> g_mocks;

void Rec(Mock* m, uint32_t slot)
{
    Mix(m->id);
    Mix(slot);
}

void RecRect(const Rect* r)
{
    if (!r) {
        Mix(0xFFFFFFFF);
        return;
    }
    Mix(static_cast<uint32_t>(r->left));
    Mix(static_cast<uint32_t>(r->top));
    Mix(static_cast<uint32_t>(r->right));
    Mix(static_cast<uint32_t>(r->bottom));
}

void RecObject(const void* p)
{
    for (Mock* m : g_mocks)
        if (m == p) {
            Mix(m->id);
            return;
        }
    Mix(p ? 0xBADu : 0u);
}

uint32_t PixelOf(Mock* m, int32_t x, int32_t y)
{
    uint32_t v = static_cast<uint32_t>(x) * 0x9E3779B1u ^ static_cast<uint32_t>(y) * 0x85EBCA77u ^ m->id;
    v ^= v >> 13;
    return v * 0xC2B2AE3Du;
}

template <int Slot>
uint32_t __fastcall Trap(Mock* m, void*)
{
    Rec(m, 0xEE00 + Slot);
    return 0;
}

// The drawing context.
template <int Slot>
uint32_t __fastcall DcPlain(Mock* m, void*)
{
    Rec(m, Slot);
    return 1;
}
uint32_t __fastcall DcSetClip(Mock* m, void*, const Rect* clip)
{
    Rec(m, 5);
    RecRect(clip);
    return 1;
}
uint32_t __fastcall DcColour(Mock* m, void*, uint32_t colour)
{
    Rec(m, 21);
    Mix(colour);
    return 1;
}
uint32_t __fastcall DcPen(Mock* m, void*, float width)
{
    Rec(m, 25);
    MixFloat(width);
    return 1;
}
uint32_t __fastcall DcFill(Mock* m, void*, const Rect* r)
{
    Rec(m, 35);
    RecRect(r);
    return 1;
}
uint32_t __fastcall DcBlt(Mock* m, void*, const void* source, const Rect* s, const Rect* d)
{
    Rec(m, 38);
    RecObject(source);
    RecRect(s);
    RecRect(d);
    return 1;
}
uint32_t __fastcall DcTile(Mock* m, void*, const void* source, const Rect* s, const Rect* d, int32_t x, int32_t y)
{
    Rec(m, 39);
    RecObject(source);
    RecRect(s);
    RecRect(d);
    Mix(static_cast<uint32_t>(x));
    Mix(static_cast<uint32_t>(y));
    return 1;
}

// A drawing context interface (0xAB300B2B) and a picture's colours
// (0x86D72B57).
uint32_t __fastcall AnyRelease(Mock* m, void*)
{
    Rec(m, 2);
    return 0;
}
Mock* __fastcall GdiContext(Mock* m, void*)
{
    Rec(m, 4);
    return m->context;
}
uint32_t __fastcall GdiSelect(Mock* m, void*, const void* source)
{
    Rec(m, 9);
    RecObject(source);
    return 1;
}
uint32_t __fastcall ColoursGet(Mock* m, void*, uint8_t* a, uint8_t* b, uint8_t* c, uint8_t* d)
{
    Rec(m, 4);
    if (m->ok) {
        *a = m->bytes[0];
        *b = m->bytes[1];
        *c = m->bytes[2];
        *d = m->bytes[3];
    }
    return m->ok;
}

// A buffer.
uint32_t __fastcall BufQuery(Mock* m, void*, uint32_t iid, void** out)
{
    Rec(m, 0);
    Mix(iid);
    if (iid == 0xAB300B2B && m->gdi) {
        *out = m->gdi;
        return 1;
    }
    if (iid == 0x86D72B57 && m->colours) {
        *out = m->colours;
        return 1;
    }
    return 0;
}
uint32_t __fastcall BufLock(Mock* m, void*, uint32_t flags)
{
    Rec(m, 6);
    Mix(flags);
    return m->ok;
}
uint32_t __fastcall BufUnlock(Mock* m, void*, uint32_t flags)
{
    Rec(m, 7);
    Mix(flags);
    return 1;
}
int32_t __fastcall BufWidth(Mock* m, void*)
{
    Rec(m, 9);
    return m->width;
}
int32_t __fastcall BufHeight(Mock* m, void*)
{
    Rec(m, 10);
    return m->height;
}
const Rect* __fastcall BufArea(Mock* m, void*)
{
    Rec(m, 12);
    return &m->area;
}
uint32_t __fastcall BufBpp(Mock* m, void*)
{
    Rec(m, 16);
    return m->bpp;
}
uint32_t __fastcall BufFill(Mock* m, void*, uint32_t colour, const Rect* r, const Rect* clip)
{
    Rec(m, 20);
    Mix(colour);
    RecRect(r);
    RecRect(clip);
    return 1;
}
uint32_t __fastcall BufGetPixel(Mock* m, void*, int32_t x, int32_t y)
{
    Rec(m, 21);
    Mix(static_cast<uint32_t>(x));
    Mix(static_cast<uint32_t>(y));
    return PixelOf(m, x, y);
}
uint32_t __fastcall BufSetPixel(Mock* m, void*, int32_t x, int32_t y, uint32_t colour)
{
    Rec(m, 22);
    Mix(static_cast<uint32_t>(x));
    Mix(static_cast<uint32_t>(y));
    Mix(colour);
    return 1;
}
uint32_t __fastcall BufTransparent(Mock* m, void*, int32_t x, int32_t y)
{
    Rec(m, 25);
    Mix(static_cast<uint32_t>(x));
    Mix(static_cast<uint32_t>(y));
    return m->transparent;
}
uint32_t __fastcall BufBlt(Mock* m, void*, const void* source, const Rect* s, const Rect* d, const Rect* clip)
{
    Rec(m, 29);
    RecObject(source);
    RecRect(s);
    RecRect(d);
    RecRect(clip);
    return 1;
}
uint32_t __fastcall BufToNative(Mock* m, void*, uint32_t r, uint32_t g, uint32_t b)
{
    Rec(m, 30);
    MixByte(static_cast<uint8_t>(r));
    MixByte(static_cast<uint8_t>(g));
    MixByte(static_cast<uint8_t>(b));
    return 0x5A000000 | (r & 0xFF) << 16 | (g & 0xFF) << 8 | (b & 0xFF);
}
uint32_t __fastcall BufToRGB(Mock* m, void*, uint32_t c, uint8_t* r, uint8_t* g, uint8_t* b)
{
    Rec(m, 31);
    Mix(c);
    *r = static_cast<uint8_t>((c >> 16) ^ 0x11);
    *g = static_cast<uint8_t>((c >> 8) ^ 0x22);
    *b = static_cast<uint8_t>(c ^ 0x33);
    return 1;
}
uint32_t __fastcall BufToNativeA(Mock* m, void*, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    Rec(m, 32);
    MixByte(static_cast<uint8_t>(r));
    MixByte(static_cast<uint8_t>(g));
    MixByte(static_cast<uint8_t>(b));
    MixByte(static_cast<uint8_t>(a));
    return (a & 0xFF) << 24 | (r & 0xFF) << 16 | (g & 0xFF) << 8 | (b & 0xFF);
}
uint32_t __fastcall BufToRGBA(Mock* m, void*, uint32_t c, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a)
{
    Rec(m, 33);
    Mix(c);
    *r = static_cast<uint8_t>(c >> 16);
    *g = static_cast<uint8_t>(c >> 8);
    *b = static_cast<uint8_t>(c);
    *a = static_cast<uint8_t>(c >> 24);
    return 1;
}
uint8_t* __fastcall BufBits(Mock* m, void*)
{
    Rec(m, 34);
    return m->bits;
}
int32_t __fastcall BufStride(Mock* m, void*)
{
    Rec(m, 35);
    return m->stride;
}

template <std::size_t... I>
void FillTraps(void** table, std::index_sequence<I...>)
{
    ((table[I] = reinterpret_cast<void*>(&Trap<static_cast<int>(I)>)), ...);
}

template <typename Fn>
void* V(Fn fn)
{
    return reinterpret_cast<void*>(fn);
}

struct Tables {
    void* context[kSlots];
    void* gdi[kSlots];
    void* colours[kSlots];
    void* buffer[kSlots];
    Tables()
    {
        for (void** table : {context, gdi, colours, buffer})
            FillTraps(table, std::make_index_sequence<kSlots>{});
        context[3] = V(&DcPlain<3>);
        context[5] = V(&DcSetClip);
        context[9] = V(&DcPlain<9>);
        context[10] = V(&DcPlain<10>);
        context[11] = V(&DcPlain<11>);
        context[21] = V(&DcColour);
        context[25] = V(&DcPen);
        context[35] = V(&DcFill);
        context[38] = V(&DcBlt);
        context[39] = V(&DcTile);
        context[40] = V(&DcPlain<40>);
        gdi[2] = V(&AnyRelease);
        gdi[4] = V(&GdiContext);
        gdi[9] = V(&GdiSelect);
        colours[2] = V(&AnyRelease);
        colours[4] = V(&ColoursGet);
        buffer[0] = V(&BufQuery);
        buffer[2] = V(&AnyRelease);
        buffer[6] = V(&BufLock);
        buffer[7] = V(&BufUnlock);
        buffer[9] = V(&BufWidth);
        buffer[10] = V(&BufHeight);
        buffer[12] = V(&BufArea);
        buffer[16] = V(&BufBpp);
        buffer[20] = V(&BufFill);
        buffer[21] = V(&BufGetPixel);
        buffer[22] = V(&BufSetPixel);
        buffer[25] = V(&BufTransparent);
        buffer[29] = V(&BufBlt);
        buffer[30] = V(&BufToNative);
        buffer[31] = V(&BufToRGB);
        buffer[32] = V(&BufToNativeA);
        buffer[33] = V(&BufToRGBA);
        buffer[34] = V(&BufBits);
        buffer[35] = V(&BufStride);
    }
};

Tables& TablesOf()
{
    static Tables tables;
    return tables;
}

Mock Make(void** table, uint32_t id)
{
    Mock m;
    m.vtable = table;
    m.id = id;
    return m;
}

template <typename Fn>
Fn Game(std::uintptr_t address)
{
    return reinterpret_cast<Fn>(address);
}

// ====================================================================================
// Math

using ShadeFn = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, int32_t);
using ClassIDFn = uint32_t(__cdecl*)();
using FloatsFn = uint32_t(__cdecl*)(float, float, float, float);
using MatrixFn = void(__cdecl*)(float*, const float*, const float*);

float FloatOf(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

void DriveMath()
{
    Random random{0xB1170001};
    const auto shade = Game<ShadeFn>(addr::GZBlit_Shade);
    const int32_t deltas[] = {0, 0x38, -0x38, 1, -1, 255, -255, 300, -300, 0x7FFFFFF0, -0x7FFFFFF0,
                              std::numeric_limits<int32_t>::min()};
    for (const int32_t delta : deltas)
        for (int i = 0; i < 6; ++i)
            Mix(shade(random.Next(), random.Next(), random.Next(), delta));
    Mix(shade(0x100, 0x1FF, 0xFFFFFF80, 0x38));
    Mix(Game<ClassIDFn>(addr::GZBlit_SoftwareClassID)());
    BlStage("shade");

    const auto floats = Game<FloatsFn>(addr::GZBlit_ColourFromFloats);
    const uint32_t specials[] = {0x00000000, 0x80000000, 0x3F800000, 0x3F000000, 0x3B808081, 0x3F7FFFFF,
                                 0x40000000, 0xBF800000, 0x7F800000, 0xFF800000, 0x7FC00000, 0x00000001,
                                 0x0DA24260, 0x3E4CCCCD, 0x3C000000, 0x3BFFFFFF, 0x3B800000, 0x3F7F0000};
    for (const uint32_t r : specials)
        for (const uint32_t a : {0x3F800000u, 0x3F000000u, 0x7FC00000u, 0xBF000000u})
            Mix(floats(FloatOf(r), FloatOf(r ^ 0x00012345), FloatOf(specials[(r >> 3) % 18]), FloatOf(a)));
    for (int i = 0; i < 64; ++i) {
        const float v[4] = {static_cast<float>(random.Next() % 1001) / 1000.0f, FloatOf(random.Next() & 0x3F8FFFFF),
                            static_cast<float>(random.Next() % 300) / 255.0f - 0.1f, FloatOf(random.Next())};
        Mix(floats(v[0], v[1], v[2], v[3]));
    }
    BlStage("floats");

    const auto matrix = Game<MatrixFn>(addr::GZBlit_MatrixMultiply);
    const auto vector = Game<MatrixFn>(addr::GZBlit_VectorTimesMatrix);
    for (int round = 0; round < 24; ++round) {
        float a[16], b[16], out[16], v[4], w[4];
        for (float& f : a)
            f = static_cast<float>(static_cast<int32_t>(random.Next() % 20001) - 10000) / 97.0f;
        for (float& f : b)
            f = FloatOf((random.Next() & 0x007FFFFF) | ((120 + random.Next() % 16) << 23) | (random.Next() & 0x80000000));
        for (float& f : v)
            f = static_cast<float>(static_cast<int32_t>(random.Next() % 2001) - 1000) / 7.0f;
        if (round % 4 == 0) {
            matrix(out, a, b);
        } else if (round % 4 == 1) {
            std::memcpy(out, a, sizeof a);
            matrix(out, out, b); // in place over a
        } else if (round % 4 == 2) {
            std::memcpy(out, b, sizeof b);
            matrix(out, a, out); // in place over b
        } else {
            matrix(out, a, a);
        }
        for (const float f : out)
            MixFloat(f);
        vector(w, v, b);
        for (const float f : w)
            MixFloat(f);
        vector(v, v, a); // in place
        for (const float f : v)
            MixFloat(f);
    }
    BlStage("matrix");
}

// ====================================================================================
// Frames and tiles

using OutlineFn = void(__cdecl*)(void*, const Rect*, uint32_t, int32_t);
using PanelFn = void(__cdecl*)(void*, const Rect*, uint32_t, uint32_t, uint32_t, uint32_t);
using PanelRGBFn = void(__cdecl*)(void*, const Rect*, uint32_t, uint32_t, uint32_t);
using FrameFn = void(__cdecl*)(void*, void*, const Rect*, const Rect*, uint32_t);
using TileFn = uint8_t(__cdecl*)(void*, void*, const Rect*, const Rect*, int32_t, int32_t, const Rect*);
using StretchFn = uint8_t(__cdecl*)(void*, void*, const Rect*, const Rect*, const Rect*);
using ThreeFn = uint8_t(__cdecl*)(void*, void*, const Rect*, const Rect*, uint32_t);

Rect RandomRect(Random& random, int32_t low, int32_t high)
{
    const int32_t x = random.Range(low, high), y = random.Range(low, high);
    return {x, y, x + random.Range(-2, 40), y + random.Range(-2, 40)};
}

void DriveFrames()
{
    Random random{0xB1170002};
    Tables& t = TablesOf();
    Mock context = Make(t.context, 0xC0);
    g_mocks = {&context};
    const auto outline = Game<OutlineFn>(addr::GZBlit_Outline);
    const auto bevel = Game<OutlineFn>(addr::GZBlit_Bevel);
    const auto panel = Game<PanelFn>(addr::GZBlit_Panel);
    const auto panelRGB = Game<PanelRGBFn>(addr::GZBlit_PanelRGB);
    for (int i = 0; i < 24; ++i) {
        const Rect r = RandomRect(random, -20, 100);
        const int32_t widths[] = {0, 1, 2, 5, -1, static_cast<int32_t>(0x80000001), 16777217, 33554435};
        outline(&context, &r, random.Next(), widths[i % 8]);
        bevel(&context, &r, random.Next(), random.Next());
        panel(&context, &r, random.Next(), random.Next(), random.Next(), random.Next() % 3 == 0 ? 0x100 : random.Next() % 2);
        panelRGB(&context, &r, random.Next(), random.Next() % 2, random.Next());
    }
    BlStage("frames");
}

void DriveTiles()
{
    Random random{0xB1170003};
    Tables& t = TablesOf();
    Mock context = Make(t.context, 0xC1);
    Mock gdi = Make(t.gdi, 0x61);
    Mock colours = Make(t.colours, 0xC2);
    Mock target = Make(t.buffer, 0xB0);
    Mock source = Make(t.buffer, 0xB1);
    Mock gdiTarget = Make(t.buffer, 0xB2);
    gdi.context = &context;
    gdiTarget.gdi = &gdi;
    g_mocks = {&context, &gdi, &colours, &target, &source, &gdiTarget};
    const auto frame = Game<FrameFn>(addr::GZBlit_DrawFrame);
    const auto tile = Game<TileFn>(addr::GZBuffer_TileBlit);
    const auto stretch = Game<StretchFn>(addr::GZBlit_StretchBlit);
    const auto three = Game<ThreeFn>(addr::GZBlit_ThreePart);

    // Nine-cell frames: with and without the picture's colours (answering
    // or not), centre on and off, targets smaller than two cells.
    for (int i = 0; i < 24; ++i) {
        source.colours = i % 3 == 0 ? nullptr : &colours;
        colours.ok = static_cast<uint8_t>(i % 3 != 1);
        colours.bytes[0] = static_cast<uint8_t>(random.Next());
        colours.bytes[3] = static_cast<uint8_t>(random.Next());
        const int32_t cw = random.Range(1, 9), ch = random.Range(1, 9);
        const int32_t cx = random.Range(0, 30), cy = random.Range(0, 30);
        const Rect cell{cx, cy, cx + cw, cy + ch};
        const int32_t tx = random.Range(-10, 50), ty = random.Range(-10, 50);
        const Rect place{tx, ty, tx + random.Range(0, 3 * cw + 20), ty + random.Range(0, 3 * ch + 20)};
        frame(&context, &source, &cell, &place, i % 2 ? 1 : 0x100);
    }
    source.colours = nullptr;
    BlStage("frame");

    // TileBlit: a 1 by 1 source in, out and transparent; the drawing
    // context path with and without a clip; the software path with shifts
    // of every sign, areas starting left of and above 0, past the target's
    // own area, and clips that cut each side or miss.
    target.area = {0, 0, 100, 80};
    source.area = {0, 0, 40, 30};
    for (int i = 0; i < 12; ++i) {
        source.transparent = static_cast<uint8_t>(i % 4 == 3);
        const Rect s1{random.Range(-2, 41), random.Range(-2, 31), 0, 0};
        const Rect one{s1.left, s1.top, s1.left + 1, s1.top + 1};
        const Rect place = RandomRect(random, -10, 90);
        const Rect clip = RandomRect(random, -10, 90);
        Mix(tile(&target, &source, &one, &place, random.Range(-5, 5), random.Range(-5, 5), i % 2 ? &clip : nullptr));
    }
    for (int i = 0; i < 6; ++i) {
        const Rect s = RandomRect(random, 0, 20);
        const Rect place = RandomRect(random, -10, 90);
        const Rect clip = RandomRect(random, -10, 90);
        Mix(tile(&gdiTarget, &source, &s, &place, random.Range(-9, 9), random.Range(-9, 9), i % 2 ? &clip : nullptr));
    }
    for (int i = 0; i < 48; ++i) {
        const int32_t sw = random.Range(2, 13), sh = random.Range(1, 11);
        const int32_t sx = random.Range(0, 20), sy = random.Range(0, 20);
        const Rect s{sx, sy, sx + sw, sy + sh};
        const int32_t tx = random.Range(-30, 90), ty = random.Range(-30, 70);
        const Rect place{tx, ty, tx + random.Range(1, 60), ty + random.Range(1, 50)};
        const Rect clip = RandomRect(random, -10, 90);
        const int32_t shifts[] = {0, sw, -sw, 3 * sw + 1, -2 * sh - 1, random.Range(-100, 100), 1, -1};
        Mix(tile(&target, &source, &s, &place, shifts[i % 8], shifts[(i / 8) % 8], i % 3 ? &clip : nullptr));
    }
    BlStage("tile");

    // StretchBlit: the drawing context path; empty and outside areas;
    // shrinking and growing; a failed lock; clips.
    for (int i = 0; i < 4; ++i) {
        const Rect s = RandomRect(random, 0, 20);
        const Rect place = RandomRect(random, 0, 50);
        const Rect clip = RandomRect(random, 0, 50);
        Mix(stretch(&gdiTarget, &source, &s, &place, i % 2 ? &clip : nullptr));
    }
    for (int i = 0; i < 40; ++i) {
        source.ok = static_cast<uint8_t>(i % 9 != 4);
        const int32_t sx = random.Range(-2, 30), sy = random.Range(-2, 25);
        const Rect s{sx, sy, sx + random.Range(-1, 14), sy + random.Range(-1, 10)};
        const int32_t tx = random.Range(-10, 90), ty = random.Range(-10, 70);
        const Rect place{tx, ty, tx + random.Range(-1, 30), ty + random.Range(-1, 20)};
        const Rect clip = RandomRect(random, -10, 90);
        Mix(stretch(&target, &source, &s, &place, i % 3 ? &clip : nullptr));
    }
    source.ok = 1;
    BlStage("stretch");

    // Three-part strips, the middle on and off.
    for (int i = 0; i < 24; ++i) {
        const int32_t sw = random.Range(1, 9), sh = random.Range(1, 9);
        const int32_t sx = random.Range(0, 10), sy = random.Range(0, 10);
        const Rect s{sx, sy, sx + sw, sy + sh};
        const int32_t tx = random.Range(-10, 50), ty = random.Range(-10, 50);
        const Rect place{tx, ty, tx + random.Range(0, 3 * sw + 30), ty + random.Range(0, 3 * sh + 30)};
        Mix(three(&target, &source, &s, &place, i % 2 ? 1 : 0x100));
    }
    BlStage("three");
}

// ====================================================================================
// Flood fill

using FillFn = uint8_t(__cdecl*)(void*, int32_t, int32_t, uint32_t);
using PushSpanFn = int32_t*(__fastcall*)(void**, void*, int32_t, int32_t, int32_t, int32_t);

// A fill of the game's called by address with x in EAX.
__declspec(naked) void __cdecl CallFlood(std::uintptr_t, int32_t, int32_t, int32_t, int32_t, uint8_t*, int32_t, uint32_t)
{
    __asm push esi
    __asm mov esi, dword ptr [esp + 8]
    __asm mov eax, dword ptr [esp + 12]
    __asm push dword ptr [esp + 36]
    __asm push dword ptr [esp + 36]
    __asm push dword ptr [esp + 36]
    __asm push dword ptr [esp + 36]
    __asm push dword ptr [esp + 36]
    __asm push dword ptr [esp + 36]
    __asm call esi
    __asm add esp, 24
    __asm pop esi
    __asm ret
}

// A picture of `width` x `height` pixels of `bytes` with a margin row above
// and below, in two colours (`a`, `b`) laid out by `pattern`; the margins
// sometimes in `a` too, so a fill reaching the row past the buffer shows.
struct Field {
    std::vector<uint8_t> memory;
    int32_t width, height, stride, bytes;
    uint8_t* Bits() { return memory.data() + stride; }
    Field(Random& random, int32_t w, int32_t h, int32_t bytesPerPixel, int pattern, uint32_t a, uint32_t b, bool padRows)
        : memory(static_cast<std::size_t>(h + 2) * (w * bytesPerPixel + (padRows ? 5 : 0)), 0), width(w), height(h),
          stride(w * bytesPerPixel + (padRows ? 5 : 0)), bytes(bytesPerPixel)
    {
        for (int32_t y = -1; y <= h; ++y) {
            for (int32_t x = 0; x < w; ++x) {
                bool first;
                switch (pattern) {
                case 0: first = random.Next() % 3 != 0; break;                         // noise
                case 1: first = ((x / 3) + (y / 2)) % 2 == 0; break;                   // checks
                case 2: first = ((x - w / 2) * (x - w / 2) + (y - h / 2) * (y - h / 2)) / 9 % 2 == 0; break; // rings
                default: first = !(x % 4 == 2 && y % 5 != 0) && !(y % 4 == 1 && x % 7 != 3); break;        // walls
                }
                const uint32_t c = first ? a : b;
                std::memcpy(memory.data() + static_cast<std::size_t>(y + 1) * stride + x * bytes, &c, bytes);
            }
        }
    }
};

void DriveFill()
{
    Random random{0xB1170004};
    Tables& t = TablesOf();
    const std::uintptr_t floods[] = {addr::GZBlit_FloodFill8, addr::GZBlit_FloodFill16, addr::GZBlit_FloodFill24,
                                     addr::GZBlit_FloodFill32};
    const uint32_t masks[] = {0xFF, 0xFFFF, 0xFFFFFF, 0xFFFFFF};
    for (int kind = 0; kind < 4; ++kind) {
        const int32_t bytes = kind + 1;
        for (int pattern = 0; pattern < 4; ++pattern) {
            for (int round = 0; round < 6; ++round) {
                const int32_t w = random.Range(1, 37), h = random.Range(1, 29);
                const uint32_t a = random.Next() & masks[kind];
                uint32_t b = random.Next() & masks[kind];
                if (b == a)
                    b ^= 1;
                // The fill colour: new, or the start's own (nothing to do);
                // for 32 bits a new alpha too.
                uint32_t colour = random.Next() & masks[kind];
                if (colour == a || colour == b)
                    colour ^= 2;
                if (round == 5)
                    colour = a;
                // (Not with the start's own colour: the game's fill would
                // then never end, the pixels still matching on 24 bits.)
                if (kind == 3 && round % 2 && round != 5)
                    colour |= 0x80000000;
                Field field(random, w, h, bytes, pattern, a, b, round % 2 == 1);
                const int32_t starts[][2] = {{w / 2, h / 2}, {0, 0}, {w - 1, h - 1}, {random.Range(0, w - 1), h - 1},
                                             {-1, 0}, {w, 0}, {0, h}, {random.Range(0, w - 1), random.Range(0, h - 1)}};
                const int32_t* s = starts[(pattern * 6 + round) % 8];
                CallFlood(floods[kind], s[0], s[1], w, h, field.Bits(), field.stride, colour);
                for (const uint8_t byte : field.memory)
                    MixByte(byte);
            }
        }
    }
    BlStage("flood");

    // The buffer's fill: through a buffer mock with real memory, every bits
    // per pixel and an unknown one.
    const auto fill = Game<FillFn>(addr::GZBlit_FloodFill);
    for (const uint32_t bpp : {8u, 16u, 24u, 32u, 12u, 0u}) {
        const int32_t bytes = bpp == 8 ? 1 : bpp == 16 ? 2 : bpp == 24 ? 3 : 4;
        const uint32_t mask = bytes == 4 ? 0xFFFFFF : (1u << (8 * bytes)) - 1;
        Field field(random, 23, 17, bytes, static_cast<int>(bpp / 8) % 4, 0x0102 & mask, 0x77 & mask, false);
        Mock buffer = Make(t.buffer, 0xB5);
        buffer.bits = field.Bits();
        buffer.stride = field.stride;
        buffer.width = field.width;
        buffer.height = field.height;
        buffer.bpp = bpp;
        g_mocks = {&buffer};
        MixByte(fill(&buffer, 11, 8, 0x5C & mask));
        for (const uint8_t byte : field.memory)
            MixByte(byte);
    }
    BlStage("fill");

    // The span push on a list of our own.
    void* const head = reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(0x18);
    static_cast<void**>(head)[0] = head;
    static_cast<void**>(head)[1] = head;
    void* list = head;
    const auto push = Game<PushSpanFn>(addr::GZBlit_PushSpan);
    for (int i = 0; i < 5; ++i) {
        int32_t* const span = push(&list, nullptr, i, -i, 2 * i, i % 2 ? 1 : -1);
        int32_t* const last = reinterpret_cast<int32_t*>(static_cast<void**>(head)[1]) + 2;
        MixByte(span == last);
    }
    for (void** node = static_cast<void**>(static_cast<void**>(head)[0]); node != head; node = static_cast<void**>(node[0])) {
        const int32_t* const span = reinterpret_cast<const int32_t*>(node + 2);
        for (int k = 0; k < 4; ++k)
            Mix(static_cast<uint32_t>(span[k]));
        MixByte(static_cast<void**>(node[1])[0] == node);
    }
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4PairList_Destroy)(&list);
    BlStage("push");
}

// OPENSC4_TEST_BLITS=<day>: the 19 by address.
void DriveBlitsDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_BLITS") || !game::IsBuild641())
        return;
    g_blOn = true;
    DriveMath();
    DriveFrames();
    DriveTiles();
    DriveFill();
    g_mocks.clear();
    log::Write("autotest: blits driven (hash %08X)", g_blHash);
}

void BlFingerprint(cISC4City*, std::string& line)
{
    if (!g_blOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | bl %08X", g_blHash);
    line += text;
}

const FingerprintPart kBlFingerprint{"blits", 4, &BlFingerprint};
const DayHook kBlDrive{"blits", 4, &DriveBlitsDay};

} // namespace
} // namespace opensc4::autotest
