// Our rasterizer of the GZ framework's software rasterizer (docs/renderer.md,
// "The software rasterizer"): the class a drawing context draws through (its
// +0x28, vtable 0x00AC17D0, 0xD4 bytes), every method of it that ran, all
// detours here, off by default:
//
//   renderer.raster.rasterizer  Make 0x0082B910 and the constructor
//                               0x00830690 (neither reached by Ghidra's
//                               analysis), Release, Attach and Detach (slots 2
//                               to 4), the setters (6 to 10, 12 to 15), the
//                               target's lock (17, 18), GetSurface (19),
//                               HowToDraw (21) and DrawSpans (22); 19 detours
//
// Left the game's: QueryInterface (slot 0), and the slots shared with two
// other classes (vtables 0x00ADB500 and 0x00AEFDE0, which have their own
// copies of the rest): AddRef (1), slot 5 (a jump to the target's slot 11),
// the empty 11, 16 and 20. The polygons HowToDraw hands to 0x008308D0 never
// ran and stay the game's, as do the span renderers' slots but the four of
// renderer.raster.spans.
//
// The target and the source are cIGZBuffers (interface 0x20732180), reached
// through their vtables: 0 QueryInterface, 2 Release, 6 Lock and 7 Unlock
// (flags 0x8040 for the target, 0x800 for a source), 9 width, 10 height, 11
// the rectangle, 15 the format (an 8-byte record filled through the pointer
// it is given, its first word the format; the game gives it 8 bytes), 17
// bytes a pixel, 26 the colour key, 30 a colour's pixel value, 34 the bits,
// 35 the stride.
//
// Return values: every one of ours answers what the game's leaves in EAX,
// callers' EAX included where the game's leaves it (the setters of a byte,
// the lock while there is nothing to do, Detach and SetSource with nothing
// held): those few are a handful of instructions of assembly, the game's own,
// in front of a body here or the whole method. The context hands several of
// these answers on as its own (BeginBatch, EndBatch, SetFlag45, SetFlag44,
// SetColour, ClipTo), and the panel code reads GetSurface's AL.
//
// Arithmetic as the game's x87 at its 53-bit precision: products and sums of
// floats and integers are worked out in doubles and rounded to float where
// the game stores a float; FISTP rounds to the nearest, ties to even, and
// answers 0x80000000 out of range; __ftol2 truncates.
//
// No compare mode: the results are calls on the target, the source and the
// span renderers. The autotest (raster_autotest.cpp, "The rasterizer")
// drives every method by address on rasterizers of the game's Make over
// recording buffers and renderers, and draws through a context onto real
// pixels.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::raster {
namespace {

struct Rect {
    int32_t left, top, right, bottom;
};

struct Rasterizer {
    void** vtable;
    int32_t references;      // 0x04
    void* target;            // 0x08: the buffer drawn into, locked while +0x75
    int32_t width, height;   // 0x0C: the target's
    Rect clip;               // 0x14
    void* source;            // 0x24
    int32_t sourceX;         // 0x28: the source's position (scaled: in 16.16 of the source)
    int32_t sourceY;         // 0x2C
    int32_t scaleX, scaleY;  // 0x30: 16.16
    int32_t stepX, stepY;    // 0x38: the inverse scales, 16.16
    uint8_t scaled;          // 0x40
    uint8_t blend;           // 0x41: the mode is above 0
    uint8_t flag45;          // 0x42: the context's flag at its +0x45
    uint8_t flag44;          // 0x43: the context's flag at its +0x44
    int32_t gradient[3][4];  // 0x44: base, x and y steps of four channels
    uint8_t gradientSet;     // 0x74
    uint8_t locked;          // 0x75
    uint8_t unused[2];
    uint32_t surface[6];     // 0x78: bits, stride, width, height, format, bytes a pixel
    int32_t pixelBytes;      // 0x90
    Rect part;               // 0x94
    uint8_t partSet;         // 0xA4
    uint8_t unused2[3];
    int32_t sourceWidth;     // 0xA8
    int32_t sourceHeight;    // 0xAC
    void* renderer;          // 0xB0: one of five span renderers, by the target's format
    uint32_t colours[4];     // 0xB4: 0x00RRGGBB
    uint32_t native[4];      // 0xC4: the target's pixel values of them
};
static_assert(sizeof(Rasterizer) == 0xD4 && offsetof(Rasterizer, source) == 0x24 &&
                  offsetof(Rasterizer, gradientSet) == 0x74 && offsetof(Rasterizer, surface) == 0x78 &&
                  offsetof(Rasterizer, partSet) == 0xA4 && offsetof(Rasterizer, renderer) == 0xB0,
              "the game's rasterizer is 0xD4 bytes");

// A vtable method of the game's (thiscall) called as __fastcall with EDX
// unused.
template <typename R, typename... A>
R Call(void* self, int slot, A... args)
{
    using Fn = R(__fastcall*)(void*, void*, A...);
    return reinterpret_cast<Fn>((*static_cast<void***>(self))[slot])(self, nullptr, args...);
}

uint32_t Word(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// EAX with AL replaced.
uint32_t WithAl(uint32_t eax, uint8_t al) { return (eax & ~0xFFu) | al; }

constexpr uint32_t kBuffer = 0x20732180; // cIGZBuffer
constexpr uint32_t kTargetLock = 0x8040;
constexpr uint32_t kSourceLock = 0x800;

// The buffers' slots.
enum BufferSlot {
    kQuery = 0,
    kRelease = 2,
    kLock = 6,
    kUnlock = 7,
    kWidth = 9,
    kHeight = 10,
    kRect = 11,
    kFormat = 15,
    kPixelBytes = 17,
    kColourKey = 26,
    kPixelValue = 30,
    kBits = 34,
    kStride = 35,
};

// The rasterizer's own slot that Attach calls, and the renderers'.
constexpr int kDetach = 4;
constexpr int kRendererDelete = 0;
enum RendererSlot { kFillSpans = 1, kGradientSpans = 2, kBlendSpans = 3, kBlendGradientSpans = 4, kCopySpans = 5 };

// FISTP under round to nearest: ties to even; NaN and out of range the
// integer indefinite.
int32_t Fist(double value)
{
    double r = std::floor(value);
    const double d = value - r;
    if (d > 0.5 || (d == 0.5 && std::fmod(r, 2.0) != 0.0))
        r += 1.0;
    if (!(r >= -2147483648.0 && r <= 2147483647.0))
        return INT32_MIN;
    return static_cast<int32_t>(r);
}

// __ftol2: truncation toward zero, the low 32 bits; out of range or NaN the
// x87's integer indefinite (2^63), whose low 32 bits are 0.
int32_t Ftol(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0))
        return 0;
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// A float's product rounded to a float, then FISTP (the game's FSTP, FLD,
// FISTP).
int32_t FistFloat(double value) { return Fist(static_cast<float>(value)); }

// SetSource's rounding of a side, `value` the x87's: 0.5 + value + 1.5 * 2^23
// rounded to a float, whose bits less 0x4B400000 are the integer; one less
// when value is that integer less one exactly.
int32_t MagicRound(double value)
{
    const float sum = static_cast<float>(0.5 + value + 12582912.0);
    uint32_t bits;
    std::memcpy(&bits, &sum, 4);
    const int32_t near = static_cast<int32_t>(bits + 0xB4C00000u);
    const int32_t below = static_cast<int32_t>(static_cast<uint32_t>(near) - 1);
    return value == static_cast<double>(below) ? below : near;
}

int32_t Sub(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }
int32_t Add(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }
int32_t Mul(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)); }

// ====================================================================================
// Making and letting go

Rasterizer* __fastcall Construct(Rasterizer* r, void*)
{
    r->vtable = reinterpret_cast<void**>(addr::GZRasterizer_vftable);
    r->references = 0;
    r->target = nullptr;
    r->source = nullptr;
    r->blend = 0;
    r->flag44 = 0;
    r->gradientSet = 0;
    r->locked = 0;
    r->renderer = nullptr;
    return r;
}

// 0x0082B910 (stdcall): the target only tested for the interface.
uint32_t __stdcall Make(void* target, Rasterizer** out)
{
    void* held = nullptr;
    uint32_t answer = Call<uint32_t>(target, kQuery, kBuffer, &held);
    if (static_cast<uint8_t>(answer) == 0) {
        if (held)
            answer = Call<uint32_t>(held, kRelease);
        return WithAl(answer, 0);
    }
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(sizeof(Rasterizer));
    Rasterizer* const r =
        memory ? reinterpret_cast<Rasterizer*(__fastcall*)(void*, void*)>(addr::GZRasterizer_GZRasterizer)(memory,
                                                                                                          nullptr)
               : nullptr;
    *out = r;
    answer = Call<uint32_t>(r, 1);
    if (held)
        answer = Call<uint32_t>(held, kRelease);
    return WithAl(answer, 1);
}

int32_t __fastcall Release(Rasterizer* r, void*)
{
    const int32_t left = --r->references;
    if (left == 0) {
        r->vtable = reinterpret_cast<void**>(addr::GZRasterizer_vftable);
        if (r->renderer)
            Call<uint32_t>(r->renderer, kRendererDelete, uint32_t{1});
        if (r->source)
            Call<uint32_t>(r->source, kRelease);
        if (r->target)
            Call<uint32_t>(r->target, kRelease);
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(r);
    }
    return left;
}

// Slot 4 past the thunk below: the renderer deleted, the target unlocked
// and released. Answers the last call's answer.
uint32_t __fastcall DetachBody(Rasterizer* r, void*)
{
    uint32_t answer = 0;
    if (void* const renderer = r->renderer) {
        answer = Call<uint32_t>(renderer, kRendererDelete, uint32_t{1});
        r->renderer = nullptr;
    }
    if (!r->target)
        return answer;
    if (r->locked)
        Call<uint32_t>(r->target, kUnlock, kTargetLock);
    void* const target = r->target;
    r->target = nullptr;
    return Call<uint32_t>(target, kRelease);
}

// Slot 4, 0x0082FF20. Holding neither leaves EAX as the caller had it.
__declspec(naked) void Detach()
{
    __asm cmp dword ptr [ecx + 0xB0], 0
    __asm jne DetachBody
    __asm cmp dword ptr [ecx + 0x08], 0
    __asm jne DetachBody
    __asm ret
}

// The span renderer for a target's format; for 2 to 9 but 3, a vtable
// (renderers are a vtable and nothing else).
std::uintptr_t RendererFor(int32_t format)
{
    switch (format) {
    case 2:
        return 0x00AC188C;
    case 4:
    case 6:
        return 0x00AC182C;
    case 5:
        return 0x00AC1844;
    case 7:
        return 0x00AC185C;
    case 8:
    case 9:
        return addr::GZRaster32_vftable;
    default:
        return 0;
    }
}

// Slot 3, 0x00830710: the target held and locked, a renderer for its format,
// its surface. A refusal answers AL 0 (the target released when it was
// held), success AL 1; EAX's other bits are the last call's.
uint32_t __fastcall Attach(Rasterizer* r, void*, void* target)
{
    Call<uint32_t>(r, kDetach);
    if (void* const old = r->target) {
        r->target = nullptr;
        Call<uint32_t>(old, kRelease);
    }
    uint32_t answer = Call<uint32_t>(target, kQuery, kBuffer, &r->target);
    if (static_cast<uint8_t>(answer) == 0)
        return WithAl(answer, 0);
    answer = Call<uint32_t>(r->target, kLock, kTargetLock);
    if (static_cast<uint8_t>(answer) == 0) {
        if (void* const held = r->target) {
            r->target = nullptr;
            answer = Call<uint32_t>(held, kRelease);
        }
        return WithAl(answer, 0);
    }
    uint32_t scratch[2];
    const int32_t format = *Call<const int32_t*>(r->target, kFormat, scratch);
    const std::uintptr_t vtable = RendererFor(format);
    if (!vtable) {
        // Left locked, as the game's leaves it.
        void* const held = r->target;
        r->target = nullptr;
        return WithAl(Call<uint32_t>(held, kRelease), 0);
    }
    void** const renderer = static_cast<void**>(
        reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(sizeof(void*)));
    if (renderer)
        *renderer = reinterpret_cast<void*>(vtable);
    r->renderer = renderer;
    r->surface[2] = Call<uint32_t>(r->target, kWidth);
    r->surface[3] = Call<uint32_t>(r->target, kHeight);
    r->surface[4] = static_cast<uint32_t>(*Call<const int32_t*>(r->target, kFormat, scratch));
    r->surface[0] = Call<uint32_t>(r->target, kBits);
    r->surface[1] = Call<uint32_t>(r->target, kStride);
    r->surface[5] = Call<uint32_t>(r->target, kPixelBytes);
    r->height = static_cast<int32_t>(r->surface[3]);
    r->width = static_cast<int32_t>(r->surface[2]);
    r->pixelBytes = static_cast<int32_t>(answer = Call<uint32_t>(r->target, kPixelBytes));
    r->partSet = 0;
    r->locked = 1;
    return WithAl(answer, 1);
}

// ====================================================================================
// The setters

// Slot 6: answers the bottom (the game's last load).
int32_t __fastcall SetClip(Rasterizer* r, void*, const Rect* clip)
{
    r->clip = *clip;
    return clip->bottom;
}

// Slot 7: the shifts are not masked; the target's slot 30 takes the low
// byte of each.
uint32_t __fastcall SetColours(Rasterizer* r, void*, int32_t first, int32_t count, const uint32_t* colours)
{
    for (int32_t i = 0; i != count; ++i) {
        const uint32_t rgb = colours[i];
        r->colours[first + i] = rgb;
        r->native[first + i] = Call<uint32_t>(r->target, kPixelValue, rgb >> 16, rgb >> 8, rgb);
    }
    return 0;
}

// Slot 8: SETG into AL over the mode.
uint32_t __fastcall SetMode(Rasterizer* r, void*, int32_t mode)
{
    r->blend = mode > 0;
    return WithAl(static_cast<uint32_t>(mode), r->blend);
}

// Slots 9 and 10: the byte through AL, the rest of EAX the caller's, so
// they are the game's three instructions.
__declspec(naked) void SetFlag45()
{
    __asm mov al, byte ptr [esp + 4]
    __asm mov byte ptr [ecx + 0x42], al
    __asm ret 4
}

__declspec(naked) void SetFlag44()
{
    __asm mov al, byte ptr [esp + 4]
    __asm mov byte ptr [ecx + 0x43], al
    __asm ret 4
}

// A channel's 1.0 in 16.16 of 0 to 255 (the double at 0x00AC17C8).
constexpr double kChannel = 255.0 * 65536.0;

// Slot 12: three rows of four channels (base, x step, y step) from 0 to 1,
// each times 255 in 16.16 and rounded; the base with half (0x8000) added and
// the steps at (x, y) taken off, truncated. Answers the last base, or 0 for
// none (the argument).
int32_t __fastcall SetGradient(Rasterizer* r, void*, const float* planes, float x, float y)
{
    if (!planes) {
        r->gradientSet = 0;
        return 0;
    }
    int32_t last = 0;
    for (int i = 0; i < 4; ++i) {
        r->gradient[1][i] = FistFloat(static_cast<double>(planes[4 + i]) * kChannel);
        r->gradient[2][i] = FistFloat(static_cast<double>(planes[8 + i]) * kChannel);
        const int32_t base = Add(FistFloat(static_cast<double>(planes[i]) * kChannel), 0x8000);
        const double steps = static_cast<double>(r->gradient[1][i]) * x + static_cast<double>(r->gradient[2][i]) * y;
        last = r->gradient[0][i] = Ftol(static_cast<double>(base) - steps);
    }
    r->gradientSet = 1;
    return last;
}

// Slot 13 past the thunk below: the source held, its scale and position, the
// rectangle it covers on the target. Answers AL 1 with the width's other
// bits, or with the query's when the source does not have the interface;
// for none AL 0 with the release's.
uint32_t __fastcall SetSourceBody(Rasterizer* r, void*, void* source, float x, float y, float scaleX, float scaleY,
                                  Rect* out)
{
    if (!source) {
        void* const held = r->source;
        r->source = nullptr;
        return WithAl(Call<uint32_t>(held, kRelease), 0);
    }
    if (void* const held = r->source) {
        r->source = nullptr;
        Call<uint32_t>(held, kRelease);
    }
    const uint32_t answer = Call<uint32_t>(source, kQuery, kBuffer, &r->source);
    if (static_cast<uint8_t>(answer) == 0)
        return WithAl(answer, 1);
    Rect rect;
    Call<uint32_t>(r->source, kRect, &rect);
    r->scaleX = FistFloat(static_cast<double>(scaleX) * 65536.0);
    r->scaleY = FistFloat(static_cast<double>(scaleY) * 65536.0);
    r->stepX = FistFloat(65536.0 / static_cast<double>(scaleX));
    r->stepY = FistFloat(65536.0 / static_cast<double>(scaleY));
    if (r->scaleX == 0x10000 && r->scaleY == 0x10000) {
        r->sourceX = Fist(x);
        r->sourceY = Fist(y);
        r->scaled = 0;
        out->left = Add(rect.left, r->sourceX);
        out->top = Add(rect.top, r->sourceY);
        out->right = Add(rect.right, r->sourceX);
        out->bottom = Add(rect.bottom, r->sourceY);
    } else {
        r->sourceX = FistFloat(static_cast<double>(r->stepX) * x);
        r->sourceY = FistFloat(static_cast<double>(r->stepY) * y);
        r->scaled = 1;
        out->left = MagicRound(static_cast<double>(x) - 0.5);
        out->top = MagicRound(static_cast<double>(y) - 0.5);
        out->right =
            MagicRound(static_cast<double>(Sub(rect.right, rect.left)) * scaleX + static_cast<double>(x) - 0.5);
        out->bottom =
            MagicRound(static_cast<double>(Sub(rect.bottom, rect.top)) * scaleY + static_cast<double>(y) - 0.5);
    }
    r->sourceWidth = Sub(out->right, out->left);
    r->sourceHeight = Sub(out->bottom, out->top);
    return WithAl(static_cast<uint32_t>(r->sourceWidth), 1);
}

// Slot 13, 0x0082FFD0 (ret 0x18). No source and none held: AL 0, the rest
// of EAX the caller's.
__declspec(naked) void SetSource()
{
    __asm cmp dword ptr [esp + 4], 0
    __asm jne SetSourceBody
    __asm cmp dword ptr [ecx + 0x24], 0
    __asm jne SetSourceBody
    __asm xor al, al
    __asm ret 0x18
}

// Slot 14: EAX untouched.
__declspec(naked) void UseWholeSource()
{
    __asm mov byte ptr [ecx + 0xA4], 0
    __asm ret
}

// Slot 15: answers the bottom.
int32_t __fastcall UseSourcePart(Rasterizer* r, void*, const Rect* part)
{
    r->partSet = 1;
    r->part = *part;
    return part->bottom;
}

// ====================================================================================
// The target's lock (the context's batches)

uint32_t __fastcall UnlockBody(Rasterizer* r, void*)
{
    const uint32_t answer = Call<uint32_t>(r->target, kUnlock, kTargetLock);
    r->locked = 0;
    return answer;
}

uint32_t __fastcall LockBody(Rasterizer* r, void*)
{
    const uint32_t answer = Call<uint32_t>(r->target, kLock, kTargetLock);
    r->locked = 1;
    return answer;
}

// Slots 17 and 18: nothing to do leaves the flag in AL and the rest of EAX
// the caller's.
__declspec(naked) void UnlockTarget()
{
    __asm mov al, byte ptr [ecx + 0x75]
    __asm test al, al
    __asm jnz UnlockBody
    __asm ret
}

__declspec(naked) void LockTarget()
{
    __asm mov al, byte ptr [ecx + 0x75]
    __asm test al, al
    __asm jz LockBody
    __asm ret
}

// Slot 19 (ret 8, the second word not read): AL 1 over out.
uint32_t __fastcall GetSurface(Rasterizer* r, void*, uint32_t* out, uint32_t)
{
    std::memcpy(out, r->surface, sizeof r->surface);
    return WithAl(Word(out), 1);
}

// ====================================================================================
// The draws

// Slot 21: 2 is the context's spans, 1 its other way, 0 done here (the
// polygons, which never ran).
uint32_t __fastcall HowToDraw(Rasterizer* r, void*, uint32_t kind, const float* points, int32_t count)
{
    if (!r->flag44)
        return 2;
    const auto polygon = reinterpret_cast<void(__fastcall*)(Rasterizer*, void*, const float*, int32_t)>(
        addr::GZRasterizer_DrawPolygon);
    if (kind == 6) {
        if (count >= 3)
            for (uint32_t n = static_cast<uint32_t>(count) / 3; n != 0; --n, points += 6)
                polygon(r, nullptr, points, 3);
        return 0;
    }
    if (kind == 7) {
        if (count >= 4)
            for (uint32_t n = static_cast<uint32_t>(count) >> 2; n != 0; --n, points += 8)
                polygon(r, nullptr, points, 4);
        return 0;
    }
    if (kind == 8) {
        polygon(r, nullptr, points, count);
        return 0;
    }
    return 1;
}

// A source's format to the copy tables' index; -1 for none.
int32_t FormatIndex(uint32_t format)
{
    switch (format) {
    case 2:
        return 6;
    case 4:
        return 2;
    case 5:
        return 3;
    case 6:
        return 1;
    case 7:
        return 0;
    case 8:
        return 5;
    case 9:
        return 4;
    default:
        return -1;
    }
}

// Slot 22: the spans to the renderer, filled, or copied from the source.
// Answers the renderer's answer, or with a source the source's last.
uint32_t __fastcall DrawSpans(Rasterizer* r, void*, const uint32_t* spans, int32_t count)
{
    void* const renderer = r->renderer;
    void* const source = r->source;
    if (!source) {
        if (r->gradientSet)
            return Call<uint32_t>(renderer, r->blend ? kBlendGradientSpans : kGradientSpans, spans, count,
                                  &r->surface[0], &r->gradient[0][0]);
        if (r->blend)
            return Call<uint32_t>(renderer, kBlendSpans, spans, count, &r->surface[0], r->native[0], r->colours[0]);
        return Call<uint32_t>(renderer, kFillSpans, spans, count, &r->surface[0], r->native[0]);
    }

    uint32_t answer = Call<uint32_t>(source, kLock, kSourceLock);
    if (static_cast<uint8_t>(answer) == 0)
        return answer;
    uint32_t from[6];
    from[2] = Call<uint32_t>(source, kWidth);
    from[3] = Call<uint32_t>(source, kHeight);
    uint32_t scratch[2];
    from[4] = static_cast<uint32_t>(*Call<const int32_t*>(source, kFormat, scratch));
    from[0] = Call<uint32_t>(source, kBits);
    from[1] = Call<uint32_t>(source, kStride);
    from[5] = Call<uint32_t>(source, kPixelBytes);
    const int32_t index = FormatIndex(from[4]);
    if (index < 0)
        return Call<uint32_t>(source, kUnlock, kSourceLock);

    // The pattern: shifts, steps, the index, colour 0, the colour key and
    // whether there is one (a byte; the rest of that word is not read).
    uint32_t pattern[8] = {};
    pattern[4] = static_cast<uint32_t>(index);
    uint8_t keyed = static_cast<uint8_t>(Call<uint32_t>(source, kColourKey, &pattern[6]));
    if (keyed) {
        static const uint32_t kMasks[] = {0xFFF, 0x7FFF, 0x7FFF, 0xFFFF, 0xFFFFFF, 0xFFFFFF};
        if (index == 6)
            keyed = 0;
        else
            pattern[6] &= kMasks[index];
    }
    pattern[7] = keyed;
    pattern[5] = r->colours[0];
    uint32_t op = r->blend ? (r->flag45 ? 3u : 1u) : (r->flag45 ? 2u : 0u);

    if (r->partSet) {
        const uint32_t width = static_cast<uint32_t>(Sub(r->part.right, r->part.left));
        const uint32_t height = static_cast<uint32_t>(Sub(r->part.bottom, r->part.top));
        from[0] = static_cast<uint32_t>(Add(Add(Mul(r->part.left, static_cast<int32_t>(from[5])),
                                                static_cast<int32_t>(from[0])),
                                            Mul(r->part.top, static_cast<int32_t>(from[1]))));
        from[2] = width;
        from[3] = height;
        pattern[0] = (0x7FFFFFFFu - 0x7FFFFFFFu % width - static_cast<uint32_t>(r->sourceX)) % width;
        pattern[1] = (0x7FFFFFFFu - 0x7FFFFFFFu % height - static_cast<uint32_t>(r->sourceY)) % height;
        op |= 8;
    } else if (r->scaled) {
        pattern[0] = static_cast<uint32_t>(Sub(r->stepX >> 1, r->sourceX));
        pattern[1] = static_cast<uint32_t>(Sub(r->stepY >> 1, r->sourceY));
        pattern[2] = static_cast<uint32_t>(r->stepX);
        pattern[3] = static_cast<uint32_t>(r->stepY);
        op |= 4;
    } else {
        from[0] = static_cast<uint32_t>(Sub(static_cast<int32_t>(from[0]),
                                            Add(Mul(r->sourceX, static_cast<int32_t>(from[5])),
                                                Mul(r->sourceY, static_cast<int32_t>(from[1])))));
    }
    Call<uint32_t>(renderer, kCopySpans, spans, count, &r->surface[0], &from[0], &pattern[0], op);
    return Call<uint32_t>(source, kUnlock, kSourceLock);
}

// ====================================================================================
// Installation

struct Detour {
    std::uintptr_t address;
    void* detour;
};

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static const Detour detours[] = {
        {addr::GZRasterizer_Make, reinterpret_cast<void*>(&Make)},
        {addr::GZRasterizer_GZRasterizer, reinterpret_cast<void*>(&Construct)},
        {addr::GZRasterizer_Release, reinterpret_cast<void*>(&Release)},
        {addr::GZRasterizer_Attach, reinterpret_cast<void*>(&Attach)},
        {addr::GZRasterizer_Detach, reinterpret_cast<void*>(&Detach)},
        {addr::GZRasterizer_SetClip, reinterpret_cast<void*>(&SetClip)},
        {addr::GZRasterizer_SetColours, reinterpret_cast<void*>(&SetColours)},
        {addr::GZRasterizer_SetMode, reinterpret_cast<void*>(&SetMode)},
        {addr::GZRasterizer_SetFlag45, reinterpret_cast<void*>(&SetFlag45)},
        {addr::GZRasterizer_SetFlag44, reinterpret_cast<void*>(&SetFlag44)},
        {addr::GZRasterizer_SetGradient, reinterpret_cast<void*>(&SetGradient)},
        {addr::GZRasterizer_SetSource, reinterpret_cast<void*>(&SetSource)},
        {addr::GZRasterizer_UseWholeSource, reinterpret_cast<void*>(&UseWholeSource)},
        {addr::GZRasterizer_UseSourcePart, reinterpret_cast<void*>(&UseSourcePart)},
        {addr::GZRasterizer_UnlockTarget, reinterpret_cast<void*>(&UnlockTarget)},
        {addr::GZRasterizer_LockTarget, reinterpret_cast<void*>(&LockTarget)},
        {addr::GZRasterizer_GetSurface, reinterpret_cast<void*>(&GetSurface)},
        {addr::GZRasterizer_HowToDraw, reinterpret_cast<void*>(&HowToDraw)},
        {addr::GZRasterizer_DrawSpans, reinterpret_cast<void*>(&DrawSpans)},
    };
    for (const Detour& d : detours) {
        const addr::Entry* entry = addr_check::Find(d.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, d.detour, &original))
            return false;
    }
    log::Write("raster: rasterizer: %u ours", static_cast<unsigned>(sizeof detours / sizeof detours[0]));
    return true;
}

const bool g_registered = registry::Register(
    {"renderer.raster.rasterizer", "renderer/raster", registry::Kind::Detour, registry::Phase::Attach,
     addr::GZRasterizer_Make,
     "Our rasterizer of the software rasterizer (vtable 0x00AC17D0): its maker and constructor, Release, Attach, "
     "Detach, the setters, the target's lock, GetSurface, HowToDraw and DrawSpans (19 detours; docs/renderer.md)",
     false, Install});

} // namespace
} // namespace opensc4::raster
