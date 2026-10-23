// Our GZBuffer blits and their neighbours, 0x008D8760 to 0x008DA8BF
// (docs/pixels.md, "Blits"): the 2D drawing helpers the framework's windows,
// the game's UI and the Software driver call. Each is a detour on the game's
// function, in four groups, off by default:
//
//   renderer.blits.tile   pictures laid onto a buffer or a drawing context:
//                         a nine-cell frame (0x008D8800), TileBlit
//                         (0x008D8BC0), a stretched copy (0x008D9020) and a
//                         three-part strip (0x008D9550)
//   renderer.blits.frame  outlines on a drawing context: a thick outline
//                         (0x008D9360), a bevel (0x008D9470) and a filled
//                         panel with its bevel (0x008D9870, 0x008D9900)
//   renderer.blits.fill   the flood fill of a buffer (0x008DA370) for 8, 16,
//                         24 and 32-bit pixels (0x008D9990, 0x008D9BE0,
//                         0x008D9E30, 0x008DA100) and the push of a span on
//                         its list (0x008D9930)
//   renderer.blits.math   a colour lightened or darkened (0x008D8760), the
//                         Software driver's class ID (0x008DA490), a colour
//                         from four floats (0x008DA4A0), a 4 by 4 matrix
//                         product (0x008DA610) and a vector times a matrix
//                         (0x008DA830)
//
// The blits work through the objects' vtables (cIGZBuffer, the drawing
// context the interface 0xAB300B2B leads to, a picture's colours through
// 0x86D72B57); ours make the same calls with the same arguments in the same
// order. Pointer arguments the game takes from its own stack get ours. The
// flood fills keep their lists in the game's allocator (SC4::stl_allocate,
// freed with SC4::Free and SC4PairList::Destroy), allocated and freed in the
// game's order. The float helpers compute in doubles and store floats, as the
// game's x87 code at 53-bit precision does, adding in the game's order.
//
// Compare mode is offered where the result is memory or a value: the fill
// and math groups (ours first, the memory put back, then the game's; the
// span push is impure and runs the game's alone then). The tile and frame
// groups draw through other objects and have no compare mode.
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

namespace opensc4::blits {
namespace {

// ====================================================================================
// Helpers

using Address = uint32_t;

inline Address At(const void* p) { return static_cast<Address>(reinterpret_cast<uintptr_t>(p)); }

template <typename T>
inline T* Ptr(Address at)
{
    return reinterpret_cast<T*>(static_cast<uintptr_t>(at));
}

struct Rect {
    int32_t left, top, right, bottom;
};

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

// Slot 0 (QueryInterface) and slot 2 (Release).
inline bool QueryInterface(void* object, uint32_t iid, void** out)
{
    return Virtual<uint8_t>(object, 0x0, iid, out) != 0;
}

inline void Release(void* object) { Virtual<uint32_t>(object, 0x8); }

void* Allocate(uint32_t size) { return reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(size); }

void Free(void* memory) { reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory); }

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("blits: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// The interfaces the blits ask for: a drawing context's (slot 4 gives the
// context) and a picture's colours.
constexpr uint32_t kGdiInterface = 0xAB300B2B;
constexpr uint32_t kColoursInterface = 0x86D72B57;

// cIGZBuffer's slots, as the game calls them (gzcom-dll's cIGZBuffer.h, its
// overloads in MSVC's reverse order).
namespace buffer {
constexpr std::size_t kLock = 0x18;
constexpr std::size_t kUnlock = 0x1C;
constexpr std::size_t kWidth = 0x24;
constexpr std::size_t kHeight = 0x28;
constexpr std::size_t kArea = 0x30; // GetBufferArea(): the rectangle
constexpr std::size_t kBitsPerPixel = 0x40;
constexpr std::size_t kFillRect = 0x50; // Fill(colour, rect const&, rect* clip)
constexpr std::size_t kGetPixel = 0x54;
constexpr std::size_t kSetPixel = 0x58;
constexpr std::size_t kIsTransparent = 0x64;
constexpr std::size_t kBlt = 0x74;
constexpr std::size_t kRGBToNative = 0x78;
constexpr std::size_t kNativeToRGB = 0x7C;
constexpr std::size_t kRGBAToNative = 0x80;
constexpr std::size_t kNativeToRGBA = 0x84;
constexpr std::size_t kBits = 0x88;
constexpr std::size_t kStride = 0x8C;
} // namespace buffer

// The drawing context's slots the blits use.
namespace dc {
constexpr std::size_t kBeginClip = 0xC;
constexpr std::size_t kSetClip = 0x14;
constexpr std::size_t kEndClip = 0x24;
constexpr std::size_t kBegin = 0x28;
constexpr std::size_t kEnd = 0x2C;
constexpr std::size_t kSetColour = 0x54;
constexpr std::size_t kSetPenWidth = 0x64;
constexpr std::size_t kFillRect = 0x8C;
constexpr std::size_t kBlt = 0x98;
constexpr std::size_t kTileBlt = 0x9C;
constexpr std::size_t kFlush = 0xA0;
} // namespace dc

// The drawing context interface: slot 4 gives the context, slot 9 selects
// the source picture.
constexpr std::size_t kGdiContext = 0x10;
constexpr std::size_t kGdiSelect = 0x24;

// ====================================================================================
// Math

// The game's 0x008D8760: a colour (alpha 0xFF) from three bytes, each moved
// by `delta` and clamped to 0..255 (the sum's sign decides, as the game's
// JNS, so a sum that wraps past 2^31 counts as negative).
uint32_t Shade(uint32_t red, uint32_t green, uint32_t blue, int32_t delta)
{
    red &= 0xFF;
    green &= 0xFF;
    blue &= 0xFF;
    if (delta == 0)
        return 0xFF000000 | red << 16 | green << 8 | blue;
    auto clamp = [delta](uint32_t c) -> uint32_t {
        const int32_t sum = static_cast<int32_t>(c + static_cast<uint32_t>(delta));
        return sum < 0 ? 0 : sum > 0xFF ? 0xFF : static_cast<uint32_t>(sum);
    };
    return 0xFF000000 | clamp(red) << 16 | clamp(green) << 8 | clamp(blue);
}

// The game's 0x008DA490: the Software driver's class ID.
uint32_t SoftwareClassID() { return 0x7ACA35C6; }

// The game's 0x008DA4A0: four floats clamped to 0..1 (NaN kept) and each
// made a byte by adding 49152 (the float's last 8 bits then hold v * 256,
// rounded) and folding 256 to 255. Colour = a << 24 | r << 16 | g << 8 | b.
uint32_t ColourFromFloats(float red, float green, float blue, float alpha)
{
    auto clamp = [](float v) {
        if (v < 0.0f)
            return 0.0f;
        if (v > 1.0f)
            return 1.0f;
        return v;
    };
    auto byte = [](float v) -> uint32_t {
        const float sum = static_cast<float>(static_cast<double>(v) + 49152.0);
        uint32_t bits;
        std::memcpy(&bits, &sum, 4);
        bits += 0xB8C00000;
        return static_cast<uint8_t>(bits - (bits >> 8));
    };
    red = clamp(red);
    green = clamp(green);
    blue = clamp(blue);
    alpha = clamp(alpha);
    return byte(alpha) << 24 | byte(red) << 16 | byte(green) << 8 | byte(blue);
}

// x * y + ... in the x87's way: products exact, sums in doubles in the
// order given, the result stored as a float.
inline double P(const float* a, int i, const float* b, int j)
{
    return static_cast<double>(a[i]) * static_cast<double>(b[j]);
}

// The game's 0x008DA610: out = a * b (rows of four), each element added up
// in the game's order and stored before the next is computed (out may be a
// or b).
void MatrixMultiply(float* out, const float* a, const float* b)
{
    out[0] = static_cast<float>(((P(b, 8, a, 2) + P(a, 1, b, 4)) + P(a, 0, b, 0)) + P(a, 3, b, 12));
    out[1] = static_cast<float>(((P(b, 9, a, 2) + P(a, 0, b, 1)) + P(a, 1, b, 5)) + P(b, 13, a, 3));
    out[2] = static_cast<float>(((P(b, 10, a, 2) + P(b, 2, a, 0)) + P(a, 1, b, 6)) + P(b, 14, a, 3));
    out[3] = static_cast<float>(((P(a, 1, b, 7) + P(a, 3, b, 15)) + P(b, 11, a, 2)) + P(b, 3, a, 0));
    out[4] = static_cast<float>(((P(a, 5, b, 4) + P(b, 8, a, 6)) + P(a, 7, b, 12)) + P(a, 4, b, 0));
    out[5] = static_cast<float>(((P(a, 4, b, 1) + P(b, 5, a, 5)) + P(b, 9, a, 6)) + P(b, 13, a, 7));
    out[6] = static_cast<float>(((P(a, 5, b, 6) + P(b, 10, a, 6)) + P(b, 14, a, 7)) + P(a, 4, b, 2));
    out[7] = static_cast<float>(((P(a, 4, b, 3) + P(a, 7, b, 15)) + P(a, 6, b, 11)) + P(a, 5, b, 7));
    out[8] = static_cast<float>(((P(a, 9, b, 4) + P(b, 8, a, 10)) + P(a, 11, b, 12)) + P(a, 8, b, 0));
    out[9] = static_cast<float>(((P(a, 8, b, 1) + P(b, 5, a, 9)) + P(b, 13, a, 11)) + P(b, 9, a, 10));
    out[10] = static_cast<float>(((P(a, 9, b, 6) + P(b, 14, a, 11)) + P(b, 10, a, 10)) + P(a, 8, b, 2));
    out[11] = static_cast<float>(((P(a, 8, b, 3) + P(a, 11, b, 15)) + P(a, 10, b, 11)) + P(a, 9, b, 7));
    out[12] = static_cast<float>(((P(a, 13, b, 4) + P(a, 15, b, 12)) + P(a, 14, b, 8)) + P(a, 12, b, 0));
    out[13] = static_cast<float>(((P(a, 14, b, 9) + P(a, 12, b, 1)) + P(b, 5, a, 13)) + P(b, 13, a, 15));
    out[14] = static_cast<float>(((P(a, 13, b, 6) + P(b, 14, a, 15)) + P(a, 14, b, 10)) + P(a, 12, b, 2));
    out[15] = static_cast<float>(((P(a, 14, b, 11) + P(a, 12, b, 3)) + P(a, 15, b, 15)) + P(a, 13, b, 7));
}

// The game's 0x008DA830: out = v * m (a row vector), the same way.
void VectorTimesMatrix(float* out, const float* v, const float* m)
{
    out[0] = static_cast<float>(((P(m, 12, v, 3) + P(m, 4, v, 1)) + P(m, 8, v, 2)) + P(v, 0, m, 0));
    out[1] = static_cast<float>(((P(m, 13, v, 3) + P(m, 5, v, 1)) + P(m, 1, v, 0)) + P(m, 9, v, 2));
    out[2] = static_cast<float>(((P(m, 14, v, 3) + P(m, 6, v, 1)) + P(m, 2, v, 0)) + P(m, 10, v, 2));
    out[3] = static_cast<float>(((P(m, 15, v, 3) + P(m, 7, v, 1)) + P(m, 3, v, 0)) + P(m, 11, v, 2));
}

// ====================================================================================
// Frames

// The game's 0x008D9470: a bevel one pixel wide, the top and left edges in
// `light`, the bottom and right in `dark`, through the context's slot 35.
void Bevel(void* context, const Rect* r, uint32_t light, uint32_t dark)
{
    Virtual<void>(context, dc::kSetColour, light);
    Rect s{r->left, r->top, r->right, r->top + 1};
    Virtual<void>(context, dc::kFillRect, &s);
    s = {r->left, r->top, r->left + 1, r->bottom};
    Virtual<void>(context, dc::kFillRect, &s);
    Virtual<void>(context, dc::kSetColour, dark);
    s = {r->left + 1, r->bottom - 1, r->right, r->bottom};
    Virtual<void>(context, dc::kFillRect, &s);
    s = {r->right - 1, r->top + 1, r->right, r->bottom};
    Virtual<void>(context, dc::kFillRect, &s);
}

// The game's 0x008D9360: an outline `width` pixels wide (the pen width set
// to it as an unsigned float), as four rectangles reaching past the right
// and bottom edges as the game's do.
void Outline(void* context, const Rect* r, uint32_t colour, int32_t width)
{
    Virtual<void>(context, dc::kBegin);
    Virtual<void>(context, dc::kSetPenWidth, static_cast<float>(static_cast<uint32_t>(width)));
    Virtual<void>(context, dc::kSetColour, colour);
    Rect s{r->left, r->top, r->right + width - 1, r->top + width};
    Virtual<void>(context, dc::kFillRect, &s);
    s = {r->left, r->top + width, r->left + width, r->bottom - width + 1};
    Virtual<void>(context, dc::kFillRect, &s);
    s = {r->right + width - 1, r->top + width, r->right + 1, r->bottom - width + 1};
    Virtual<void>(context, dc::kFillRect, &s);
    s = {r->left, r->bottom - width + 1, r->right + 1, r->bottom + 1};
    Virtual<void>(context, dc::kFillRect, &s);
    Virtual<void>(context, dc::kEnd);
}

// The game's 0x008D9870: the rectangle filled with the colour (bytes, alpha
// 0xFF) and bevelled 0x38 lighter on the top left (darker when sunken).
void Panel(void* context, const Rect* r, uint32_t red, uint32_t green, uint32_t blue, uint32_t sunken)
{
    const int32_t delta = (sunken & 0xFF) != 0 ? 0x38 : -0x38;
    Virtual<void>(context, dc::kSetColour, 0xFF000000 | (red & 0xFF) << 16 | (green & 0xFF) << 8 | (blue & 0xFF));
    Virtual<void>(context, dc::kFillRect, r);
    const uint32_t dark = Shade(red, green, blue, -delta);
    const uint32_t light = Shade(red, green, blue, delta);
    Bevel(context, r, light, dark);
}

// The game's 0x008D9900: the same from a 0x00RRGGBB colour (it passes a
// fifth argument 0x008D9870 does not read).
void PanelRGB(void* context, const Rect* r, uint32_t rgb, uint32_t sunken, uint32_t)
{
    Panel(context, r, rgb >> 16, rgb >> 8, rgb, sunken);
}

// ====================================================================================
// Tiles

// The game's 0x008D8BC0, GZBuffer::TileBlit: covers the target area with
// repeats of the source area, the pattern shifted by (shiftX, shiftY), within
// the target's own area and the clip. A 1 by 1 source fills with its pixel
// (unless transparent; false when it is outside the source); a target with a
// drawing context draws through it.
bool TileBlit(void* target, void* source, const Rect* sourceArea, const Rect* targetArea, int32_t shiftX,
              int32_t shiftY, const Rect* clip)
{
    int32_t left = targetArea->left, top = targetArea->top;
    int32_t right = targetArea->right, bottom = targetArea->bottom;
    const int32_t w = sourceArea->right - sourceArea->left;
    const int32_t h = sourceArea->bottom - sourceArea->top;
    if (w == 1 && h == 1) {
        const Rect* const area = Virtual<const Rect*>(source, buffer::kArea);
        const int32_t x = sourceArea->left, y = sourceArea->top;
        if (x < area->left || y < area->top || x >= area->right || y >= area->bottom)
            return false;
        if (Virtual<uint8_t>(source, buffer::kIsTransparent, x, y))
            return true;
        // The bytes land in the game's own slots (w, h and the source area's
        // argument), whose low bytes they replace.
        uint8_t red = static_cast<uint8_t>(w), green = static_cast<uint8_t>(h);
        uint8_t blue = static_cast<uint8_t>(At(sourceArea));
        const uint32_t pixel = Virtual<uint32_t>(source, buffer::kGetPixel, x, y);
        Virtual<uint32_t>(source, buffer::kNativeToRGB, pixel, &red, &green, &blue);
        const uint32_t colour = Virtual<uint32_t>(target, buffer::kRGBToNative, red, green, blue);
        return Virtual<uint8_t>(target, buffer::kFillRect, colour, targetArea, clip) != 0;
    }
    void* gdi = nullptr;
    if (QueryInterface(target, kGdiInterface, &gdi)) {
        void* const context = Virtual<void*>(gdi, kGdiContext);
        Virtual<void>(gdi, kGdiSelect, source);
        Virtual<void>(context, dc::kBeginClip);
        if (clip)
            Virtual<void>(context, dc::kSetClip, clip);
        Virtual<void>(context, dc::kTileBlt, source, sourceArea, targetArea, shiftX, shiftY);
        Virtual<void>(context, dc::kEndClip);
        Virtual<void>(context, dc::kFlush);
        if (gdi)
            Release(gdi);
        return true;
    }
    int32_t sx = shiftX % w;
    int32_t sy = shiftY % h;
    if (sx > 0)
        sx -= w;
    if (sy > 0)
        sy -= h;
    if (left < 0) {
        sx += left - ((left + sx) / w) * w;
        left = 0;
    }
    if (top < 0) {
        sy += top - ((top + sy) / h) * h;
        top = 0;
    }
    const Rect* const bounds = Virtual<const Rect*>(target, buffer::kArea);
    if (right > bounds->right)
        right = bounds->right;
    if (bottom > bounds->bottom)
        bottom = bounds->bottom;
    if (clip) {
        int32_t d = clip->left - left;
        if (d > 0) {
            sx = sx + ((d - sx) / w) * w + left;
            left += d;
        }
        d = clip->top - top;
        if (d > 0) {
            sy = sy + ((d - sy) / h) * h + top;
            top += d;
        }
        d = right - clip->right;
        if (d > 0)
            right -= d;
        d = bottom - clip->bottom;
        if (d > 0)
            bottom -= d;
    }
    if (top < bottom && left < right) {
        const int32_t x0 = left + sx;
        int32_t x = x0, xEnd = w + left + sx;
        int32_t y = top + sy, yEnd = h + top + sy;
        for (;;) {
            Rect s = *sourceArea;
            Rect t{x, y, xEnd, yEnd};
            if (x < left) {
                s.left += left - x;
                t.left = left;
            }
            if (t.right > right) {
                s.right += right - t.right;
                t.right = right;
            }
            if (t.top < top) {
                s.top += top - t.top;
                t.top = top;
            }
            if (t.bottom > bottom) {
                s.bottom += bottom - t.bottom;
                t.bottom = bottom;
            }
            if (clip) {
                int32_t d = t.right - clip->right;
                if (d > 0) {
                    t.right -= d;
                    s.right -= d;
                }
                d = clip->left - t.left;
                if (d > 0) {
                    t.left += d;
                    s.left += d;
                }
                d = t.bottom - clip->bottom;
                if (d > 0) {
                    t.bottom -= d;
                    s.bottom -= d;
                }
                d = clip->top - t.top;
                if (d > 0) {
                    t.top += d;
                    s.top += d;
                }
            }
            Virtual<uint8_t>(target, buffer::kBlt, source, &s, &t, static_cast<const Rect*>(nullptr));
            x += w;
            xEnd += w;
            if (x < right)
                continue;
            y = yEnd;
            if (yEnd >= bottom)
                break;
            yEnd += h;
            x = x0;
            xEnd = w + x0;
        }
    }
    if (gdi)
        Release(gdi);
    return true;
}

// The game's 0x008D9020: the source area stretched over the target area (a
// drawing context's slot 38 when the target has one), else pixel by pixel:
// 16.16 steps sampled at their middles, over the target area cut to the
// target's own area and the clip (0x0079DD60), each pixel converted through
// RGBA. False for an empty area or a source area outside the source.
bool StretchBlit(void* target, void* source, const Rect* sourceArea, const Rect* targetArea, const Rect* clip)
{
    void* gdi = nullptr;
    if (QueryInterface(target, kGdiInterface, &gdi)) {
        void* const context = Virtual<void*>(gdi, kGdiContext);
        Virtual<void>(gdi, kGdiSelect, source);
        Virtual<void>(context, dc::kBeginClip);
        if (clip)
            Virtual<void>(context, dc::kSetClip, clip);
        Virtual<void>(context, dc::kBlt, source, sourceArea, targetArea);
        Virtual<void>(context, dc::kEndClip);
        Virtual<void>(context, dc::kFlush);
        if (gdi)
            Release(gdi);
        return true;
    }
    auto fail = [&gdi] {
        if (gdi)
            Release(gdi);
        return false;
    };
    if (sourceArea->right - sourceArea->left <= 0 || sourceArea->bottom - sourceArea->top <= 0)
        return fail();
    if (targetArea->right - targetArea->left <= 0 || targetArea->bottom - targetArea->top <= 0)
        return fail();
    const Rect* const area = Virtual<const Rect*>(source, buffer::kArea);
    if (sourceArea->left < area->left || sourceArea->top < area->top || sourceArea->right > area->right ||
        sourceArea->bottom > area->bottom)
        return fail();
    const uint32_t xStep = (static_cast<uint32_t>(sourceArea->right - sourceArea->left) << 16) /
                           static_cast<uint32_t>(targetArea->right - targetArea->left);
    const uint32_t xStart = static_cast<uint32_t>(sourceArea->left) << 16;
    const uint32_t yStep = (static_cast<uint32_t>(sourceArea->bottom - sourceArea->top) << 16) /
                           static_cast<uint32_t>(targetArea->bottom - targetArea->top);
    const uint32_t yStart = static_cast<uint32_t>(sourceArea->top) << 16;
    const Rect bounds = *Virtual<const Rect*>(target, buffer::kArea);
    Rect r;
    CallThis<uint8_t>(addr::SC4UI_F79DD60, &r, targetArea, &bounds);
    if (clip)
        CallThis<uint8_t>(addr::SC4UI_F79DD60, &r, &r, clip);
    if (!CallThis<uint32_t>(addr::cRZRect_IsEmpty, &r)) {
        const uint32_t width = static_cast<uint32_t>(r.right - r.left);
        const uint32_t height = static_cast<uint32_t>(r.bottom - r.top);
        const uint32_t x = xStart + (xStep >> 1) + static_cast<uint32_t>(r.left - targetArea->left) * xStep;
        uint32_t y = yStart + (yStep >> 1) + static_cast<uint32_t>(r.top - targetArea->top) * yStep;
        if (Virtual<uint8_t>(source, buffer::kLock, uint32_t{0x800})) {
            uint8_t red = 0, green = 0, blue = 0, alpha = 0;
            for (uint32_t row = 0; row < height; ++row) {
                if (width != 0) {
                    uint32_t xx = x;
                    const uint32_t sy = y >> 16;
                    const int32_t ty = static_cast<int32_t>(row) + r.top;
                    for (uint32_t col = 0; col < width; ++col) {
                        const uint32_t pixel = Virtual<uint32_t>(source, buffer::kGetPixel, xx >> 16, sy);
                        Virtual<uint32_t>(source, buffer::kNativeToRGBA, pixel, &red, &green, &blue, &alpha);
                        const uint32_t colour = Virtual<uint32_t>(target, buffer::kRGBAToNative, red, green, blue, alpha);
                        Virtual<void>(target, buffer::kSetPixel, static_cast<int32_t>(col) + r.left, ty, colour);
                        xx += xStep;
                    }
                }
                y += yStep;
            }
            Virtual<uint8_t>(source, buffer::kUnlock, uint32_t{0x800});
        }
    }
    if (gdi)
        Release(gdi);
    return true;
}

// The game's 0x008D9550: a strip three source areas wide (the source area
// is the first; the next two sit to its right) laid over the target area in
// three rows: the corners copied (slot 29), the edges tiled (TileBlit), the
// middle tiled too when `middle` is set. Always true.
bool ThreePart(void* target, void* source, const Rect* sourceArea, const Rect* targetArea, uint32_t middle)
{
    Rect s = *sourceArea;
    const int32_t w = sourceArea->right - sourceArea->left;
    const int32_t h = sourceArea->bottom - sourceArea->top;
    const Rect* const t = targetArea;
    const int32_t dx = t->left - sourceArea->left, dy = t->top - sourceArea->top;
    Rect d{sourceArea->left + dx, sourceArea->top + dy, sourceArea->right + dx, sourceArea->bottom + dy};
    const Rect* const none = nullptr;
    auto blt = [&] { Virtual<uint8_t>(target, buffer::kBlt, source, &s, &d, none); };
    auto tile = [&] { TileBlit(target, source, &s, &d, 0, 0, nullptr); };
    // Top row.
    blt();
    s.right += w;
    s.left += w;
    d.left = t->left + w;
    d.right = t->right - w;
    if (d.left < d.right)
        tile();
    s.right += w;
    s.left += w;
    d.left = d.right;
    d.right = t->right;
    blt();
    // Middle row.
    int32_t ox = sourceArea->left - s.left;
    const int32_t oy = h - s.top + sourceArea->top;
    s.left += ox;
    s.right += ox;
    s.bottom += oy;
    s.top += oy;
    d.left = t->left;
    d.right = t->left + w;
    d.top = d.bottom;
    d.bottom = t->bottom - h;
    if (d.top < d.bottom)
        tile();
    s.left += w;
    s.right += w;
    d.left = d.right;
    d.right = t->right - w;
    if ((middle & 0xFF) != 0 && d.left < d.right && d.top < d.bottom)
        tile();
    s.left += w;
    s.right += w;
    d.left = d.right;
    d.right = t->right;
    if (d.top < d.bottom)
        tile();
    // Bottom row.
    ox = sourceArea->left - s.left;
    const int32_t down = s.bottom - s.top;
    s.left += ox;
    s.right += ox;
    s.top += down;
    s.bottom += down;
    d.left = t->left;
    d.right = t->left + w;
    d.top = d.bottom;
    d.bottom = d.top + h;
    blt();
    s.right += w;
    s.left += w;
    d.left = d.right;
    d.right = t->right - w;
    if (d.left < d.right)
        tile();
    s.right += w;
    s.left += w;
    d.left = d.right;
    d.right = t->right;
    blt();
    return true;
}

// The game's 0x008D8800: a frame of nine cells (the cell area and the cells
// right of and below it) laid over the target rectangle through a drawing
// context: corners copied (slot 38), edges tiled (slot 39), the centre tiled
// when `centre` is set. The context's colour is set to the picture's colour
// (0x86D72B57's slot 4: four bytes) or -1 first, between slots 10 and 11.
void DrawFrame(void* context, void* picture, const Rect* cell, const Rect* target, uint32_t centre)
{
    Rect s = *cell;
    Rect d = *cell;
    const int32_t w = cell->right - cell->left;
    const int32_t h = cell->bottom - cell->top;
    Virtual<void>(context, dc::kBegin);
    void* colours = nullptr;
    if (QueryInterface(picture, kColoursInterface, &colours)) {
        // The first and fourth bytes land in the game's own argument slots.
        uint8_t c0 = static_cast<uint8_t>(At(picture)), c1 = 0, c2 = 0, c3 = static_cast<uint8_t>(At(context));
        if (Virtual<uint8_t>(colours, 0x10, &c0, &c1, &c2, &c3))
            Virtual<void>(context, dc::kSetColour, static_cast<uint32_t>(c3) << 24 | c0 << 16 | c1 << 8 | c2);
        else
            Virtual<void>(context, dc::kSetColour, 0xFFFFFFFFu);
    } else {
        Virtual<void>(context, dc::kSetColour, 0xFFFFFFFFu);
    }
    auto blt = [&] { Virtual<void>(context, dc::kBlt, picture, &s, &d); };
    auto tile = [&] { Virtual<void>(context, dc::kTileBlt, picture, &s, &d, 0, 0); };
    // Top row.
    const int32_t dx = target->left - d.left, dy = target->top - d.top;
    d.left += dx;
    d.right += dx;
    d.top += dy;
    d.bottom += dy;
    blt();
    s.right += w;
    s.left += w;
    d.left = target->left + w;
    d.right = target->right - w;
    if (d.left < d.right)
        tile();
    s.left += w;
    s.right += w;
    d.left = d.right;
    d.right = target->right;
    blt();
    // Middle row.
    int32_t ox = cell->left - s.left;
    const int32_t oy = h - s.top + cell->top;
    s.left += ox;
    s.right += ox;
    s.bottom += oy;
    s.top += oy;
    d.left = target->left;
    d.right = target->left + w;
    d.top = d.bottom;
    d.bottom = target->bottom - h;
    if (d.top < d.bottom)
        tile();
    s.right += w;
    s.left += w;
    d.left = d.right;
    d.right = target->right - w;
    if ((centre & 0xFF) != 0 && d.left < d.right && d.top < d.bottom)
        tile();
    s.left += w;
    s.right += w;
    d.left = d.right;
    d.right = target->right;
    if (d.top < d.bottom)
        tile();
    // Bottom row.
    ox = cell->left - s.left;
    const int32_t down = s.bottom - s.top;
    d.top = d.bottom;
    s.left += ox;
    s.right += ox;
    s.top += down;
    s.bottom += down;
    d.left = target->left;
    d.right = target->left + w;
    d.bottom = d.top + h;
    blt();
    s.right += w;
    s.left += w;
    d.left = d.right;
    d.right = target->right - w;
    if (d.left < d.right)
        tile();
    s.right += w;
    s.left += w;
    d.left = d.right;
    d.right = target->right;
    blt();
    Virtual<void>(context, dc::kEnd);
    if (colours)
        Release(colours);
}

// ====================================================================================
// Flood fill

// A span list node (0x18 bytes from SC4::stl_allocate): next, previous, and
// the span: the row it was filled on, its ends, and the direction to look.
struct Span {
    Span* next;
    Span* previous;
    int32_t row, x1, x2, dir;
};

// A new node at the list's end, its span zeroed first as the game's are.
Span* Append(Span* head)
{
    Span* const node = static_cast<Span*>(Allocate(sizeof(Span)));
    if (At(node) + 8 != 0) {
        node->row = 0;
        node->x1 = 0;
        node->x2 = 0;
        node->dir = 0;
    }
    Span* const last = head->previous;
    node->previous = last;
    node->next = head;
    last->next = node;
    head->previous = node;
    return node;
}

// The game's 0x008D9930 (thiscall on the list, ret 0x10): a span at the
// end; its span's address answered.
int32_t* PushSpan(Span** list, int32_t row, int32_t x1, int32_t x2, int32_t dir)
{
    Append(*list);
    Span* const node = (*list)->previous;
    node->row = row;
    node->x1 = x1;
    node->x2 = x2;
    node->dir = dir;
    return &node->row;
}

// Pixels of 1, 2, 3 and 4 bytes: what the fill compares (the 32-bit one its
// low 24 bits) and what it writes.
struct Pixels8 {
    static uint32_t Read(Address line, int32_t x) { return *Ptr<uint8_t>(line + x); }
    static void Write(Address line, int32_t x, uint32_t c) { *Ptr<uint8_t>(line + x) = static_cast<uint8_t>(c); }
};
struct Pixels16 {
    static uint32_t Read(Address line, int32_t x) { return *Ptr<uint16_t>(line + 2 * x); }
    static void Write(Address line, int32_t x, uint32_t c) { *Ptr<uint16_t>(line + 2 * x) = static_cast<uint16_t>(c); }
};
struct Pixels24 {
    static uint32_t Read(Address line, int32_t x)
    {
        const Address p = line + 3 * x;
        return (static_cast<uint32_t>(*Ptr<uint8_t>(p + 2)) << 8 | *Ptr<uint8_t>(p + 1)) << 8 | *Ptr<uint8_t>(p);
    }
    static void Write(Address line, int32_t x, uint32_t c)
    {
        const Address p = line + 3 * x;
        *Ptr<uint8_t>(p) = static_cast<uint8_t>(c);
        *Ptr<uint8_t>(p + 1) = static_cast<uint8_t>(c >> 8);
        *Ptr<uint8_t>(p + 2) = static_cast<uint8_t>(c >> 16);
    }
};
struct Pixels32 {
    static uint32_t Read(Address line, int32_t x) { return *Ptr<uint32_t>(line + 4 * x) & 0xFFFFFF; }
    static void Write(Address line, int32_t x, uint32_t c) { *Ptr<uint32_t>(line + 4 * x) = c; }
};

// The game's 0x008D9990, 0x008D9BE0, 0x008D9E30 and 0x008DA100 (x in EAX):
// a scanline flood fill from (x, y) over the pixels equal to the start's,
// with `colour`. Spans wait on a list: (row filled, x1, x2, direction to
// look); the start pushes (y, x, x, 1) and (y + 1, x, x, -1), so the row
// below the start is looked at without a bounds check (the row past the
// buffer when y is its last). A popped node is freed before its row is
// filled. Nothing when the start is outside or already has the colour.
template <typename Pixels>
void FloodFill(int32_t x, int32_t y, int32_t width, int32_t height, uint8_t* bits, int32_t stride, uint32_t colour)
{
    if ((x | y) < 0 || x >= width || y >= height)
        return;
    const Address base = At(bits);
    const uint32_t old = Pixels::Read(base + static_cast<uint32_t>(stride) * static_cast<uint32_t>(y), x);
    Span* list = static_cast<Span*>(Allocate(sizeof(Span)));
    list->next = list;
    list->previous = list;
    if (old != colour) {
        PushSpan(&list, y, x, x, 1);
        PushSpan(&list, y + 1, x, x, -1);
        for (;;) {
            Span* const node = list->next;
            if (node == list)
                break;
            const int32_t dir = node->dir;
            const int32_t row = node->row + dir;
            const int32_t x1 = node->x1;
            const int32_t x2 = node->x2;
            Span* const previous = node->previous;
            Span* const next = node->next;
            previous->next = next;
            next->previous = previous;
            Free(node);
            const Address line = base + static_cast<uint32_t>(stride) * static_cast<uint32_t>(row);
            const bool inside = [&] { return row - dir >= 0 && row - dir < height; }();
            const bool ahead = [&] { return row + dir >= 0 && row + dir < height; }();
            int32_t i = x1;
            int32_t left = 0;
            bool fill = false;
            if (x1 >= 0) {
                while (Pixels::Read(line, i) == old) {
                    Pixels::Write(line, i, colour);
                    if (--i < 0)
                        break;
                }
                if (i < x1) {
                    left = ++i;
                    if (i < x1 && inside)
                        PushSpan(&list, row, i, x1 - 1, -dir);
                    i = x1 + 1;
                    fill = true;
                }
            }
            for (;;) {
                if (!fill) {
                    ++i;
                    while (i <= x2 && Pixels::Read(line, i) != old)
                        ++i;
                    left = i;
                    if (i > x2)
                        break;
                }
                fill = false;
                while (i < width && Pixels::Read(line, i) == old) {
                    Pixels::Write(line, i, colour);
                    ++i;
                }
                if (ahead) {
                    Span* const span = Append(list);
                    span->x1 = left;
                    span->x2 = i - 1;
                    span->dir = dir;
                    span->row = row;
                }
                if (i > x2 + 1 && inside) {
                    Span* const span = Append(list);
                    span->x1 = x2 + 1;
                    span->x2 = i - 1;
                    span->dir = -dir;
                    span->row = row;
                }
            }
        }
    }
    CallThis<void>(addr::SC4PairList_Destroy, &list);
}

// The game's 0x008DA370: the buffer's flood fill by its bits per pixel
// (false for others), from the buffer's bits, stride, width and height.
bool Fill(void* target, int32_t x, int32_t y, uint32_t colour)
{
    uint8_t* const bits = Virtual<uint8_t*>(target, buffer::kBits);
    const int32_t stride = Virtual<int32_t>(target, buffer::kStride);
    const int32_t width = Virtual<int32_t>(target, buffer::kWidth);
    const int32_t height = Virtual<int32_t>(target, buffer::kHeight);
    switch (Virtual<uint32_t>(target, buffer::kBitsPerPixel)) {
    case 8: FloodFill<Pixels8>(x, y, width, height, bits, stride, colour); return true;
    case 16: FloodFill<Pixels16>(x, y, width, height, bits, stride, colour); return true;
    case 24: FloodFill<Pixels24>(x, y, width, height, bits, stride, colour); return true;
    case 32: FloodFill<Pixels32>(x, y, width, height, bits, stride, colour); return true;
    default: return false;
    }
}

// ====================================================================================
// Detours

enum Group { kTile, kFrame, kFill, kMath, kGroups };
bool g_compare[kGroups] = {};

struct Region {
    Address at = 0;
    uint32_t size = 0;
};

Region Rows(const void* base, uint32_t rows, uint32_t stride, uint32_t rowBytes)
{
    if (rows == 0 || rowBytes == 0)
        return {};
    const Address first = At(base);
    const Address last = first + (rows - 1) * stride;
    const Address low = std::min(first, last), high = std::max(first, last);
    return {low, high - low + rowBytes};
}

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

struct Nothing {
    bool operator==(const Nothing&) const { return true; }
};

// Ours first, its writes kept aside and the regions put back, then the
// game's; results and regions compared (as in pixels.cpp).
template <typename R, typename Original, typename Ours>
R Compared(const char* id, std::initializer_list<Region> regions, Original&& original, Ours&& ours)
{
    std::vector<std::vector<uint8_t>> before, mine;
    for (const Region& r : regions) {
        const auto* p = Ptr<const uint8_t>(r.at);
        before.emplace_back(p, p + r.size);
    }
    const R ourResult = ours();
    std::size_t k = 0;
    for (const Region& r : regions) {
        auto* p = Ptr<uint8_t>(r.at);
        mine.emplace_back(p, p + r.size);
        std::memcpy(p, before[k++].data(), r.size);
    }
    const R result = original();
    bool same = result == ourResult;
    std::string game, text;
    if (!same) {
        game = "result " + Hex(reinterpret_cast<const uint8_t*>(&result), sizeof(R));
        text = "result " + Hex(reinterpret_cast<const uint8_t*>(&ourResult), sizeof(R));
    }
    k = 0;
    for (const Region& r : regions) {
        const auto* p = Ptr<const uint8_t>(r.at);
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

// Math

using ShadeFn = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, int32_t);
using ClassIDFn = uint32_t(__cdecl*)();
using FloatsFn = uint32_t(__cdecl*)(float, float, float, float);
using MatrixFn = void(__cdecl*)(float*, const float*, const float*);
ShadeFn g_shade = nullptr;
ClassIDFn g_classID = nullptr;
FloatsFn g_floats = nullptr;
MatrixFn g_matrix = nullptr;
MatrixFn g_vector = nullptr;

uint32_t __cdecl DShade(uint32_t red, uint32_t green, uint32_t blue, int32_t delta)
{
    OPENSC4_FIRST("Shade 0x008D8760");
    if (!g_compare[kMath])
        return Shade(red, green, blue, delta);
    return compare::Call("blits.shade", [&] { return g_shade(red, green, blue, delta); },
                         [&] { return Shade(red, green, blue, delta); });
}

uint32_t __cdecl DSoftwareClassID()
{
    OPENSC4_FIRST("SoftwareClassID 0x008DA490");
    if (!g_compare[kMath])
        return SoftwareClassID();
    return compare::Call("blits.classid", [&] { return g_classID(); }, [&] { return SoftwareClassID(); });
}

uint32_t __cdecl DColourFromFloats(float red, float green, float blue, float alpha)
{
    OPENSC4_FIRST("ColourFromFloats 0x008DA4A0");
    if (!g_compare[kMath])
        return ColourFromFloats(red, green, blue, alpha);
    return compare::Call("blits.floats", [&] { return g_floats(red, green, blue, alpha); },
                         [&] { return ColourFromFloats(red, green, blue, alpha); });
}

void __cdecl DMatrixMultiply(float* out, const float* a, const float* b)
{
    OPENSC4_FIRST("MatrixMultiply 0x008DA610");
    if (!g_compare[kMath]) {
        MatrixMultiply(out, a, b);
        return;
    }
    Compared<Nothing>(
        "blits.matrix", {Rows(out, 1, 0, 64)},
        [&] {
            g_matrix(out, a, b);
            return Nothing{};
        },
        [&] {
            MatrixMultiply(out, a, b);
            return Nothing{};
        });
}

void __cdecl DVectorTimesMatrix(float* out, const float* v, const float* m)
{
    OPENSC4_FIRST("VectorTimesMatrix 0x008DA830");
    if (!g_compare[kMath]) {
        VectorTimesMatrix(out, v, m);
        return;
    }
    Compared<Nothing>(
        "blits.vector", {Rows(out, 1, 0, 16)},
        [&] {
            g_vector(out, v, m);
            return Nothing{};
        },
        [&] {
            VectorTimesMatrix(out, v, m);
            return Nothing{};
        });
}

OPENSC4_CDECL_THUNK(TShade, DShade, 16)
OPENSC4_CDECL_THUNK(TSoftwareClassID, DSoftwareClassID, 0)
OPENSC4_CDECL_THUNK(TColourFromFloats, DColourFromFloats, 16)
OPENSC4_CDECL_THUNK(TMatrixMultiply, DMatrixMultiply, 12)
OPENSC4_CDECL_THUNK(TVectorTimesMatrix, DVectorTimesMatrix, 12)

// Frames and tiles (ours only).

using OutlineFn = void(__cdecl*)(void*, const Rect*, uint32_t, int32_t);
OutlineFn g_outline = nullptr;
OutlineFn g_bevel = nullptr;
void* g_panel = nullptr;
void* g_panelRGB = nullptr;
void* g_tileBlit = nullptr;
void* g_stretch = nullptr;
void* g_threePart = nullptr;
void* g_drawFrame = nullptr;

void __cdecl DOutline(void* context, const Rect* r, uint32_t colour, int32_t width)
{
    OPENSC4_FIRST("Outline 0x008D9360");
    Outline(context, r, colour, width);
}

void __cdecl DBevel(void* context, const Rect* r, uint32_t light, uint32_t dark)
{
    OPENSC4_FIRST("Bevel 0x008D9470");
    Bevel(context, r, light, dark);
}

void __cdecl DPanel(void* context, const Rect* r, uint32_t red, uint32_t green, uint32_t blue, uint32_t sunken)
{
    OPENSC4_FIRST("Panel 0x008D9870");
    Panel(context, r, red, green, blue, sunken);
}

void __cdecl DPanelRGB(void* context, const Rect* r, uint32_t rgb, uint32_t sunken, uint32_t unused)
{
    OPENSC4_FIRST("PanelRGB 0x008D9900");
    PanelRGB(context, r, rgb, sunken, unused);
}

bool __cdecl DTileBlit(void* target, void* source, const Rect* sourceArea, const Rect* targetArea, int32_t shiftX,
                       int32_t shiftY, const Rect* clip)
{
    OPENSC4_FIRST("TileBlit 0x008D8BC0");
    return TileBlit(target, source, sourceArea, targetArea, shiftX, shiftY, clip);
}

bool __cdecl DStretchBlit(void* target, void* source, const Rect* sourceArea, const Rect* targetArea, const Rect* clip)
{
    OPENSC4_FIRST("StretchBlit 0x008D9020");
    return StretchBlit(target, source, sourceArea, targetArea, clip);
}

bool __cdecl DThreePart(void* target, void* source, const Rect* sourceArea, const Rect* targetArea, uint32_t middle)
{
    OPENSC4_FIRST("ThreePart 0x008D9550");
    return ThreePart(target, source, sourceArea, targetArea, middle);
}

void __cdecl DDrawFrame(void* context, void* picture, const Rect* cell, const Rect* target, uint32_t centre)
{
    OPENSC4_FIRST("DrawFrame 0x008D8800");
    DrawFrame(context, picture, cell, target, centre);
}

OPENSC4_CDECL_THUNK(TOutline, DOutline, 16)
OPENSC4_CDECL_THUNK(TBevel, DBevel, 16)
OPENSC4_CDECL_THUNK(TPanel, DPanel, 24)
OPENSC4_CDECL_THUNK(TPanelRGB, DPanelRGB, 20)
OPENSC4_CDECL_THUNK(TTileBlit, DTileBlit, 28)
OPENSC4_CDECL_THUNK(TStretchBlit, DStretchBlit, 20)
OPENSC4_CDECL_THUNK(TThreePart, DThreePart, 20)
OPENSC4_CDECL_THUNK(TDrawFrame, DDrawFrame, 20)

// Fills

using PushSpanFn = int32_t*(__fastcall*)(Span**, void*, int32_t, int32_t, int32_t, int32_t);
using FillFn = uint8_t(__cdecl*)(void*, int32_t, int32_t, uint32_t);
PushSpanFn g_pushSpan = nullptr;
FillFn g_fill = nullptr;
void* g_flood[4] = {};

int32_t* __fastcall DPushSpan(Span** list, void*, int32_t row, int32_t x1, int32_t x2, int32_t dir)
{
    OPENSC4_FIRST("PushSpan 0x008D9930");
    if (g_compare[kFill])
        return g_pushSpan(list, nullptr, row, x1, x2, dir);
    return PushSpan(list, row, x1, x2, dir);
}

OPENSC4_THUNK(TPushSpan, DPushSpan, 16)

// A flood fill of the game's called with x in EAX.
__declspec(naked) void __cdecl CallGameFlood(void*, int32_t, int32_t, int32_t, int32_t, uint8_t*, int32_t, uint32_t)
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

// The bytes per pixel of the four fills.
constexpr uint32_t kFloodBytes[4] = {1, 2, 3, 4};
constexpr const char* kFloodIds[4] = {"blits.flood8", "blits.flood16", "blits.flood24", "blits.flood32"};

void FloodOf(int kind, int32_t x, int32_t y, int32_t width, int32_t height, uint8_t* bits, int32_t stride, uint32_t colour)
{
    switch (kind) {
    case 0: FloodFill<Pixels8>(x, y, width, height, bits, stride, colour); break;
    case 1: FloodFill<Pixels16>(x, y, width, height, bits, stride, colour); break;
    case 2: FloodFill<Pixels24>(x, y, width, height, bits, stride, colour); break;
    default: FloodFill<Pixels32>(x, y, width, height, bits, stride, colour); break;
    }
}

// What a fill may write when the start is inside: rows 0 to height - 1,
// and the row past the buffer when the start is on the last row (see
// FloodFill).
Region FloodRegion(int kind, int32_t x, int32_t y, int32_t width, int32_t height, uint8_t* bits, int32_t stride)
{
    if ((x | y) < 0 || x >= width || y >= height)
        return {};
    return Rows(bits, static_cast<uint32_t>(height) + (y == height - 1 ? 1 : 0), static_cast<uint32_t>(stride),
                static_cast<uint32_t>(width) * kFloodBytes[kind]);
}

template <int Kind>
void __cdecl DFlood(int32_t x, int32_t y, int32_t width, int32_t height, uint8_t* bits, int32_t stride, uint32_t colour)
{
    static bool logged = false;
    if (!logged) {
        char name[48];
        std::snprintf(name, sizeof name, "FloodFill%u", kFloodBytes[Kind] * 8);
        FirstCall(name, logged);
    }
    if (!g_compare[kFill]) {
        FloodOf(Kind, x, y, width, height, bits, stride, colour);
        return;
    }
    Compared<Nothing>(
        kFloodIds[Kind], {FloodRegion(Kind, x, y, width, height, bits, stride)},
        [&] {
            CallGameFlood(g_flood[Kind], x, y, width, height, bits, stride, colour);
            return Nothing{};
        },
        [&] {
            FloodOf(Kind, x, y, width, height, bits, stride, colour);
            return Nothing{};
        });
}

void __cdecl DFlood8(int32_t x, int32_t y, int32_t width, int32_t height, uint8_t* bits, int32_t stride, uint32_t colour)
{
    DFlood<0>(x, y, width, height, bits, stride, colour);
}

void __cdecl DFlood16(int32_t x, int32_t y, int32_t width, int32_t height, uint8_t* bits, int32_t stride, uint32_t colour)
{
    DFlood<1>(x, y, width, height, bits, stride, colour);
}

void __cdecl DFlood24(int32_t x, int32_t y, int32_t width, int32_t height, uint8_t* bits, int32_t stride, uint32_t colour)
{
    DFlood<2>(x, y, width, height, bits, stride, colour);
}

void __cdecl DFlood32(int32_t x, int32_t y, int32_t width, int32_t height, uint8_t* bits, int32_t stride, uint32_t colour)
{
    DFlood<3>(x, y, width, height, bits, stride, colour);
}

// The fills' registers: x in EAX, six arguments on the stack (the caller
// pops them); ECX and EDX kept.
#define OPENSC4_FLOOD_THUNK(Name, Target)                                                                        \
    __declspec(naked) void Name()                                                                              \
    {                                                                                                          \
        __asm push ecx                                                                                         \
        __asm push edx                                                                                         \
        OPENSC4_REPUSH_24                                                                                      \
        __asm push eax                                                                                         \
        __asm call Target                                                                                      \
        __asm add esp, 28                                                                                      \
        __asm pop edx                                                                                          \
        __asm pop ecx                                                                                          \
        __asm ret                                                                                              \
    }

OPENSC4_FLOOD_THUNK(TFlood8, DFlood8)
OPENSC4_FLOOD_THUNK(TFlood16, DFlood16)
OPENSC4_FLOOD_THUNK(TFlood24, DFlood24)
OPENSC4_FLOOD_THUNK(TFlood32, DFlood32)

uint8_t __cdecl DFill(void* target, int32_t x, int32_t y, uint32_t colour)
{
    OPENSC4_FIRST("Fill 0x008DA370");
    if (!g_compare[kFill])
        return Fill(target, x, y, colour) ? 1 : 0;
    // The region from the buffer's own answers (getters only).
    uint8_t* const bits = Virtual<uint8_t*>(target, buffer::kBits);
    const int32_t stride = Virtual<int32_t>(target, buffer::kStride);
    const int32_t width = Virtual<int32_t>(target, buffer::kWidth);
    const int32_t height = Virtual<int32_t>(target, buffer::kHeight);
    const uint32_t bpp = Virtual<uint32_t>(target, buffer::kBitsPerPixel);
    Region region;
    if (bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32)
        region = FloodRegion(static_cast<int>(bpp / 8 - 1), x, y, width, height, bits, stride);
    return Compared<uint8_t>(
        "blits.fill", {region}, [&] { return static_cast<uint8_t>(g_fill(target, x, y, colour) & 0xFF); },
        [&] { return static_cast<uint8_t>(Fill(target, x, y, colour) ? 1 : 0); });
}

OPENSC4_CDECL_THUNK(TFill, DFill, 16)

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
    log::Write("blits: %s: %zu functions %s", name, N,
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

template <typename Fn>
void** Slot(Fn* fn)
{
    return reinterpret_cast<void**>(fn);
}

bool InstallTile(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZBlit_DrawFrame, &TDrawFrame, &g_drawFrame},
        {addr::GZBuffer_TileBlit, &TTileBlit, &g_tileBlit},
        {addr::GZBlit_StretchBlit, &TStretchBlit, &g_stretch},
        {addr::GZBlit_ThreePart, &TThreePart, &g_threePart},
    };
    return InstallAll(mode, kTile, "tile", detours);
}

bool InstallFrame(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZBlit_Outline, &TOutline, Slot(&g_outline)},
        {addr::GZBlit_Bevel, &TBevel, Slot(&g_bevel)},
        {addr::GZBlit_Panel, &TPanel, &g_panel},
        {addr::GZBlit_PanelRGB, &TPanelRGB, &g_panelRGB},
    };
    return InstallAll(mode, kFrame, "frame", detours);
}

bool InstallFill(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZBlit_PushSpan, &TPushSpan, Slot(&g_pushSpan)},
        {addr::GZBlit_FloodFill8, &TFlood8, &g_flood[0]},
        {addr::GZBlit_FloodFill16, &TFlood16, &g_flood[1]},
        {addr::GZBlit_FloodFill24, &TFlood24, &g_flood[2]},
        {addr::GZBlit_FloodFill32, &TFlood32, &g_flood[3]},
        {addr::GZBlit_FloodFill, &TFill, Slot(&g_fill)},
    };
    return InstallAll(mode, kFill, "fill", detours);
}

bool InstallMath(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZBlit_Shade, &TShade, Slot(&g_shade)},
        {addr::GZBlit_SoftwareClassID, &TSoftwareClassID, Slot(&g_classID)},
        {addr::GZBlit_ColourFromFloats, &TColourFromFloats, Slot(&g_floats)},
        {addr::GZBlit_MatrixMultiply, &TMatrixMultiply, Slot(&g_matrix)},
        {addr::GZBlit_VectorTimesMatrix, &TVectorTimesMatrix, Slot(&g_vector)},
    };
    return InstallAll(mode, kMath, "math", detours);
}

const bool g_registered[] = {
    registry::Register({"renderer.blits.tile", "renderer/blits", registry::Kind::Detour, registry::Phase::Attach,
                        addr::GZBuffer_TileBlit,
                        "Our picture blits: the nine-cell frame, TileBlit, the stretched copy and the three-part "
                        "strip (4 detours; docs/pixels.md)",
                        false, InstallTile}),
    registry::Register({"renderer.blits.frame", "renderer/blits", registry::Kind::Detour, registry::Phase::Attach,
                        addr::GZBlit_Panel,
                        "Our outlines on a drawing context: the thick outline, the bevel and the bevelled panels "
                        "(4 detours; docs/pixels.md)",
                        false, InstallFrame}),
    registry::Register({"renderer.blits.fill", "renderer/blits", registry::Kind::Detour, registry::Phase::Attach,
                        addr::GZBlit_FloodFill,
                        "Our flood fill of a buffer: by bits per pixel, the four fills and the span push (6 detours; "
                        "docs/pixels.md)",
                        true, InstallFill}),
    registry::Register({"renderer.blits.math", "renderer/blits", registry::Kind::Detour, registry::Phase::Attach,
                        addr::GZBlit_Shade,
                        "Our colour and matrix helpers: the shade, the Software driver's class ID, a colour from "
                        "floats, the matrix product and the vector times a matrix (5 detours; docs/pixels.md)",
                        true, InstallMath}),
};

} // namespace
} // namespace opensc4::blits
