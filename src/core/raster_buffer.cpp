// Our buffer with a drawing context of the GZ framework's software rasterizer
// (docs/renderer.md, "The software rasterizer"): the 0x4C-byte buffer panels
// draw into (vtable 0x00AC1550, docs/ui-scaling.md, "Panels get tiles of
// their own"), made by the factory's MakeContextBuffer 0x0082B980 together
// with a drawing context. It keeps a plain buffer of its own (+0x14, the
// pixels) and the context (+0x10) that draws into it. Its methods, all
// detours here, off by default:
//
//   renderer.raster.buffer  the constructor 0x0082B780 and destructor
//                           0x0082B7D0; the cIGZBuffer's QueryInterface,
//                           Lock and Unlock (slots 6, 7), IsLocked (8), the
//                           sizes and the rectangle (9 to 11), the formats
//                           (14 to 16), FillClip (18), FillSides (19),
//                           FillRect (20), GetPixel and SetPixel (21, 22),
//                           the colour key (23, 26, 27), IsKeyAt (25),
//                           DrawLine (28), Blt (29), a colour's pixel value
//                           and a pixel's colour (30, 31) and the cache
//                           (37, 38); the side interface's (+0x0C, vtable
//                           0x00AC1510, interface 0xAB300B2B) GetContext,
//                           LockContext, UnlockContext, SetContext, Retarget
//                           and UseColourOf (slots 4 to 9); and the
//                           pixel-to-colour helper 0x0082ABF0; 34 detours
//
// Slots 18, 19, 21, 22, 25 and 28 and the side's 8 never ran in the game's
// runs; the autotest is what reaches them. Slot 19 (0x0082B2F0) is also
// slot 19 of the plain buffer (0x00AC1400) and of 0x00ADB464's class, so
// theirs is ours too with the group on (it calls its own slot 20).
//
// Left the game's: AddRef and Release and the other slots shared with other
// classes (slots 1 to 5, 12, 13, 17, 24, 32 to 36 and 39 of 0x00AC1550; 1
// and 2 of the side's), the counted base's methods (+0x04, vtable
// 0x00AC1538) and the side's 0 and 3, which never ran.
//
// The plain buffer is reached through its vtable (0x00AC1400 in the game):
// 0 QueryInterface, 1 AddRef, 2 Release, 12 its rectangle, 15 the format
// (an 8-byte record, given 8 bytes), 17 bytes a pixel, 23 SetColourKey, 26
// GetColourKey, 27 ClearColourKey, 37 and 38 the hardware cache. The
// context through its own (raster_context.cpp names them).
//
// Return values: every one of ours answers what the game's leaves in EAX.
// Where that is the caller's EAX or a callee's that may be (Unlock with the
// count left above 0, the methods that jump on to the plain buffer's,
// UnlockContext, and SetContext, whose last call is the context's SetTarget
// with EAX the width), a few instructions are assembly, the game's own, in
// front of a body here or the whole method.
//
// DrawLine's integers become floats on the x87 under the game's 53-bit
// precision control; its sums are worked out in doubles here and rounded to
// float once, which is what the x87 gives.
//
// No compare mode: the results are calls on the context and the plain
// buffer. The autotest (raster_autotest.cpp, "The buffer") drives every
// method by address with a known EAX on buffers of the game's constructor
// over a recording context and plain buffer, and draws through a buffer, a
// game context and a real rasterizer onto real pixels.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::raster {
namespace {

struct Rect {
    int32_t left, top, right, bottom;
};

struct ContextBuffer {
    void** vtable;         // 0x00: 0x00AC1550, a cIGZBuffer
    void** countedVtable;  // 0x04: the counted base, 0x00AC1538
    int32_t references;    // 0x08: the counted base's
    void** sideVtable;     // 0x0C: 0x00AC1510, interface 0xAB300B2B
    void* context;         // 0x10: the drawing context
    void* inner;           // 0x14: the plain buffer with the pixels
    Rect rect;             // 0x18: the plain buffer's
    int32_t locks;         // 0x28
    uint32_t format;       // 0x2C: the plain buffer's
    uint32_t surface[6];   // 0x30: bits, stride, width, height, format, bytes a pixel (the context's GetSurface)
    uint8_t keyed;         // 0x48: SetColourKey's, cleared by ClearColourKey
    uint8_t resume;        // 0x49: the last Unlock gives the context its batch back
    uint8_t unused[2];
};
static_assert(sizeof(ContextBuffer) == 0x4C && offsetof(ContextBuffer, context) == 0x10 &&
                  offsetof(ContextBuffer, locks) == 0x28 && offsetof(ContextBuffer, surface) == 0x30 &&
                  offsetof(ContextBuffer, keyed) == 0x48,
              "the game's buffer with a drawing context is 0x4C bytes");

constexpr std::size_t kSide = offsetof(ContextBuffer, sideVtable);

ContextBuffer* FromSide(void* side) { return reinterpret_cast<ContextBuffer*>(static_cast<uint8_t*>(side) - kSide); }

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

constexpr uint32_t kUnknown = 1;
constexpr uint32_t kBuffer = 0x20732180;       // cIGZBuffer
constexpr uint32_t kSideInterface = 0xAB300B2B; // the side's
constexpr uint32_t kColourSource = 0x86D72B57;  // what UseColourOf asks a source for
constexpr uint32_t kContextLock = 0x8010;

// The buffers' slots (this class's and the plain buffer's).
enum BufferSlot {
    kQuery = 0,
    kAddRef = 1,
    kRelease = 2,
    kLock = 6,
    kRectOf = 12,
    kFormat = 15,
    kPixelBytes = 17,
    kOwnFillRect = 20,
    kGetPixel = 21,
    kGetColourKey = 26,
};

// The context's slots used here.
enum ContextSlot {
    kPushTransform = 3,
    kClipTo = 5,
    kPopTransform = 9,
    kGetOrigin = 14,
    kHasTarget = 16,
    kSetTarget = 17,
    kSetColour = 20,
    kSetForeColour = 21,
    kSetPenWidth = 25,
    kFillClip = 32,
    kPlot = 33,
    kFrameRect = 34,
    kFillRect = 35,
    kBlt = 38,
    kSlot40 = 40,
    kIsBatching = 41,
    kBeginBatch = 42,
    kEndBatch = 43,
    kDraw = 44,
    kGetSurface = 46,
};

// The side's slot that Blt calls.
constexpr int kUseColourOf = 9;

// SC4BufferColorType::DepthOf: the bits a pixel of a format.
uint32_t Depth(uint32_t format)
{
    switch (format) {
    case 2:
        return 8;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        return 16;
    case 8:
    case 9:
        return 32;
    default:
        return 0;
    }
}

// ====================================================================================
// Making and letting go

ContextBuffer* __fastcall Construct(ContextBuffer* b, void*)
{
    b->vtable = reinterpret_cast<void**>(addr::GZContextBuffer_vftable_base);
    reinterpret_cast<void*(__fastcall*)(void*, void*)>(addr::cRZBaseRefCount_Construct)(&b->countedVtable, nullptr);
    b->sideVtable = reinterpret_cast<void**>(addr::GZContextBuffer_vftable_C_base);
    b->vtable = reinterpret_cast<void**>(addr::GZContextBuffer_vftable);
    b->countedVtable = reinterpret_cast<void**>(addr::GZContextBuffer_vftable_4);
    b->sideVtable = reinterpret_cast<void**>(addr::GZContextBuffer_vftable_C);
    b->context = nullptr;
    b->inner = nullptr;
    b->locks = 0;
    b->keyed = 0;
    b->surface[0] = 0;
    b->surface[4] = 1;
    return b;
}

// The plain buffer released first, then the context; with flag 1 the memory
// freed. Answers this.
ContextBuffer* __fastcall Destroy(ContextBuffer* b, void*, uint32_t flags)
{
    b->vtable = reinterpret_cast<void**>(addr::GZContextBuffer_vftable);
    b->countedVtable = reinterpret_cast<void**>(addr::GZContextBuffer_vftable_4);
    b->sideVtable = reinterpret_cast<void**>(addr::GZContextBuffer_vftable_C);
    if (b->inner)
        Call<uint32_t>(b->inner, kRelease);
    if (b->context)
        Call<uint32_t>(b->context, kRelease);
    reinterpret_cast<void(__fastcall*)(void*, void*)>(addr::cRZBaseRefCount_Destruct)(&b->countedVtable, nullptr);
    if (flags & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(b);
    return b;
}

// Slot 0: itself as the unknown and as a cIGZBuffer, the side as its own
// interface, else whatever the plain buffer answers.
uint32_t __fastcall QueryInterface(ContextBuffer* b, void*, uint32_t iid, void** out)
{
    if (iid == kUnknown || iid == kBuffer) {
        *out = b;
        return WithAl(Call<uint32_t>(b, kAddRef), 1);
    }
    if (iid == kSideInterface) {
        *out = &b->sideVtable;
        return WithAl(Call<uint32_t>(b, kAddRef), 1);
    }
    if (!b->inner)
        return WithAl(iid, 0);
    return Call<uint32_t>(b->inner, kQuery, iid, out);
}

// ====================================================================================
// The lock

// Slot 6: the first lock points the context at the plain buffer, putting a
// batch it is in on hold (and remembering to give it back); every lock asks
// the context for the surface. Flag 0x8000 says whether the surface is
// wanted whole (the second word is 0 then). AL 1 over the surface's answer.
uint32_t __fastcall Lock(ContextBuffer* b, void*, uint32_t flags)
{
    void* const context = b->context;
    if (b->locks == 0) {
        uint8_t resume = 1;
        if (static_cast<uint8_t>(Call<uint32_t>(context, kHasTarget)) != 0 &&
            static_cast<uint8_t>(Call<uint32_t>(context, kIsBatching)) == 0)
            resume = 0;
        b->resume = resume;
        Call<uint32_t>(context, kSetTarget, b->inner);
        if (static_cast<uint8_t>(Call<uint32_t>(context, kIsBatching)) != 0)
            Call<uint32_t>(context, kEndBatch);
    }
    const uint32_t shifted = flags >> 15;
    const uint32_t whole = (shifted & 0xFFFFFF00u) | (~shifted & 1u);
    const uint32_t answer = Call<uint32_t>(b->context, kGetSurface, &b->surface[0], whole);
    ++b->locks;
    return WithAl(answer, 1);
}

// Slot 7 past the thunk below: the last lock gone, the context's batch
// given back.
uint32_t __fastcall UnlockBody(ContextBuffer* b, void*, uint32_t)
{
    return WithAl(Call<uint32_t>(b->context, kBeginBatch), 1);
}

// Slot 7, 0x0082B1A0 (ret 4). Locks left, or nothing to give back, leave
// AL 1 over the caller's EAX.
__declspec(naked) void Unlock()
{
    __asm {
        dec dword ptr [ecx + 0x28]
        jnz kept
        mov al, byte ptr [ecx + 0x49]
        test al, al
        jnz UnlockBody
    kept:
        mov al, 1
        ret 4
    }
}

uint32_t __fastcall IsLocked(ContextBuffer* b, void*) { return b->locks != 0; }

// ====================================================================================
// Sizes and formats

uint32_t __fastcall Width(ContextBuffer* b, void*)
{
    const Rect* const r = Call<const Rect*>(b, kRectOf);
    return static_cast<uint32_t>(r->right) - static_cast<uint32_t>(r->left);
}

uint32_t __fastcall Height(ContextBuffer* b, void*)
{
    const Rect* const r = Call<const Rect*>(b, kRectOf);
    return static_cast<uint32_t>(r->bottom) - static_cast<uint32_t>(r->top);
}

// Slot 11: the rectangle copied out; answers out.
Rect* __fastcall CopyRect(ContextBuffer* b, void*, Rect* out)
{
    *out = b->rect;
    return out;
}

// Slot 14: the format and its depth; answers the depth.
uint32_t __fastcall FormatAndDepth(ContextBuffer* b, void*, uint32_t* out)
{
    const uint32_t depth = Depth(b->format);
    out[0] = b->format;
    out[1] = depth;
    return depth;
}

// Slot 15: the same record; answers out.
uint32_t* __fastcall Format(ContextBuffer* b, void*, uint32_t* out)
{
    out[0] = b->format;
    out[1] = Depth(b->format);
    return out;
}

uint32_t __fastcall BitsPerPixel(ContextBuffer* b, void*) { return b->surface[5] << 3; }

// 0x0082ABF0 (thiscall, ret 4): a pixel value as 0xAARRGGBB, by the format
// (4 and 6 are 1-5-5-5, 5 is 5-6-5, 7 is 4-4-4-4, 8 and 9 are 32-bit; alpha
// 0xFF but for 4-4-4-4's own), the low bits filled from the high.
uint32_t __fastcall ToArgb(ContextBuffer* b, void*, uint32_t p)
{
    switch (b->format) {
    case 4:
    case 6: {
        const uint32_t v = ((p & 0x1F) + ((p & 0x3E0) + (p & 0x7C00) * 8) * 8) * 8;
        return ((v >> 5) & 0x70707) + v + 0xFF000000u;
    }
    case 5: {
        const uint32_t v = (((p & 0xF800) << 5) + (p & 0x1F)) << 3;
        return ((v >> 5) & 0x70007) + ((p >> 1) & 0x300) + v + (((p & 0x7E0) + 0x7F80000) << 5);
    }
    case 7: {
        const uint32_t v = (((((p & 0xF00) + 0xF0000) << 4) + (p & 0xF0)) << 4) + (p & 0xF);
        return v * 17;
    }
    case 8:
    case 9:
        return p | 0xFF000000u;
    default:
        return 0;
    }
}

// Slot 30 (ret 0xC, the low bytes read): a colour's pixel value, by the
// format; the 16-bit formats with their alpha bits set.
uint32_t __fastcall PixelOf(ContextBuffer* b, void*, uint32_t red, uint32_t green, uint32_t blue)
{
    const uint32_t r = red & 0xFF, g = green & 0xFF, bl = blue & 0xFF;
    switch (b->format) {
    case 4:
    case 6:
        return ((((r & 0xF8) + 0x1E0) << 5) + (g & 0xF8)) * 4 + (bl >> 3);
    case 5:
        return ((((r & 0xF8) + 0xF0) << 5) + (g & 0xFC)) * 8 + (bl >> 3);
    case 7:
        return ((((r & 0xF0) + 0xF00) << 4) + (g & 0xF0)) + (bl >> 4);
    case 8:
    case 9:
        return ((((r + 0xFF00) << 8) + g) << 8) + bl;
    default:
        return 0;
    }
}

// Slot 31: a pixel's red, green and blue; AL 1 over its 0xAARRGGBB.
uint32_t __fastcall ColourOf(ContextBuffer* b, void*, uint32_t pixel, uint8_t* red, uint8_t* green, uint8_t* blue)
{
    const uint32_t argb = ToArgb(b, nullptr, pixel);
    *red = static_cast<uint8_t>(argb >> 16);
    *green = static_cast<uint8_t>(argb >> 8);
    *blue = static_cast<uint8_t>(argb);
    return WithAl(argb, 1);
}

// ====================================================================================
// Drawing through the context

// Slot 20 (ret 0xC): a rectangle filled with a pixel value, inside clip
// when one is given. AL 1 over the context's slot 40's answer.
uint32_t __fastcall FillRect(ContextBuffer* b, void*, uint32_t pixel, const Rect* r, const Rect* clip)
{
    Call<uint32_t>(b->context, kPushTransform);
    if (clip)
        Call<uint32_t>(b->context, kClipTo, clip);
    Call<uint32_t>(b->context, kSetColour, uint32_t{0}, ToArgb(b, nullptr, pixel));
    Call<uint32_t>(b->context, kFillRect, r);
    Call<uint32_t>(b->context, kPopTransform);
    return WithAl(Call<uint32_t>(b->context, kSlot40), 1);
}

// Slot 29 (ret 0x10): the context's Blt of the source from `to` moved to
// `from`'s top left (so `from`'s corner at `to`'s size) to `to`, inside
// clip when one is given, the fore colour first from the source (the
// side's UseColourOf). AL 1 over the context's slot 40's answer.
uint32_t __fastcall Blt(ContextBuffer* b, void*, void* source, const Rect* from, const Rect* to, const Rect* clip)
{
    Call<uint32_t>(b->context, kPushTransform);
    if (clip)
        Call<uint32_t>(b->context, kClipTo, clip);
    Call<uint32_t>(&b->sideVtable, kUseColourOf, source);
    const int32_t dx = static_cast<int32_t>(static_cast<uint32_t>(from->left) - static_cast<uint32_t>(to->left));
    const int32_t dy = static_cast<int32_t>(static_cast<uint32_t>(from->top) - static_cast<uint32_t>(to->top));
    const Rect moved{static_cast<int32_t>(static_cast<uint32_t>(to->left) + static_cast<uint32_t>(dx)),
                     static_cast<int32_t>(static_cast<uint32_t>(to->top) + static_cast<uint32_t>(dy)),
                     static_cast<int32_t>(static_cast<uint32_t>(to->right) + static_cast<uint32_t>(dx)),
                     static_cast<int32_t>(static_cast<uint32_t>(to->bottom) + static_cast<uint32_t>(dy))};
    Call<uint32_t>(b->context, kBlt, source, &moved, to);
    Call<uint32_t>(b->context, kPopTransform);
    return WithAl(Call<uint32_t>(b->context, kSlot40), 1);
}

// Slot 18, 0x0082B330 (ret 8): the context's clip filled with a pixel
// value (its FillClip), inside clip when one is given. AL 1 over the
// context's slot 40's answer.
uint32_t __fastcall FillClip(ContextBuffer* b, void*, uint32_t pixel, const Rect* clip)
{
    Call<uint32_t>(b->context, kPushTransform);
    if (clip)
        Call<uint32_t>(b->context, kClipTo, clip);
    Call<uint32_t>(b->context, kSetColour, uint32_t{0}, ToArgb(b, nullptr, pixel));
    Call<uint32_t>(b->context, kFillClip);
    Call<uint32_t>(b->context, kPopTransform);
    return WithAl(Call<uint32_t>(b->context, kSlot40), 1);
}

// Slot 19, 0x0082B2F0 (ret 0x18): FillRect (its own slot 20) by the
// rectangle's four sides; answers its answer. Shared with the plain buffer.
uint32_t __fastcall FillSides(void* self, void*, uint32_t pixel, int32_t left, int32_t top, int32_t right,
                              int32_t bottom, const Rect* clip)
{
    const Rect r{left, top, right, bottom};
    return Call<uint32_t>(self, kOwnFillRect, pixel, &r, clip);
}

// Slot 21, 0x0082B390 (ret 8): the pixel value at x, y of the context's
// surface (its slot 46, whole), by its bytes a pixel (1 to 4; three bytes
// low first); 0 for any other or when the surface is not given.
uint32_t __fastcall GetPixel(ContextBuffer* b, void*, int32_t x, int32_t y)
{
    uint32_t surface[6];
    if (static_cast<uint8_t>(Call<uint32_t>(b->context, kGetSurface, surface, uint32_t{1})) == 0)
        return 0;
    const uintptr_t row = surface[1] * static_cast<uint32_t>(y) + surface[0];
    const uint32_t at = static_cast<uint32_t>(x);
    const uint8_t* const p = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(row + at * surface[5]));
    switch (surface[5]) {
    case 1:
        return p[0];
    case 2: {
        uint16_t v;
        std::memcpy(&v, p, sizeof v);
        return v;
    }
    case 3:
        return uint32_t{p[2]} << 16 | uint32_t{p[1]} << 8 | p[0];
    case 4: {
        uint32_t v;
        std::memcpy(&v, p, sizeof v);
        return v;
    }
    default:
        return 0;
    }
}

// Slot 22, 0x0082B460 (ret 0xC): the context's slot 33 at x, y in the
// pixel value's colour. Answers the context's slot 40's answer.
uint32_t __fastcall SetPixel(ContextBuffer* b, void*, int32_t x, int32_t y, uint32_t pixel)
{
    Call<uint32_t>(b->context, kSetColour, uint32_t{0}, ToArgb(b, nullptr, pixel));
    Call<uint32_t>(b->context, kPlot, x, y);
    return Call<uint32_t>(b->context, kSlot40);
}

// 0x00825C90 (the format in EAX): the bits of a pixel value that a colour
// key compares, by the format.
uint32_t KeyMask(uint32_t format)
{
    switch (format) {
    case 4:
    case 6:
        return 0x7FFF;
    case 5:
        return 0xFFFF;
    case 7:
        return 0xFFF;
    case 8:
    case 9:
        return 0xFFFFFF;
    default:
        return 0xFFFFFFFF;
    }
}

// Slot 25, 0x0082B4C0 (ret 8): whether the pixel at x, y moved by the
// context's origin (its slot 14) is the plain buffer's colour key (its
// slot 26) in the bits the surface's format (+0x40) compares; 0 without a
// key or outside the surface's width and height (unsigned). The pixel is
// read through its own slot 21. Answers 1 or 0.
uint32_t __fastcall IsKeyAt(ContextBuffer* b, void*, int32_t x, int32_t y)
{
    int32_t origin[2];
    Call<uint32_t>(b->context, kGetOrigin, origin);
    const uint32_t atX = static_cast<uint32_t>(x) + static_cast<uint32_t>(origin[0]);
    const uint32_t atY = static_cast<uint32_t>(y) + static_cast<uint32_t>(origin[1]);
    uint32_t key;
    if (static_cast<uint8_t>(Call<uint32_t>(b->inner, kGetColourKey, &key)) == 0 || atX >= b->surface[2] ||
        atY >= b->surface[3])
        return 0;
    const uint32_t mask = KeyMask(b->surface[4]);
    const uint32_t pixel = Call<uint32_t>(b, kGetPixel, atX, atY);
    return ((pixel ^ key) & mask) == 0 ? 1 : 0;
}

// The game's integer difference's size: CDQ, XOR, SUB (the lowest stays
// negative).
int32_t Size(int32_t from, int32_t to)
{
    const uint32_t d = static_cast<uint32_t>(to) - static_cast<uint32_t>(from);
    const uint32_t sign = static_cast<int32_t>(d) < 0 ? 0xFFFFFFFFu : 0;
    return static_cast<int32_t>((d ^ sign) - sign);
}

// Slot 28, 0x0082B560 (ret 0x18): a line in the pixel value's colour, the
// pen the width (unsigned, as a float). At most 1 wide it is the context's
// slot 34 with the four ends; wider, the ends as floats moved by half of
// one less than the width, across the line (y when it is at least as wide
// as it is tall, else x), go to the context's Draw (kind 1, 2 points). AL
// 1 over the context's slot 40's answer.
uint32_t __fastcall DrawLine(ContextBuffer* b, void*, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t pixel,
                             uint32_t width)
{
    Call<uint32_t>(b->context, kPushTransform);
    Call<uint32_t>(b->context, kSetColour, uint32_t{0}, ToArgb(b, nullptr, pixel));
    Call<uint32_t>(b->context, kSetPenWidth, static_cast<float>(static_cast<double>(width)));
    if (width <= 1) {
        Call<uint32_t>(b->context, kFrameRect, x0, y0, x1, y1);
    } else {
        float points[4] = {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1),
                           static_cast<float>(y1)};
        const double half = static_cast<double>(width - 1) * 0.5;
        const int first = Size(x0, x1) >= Size(y0, y1) ? 1 : 0;
        points[first] = static_cast<float>(points[first] + half);
        points[first + 2] = static_cast<float>(points[first + 2] + half);
        Call<uint32_t>(b->context, kDraw, uint32_t{1}, static_cast<const float*>(points), uint32_t{2});
    }
    Call<uint32_t>(b->context, kPopTransform);
    return WithAl(Call<uint32_t>(b->context, kSlot40), 1);
}

// ====================================================================================
// The plain buffer's own: the game's jumps, so its answers (which can be
// the EAX they were reached with: the plain buffer's vtable) go back.

// Slot 23: SetColourKey, the flag set first.
__declspec(naked) void SetColourKey()
{
    __asm mov byte ptr [ecx + 0x48], 1
    __asm mov ecx, dword ptr [ecx + 0x14]
    __asm mov eax, dword ptr [ecx]
    __asm jmp dword ptr [eax + 0x5C]
}

// Slot 26: GetColourKey.
__declspec(naked) void GetColourKey()
{
    __asm mov ecx, dword ptr [ecx + 0x14]
    __asm mov eax, dword ptr [ecx]
    __asm jmp dword ptr [eax + 0x68]
}

// Slot 27: ClearColourKey, the flag cleared first.
__declspec(naked) void ClearColourKey()
{
    __asm mov byte ptr [ecx + 0x48], 0
    __asm mov ecx, dword ptr [ecx + 0x14]
    __asm mov eax, dword ptr [ecx]
    __asm jmp dword ptr [eax + 0x6C]
}

// Slots 37 and 38: the hardware cache.
__declspec(naked) void GetCache()
{
    __asm mov ecx, dword ptr [ecx + 0x14]
    __asm mov eax, dword ptr [ecx]
    __asm jmp dword ptr [eax + 0x94]
}

__declspec(naked) void SetCache()
{
    __asm mov ecx, dword ptr [ecx + 0x14]
    __asm mov eax, dword ptr [ecx]
    __asm jmp dword ptr [eax + 0x98]
}

// ====================================================================================
// The side interface (+0x0C; `this` is the side)

// Slot 4: the context, pointed at the plain buffer (and its batch begun)
// when it has no target.
void* __fastcall GetContext(void* side, void*)
{
    ContextBuffer* const b = FromSide(side);
    if (static_cast<uint8_t>(Call<uint32_t>(b->context, kHasTarget)) == 0) {
        Call<uint32_t>(b->context, kSetTarget, b->inner);
        Call<uint32_t>(b->context, kBeginBatch);
    }
    return b->context;
}

// Slot 5: the buffer locked for the context; answers the context.
void* __fastcall LockContext(void* side, void*)
{
    ContextBuffer* const b = FromSide(side);
    Call<uint32_t>(b, kLock, kContextLock);
    return b->context;
}

// Slot 6, 0x0082ABE0: the buffer's Unlock, reached with EAX its vtable
// (which it can answer).
__declspec(naked) void UnlockContext()
{
    __asm mov eax, dword ptr [ecx - 0x0C]
    __asm add ecx, -0x0C
    __asm push 0x8010
    __asm call dword ptr [eax + 0x1C]
    __asm ret
}

// Slot 7 before the thunk's last call: the context and the plain buffer
// held (each taken before the one it replaces is let go), the plain
// buffer's rectangle, format and bytes a pixel, its width and height into
// the surface. Answers the width.
uint32_t __fastcall SetContextBody(void* side, void*, void* context, void* inner)
{
    ContextBuffer* const b = FromSide(side);
    if (void* const old = b->context; context != old) {
        if (context)
            Call<uint32_t>(context, kAddRef);
        b->context = context;
        if (old)
            Call<uint32_t>(old, kRelease);
    }
    if (void* const old = b->inner; inner != old) {
        if (inner)
            Call<uint32_t>(inner, kAddRef);
        b->inner = inner;
        if (old)
            Call<uint32_t>(old, kRelease);
    }
    b->rect = *Call<const Rect*>(inner, kRectOf);
    uint32_t record[2];
    b->format = *Call<const uint32_t*>(inner, kFormat, record);
    b->surface[5] = Call<uint32_t>(inner, kPixelBytes);
    const uint32_t width = static_cast<uint32_t>(b->rect.right) - static_cast<uint32_t>(b->rect.left);
    b->surface[2] = width;
    b->surface[3] = static_cast<uint32_t>(b->rect.bottom) - static_cast<uint32_t>(b->rect.top);
    return width;
}

// Slot 7, 0x0082AF70 (ret 8): last the context's SetTarget(0), reached
// with EAX the width (its answer when the context has no target).
__declspec(naked) void SetContext()
{
    __asm push esi
    __asm mov esi, ecx
    __asm push dword ptr [esp + 0x0C]
    __asm push dword ptr [esp + 0x0C]
    __asm call SetContextBody
    __asm mov ecx, dword ptr [esi + 4]
    __asm mov edx, dword ptr [ecx]
    __asm push 0
    __asm call dword ptr [edx + 0x44]
    __asm pop esi
    __asm ret 8
}

// Slot 8, 0x0082B030: the plain buffer's rectangle again, the context
// pointed at nothing and then back at the plain buffer, and its batch
// begun (the game's tail jump). Answers BeginBatch's answer.
uint32_t __fastcall Retarget(void* side, void*)
{
    ContextBuffer* const b = FromSide(side);
    b->rect = *Call<const Rect*>(b->inner, kRectOf);
    Call<uint32_t>(b->context, kSetTarget, static_cast<void*>(nullptr));
    Call<uint32_t>(b->context, kSetTarget, b->inner);
    return Call<uint32_t>(b->context, kBeginBatch);
}

// Slot 9 (ret 4): the context's fore colour from a source that has a
// colour (interface 0x86D72B57, its slot 4 the red, green, blue and alpha
// bytes), else 0xFFFFFFFF. Answers the context's answer, or with the
// source's colour held its Release's.
uint32_t __fastcall UseColourOf(void* side, void*, void* source)
{
    ContextBuffer* const b = FromSide(side);
    void* colour = nullptr;
    uint32_t answer;
    if (static_cast<uint8_t>(Call<uint32_t>(source, kQuery, kColourSource, &colour)) == 0) {
        answer = Call<uint32_t>(b->context, kSetForeColour, 0xFFFFFFFFu);
    } else {
        // The game's alpha goes into its argument's word, the source.
        uint32_t alpha = Word(source);
        uint8_t rgb[3];
        if (static_cast<uint8_t>(Call<uint32_t>(colour, 4, &rgb[0], &rgb[1], &rgb[2], &alpha)) == 0)
            answer = Call<uint32_t>(b->context, kSetForeColour, 0xFFFFFFFFu);
        else
            answer = Call<uint32_t>(b->context, kSetForeColour,
                                    (alpha & 0xFF) << 24 | uint32_t{rgb[0]} << 16 | uint32_t{rgb[1]} << 8 | rgb[2]);
    }
    if (colour)
        answer = Call<uint32_t>(colour, kRelease);
    return answer;
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
        {addr::GZContextBuffer_GZContextBuffer, reinterpret_cast<void*>(&Construct)},
        {addr::GZContextBuffer_Destroy, reinterpret_cast<void*>(&Destroy)},
        {addr::GZContextBuffer_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::GZContextBuffer_Lock, reinterpret_cast<void*>(&Lock)},
        {addr::GZContextBuffer_Unlock, reinterpret_cast<void*>(&Unlock)},
        {addr::GZContextBuffer_IsLocked, reinterpret_cast<void*>(&IsLocked)},
        {addr::GZContextBuffer_Width, reinterpret_cast<void*>(&Width)},
        {addr::GZContextBuffer_Height, reinterpret_cast<void*>(&Height)},
        {addr::GZContextBuffer_CopyRect, reinterpret_cast<void*>(&CopyRect)},
        {addr::GZContextBuffer_FormatAndDepth, reinterpret_cast<void*>(&FormatAndDepth)},
        {addr::GZContextBuffer_Format, reinterpret_cast<void*>(&Format)},
        {addr::GZContextBuffer_BitsPerPixel, reinterpret_cast<void*>(&BitsPerPixel)},
        {addr::GZContextBuffer_ToArgb, reinterpret_cast<void*>(&ToArgb)},
        {addr::GZContextBuffer_PixelOf, reinterpret_cast<void*>(&PixelOf)},
        {addr::GZContextBuffer_ColourOf, reinterpret_cast<void*>(&ColourOf)},
        {addr::GZContextBuffer_FillClip, reinterpret_cast<void*>(&FillClip)},
        {addr::GZContextBuffer_FillSides, reinterpret_cast<void*>(&FillSides)},
        {addr::GZContextBuffer_FillRect, reinterpret_cast<void*>(&FillRect)},
        {addr::GZContextBuffer_GetPixel, reinterpret_cast<void*>(&GetPixel)},
        {addr::GZContextBuffer_SetPixel, reinterpret_cast<void*>(&SetPixel)},
        {addr::GZContextBuffer_IsKeyAt, reinterpret_cast<void*>(&IsKeyAt)},
        {addr::GZContextBuffer_DrawLine, reinterpret_cast<void*>(&DrawLine)},
        {addr::GZContextBuffer_Blt, reinterpret_cast<void*>(&Blt)},
        {addr::GZContextBuffer_SetColourKey, reinterpret_cast<void*>(&SetColourKey)},
        {addr::GZContextBuffer_GetColourKey, reinterpret_cast<void*>(&GetColourKey)},
        {addr::GZContextBuffer_ClearColourKey, reinterpret_cast<void*>(&ClearColourKey)},
        {addr::GZContextBuffer_GetCache, reinterpret_cast<void*>(&GetCache)},
        {addr::GZContextBuffer_SetCache, reinterpret_cast<void*>(&SetCache)},
        {addr::GZContextBuffer_GetContext, reinterpret_cast<void*>(&GetContext)},
        {addr::GZContextBuffer_LockContext, reinterpret_cast<void*>(&LockContext)},
        {addr::GZContextBuffer_UnlockContext, reinterpret_cast<void*>(&UnlockContext)},
        {addr::GZContextBuffer_SetContext, reinterpret_cast<void*>(&SetContext)},
        {addr::GZContextBuffer_Retarget, reinterpret_cast<void*>(&Retarget)},
        {addr::GZContextBuffer_UseColourOf, reinterpret_cast<void*>(&UseColourOf)},
    };
    for (const Detour& d : detours) {
        const addr::Entry* entry = addr_check::Find(d.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, d.detour, &original))
            return false;
    }
    log::Write("raster: buffer: %u ours", static_cast<unsigned>(sizeof detours / sizeof detours[0]));
    return true;
}

const bool g_registered = registry::Register(
    {"renderer.raster.buffer", "renderer/raster", registry::Kind::Detour, registry::Phase::Attach,
     addr::GZContextBuffer_GZContextBuffer,
     "Our buffer with a drawing context of the software rasterizer (vtable 0x00AC1550, the panels' buffer): its "
     "constructor and destructor, QueryInterface, the lock, sizes, formats and colours, the fills, pixels and lines, "
     "Blt, the colour key and cache, and the side interface's context methods (34 detours; docs/renderer.md)",
     false, Install});

} // namespace
} // namespace opensc4::raster
