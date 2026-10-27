// Our drawing context of the GZ framework's software rasterizer (docs/
// renderer.md, "The software rasterizer"): every method of the context
// (vtable 0x00AC16C8) that ran but QueryInterface and SetTarget, all
// detours here, off by default, in two groups:
//
//   renderer.raster.context  36 small methods: AddRef and Release, the two
//                            state stacks (the origin and clip at +0x7C, the
//                            settings at +0x88), the clip, origin, colour,
//                            mode, flag and pen setters, the source setters,
//                            the rectangle fills, Blt and TiledBlt, and the
//                            batch count
//   renderer.raster.draws    the draws: Draw and DrawShape (slots 44, 45),
//                            the two shapes that ran (filled rectangles and
//                            lines), the rectangle spans, the lines and the
//                            line stepper (7)
//
// Left the game's: QueryInterface (slot 0) and SetTarget (17, which makes
// the rasterizer through the framework, so the mock test cannot reach it),
// and the seven shapes that never ran.
//
// Most of these hand their state to the rasterizer (+0x28) or call the
// context's own slots, both through their vtables as the game does, so the
// context's slots reach ours or the game's alike. Integers become floats on
// the x87 under the game's 53-bit precision control: sums and quotients are
// worked out in doubles here and rounded to float once, which is what the x87
// gives. __ftol2 truncates and keeps the low 32 bits of the 64-bit result.
//
// Return values: every one of ours answers what the game's leaves in EAX
// (the last call's answer, the argument, a pointer), because callers do read
// some of them: the panel code at 0x0082A6AD reads the origin through
// GetOrigin's answer, the point it was given. The one exception is an
// unchanged pen width, where the game's leaves the FPU status word.
//
// No compare mode: the results are calls on the rasterizer, which would run
// twice. The autotest (raster_autotest.cpp, "context") drives every method
// by address on a context of the game's own constructor over a recording
// rasterizer and target, so a run with the group off hashes the game's calls
// and one with it on hashes ours.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::raster {
namespace {

struct Rect {
    int32_t left, top, right, bottom;
};

template <typename T>
struct Vector {
    T* begin;
    T* end;
    T* capacity;
};

// The origin and clip, as pushed by slot 3.
struct Transform {
    float originX, originY;
    Rect clip;
};

// The settings, as pushed by slot 10.
struct Settings {
    uint8_t flag44, flag45, unused[2];
    int32_t mode;
    float pen;
    int32_t penPixels;
};

struct Context {
    void** vtable;
    uint32_t colours[4];      // 0x04: 0x00RRGGBB
    uint32_t native[4];       // 0x14: the target's pixel values of them
    void* target;             // 0x24
    void* rasterizer;         // 0x28
    Transform transform;      // 0x2C
    Settings settings;        // 0x44
    Rect area;                // 0x54: the whole target
    Rect source;              // 0x64: the source rectangle, set by the rasterizer
    uint8_t sourceSet;        // 0x74
    uint8_t sourcePart;       // 0x75
    uint8_t unused[2];
    int32_t batch;            // 0x78
    Vector<Transform> transforms; // 0x7C
    Vector<Settings> stack;       // 0x88
    Vector<float> points;     // 0x94: Draw's points plus the origin
    uint32_t swappedA[3];     // 0xA0: DrawShape swaps these with the next three
    uint32_t swappedB[3];     // 0xAC
    Vector<uint32_t> spans;   // 0xB8: the shapes' spans
    int32_t references;       // 0xC4
};
static_assert(sizeof(Context) == 200 && offsetof(Context, settings) == 0x44 && offsetof(Context, batch) == 0x78 &&
                  offsetof(Context, references) == 0xC4,
              "the game's context is 200 bytes");

// A vtable method of the game's (thiscall) called as __fastcall with EDX
// unused.
template <typename R, typename... A>
R Call(void* self, int slot, A... args)
{
    using Fn = R(__fastcall*)(void*, void*, A...);
    return reinterpret_cast<Fn>((*static_cast<void***>(self))[slot])(self, nullptr, args...);
}

// The rasterizer's slots used here (the names are what the context hands
// them, nothing more is known).
enum RasterSlot {
    kRasterDetach = 4,
    kRasterClip = 6,
    kRasterColours = 7,
    kRasterMode = 8,
    kRasterFlag45 = 9,
    kRasterFlag44 = 10,
    kRasterPen = 11,
    kRaster12 = 12,
    kRasterSource = 13,
    kRasterWhole = 14,
    kRasterPart = 15,
    kRaster16 = 16,
    kRaster17 = 17,
    kRaster18 = 18,
    kRaster19 = 19,
    kRaster20 = 20,
};

// The context's own slots that its methods call.
enum ContextSlot {
    kSetColour = 20,
    kSetMode = 22,
    kSetFlag45 = 23,
    kSetFlag44 = 24,
    kSetPenWidth = 25,
    kSlot26 = 26,
    kClearSource = 28,
    kSetSource = 29,
    kUseWholeSource = 30,
    kUseSourcePart = 31,
    kFillRect = 35,
    kDraw = 44,
    kDrawShape = 45,
};

constexpr float kOne = 1.0f;

// __ftol2: truncation toward zero, the low 32 bits; out of range or NaN the
// x87's integer indefinite (2^63), whose low 32 bits are 0.
int32_t Ftol(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0))
        return 0;
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// 0x0079DD60: `out` is a and b's overlap (a.left < b.right and so on, all
// four strict), or all zero and false. Every field is read before it is
// written, so `out` may be a or b.
bool Intersect(Rect& out, const Rect& a, const Rect& b)
{
    if (a.right <= b.left || a.left >= b.right || a.bottom <= b.top || a.top >= b.bottom) {
        out = Rect{0, 0, 0, 0};
        return false;
    }
    const Rect r{a.left > b.left ? a.left : b.left, a.top > b.top ? a.top : b.top,
                 a.right < b.right ? a.right : b.right, a.bottom < b.bottom ? a.bottom : b.bottom};
    out = r;
    return true;
}

// The clip handed to the rasterizer: cut to the source rectangle while a
// whole source is set. Answers the rasterizer's answer.
uint32_t HandOnClip(Context* c)
{
    if (c->sourceSet && !c->sourcePart) {
        Rect cut;
        Intersect(cut, c->transform.clip, c->source);
        return Call<uint32_t>(c->rasterizer, kRasterClip, &cut);
    }
    return Call<uint32_t>(c->rasterizer, kRasterClip, &c->transform.clip);
}

// The game's integer sums and differences, which wrap.
int32_t Wrap(int64_t value) { return static_cast<int32_t>(static_cast<uint32_t>(value)); }

uint32_t Word(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// ====================================================================================
// Reference count

uint32_t __fastcall AddRef(Context* c, void*)
{
    return static_cast<uint32_t>(++c->references);
}

uint32_t __fastcall Release(Context* c, void*)
{
    const int32_t left = --c->references;
    if (left == 0) {
        reinterpret_cast<void(__fastcall*)(Context*, void*)>(addr::GZDrawContext_Destroy)(c, nullptr);
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(c);
    }
    return static_cast<uint32_t>(left);
}

// ====================================================================================
// The two stacks

// Answers what the push answers (the record's place, when it had room).
uint32_t __fastcall PushTransform(Context* c, void*)
{
    return reinterpret_cast<uint32_t(__fastcall*)(void*, void*, const Transform*)>(addr::SC4CornerVector_PushBack)(
        &c->transforms, nullptr, &c->transform);
}

uint32_t __fastcall PopTransform(Context* c, void*)
{
    if (c->transforms.begin == c->transforms.end)
        return Word(c);
    c->transform = c->transforms.end[-1];
    --c->transforms.end;
    return Call<uint32_t>(c->rasterizer, kRasterClip, &c->transform.clip);
}

int32_t __fastcall TransformDepth(Context* c, void*)
{
    const int32_t bytes = static_cast<int32_t>(reinterpret_cast<uintptr_t>(c->transforms.end) -
                                               reinterpret_cast<uintptr_t>(c->transforms.begin));
    return bytes / static_cast<int32_t>(sizeof(Transform));
}

uint32_t __fastcall PushSettings(Context* c, void*)
{
    Settings* const end = c->stack.end;
    if (end == c->stack.capacity) {
        // STLport's insert_overflow (position, value, tag, count, at end);
        // the tag is only a type.
        uint8_t tag = 0;
        return reinterpret_cast<uint32_t(__fastcall*)(void*, void*, Settings*, const Settings*, const void*, uint32_t,
                                                      uint32_t)>(addr::SC4Vector16_InsertOverflow)(
            &c->stack, nullptr, end, &c->settings, &tag, 1, 1);
    }
    if (end)
        *end = c->settings;
    ++c->stack.end;
    return Word(end);
}

uint32_t __fastcall PopSettings(Context* c, void*)
{
    if (c->stack.begin == c->stack.end)
        return Word(c->stack.begin);
    const Settings* const last = c->stack.end - 1;
    Call<uint32_t>(c, kSetFlag44, static_cast<uint32_t>(last->flag44));
    Call<uint32_t>(c, kSetFlag45, static_cast<uint32_t>(last->flag45));
    Call<uint32_t>(c, kSetMode, last->mode);
    const uint32_t answer = Call<uint32_t>(c, kSetPenWidth, last->pen);
    --c->stack.end;
    return answer;
}

// ====================================================================================
// Clip and origin

uint32_t __fastcall ResetClip(Context* c, void*)
{
    c->transform.clip = c->area;
    return static_cast<uint32_t>(c->area.bottom);
}

uint32_t __fastcall ClipTo(Context* c, void*, const Rect* r)
{
    const int32_t dy = Ftol(c->transform.originY), dx = Ftol(c->transform.originX);
    Rect moved{Wrap(int64_t{r->left} + dx), Wrap(int64_t{r->top} + dy), Wrap(int64_t{r->right} + dx),
               Wrap(int64_t{r->bottom} + dy)};
    Intersect(c->transform.clip, c->transform.clip, moved);
    if (c->sourceSet && !c->sourcePart) {
        Intersect(moved, c->transform.clip, c->source);
        return Call<uint32_t>(c->rasterizer, kRasterClip, &moved);
    }
    return Call<uint32_t>(c->rasterizer, kRasterClip, &c->transform.clip);
}

uint32_t __fastcall ClipToAbsolute(Context* c, void*, const Rect* r)
{
    Intersect(c->transform.clip, c->transform.clip, *r);
    return Call<uint32_t>(c->rasterizer, kRasterClip, &c->transform.clip);
}

// Answers the point (the game's leaves it in EAX).
uint32_t __fastcall SetOrigin(Context* c, void*, const int32_t* point)
{
    c->transform.originX = static_cast<float>(point[0]);
    c->transform.originY = static_cast<float>(point[1]);
    return Word(point);
}

// Answers the point: callers read the origin through the answer (0x0082A6AD).
int32_t* __fastcall GetOrigin(Context* c, void*, int32_t* point)
{
    const int32_t x = Ftol(c->transform.originX), y = Ftol(c->transform.originY);
    point[0] = x;
    point[1] = y;
    return point;
}

uint32_t __fastcall HasTarget(Context* c, void*) { return c->target != nullptr; }

// ====================================================================================
// Settings

uint32_t __fastcall ResetSettings(Context* c, void*)
{
    Call<uint32_t>(c, kSetMode, int32_t{0});
    Call<uint32_t>(c, kSetFlag45, uint32_t{0});
    Call<uint32_t>(c, kSetFlag44, uint32_t{0});
    Call<uint32_t>(c, kSetPenWidth, kOne);
    Call<uint32_t>(c, kClearSource);
    Call<uint32_t>(c, kUseWholeSource);
    return Call<uint32_t>(c, kSlot26);
}

uint32_t __fastcall SetColour(Context* c, void*, int32_t index, uint32_t rgb)
{
    c->colours[index] = rgb;
    const uint32_t native = Call<uint32_t>(c->target, 30, rgb >> 16, rgb >> 8, rgb);
    c->native[index] = native;
    if (c->rasterizer)
        return Call<uint32_t>(c->rasterizer, kRasterColours, uint32_t{0}, uint32_t{4}, &c->colours[0]);
    return native;
}

uint32_t __fastcall SetForeColour(Context* c, void*, uint32_t rgb)
{
    return Call<uint32_t>(c, kSetColour, int32_t{0}, rgb);
}

uint32_t __fastcall SetMode(Context* c, void*, int32_t mode)
{
    if (c->settings.mode != mode) {
        c->settings.mode = mode;
        if (c->rasterizer)
            return Call<uint32_t>(c->rasterizer, kRasterMode, mode);
    }
    return static_cast<uint32_t>(mode);
}

// The byte compared and kept; the whole word handed on.
uint32_t __fastcall SetFlag45(Context* c, void*, uint32_t flag)
{
    if (c->settings.flag45 != static_cast<uint8_t>(flag)) {
        c->settings.flag45 = static_cast<uint8_t>(flag);
        if (c->rasterizer)
            return Call<uint32_t>(c->rasterizer, kRasterFlag45, flag);
    }
    return flag;
}

uint32_t __fastcall SetFlag44(Context* c, void*, uint32_t flag)
{
    if (c->settings.flag44 != static_cast<uint8_t>(flag)) {
        c->settings.flag44 = static_cast<uint8_t>(flag);
        if (c->rasterizer)
            return Call<uint32_t>(c->rasterizer, kRasterFlag44, flag);
    }
    return flag;
}

// The x87's FUCOMPP: equal (not NaN) leaves it; 0 < width keeps the width,
// anything else (NaN too) is 0. Unchanged, the game's answers the FPU
// status word in AX; nothing reads it.
uint32_t __fastcall SetPenWidth(Context* c, void*, float width)
{
    if (c->settings.pen == width)
        return 0;
    const float kept = 0.0f < width ? width : 0.0f;
    c->settings.pen = kept;
    const int32_t pixels = Ftol(static_cast<double>(kept) + 0.5);
    c->settings.penPixels = pixels == 0 ? 1 : pixels;
    if (c->rasterizer)
        return Call<uint32_t>(c->rasterizer, kRasterPen, kept);
    return static_cast<uint32_t>(pixels);
}

uint32_t __fastcall Slot26(Context* c, void*)
{
    return Call<uint32_t>(c->rasterizer, kRaster12, uint32_t{0}, uint32_t{0}, uint32_t{0});
}

// ====================================================================================
// The source

uint32_t __fastcall ClearSource(Context* c, void*)
{
    Call<uint32_t>(c, kSetSource, static_cast<void*>(nullptr), 0.0f, 0.0f, kOne, kOne);
    c->sourceSet = 0;
    return Call<uint32_t>(c->rasterizer, kRasterClip, &c->transform.clip);
}

uint32_t __fastcall SetSource(Context* c, void*, void* source, float x, float y, float scaleX, float scaleY)
{
    const float atY = static_cast<float>(static_cast<double>(y) + c->transform.originY);
    const float atX = static_cast<float>(static_cast<double>(x) + c->transform.originX);
    c->sourceSet = Call<uint8_t>(c->rasterizer, kRasterSource, source, atX, atY, scaleX, scaleY, &c->source);
    return HandOnClip(c);
}

uint32_t __fastcall UseWholeSource(Context* c, void*)
{
    c->sourcePart = 0;
    Call<uint32_t>(c->rasterizer, kRasterWhole);
    return HandOnClip(c);
}

uint32_t __fastcall UseSourcePart(Context* c, void*, const Rect* part)
{
    c->sourcePart = 1;
    Call<uint32_t>(c->rasterizer, kRasterPart, part);
    return HandOnClip(c);
}

// ====================================================================================
// Rectangles and blits

uint32_t __fastcall FillClip(Context* c, void*)
{
    const Rect& r = c->transform.clip;
    const float points[4] = {static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
                             static_cast<float>(r.bottom)};
    return Call<uint32_t>(c, kDrawShape, uint32_t{3}, points, uint32_t{2});
}

// Pixel centres: each plus 0.5, exact in a double.
uint32_t __fastcall FrameRect(Context* c, void*, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    const float points[4] = {static_cast<float>(left + 0.5), static_cast<float>(top + 0.5),
                             static_cast<float>(right + 0.5), static_cast<float>(bottom + 0.5)};
    return Call<uint32_t>(c, kDraw, uint32_t{1}, points, uint32_t{2});
}

uint32_t __fastcall FillRect(Context* c, void*, const Rect* r)
{
    const float points[4] = {static_cast<float>(r->left), static_cast<float>(r->top), static_cast<float>(r->right),
                             static_cast<float>(r->bottom)};
    return Call<uint32_t>(c, kDraw, uint32_t{3}, points, uint32_t{2});
}

// An empty rectangle answers what the game's test left in EAX.
uint32_t __fastcall Blt(Context* c, void*, void* source, const Rect* from, const Rect* to)
{
    if (from->right <= from->left)
        return static_cast<uint32_t>(from->right);
    if (from->bottom <= from->top)
        return static_cast<uint32_t>(from->top);
    if (to->right <= to->left || to->bottom <= to->top)
        return Word(to);
    const int32_t fromWidth = Wrap(int64_t{from->right} - from->left);
    const int32_t fromHeight = Wrap(int64_t{from->bottom} - from->top);
    const int32_t toWidth = Wrap(int64_t{to->right} - to->left);
    const int32_t toHeight = Wrap(int64_t{to->bottom} - to->top);
    const float scaleY = static_cast<float>(static_cast<double>(toHeight) / fromHeight);
    const float scaleX = static_cast<float>(static_cast<double>(toWidth) / fromWidth);
    const float y = static_cast<float>(Wrap(int64_t{to->top} - from->top));
    const float x = static_cast<float>(Wrap(int64_t{to->left} - from->left));
    Call<uint32_t>(c, kSetSource, source, x, y, scaleX, scaleY);
    Call<uint32_t>(c, kUseWholeSource);
    Call<uint32_t>(c, kFillRect, to);
    return Call<uint32_t>(c, kClearSource);
}

uint32_t __fastcall TiledBlt(Context* c, void*, void* source, const Rect* from, const Rect* to, int32_t dx,
                             int32_t dy)
{
    if (from->right <= from->left || from->bottom <= from->top || to->right <= to->left)
        return static_cast<uint32_t>(from->right);
    if (to->bottom <= to->top)
        return static_cast<uint32_t>(to->top);
    const float y = static_cast<float>(Wrap(int64_t{to->top} + dy));
    const float x = static_cast<float>(Wrap(int64_t{to->left} + dx));
    Call<uint32_t>(c, kSetSource, source, x, y, kOne, kOne);
    Call<uint32_t>(c, kUseSourcePart, from);
    Call<uint32_t>(c, kFillRect, to);
    Call<uint32_t>(c, kUseWholeSource);
    return Call<uint32_t>(c, kClearSource);
}

// ====================================================================================
// The batch count and the rasterizer's own slots

uint32_t __fastcall Slot40(Context* c, void*) { return Call<uint32_t>(c->rasterizer, kRaster16); }

uint32_t __fastcall IsBatching(Context* c, void*) { return c->batch > 0; }

uint32_t __fastcall BeginBatch(Context* c, void*)
{
    ++c->batch;
    Call<void>(c->rasterizer, kRaster16);
    return Call<uint32_t>(c->rasterizer, kRaster17);
}

uint32_t __fastcall EndBatch(Context* c, void*)
{
    if (c->batch == 0)
        return 0;
    if (--c->batch != 0)
        return static_cast<uint32_t>(c->batch);
    return Call<uint32_t>(c->rasterizer, kRaster18);
}

uint32_t __fastcall Slot46(Context* c, void*) { return Call<uint32_t>(c->rasterizer, kRaster19); }

uint32_t __fastcall Slot47(Context* c, void*) { return Call<uint32_t>(c->rasterizer, kRaster20); }

// ====================================================================================
// The draws (renderer.raster.draws)
//
// Draw (slot 44) adds the origin and hands the points to DrawShape (45),
// which picks one of nine shape routines. Only two ever ran: kind 3, the
// filled rectangles of FillRect and FillClip, and kind 1, the lines of
// FrameRect. Each asks the rasterizer (its slot 21) how to draw; it answered
// 2, the context's own spans, which go to its slot 22. The other kinds and
// the other answers stay the game's, called by address.

constexpr int kRasterHow = 21;
constexpr int kRasterSpans = 22;

using ShapeFn = uint32_t(__fastcall*)(Context*, void*, const float*, int32_t);

// Wrapping arithmetic, as the game's 32-bit registers.
int32_t Add(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }
int32_t Sub(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }
int32_t Mul(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)); }
int32_t Shl(int32_t a, int n) { return static_cast<int32_t>(static_cast<uint32_t>(a) << n); }
// CDQ, XOR, SUB: INT_MIN stays itself.
int32_t Abs(int32_t a) { return a < 0 ? Sub(0, a) : a; }

uint32_t SpanCount(const Context* c)
{
    const int32_t words = static_cast<int32_t>(reinterpret_cast<uintptr_t>(c->spans.end) -
                                               reinterpret_cast<uintptr_t>(c->spans.begin)) >> 2;
    return static_cast<uint32_t>(words) >> 1;
}

uint32_t HandOnSpans(Context* c)
{
    return Call<uint32_t>(c->rasterizer, kRasterSpans, c->spans.begin, SpanCount(c));
}

// A word appended in place, or through the game's growing insert.
void PushSpanWord(Context* c, uint32_t value)
{
    if (c->spans.end != c->spans.capacity) {
        if (c->spans.end)
            *c->spans.end = value;
        ++c->spans.end;
        return;
    }
    uint32_t tag = 0;
    reinterpret_cast<void(__fastcall*)(void*, void*, uint32_t*, const uint32_t*, const void*, uint32_t, uint32_t)>(
        addr::SC4PtrVector_InsertN)(&c->spans, nullptr, c->spans.end, &value, &tag, 1, 1);
}

// 0x0082D040: a line between 24.8 fixed-point ends, one pixel a step along
// its major axis. Written after the game's registers: u is the major axis,
// v the minor (x and y, or swapped when the line is steep), each end with
// bit 0 set; the ends are swapped so u runs toward the clip's far side.
uint32_t __fastcall Line(Context* c, void*, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t u0 = x0 | 1, u1 = x1 | 1, v0 = y0 | 1, v1 = y1 | 1;
    int32_t major = Abs(Sub(u1, u0)), minor = Abs(Sub(v1, v0));
    const bool steep = minor > major;
    if (steep)
        std::swap(major, minor);
    // The clip's bounds along u and v, in the direction of travel.
    int32_t uFrom = c->transform.clip.left, uTo = Sub(c->transform.clip.right, 1);
    int32_t vFrom = c->transform.clip.top, vTo = Sub(c->transform.clip.bottom, 1);
    if (u0 > u1)
        std::swap(uFrom, uTo);
    if (v0 > v1)
        std::swap(vFrom, vTo);
    if (steep) {
        std::swap(uFrom, vFrom);
        std::swap(uTo, vTo);
        std::swap(u0, v0);
        std::swap(u1, v1);
    }
    bool reversed = false;
    if (uFrom > uTo) {
        std::swap(uFrom, uTo);
        std::swap(vFrom, vTo);
        std::swap(u0, u1);
        std::swap(v0, v1);
        reversed = true;
    }
    const int32_t vStep = vFrom < vTo ? 1 : -1;
    int32_t v = v0 >> 8, u = u0 >> 8, uLast = u1 >> 8;
    const int32_t vLast = v1 >> 8;
    // Whether an end is more than half a pixel from its pixel's centre (the
    // fractions' distances from 0x80 summed).
    const auto outside = [](int32_t a, int32_t b) { return Abs((a & 0xFF) - 0x80) + Abs((b & 0xFF) - 0x80) > 0x80; };
    // The game's two flags: the first pixel is stepped over unless `first`,
    // the last dropped unless `last`. The high half of a pixel is the sign
    // of the end's low byte.
    const bool uHigh0 = static_cast<int8_t>(u0) < 0, uHigh1 = static_cast<int8_t>(u1) < 0;
    bool first, last;
    if (reversed) {
        first = !uHigh0 && outside(u0, v0);
        last = uHigh1 || !outside(u1, v1);
    } else {
        first = !uHigh0 || !outside(u0, v0);
        last = uHigh1 && outside(u1, v1);
    }
    const int32_t twoMajor = Add(major, major);
    const int32_t step = Shl(Sub(0, minor), 1);
    int32_t err = Mul(Mul(Add(Sub(Shl(v, 8), v0), 0x80), twoMajor), vStep);
    err = Add(err, Mul(Add(Sub(Shl(u, 8), u0), 0x80), step));
    err = Add(err >> 8, twoMajor >> 1);
    if (!first) {
        u = Add(u, 1);
        err = Add(err, step);
        if (err < 0) {
            err = Add(err, twoMajor);
            v = Add(v, vStep);
        }
    }
    if (!last)
        uLast = Sub(uLast, 1);
    if (err < 0) {
        v = Add(v, vStep);
        err = Add(err, twoMajor);
    }
    if (err >= twoMajor) {
        v = Sub(v, vStep);
        err = Sub(err, twoMajor);
    }
    if (u < uFrom) {
        err = Add(err, Mul(Sub(uFrom, u), step));
        while (err < 0) {
            err = Add(err, twoMajor);
            v = Add(v, vStep);
        }
        u = uFrom;
    }
    if (uLast > uTo)
        uLast = uTo;
    if (u > uLast)
        return 0;
    // Entering the clip along v: move u to the first step inside.
    const auto enter = [&](int32_t across) {
        err = Add(err, Mul(across, twoMajor));
        v = vFrom;
        if (err >= twoMajor) {
            const int32_t k = Sub(1, Sub(err, twoMajor) / step);
            u = Add(u, k);
            err = Add(err, Mul(k, step));
        }
    };
    // Leaving it: the last step inside.
    const auto leave = [&](int32_t across) {
        const int32_t end = Sub(u, Add(Mul(across, twoMajor), err) / step);
        if (uLast > end)
            uLast = end;
    };
    if (vFrom < vTo) {
        if (v < vFrom) {
            if (vLast < vFrom)
                return 0;
            enter(Sub(vFrom, v));
        }
        if (vLast > vTo) {
            if (v > vTo)
                return 0;
            leave(Sub(vTo, v));
        }
    } else {
        if (v > vFrom) {
            if (vLast > vFrom)
                return 0;
            enter(Sub(v, vFrom));
        }
        if (vLast < vTo) {
            if (v < vTo)
                return 0;
            leave(Sub(v, vTo));
        }
    }
    if (u > uLast)
        return 0;
    const int32_t size = static_cast<int32_t>(reinterpret_cast<uintptr_t>(c->spans.end) -
                                              reinterpret_cast<uintptr_t>(c->spans.begin)) >> 2;
    reinterpret_cast<void(__fastcall*)(void*, void*, int32_t)>(addr::SC4PtrVector_Reserve)(
        &c->spans, nullptr, Add(Add(size, Mul(Sub(uLast, u), 2)), 2));
    int32_t vRow = Shl(v, 16), uRow = Shl(u, 16);
    const int32_t vRowStep = Shl(vStep, 16);
    do {
        const int32_t at = steep ? Add(uRow, v) : Add(vRow, u);
        PushSpanWord(c, static_cast<uint32_t>(at));
        PushSpanWord(c, static_cast<uint32_t>(at) + 1);
        u = Add(u, 1);
        uRow = Add(uRow, 0x10000);
        err = Add(err, step);
        if (err < 0) {
            v = Add(v, vStep);
            err = Add(err, twoMajor);
            vRow = Add(vRow, vRowStep);
        }
    } while (u <= uLast);
    if (c->spans.begin != c->spans.end)
        return HandOnSpans(c);
    return 0;
}

// 0x0082F240: each pair of points a rectangle, rounded to pixel centres.
uint32_t __fastcall FillRects(Context* c, void*, const float* points, int32_t count)
{
    c->spans.end = c->spans.begin;
    for (int32_t n = count >> 1; n > 0; --n) {
        int32_t left = Ftol(std::ceil(static_cast<double>(points[0]) - 0.5));
        int32_t top = Ftol(std::ceil(static_cast<double>(points[1]) - 0.5));
        int32_t right = Ftol(std::ceil(static_cast<double>(points[2]) - 0.5));
        int32_t bottom = Ftol(std::ceil(static_cast<double>(points[3]) - 0.5));
        points += 4;
        const Rect& clip = c->transform.clip;
        if (left < clip.left)
            left = clip.left;
        if (top < clip.top)
            top = clip.top;
        if (right > clip.right)
            right = clip.right;
        if (bottom > clip.bottom)
            bottom = clip.bottom;
        if (bottom <= top || right <= left)
            continue;
        const int32_t rows = Sub(bottom, top);
        const int32_t size = static_cast<int32_t>(reinterpret_cast<uintptr_t>(c->spans.end) -
                                                  reinterpret_cast<uintptr_t>(c->spans.begin)) >> 2;
        const uint32_t wanted = static_cast<uint32_t>(Add(size, Mul(rows, 2)));
        if (wanted < static_cast<uint32_t>(size)) {
            c->spans.end = c->spans.begin + wanted;
        } else {
            const uint32_t zero = 0;
            reinterpret_cast<void(__fastcall*)(void*, void*, uint32_t*, uint32_t, const uint32_t*)>(
                addr::SC4Uint32Vector_InsertN)(&c->spans, nullptr, c->spans.end, wanted - static_cast<uint32_t>(size),
                                               &zero);
        }
        uint32_t* at = c->spans.begin + size;
        int32_t row = Add(Shl(top, 16), right);
        for (int32_t k = rows; k != 0; --k) {
            at[1] = static_cast<uint32_t>(row);
            at[0] = static_cast<uint32_t>(Add(Sub(left, right), row));
            at += 2;
            row = Add(row, 0x10000);
        }
    }
    if (c->spans.begin != c->spans.end)
        return HandOnSpans(c);
    return Word(&c->spans);
}

// 0x0082E8A0: each pair of points a line, in 24.8 fixed point.
uint32_t __fastcall Lines(Context* c, void*, const float* points, int32_t count)
{
    c->spans.end = c->spans.begin;
    uint32_t answer = Word(c->spans.begin);
    for (int32_t n = count >> 1; n > 0; --n) {
        int32_t ends[4];
        for (int i = 0; i < 4; ++i)
            ends[i] = Ftol(std::floor(static_cast<double>(points[i]) * 256.0 + 0.5));
        points += 4;
        answer = Line(c, nullptr, ends[0], ends[1], ends[2], ends[3]);
    }
    if (c->spans.begin != c->spans.end)
        return HandOnSpans(c);
    return answer;
}

// Shape kinds 3 and 1: the rasterizer says how.
template <uint32_t Kind>
uint32_t Shape(Context* c, const float* points, int32_t count, std::uintptr_t other, ShapeFn spans)
{
    const uint32_t how = Call<uint32_t>(c->rasterizer, kRasterHow, Kind, points, count);
    if (how == 0)
        return 0;
    if (how == 1)
        return reinterpret_cast<ShapeFn>(other)(c, nullptr, points, count);
    if (how == 2)
        return spans(c, nullptr, points, count);
    return how - 2;
}

uint32_t __fastcall Fill(Context* c, void*, const float* points, int32_t count)
{
    return Shape<3>(c, points, count, addr::GZDrawContext_FillOther, &FillRects);
}

uint32_t __fastcall Outline(Context* c, void*, const float* points, int32_t count)
{
    return Shape<1>(c, points, count, addr::GZDrawContext_OutlineOther, &Lines);
}

uint32_t __fastcall DrawShape(Context* c, void*, uint32_t kind, const float* points, int32_t count)
{
    for (int i = 0; i < 3; ++i)
        std::swap(c->swappedA[i], c->swappedB[i]);
    const std::uintptr_t kinds[9] = {addr::GZDrawContext_Shape0, 0, addr::GZDrawContext_Shape2, 0,
                                     addr::GZDrawContext_Shape4, addr::GZDrawContext_Shape5,
                                     addr::GZDrawContext_Shape6, addr::GZDrawContext_Shape7,
                                     addr::GZDrawContext_Shape8};
    if (kind == 1)
        return Outline(c, nullptr, points, count);
    if (kind == 3)
        return Fill(c, nullptr, points, count);
    if (kind > 8)
        return kind;
    return reinterpret_cast<ShapeFn>(kinds[kind])(c, nullptr, points, count);
}

uint32_t __fastcall Draw(Context* c, void*, uint32_t kind, const float* points, int32_t count)
{
    const float zero = 0.0f;
    reinterpret_cast<void(__fastcall*)(void*, void*, uint32_t, const float*)>(addr::SC4FloatVector_Resize2)(
        &c->points, nullptr, static_cast<uint32_t>(Add(count, count)), &zero);
    for (int32_t i = 0; i < count; ++i) {
        c->points.begin[2 * i] = static_cast<float>(static_cast<double>(points[2 * i]) + c->transform.originX);
        c->points.begin[2 * i + 1] =
            static_cast<float>(static_cast<double>(points[2 * i + 1]) + c->transform.originY);
    }
    return Call<uint32_t>(c, kDrawShape, kind, static_cast<const float*>(c->points.begin), count);
}

// ====================================================================================
// Installation

struct Detour {
    std::uintptr_t address;
    void* detour;
};

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& d : detours) {
        const addr::Entry* entry = addr_check::Find(d.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, d.detour, &original))
            return false;
    }
    log::Write("raster: %s: %u ours", what, static_cast<unsigned>(N));
    return true;
}

bool Install(registry::Mode)
{
    static const Detour detours[] = {
        {addr::GZDrawContext_AddRef, reinterpret_cast<void*>(&AddRef)},
        {addr::GZDrawContext_Release, reinterpret_cast<void*>(&Release)},
        {addr::GZDrawContext_PushTransform, reinterpret_cast<void*>(&PushTransform)},
        {addr::GZDrawContext_ResetClip, reinterpret_cast<void*>(&ResetClip)},
        {addr::GZDrawContext_ClipTo, reinterpret_cast<void*>(&ClipTo)},
        {addr::GZDrawContext_ClipToAbsolute, reinterpret_cast<void*>(&ClipToAbsolute)},
        {addr::GZDrawContext_SetOrigin, reinterpret_cast<void*>(&SetOrigin)},
        {addr::GZDrawContext_PopTransform, reinterpret_cast<void*>(&PopTransform)},
        {addr::GZDrawContext_PushSettings, reinterpret_cast<void*>(&PushSettings)},
        {addr::GZDrawContext_PopSettings, reinterpret_cast<void*>(&PopSettings)},
        {addr::GZDrawContext_GetOrigin, reinterpret_cast<void*>(&GetOrigin)},
        {addr::GZDrawContext_TransformDepth, reinterpret_cast<void*>(&TransformDepth)},
        {addr::GZDrawContext_HasTarget, reinterpret_cast<void*>(&HasTarget)},
        {addr::GZDrawContext_ResetSettings, reinterpret_cast<void*>(&ResetSettings)},
        {addr::GZDrawContext_SetColour, reinterpret_cast<void*>(&SetColour)},
        {addr::GZDrawContext_SetForeColour, reinterpret_cast<void*>(&SetForeColour)},
        {addr::GZDrawContext_SetMode, reinterpret_cast<void*>(&SetMode)},
        {addr::GZDrawContext_SetFlag45, reinterpret_cast<void*>(&SetFlag45)},
        {addr::GZDrawContext_SetFlag44, reinterpret_cast<void*>(&SetFlag44)},
        {addr::GZDrawContext_SetPenWidth, reinterpret_cast<void*>(&SetPenWidth)},
        {addr::GZDrawContext_Slot26, reinterpret_cast<void*>(&Slot26)},
        {addr::GZDrawContext_ClearSource, reinterpret_cast<void*>(&ClearSource)},
        {addr::GZDrawContext_SetSource, reinterpret_cast<void*>(&SetSource)},
        {addr::GZDrawContext_UseWholeSource, reinterpret_cast<void*>(&UseWholeSource)},
        {addr::GZDrawContext_UseSourcePart, reinterpret_cast<void*>(&UseSourcePart)},
        {addr::GZDrawContext_FillClip, reinterpret_cast<void*>(&FillClip)},
        {addr::GZDrawContext_FrameRect, reinterpret_cast<void*>(&FrameRect)},
        {addr::GZDrawContext_FillRect, reinterpret_cast<void*>(&FillRect)},
        {addr::GZDrawContext_Blt, reinterpret_cast<void*>(&Blt)},
        {addr::GZDrawContext_TiledBlt, reinterpret_cast<void*>(&TiledBlt)},
        {addr::GZDrawContext_Slot40, reinterpret_cast<void*>(&Slot40)},
        {addr::GZDrawContext_IsBatching, reinterpret_cast<void*>(&IsBatching)},
        {addr::GZDrawContext_BeginBatch, reinterpret_cast<void*>(&BeginBatch)},
        {addr::GZDrawContext_EndBatch, reinterpret_cast<void*>(&EndBatch)},
        {addr::GZDrawContext_Slot46, reinterpret_cast<void*>(&Slot46)},
        {addr::GZDrawContext_Slot47, reinterpret_cast<void*>(&Slot47)},
    };
    return InstallAll(detours, "context methods");
}

bool InstallDraws(registry::Mode)
{
    static const Detour detours[] = {
        {addr::GZDrawContext_Draw, reinterpret_cast<void*>(&Draw)},
        {addr::GZDrawContext_DrawShape, reinterpret_cast<void*>(&DrawShape)},
        {addr::GZDrawContext_Fill, reinterpret_cast<void*>(&Fill)},
        {addr::GZDrawContext_FillRects, reinterpret_cast<void*>(&FillRects)},
        {addr::GZDrawContext_Outline, reinterpret_cast<void*>(&Outline)},
        {addr::GZDrawContext_Lines, reinterpret_cast<void*>(&Lines)},
        {addr::GZDrawContext_Line, reinterpret_cast<void*>(&Line)},
    };
    return InstallAll(detours, "draws");
}

const bool g_registered = registry::Register(
    {"renderer.raster.context", "renderer/raster", registry::Kind::Detour, registry::Phase::Attach,
     addr::GZDrawContext_AddRef,
     "Our drawing context of the software rasterizer: the 36 of its methods that run besides SetTarget and the two "
     "draws (state stacks, clip, origin, colours, settings, source, rectangles, Blt; 36 detours; docs/renderer.md)",
     false, Install});

const bool g_registeredDraws = registry::Register(
    {"renderer.raster.draws", "renderer/raster", registry::Kind::Detour, registry::Phase::Attach,
     addr::GZDrawContext_Draw,
     "Our draws of the software rasterizer's drawing context: Draw and DrawShape (slots 44, 45), the two shapes that "
     "run (filled rectangles and lines) and the line stepper (7 detours; docs/renderer.md)",
     false, InstallDraws});

} // namespace
} // namespace opensc4::raster
