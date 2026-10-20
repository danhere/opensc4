// Our cSC4PropOccupantSign's drawing (build 641; the class map and the
// fields are in propkinds.h, the logic in propkinds_sign.cpp): the geometry
// builder, which lays the board, the frame, the post, the text and the caret
// out in the sign's own frame, turns the sign to face the camera, makes the
// four boxes and tells the occupant manager and the renderer; its two
// helpers (a box from two corners, the text's vertices grown); and the
// sign's own functions behind the model interface (the virtual base at
// +0x58C, vtable 0x00AA5298): the occupant, the two boxes, the transform,
// the hit test, the draw and the texture.
//
// The model interface's 19 slots are adjustor thunks (0x005F59D0 to
// 0x005F5CD0; each subtracts the displacement at +0x588 first). They stay the
// game's: the eight that end in `jmp` to the class's own functions land in
// the detours below; the other eleven go to QueryInterface, AddRef and
// Release (the logic's and the base's), to the model part's functions
// shared with every model (0x00801890, 0x008018A0, 0x008018E0, 0x008018F0,
// 0x00801E10, 0x00801E50) or to stubs shared with other classes (0x00648F10,
// 0x004E13E0). Those stay the game's, as do the render context and the
// vector helpers the code calls by address.
//
// The functions behind the thunks take the virtual base (the object +
// 0x58C, less the displacement, which is 0) as `this`.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`, or, where the callee is behind an interface the code does not
// name, from the caller's stack (it reads its own frame at fixed offsets
// after the call, so the callee took what was pushed):
//   - a glyph (the text's textures, the service 0x00B43CE4's vector): 0x14
//     one (its size: width, height), 0x18 one (its texture coordinates: u0,
//     v0, u1, v1), 0x1C none (its texture binding), from the caller's stack;
//   - the virtual base 0x00AA5298: 0x34 none (0x005F5A50 to 0x008018E0);
//   - the occupant part 0x00AA5380: 0x14 IsInitialized none (0x00458A40),
//     0x80 IsFlagSet one (0x005E8BC0);
//   - the occupant manager 0x00B43D0C: 0x60 two;
//   - the renderer 0x00ABAB60 (the global at 0x00B43DD0): 0x78 one
//     (0x007C9220), 0x7C two (0x007C9320).
// Direct callees: the camera view's 0x007F6690 (ret 4, a float in ST0);
// the render context's 0x007D3400, 0x007D4420, 0x007D4530, 0x007D3140 and
// 0x007D4480 (ret 4), 0x007D3EF0 and 0x007D4070 (ret 8), 0x007D41E0 (ret
// 0xC), 0x007D2990 (ret 0x10), 0x007D2A30 and 0x007D3290 (ret); 0x00626330
// (ret 0x14); cdecl: 0x005D6580 (five), 0x005D7660 (two, a float in ST0),
// 0x00624070 (five), 0x005F51B0, 0x0063BFA0 (four), 0x005F4A20 (five),
// 0x005F49E0 (three).
//
// Callers: the builder from the class's Init, SetText, SetEditing and
// DoMessage (0x005F736A, 0x005F7393, 0x005F74FD, 0x005F753B, 0x005F7640;
// ours in propkinds_sign.cpp call the same address); the box helper and the
// vector helper from the builder only (0x005F6F45, 0x005F710F, 0x005F72A7;
// 0x005F697D); the eight model functions from their thunks only. None of the
// callers keeps anything in ECX or EDX across the call, or anything on the
// x87 stack. No jump or call in the exe lands inside the first five bytes of
// any of the eleven. The detours are plain functions.
#include "core/propkinds.h"

#include "core/compare.h"
#include "core/gd/gd.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace opensc4::propkinds {
namespace {

constexpr uint32_t kOne = 0x3F800000; // 1.0f
constexpr uint32_t kTransformRotated = 4; // the transform's flag: the rotation is the builder's
constexpr uint32_t kTransformScaled = 2;  // the flag the builder clears when it puts the scale back to 1

// The colour of the board's four corners, by highlight (the table at
// 0x00AA5204; SetHighlight writes the same), and of the caret, in the
// table's byte order (a vertex holds the third, second, first and fourth:
// the caret's vertices hold 6A 55 4E FF).
constexpr uint32_t kBoardColours[2] = {0xFFBEAC90, 0xCCFFFFFF};
constexpr uint8_t kCaretColour[4] = {0x4E, 0x55, 0x6A, 0xFF};

// The virtual base (the model interface) of an object whose vbptr is set.
char* VirtualBase(char* self)
{
    return self + sign::kVbptr + At<int32_t>(At<char*>(self, sign::kVbptr), 0x4);
}

// A length of pixels in metres at the camera's zoom: the camera view's
// 0x007F6690 (the argument times its +0x150) in ST0, kept at the x87's
// precision.
double Metres(void* camera, float pixels)
{
    return This<double, float>(addr::SC4CameraView_ScaleLength, camera, pixels);
}

// A vertex of five floats: x, y, z (0), u, v.
void Vertex(char* at, float x, float y, float u, float v)
{
    At<float>(at, 0x0) = x;
    At<float>(at, 0x4) = y;
    At<uint32_t>(at, 0x8) = 0;
    At<float>(at, 0xC) = u;
    At<float>(at, 0x10) = v;
}

// A quad of four such vertices from (x0, y0) to (x1, y1): the corners
// (x0, y0), (x0, y1), (x1, y1), (x1, y0) with the texture's corners (0, 0),
// (0, 1), (1, 1), (1, 0), or, turned, (1, 0), (0, 0), (0, 1), (1, 1).
void Quad(char* at, float x0, float y0, float x1, float y1, bool turned = false)
{
    if (turned) {
        Vertex(at, x0, y0, 1.0f, 0.0f);
        Vertex(at + 0x14, x0, y1, 0.0f, 0.0f);
        Vertex(at + 0x28, x1, y1, 0.0f, 1.0f);
        Vertex(at + 0x3C, x1, y0, 1.0f, 1.0f);
        return;
    }
    Vertex(at, x0, y0, 0.0f, 0.0f);
    Vertex(at + 0x14, x0, y1, 0.0f, 1.0f);
    Vertex(at + 0x28, x1, y1, 1.0f, 1.0f);
    Vertex(at + 0x3C, x1, y0, 1.0f, 0.0f);
}

// A coloured vertex: x, y, z (0) and the colour's four bytes, the table's
// third, second, first and fourth (as SetHighlight writes them).
void ColouredVertex(char* at, float x, float y, const uint8_t* colour)
{
    At<float>(at, 0x0) = x;
    At<float>(at, 0x4) = y;
    At<uint32_t>(at, 0x8) = 0;
    At<uint8_t>(at, 0xC) = colour[2];
    At<uint8_t>(at, 0xD) = colour[1];
    At<uint8_t>(at, 0xE) = colour[0];
    At<uint8_t>(at, 0xF) = colour[3];
}

void ColouredQuad(char* at, float x0, float y0, float x1, float y1, const uint8_t* colour)
{
    ColouredVertex(at, x0, y0, colour);
    ColouredVertex(at + 0x10, x0, y1, colour);
    ColouredVertex(at + 0x20, x1, y1, colour);
    ColouredVertex(at + 0x30, x1, y0, colour);
}

// The rotation turned about the vertical by the camera's heading,
// atan2(-x, -z) of the camera's direction (x at +0x54, z at +0x5C). FPATAN, FCOS and
// FSIN give 64-bit results whatever the precision control, and the game
// multiplies the sine as it is (rounded only by the product); so the angle,
// the cosine (stored as a float) and the six products with the sine are
// taken with the x87's own instructions, in the game's order. The rest is
// the game's arithmetic: each product with the cosine and those of the sine
// that the game stores are rounded to floats, except the two the third
// row's first element is made from, which it keeps.
void TurnRotation(float* m, const char* camera)
{
    const float a00 = m[0], a02 = m[2], a10 = m[3], a12 = m[5], a20 = m[6], a22 = m[8];
    float cosine;
    double s22, s12, s02, s20, s10, s00;
    __asm {
        mov eax, camera
        fld dword ptr [eax + 0x5C]
        fchs
        fld dword ptr [eax + 0x54]
        fchs
        fxch st(1)
        fpatan
        fld st(0)
        fcos
        fstp cosine
        fsin
        fld a22
        fmul st, st(1)
        fstp s22
        fld a12
        fmul st, st(1)
        fstp s12
        fld a02
        fmul st, st(1)
        fstp s02
        fld a20
        fmul st, st(1)
        fstp s20
        fld a10
        fmul st, st(1)
        fstp s10
        fld a00
        fmul st, st(1)
        fstp s00
        fstp st(0)
    }
    const double c = cosine;
    const double c20 = static_cast<double>(a20) * c;
    const float c10 = static_cast<float>(static_cast<double>(a10) * c);
    const float c00 = static_cast<float>(static_cast<double>(a00) * c);
    m[0] = static_cast<float>(static_cast<double>(c00) - static_cast<double>(static_cast<float>(s02)));
    m[3] = static_cast<float>(static_cast<double>(c10) - static_cast<double>(static_cast<float>(s12)));
    m[6] = static_cast<float>(c20 - s22);
    const float c22 = static_cast<float>(static_cast<double>(a22) * c);
    const float c12 = static_cast<float>(static_cast<double>(a12) * c);
    const float c02 = static_cast<float>(static_cast<double>(a02) * c);
    m[8] = static_cast<float>(static_cast<double>(static_cast<float>(s20)) + static_cast<double>(c22));
    m[5] = static_cast<float>(static_cast<double>(static_cast<float>(s10)) + static_cast<double>(c12));
    m[2] = static_cast<float>(static_cast<double>(static_cast<float>(s00)) + static_cast<double>(c02));
}

bool g_compare = false;

// ====================================================================================
// Not virtual (`self` is the object)
// ====================================================================================

// 0x005F4D10 (cdecl): the box with the corners `a` and `b`: the high corner
// takes the larger of each pair (b when they are equal or unordered), the
// low corner the smaller (b likewise). The high corner is copied as bits;
// the low one goes through the x87 in the game, which leaves floats as they
// are. The box's address back.
float* __cdecl MakeBox(float* out, float ax, float ay, float az, float bx, float by, float bz)
{
    static bool logged = false;
    FirstCall("sign MakeBox", logged);
    out[3] = ax > bx ? ax : bx;
    out[4] = ay > by ? ay : by;
    out[5] = az > bz ? az : bz;
    out[0] = ax < bx ? ax : bx;
    out[1] = ay < by ? ay : by;
    out[2] = az < bz ? az : bz;
    return out;
}

// 0x005F53C0 (ret 0xC): `n` copies of the vertex `value` inserted before
// `at` in a vector of 20-byte vertices (begin, end, capacity): nothing for
// 0; without room for them the game's reallocating insert (0x00626330, the
// address of its own argument as the tag, 0 last); else in place, through
// the game's helpers: with more vertices after `at` than are inserted, the
// last `n` copied past the end, the rest moved up and the gap filled; else
// the copies past the end that land there, the vertices after `at` moved
// past those, and the old ones filled. The value is copied first.
void __fastcall InsertVertices(char* vector, void*, char* at, uint32_t n, const uint32_t* value)
{
    static bool logged = false;
    FirstCall("sign InsertVertices", logged);
    if (!n)
        return;
    char* const end = At<char*>(vector, 0x4);
    const int32_t room = static_cast<int32_t>(At<char*>(vector, 0x8) - end) / 20;
    if (static_cast<uint32_t>(room) < n) {
        This<void, char*, const uint32_t*, const uint32_t**, uint32_t, uint32_t>(addr::SC4LocationVector_InsertN, vector, at, value,
                                                                                &value, n, 0u);
        return;
    }
    uint32_t copy[5];
    std::memcpy(copy, value, sizeof copy);
    uint32_t tag = 0;
    const uint32_t after = static_cast<uint32_t>(static_cast<int32_t>(end - at) / 20);
    if (after > n) {
        char* const from = end - n * 20;
        Cdecl<char*>(addr::SC4Vector20_UninitializedCopy, from, end, end, &tag);
        At<char*>(vector, 0x4) += n * 20;
        Cdecl<char*>(addr::SC4Vector20_CopyBackward, at, from, end, &tag, 0u);
        Cdecl<void>(addr::SC4Vector20_Fill, at, at + n * 20, static_cast<const uint32_t*>(copy));
        return;
    }
    Cdecl<void>(addr::SC4Vector20_UninitializedFill, end, n - after, static_cast<const uint32_t*>(copy), &tag);
    At<char*>(vector, 0x4) += (n - after) * 20;
    Cdecl<char*>(addr::SC4Vector20_UninitializedCopy, at, end, At<char*>(vector, 0x4), &tag);
    At<char*>(vector, 0x4) += after * 20;
    Cdecl<void>(addr::SC4Vector20_Fill, at, end, static_cast<const uint32_t*>(copy));
}

// A corner of the sign in the world (the builder's boxes).
struct Corner {
    float x, y, z;
};

// 0x005F5FB0: the sign's geometry. In the sign's frame (x along the text,
// y up, metres at the camera's zoom: pixels times the camera view's scale
// times the zoom's factor, the table at 0x00AA523C):
//   - the text's size: its glyphs' widths summed (4 pixels more for the
//     caret while it is edited) and the tallest glyph (24 when there is none,
//     or for an empty text not being edited, which is 1 pixel wide);
//   - the heights from the top down: the frame's top, the board's top, the
//     board's bottom (the text's height below) and the frame's bottom, 8
//     pixels of frame each side, with 64 pixels of post below the frame and
//     8 more; the post is 8 pixels wide, centred;
//   - the widths: the frame's left edge half the text and 8 pixels left of
//     the middle, the text's left and right, the frame's right edge;
//   - the board's four coloured corners (the highlight's colour, by bit 0 of
//     the model part's flags), the eleven textured quads (the text's area,
//     the frame's four corners, its four sides, the post's two pieces), the
//     text's vertices, four per glyph, from left to right (the vector grown
//     or cut to four per glyph first), and while edited the caret, 2 pixels
//     wide after the text with 2 pixels unscaled by the zoom before it;
//   - the scale put back to 1 (the transform's flag 2 cleared, its count
//     raised) when it is not; the rotation reset to the identity when the
//     builder turned it before (flag 4); then turned to the camera (flag 4
//     set, the count raised);
//   - the box in the sign's frame, then three boxes in the world (the sign,
//     the board, the post: two corners turned, scaled and moved, then made a
//     box); the prop's box is the sign's. The occupant manager is told of a
//     sign it holds (slot 24 with 0), the renderer of a shown one (slot 30,
//     then 31 with 3).
// The x87 arithmetic is the game's, with its roundings: the game keeps some
// sums on the x87 stack and stores others as floats before it uses them
// again, and the code below does the same at the same places.
void __fastcall BuildGeometry(char* self, void*)
{
    static bool logged = false;
    FirstCall("sign BuildGeometry", logged);
    const int32_t count = static_cast<int32_t>(At<char*>(self, sign::kGlyphs + 4) - At<char*>(self, sign::kGlyphs)) >> 2;
    uint32_t sum = 0, tallest = 0;
    for (int32_t i = 0; i < count; ++i) {
        uint32_t size[8] = {};
        Virtual<void>(At<char**>(self, sign::kGlyphs)[i], 0x14, static_cast<uint32_t*>(size));
        sum += size[0];
        if (size[1] > tallest)
            tallest = size[1];
    }
    if (At<uint8_t>(self, sign::kEditing)) {
        sum += 4;
        if (tallest == 0)
            tallest = 0x18;
    } else if (sum == 0) {
        sum = 1;
        tallest = 0x18;
    }

    void* const camera = Global<void*>(addr::SC4_CameraView);
    const float zoom = reinterpret_cast<const float*>(addr::cSC4PropOccupantSign_ZoomFactors)[At<int32_t>(camera, 0xC)];
    const auto scaled = [camera, zoom](float pixels) { return Metres(camera, pixels) * static_cast<double>(zoom); };
    const float width = static_cast<float>(scaled(static_cast<float>(static_cast<int32_t>(sum))));
    const float height = static_cast<float>(scaled(static_cast<float>(static_cast<int32_t>(tallest))));
    const float halfWidth = static_cast<float>(static_cast<double>(width) * 0.5); // 0x00A84D2C
    const float frame = static_cast<float>(scaled(8.0f));
    const float post = static_cast<float>(scaled(64.0f));
    const double postFoot = scaled(8.0f);
    const float postHalf = static_cast<float>(0.5 * postFoot); // 0x00A92D28, a double

    double y = static_cast<double>(frame) + static_cast<double>(frame);
    y += postFoot;
    y += post;
    y += height;
    const float frameTop = static_cast<float>(y);
    y -= frame;
    const float boardTop = static_cast<float>(y);
    y -= height;
    const float boardBottom = static_cast<float>(y);
    y -= frame;
    const float frameBottom = static_cast<float>(y);
    double x = -(static_cast<double>(frame) + static_cast<double>(halfWidth));
    const float frameLeft = static_cast<float>(x);
    x += frame;
    const float textLeft = static_cast<float>(x);
    x += width;
    const float textRight = static_cast<float>(x);
    x += frame;
    const float frameRight = static_cast<float>(x);
    double down = static_cast<double>(frameBottom) - static_cast<double>(post);
    const float postMiddle = static_cast<float>(down);
    down -= postFoot;
    const float postBottom = static_cast<float>(down);
    const float postLeft = -postHalf;

    const uint32_t highlighted = Virtual<uint32_t>(VirtualBase(self), 0x34) & 1;
    uint8_t colour[4];
    std::memcpy(colour, &kBoardColours[highlighted], 4);
    ColouredQuad(self + sign::kBoard, textLeft, boardTop, textRight, boardBottom, colour);
    char* const quads = self + sign::kQuads;
    Quad(quads + 0x000, textLeft, boardTop, textRight, boardBottom);    // the text's area
    Quad(quads + 0x050, frameLeft, frameTop, textLeft, boardTop);       // the frame's corners
    Quad(quads + 0x0A0, textRight, frameTop, frameRight, boardTop);
    Quad(quads + 0x0F0, frameLeft, boardBottom, textLeft, frameBottom);
    Quad(quads + 0x140, textRight, boardBottom, frameRight, frameBottom);
    Quad(quads + 0x190, textLeft, frameTop, textRight, boardTop);       // its top and bottom
    Quad(quads + 0x1E0, textLeft, boardBottom, textRight, frameBottom);
    Quad(quads + 0x230, frameLeft, boardTop, textLeft, boardBottom, true); // its sides, the texture turned
    Quad(quads + 0x280, textRight, boardTop, frameRight, boardBottom, true);
    Quad(quads + 0x2D0, postLeft, frameBottom, postHalf, postMiddle);   // the post
    Quad(quads + 0x320, postLeft, postMiddle, postHalf, postBottom);

    // Four vertices a glyph: the vector cut (the game's move of nothing to
    // the new end) or grown with zero vertices.
    char* const vector = self + sign::kGlyphVertices;
    const uint32_t wanted = static_cast<uint32_t>(count) << 2;
    const int32_t held = static_cast<int32_t>(At<char*>(vector, 0x4) - At<char*>(vector, 0x0)) / 20;
    if (wanted < static_cast<uint32_t>(held)) {
        char* const end = At<char*>(vector, 0x4);
        uint8_t tag = colour[3];
        At<char*>(vector, 0x4) =
            Cdecl<char*>(addr::SC4LocationVector_Move, end, end, At<char*>(vector, 0x0) + wanted * 20, &tag, 0u);
    } else {
        const uint32_t zero[5] = {0, 0, 0, 0, 0};
        InsertVertices(vector, nullptr, At<char*>(vector, 0x4), wanted - static_cast<uint32_t>(held), zero);
    }
    float left = textLeft, right = textLeft;
    float uv[8] = {};
    for (int32_t i = 0; i < count; ++i) {
        uint32_t size[8] = {};
        char* const glyph = At<char**>(self, sign::kGlyphs)[i];
        Virtual<void>(glyph, 0x18, static_cast<float*>(uv));
        Virtual<void>(glyph, 0x14, static_cast<uint32_t*>(size));
        const double advance = scaled(static_cast<float>(static_cast<double>(size[0])));
        right = static_cast<float>(advance + static_cast<double>(right));
        char* const at = At<char*>(vector, 0x0) + i * 0x50;
        Vertex(at, left, boardTop, uv[0], uv[1]);
        Vertex(at + 0x14, left, boardBottom, uv[0], uv[3]);
        Vertex(at + 0x28, right, boardBottom, uv[2], uv[3]);
        Vertex(at + 0x3C, right, boardTop, uv[2], uv[1]);
        left = right;
    }
    if (At<uint8_t>(self, sign::kEditing)) {
        const double gap = Metres(camera, 2.0f);
        const float gapF = static_cast<float>(gap);
        const float caretLeft = static_cast<float>(gap + static_cast<double>(left));
        const float caretRight = static_cast<float>((scaled(2.0f) + static_cast<double>(gapF)) + static_cast<double>(right));
        ColouredQuad(self + sign::kCaretQuad, caretLeft, boardTop, caretRight, boardBottom, kCaretColour);
    }

    if (At<uint32_t>(self, sign::kScale) != kOne) {
        At<uint32_t>(self, sign::kScale) = kOne;
        At<uint8_t>(self, sign::kTransformFlags) &= ~kTransformScaled;
        ++At<uint8_t>(self, sign::kTransformCount);
    }
    float* const m = &At<float>(self, sign::kRotation);
    if (At<uint8_t>(self, sign::kTransformFlags) & kTransformRotated) {
        static const uint32_t identity[9] = {kOne, 0, 0, 0, kOne, 0, 0, 0, kOne};
        std::memcpy(m, identity, sizeof identity);
        At<uint8_t>(self, sign::kTransformFlags) &= ~kTransformRotated;
        ++At<uint8_t>(self, sign::kTransformCount);
    }
    TurnRotation(m, static_cast<const char*>(camera));
    At<uint8_t>(self, sign::kTransformFlags) |= kTransformRotated;
    ++At<uint8_t>(self, sign::kTransformCount);

    // The box in the sign's frame: from the frame's left, the post's foot,
    // to the frame's right and top, flat.
    float* const local = &At<float>(self, sign::kLocalBox);
    local[0] = frameLeft;
    local[1] = postBottom;
    At<uint32_t>(self, sign::kLocalBox + 0x8) = 0;
    local[3] = frameRight;
    local[4] = frameTop;
    At<uint32_t>(self, sign::kLocalBox + 0x14) = 0;

    const double p0 = At<float>(self, sign::kPosition), p1 = At<float>(self, sign::kPosition + 4),
                 p2 = At<float>(self, sign::kPosition + 8);
    const double m00 = m[0], m01 = m[1], m02 = m[2], m10 = m[3], m11 = m[4], m12 = m[5], m20 = m[6], m21 = m[7], m22 = m[8];
    const double scale = At<float>(self, sign::kScale);
    const double z0 = 0.0f; // 0x00A81054: the corners' z, as a float in memory
    // The world box's corners: the low one (the box in the sign's frame, read
    // back) with each row summed z, y, x, the first two rows' sums stored as
    // floats before they are scaled; the high one from the same values with
    // the third coordinate's scaled sum kept.
    Corner low;
    {
        const double lx = local[0], ly = local[1], lz = At<float>(self, sign::kLocalBox + 0x8);
        const float a2 = static_cast<float>((lz * m22 + ly * m21) + lx * m20);
        const float a1 = static_cast<float>((lz * m12 + ly * m11) + lx * m10);
        const double a0 = (lz * m02 + ly * m01) + lx * m00;
        low.x = static_cast<float>(static_cast<double>(static_cast<float>(a0 * scale)) + p0);
        low.y = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(a1) * scale)) + p1);
        low.z = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(a2) * scale)) + p2);
    }
    Corner high;
    {
        const double hx = frameRight, hy = frameTop, hz = 0.0;
        const double b2 = (hz * m22 + hy * m21) + hx * m20;
        const double b1 = (hz * m12 + hy * m11) + hx * m10;
        const double b0 = (hz * m02 + hy * m01) + hx * m00;
        high.x = static_cast<float>(static_cast<double>(static_cast<float>(b0 * scale)) + p0);
        high.y = static_cast<float>(static_cast<double>(static_cast<float>(b1 * scale)) + p1);
        high.z = static_cast<float>(b2 * scale + p2);
    }
    float* const world = &At<float>(self, sign::kWorldBox);
    MakeBox(world, low.x, low.y, low.z, high.x, high.y, high.z);
    std::memcpy(self + layout::kMin, world, 12);
    std::memcpy(self + layout::kMax, world + 3, 12);

    // The board's and the post's corners: z is the float 0 in memory, and
    // each row is summed in the game's order, which for the middle row is
    // not the same for the low corner and the high one. The low corner's
    // three scaled values are stored as floats; the high corner's third is
    // kept.
    const auto lowCorner = [&](double cx, double cy) {
        const double e2 = (cy * m21 + m22 * z0) + cx * m20;
        const double e1 = (cy * m11 + cx * m10) + m12 * z0;
        const double e0 = (cy * m01 + m02 * z0) + cx * m00;
        return Corner{static_cast<float>(static_cast<double>(static_cast<float>(e0 * scale)) + p0),
                      static_cast<float>(static_cast<double>(static_cast<float>(e1 * scale)) + p1),
                      static_cast<float>(static_cast<double>(static_cast<float>(e2 * scale)) + p2)};
    };
    const auto highCorner = [&](double cx, double cy) {
        const double f2 = (cy * m21 + m22 * z0) + cx * m20;
        const double f1 = (cy * m11 + m12 * z0) + cx * m10;
        const double f0 = (cy * m01 + m02 * z0) + cx * m00;
        return Corner{static_cast<float>(static_cast<double>(static_cast<float>(f0 * scale)) + p0),
                      static_cast<float>(static_cast<double>(static_cast<float>(f1 * scale)) + p1),
                      static_cast<float>(f2 * scale + p2)};
    };
    const Corner boardLow = lowCorner(frameLeft, frameBottom);
    const Corner boardHigh = highCorner(frameRight, frameTop);
    MakeBox(&At<float>(self, sign::kBoardBox), boardLow.x, boardLow.y, boardLow.z, boardHigh.x, boardHigh.y, boardHigh.z);
    const Corner postLow = lowCorner(postLeft, postBottom);
    const Corner postHigh = highCorner(postHalf, frameBottom);
    MakeBox(&At<float>(self, sign::kPostBox), postLow.x, postLow.y, postLow.z, postHigh.x, postHigh.y, postHigh.z);

    char* const occupant = self + layout::kOccupant;
    if (Virtual<uint8_t>(occupant, 0x80, 1u))
        Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0x60, static_cast<void*>(occupant), 0u);
    if (Virtual<uint8_t>(occupant, 0x80, 2u)) {
        void* const renderer = Global<void*>(addr::SC4_View43DD0);
        Virtual<void>(renderer, 0x78, static_cast<void*>(VirtualBase(self)));
        Virtual<void>(renderer, 0x7C, static_cast<void*>(VirtualBase(self)), 3u);
    }
}

// ====================================================================================
// The model interface (`model` is the virtual base, the object + 0x58C)
// ====================================================================================

char* SelfOf(char* model) { return model - sign::kVirtualBase; }

// Slot 3 (0x005F5A10): the occupant part, null for a null object.
void* __fastcall GetOccupant(char* model, void*)
{
    static bool logged = false;
    FirstCall("sign GetOccupant", logged);
    return SelfOf(model) ? SelfOf(model) + layout::kOccupant : nullptr;
}

// Slot 7 (0x005F5AF0): the box in the sign's frame into `out` (its high
// corner first, then the low one).
void __fastcall GetLocalBox(char* model, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("sign GetLocalBox", logged);
    std::memcpy(out + 3, SelfOf(model) + sign::kLocalBox + 0xC, 12);
    std::memcpy(out, SelfOf(model) + sign::kLocalBox, 12);
}

// Slot 8 (0x005F5B40): the box in the world, the same way.
void __fastcall GetWorldBox(char* model, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("sign GetWorldBox", logged);
    std::memcpy(out + 3, SelfOf(model) + sign::kWorldBox + 0xC, 12);
    std::memcpy(out, SelfOf(model) + sign::kWorldBox, 12);
}

// Slot 9 (0x005F5B90): the transform into `out`: its change count raised
// (out's own), the rotation, position and scale, then the flags.
void __fastcall GetTransform(char* model, void*, uint8_t* out)
{
    static bool logged = false;
    FirstCall("sign GetTransform", logged);
    ++out[0];
    std::memcpy(out + 4, SelfOf(model) + sign::kRotation, 0x34);
    out[1] = At<uint8_t>(SelfOf(model), sign::kTransformFlags);
}

// Slot 10 (0x005F5C30): the transform from `in`: the sign's change count
// raised, the rotation, position and scale, then the flags.
void __fastcall SetTransform(char* model, void*, const uint8_t* in)
{
    static bool logged = false;
    FirstCall("sign SetTransform", logged);
    ++At<uint8_t>(SelfOf(model), sign::kTransformCount);
    std::memcpy(SelfOf(model) + sign::kRotation, in + 4, 0x34);
    At<uint8_t>(SelfOf(model), sign::kTransformFlags) = in[1];
}

// Slot 11 (0x005F4DF0, ret 4): the ray (origin +0, direction +0xC, the
// nearest distance so far +0x28) against the board's box, then the post's:
// at the first that it meets (0x005D6580, the point into a copy) no farther
// than the distance so far (0x005D7660 from the origin, not a number never),
// the distance is kept and the answer is true. False when neither.
uint32_t __fastcall HitTest(char* model, void*, char* ray)
{
    static bool logged = false;
    FirstCall("sign HitTest", logged);
    char* const self = SelfOf(model);
    for (std::size_t box = sign::kBoardBox; box <= sign::kPostBox; box += 0x18) {
        float low[3], high[3], point[3];
        std::memcpy(low, self + box, 12);
        std::memcpy(high, self + box + 0xC, 12);
        if (!Cdecl<uint8_t>(addr::SC4_RayHitsBox, static_cast<void*>(ray), static_cast<void*>(ray + 0xC), static_cast<float*>(low),
                            static_cast<float*>(high), static_cast<float*>(point)))
            continue;
        const double distance = Cdecl<double>(addr::cS3DVector3_Distance, static_cast<void*>(ray), static_cast<float*>(point));
        if (distance <= static_cast<double>(At<float>(ray, 0x28))) {
            At<float>(ray, 0x28) = static_cast<float>(distance);
            return 1;
        }
    }
    return 0;
}

// Slot 12 (0x005F5200, ret 8; the second argument is not read): nothing
// for a sign that is not initialized. The transform, no texture matrix, the
// render state, the context's value 0x84 reset, states (2, 2, 0) and (0, 0);
// the board's coloured quad (fan, format 1, 4 vertices); state (1, 0), the
// colour at 0x00B0A960, depth offset 1; each glyph's texture and its four
// vertices (format 2); the colour reset; the caret's quad while edited and
// shown, untextured, as the board; the ten bindings' textures with the ten
// quads from +0x19C; depth offset 0. True.
uint32_t __fastcall Draw(char* model, void*, void* context, uint32_t)
{
    static bool logged = false;
    FirstCall("sign Draw", logged);
    char* const self = SelfOf(model);
    if (!Virtual<uint8_t>(self + layout::kOccupant, 0x14))
        return 1;
    This<void, void*>(addr::SC4RenderContext_SetModelTransform, context, static_cast<void*>(self + sign::kTransformCount));
    This<void, uint32_t>(addr::SC4RenderContext_ClearTextureMatrix, context, 0u);
    This<void, void*>(addr::SC4RenderContext_SetRenderState, context, static_cast<void*>(self + sign::kRenderState));
    This<void>(addr::SC4RenderContext_ResetDriverValue84, context);
    This<void, uint32_t, uint32_t, uint32_t>(addr::SC4RenderContext_SetState41E0, context, 2u, 2u, 0u);
    This<void, uint32_t, uint32_t>(addr::SC4RenderContext_SetState3EF0, context, 0u, 0u);
    This<void, uint32_t, uint32_t, uint32_t, void*>(addr::SC4RenderContext_DrawArrays, context, 6u, 1u, 4u,
                                                    static_cast<void*>(self + sign::kBoard));
    This<void, uint32_t, uint32_t>(addr::SC4RenderContext_SetState3EF0, context, 1u, 0u);
    This<void, const void*>(addr::SC4RenderContext_SetColour, context,
                            reinterpret_cast<const void*>(addr::cSC4PropOccupantSign_TextColour));
    const int32_t count = static_cast<int32_t>(At<char*>(self, sign::kGlyphs + 4) - At<char*>(self, sign::kGlyphs)) >> 2;
    This<void, int32_t>(addr::SC4RenderContext_SetDepthOffset, context, 1);
    for (int32_t i = 0; i < count; ++i) {
        void** const binding = Virtual<void**>(At<char**>(self, sign::kGlyphs)[i], 0x1C);
        This<void, uint32_t, uint32_t>(addr::SC4RenderContext_BindTexture, context, **reinterpret_cast<uint32_t**>(binding), 0u);
        This<void, uint32_t, uint32_t, uint32_t, void*>(addr::SC4RenderContext_DrawArrays, context, 6u, 2u, 4u,
                                                        static_cast<void*>(At<char*>(self, sign::kGlyphVertices) + i * 0x50));
    }
    This<void>(addr::SC4RenderContext_ResetColour, context);
    if (At<uint8_t>(self, sign::kEditing) && At<uint8_t>(self, sign::kCaret)) {
        This<void, uint32_t, uint32_t>(addr::SC4RenderContext_SetState3EF0, context, 0u, 0u);
        This<void, uint32_t, uint32_t, uint32_t, void*>(addr::SC4RenderContext_DrawArrays, context, 6u, 1u, 4u,
                                                        static_cast<void*>(self + sign::kCaretQuad));
        This<void, uint32_t, uint32_t>(addr::SC4RenderContext_SetState3EF0, context, 1u, 0u);
    }
    for (uint32_t i = 0; i < sign::kBindingCount; ++i) {
        void* const binding = At<void*>(self, sign::kBindings + 4 * i);
        This<void, uint32_t, uint32_t>(addr::SC4RenderContext_BindTexture, context, **static_cast<uint32_t**>(binding), 0u);
        This<void, uint32_t, uint32_t, uint32_t, void*>(addr::SC4RenderContext_DrawArrays, context, 6u, 2u, 4u,
                                                        static_cast<void*>(self + sign::kQuads + sign::kQuadSize * (i + 1)));
    }
    This<void, int32_t>(addr::SC4RenderContext_SetDepthOffset, context, 0);
    return 1;
}

// Slot 17 (0x005F5AA0): no texture (null into `out`).
void __fastcall GetTexture(char*, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("sign GetTexture", logged);
    *out = 0;
}

// ====================================================================================
// Compare mode
// ====================================================================================

using BuildFn = void(__fastcall*)(char*, void*);
using MakeBoxFn = float*(__cdecl*)(float*, float, float, float, float, float, float);
using InsertFn = void(__fastcall*)(char*, void*, char*, uint32_t, const uint32_t*);
using PointerFn = void*(__fastcall*)(char*, void*);
using OutFn = void(__fastcall*)(char*, void*, void*);
using HitFn = uint32_t(__fastcall*)(char*, void*, char*);
using DrawFn = uint32_t(__fastcall*)(char*, void*, void*, uint32_t);
BuildFn g_build;
MakeBoxFn g_makeBox;
InsertFn g_insert;
PointerFn g_getOccupant;
OutFn g_getLocalBox, g_getWorldBox, g_getTransform, g_setTransform, g_getTexture;
HitFn g_hitTest;
DrawFn g_draw;
int g_depth = 0;

std::string Hex(const void* data, std::size_t size)
{
    std::string text;
    char hex[4];
    for (std::size_t i = 0; i < size; ++i) {
        std::snprintf(hex, sizeof hex, "%02X", static_cast<const uint8_t*>(data)[i]);
        text += hex;
    }
    return text;
}

// The first difference of two byte ranges, as a compare record.
bool RecordBytes(const char* id, const char* what, const char* game, const char* ours, std::size_t size, std::size_t base = 0)
{
    std::size_t at = 0;
    while (at < size && game[at] == ours[at])
        ++at;
    if (at == size) {
        compare::Record(id, true, "", "");
        return true;
    }
    at &= ~std::size_t{3};
    const std::size_t n = size - at < 8 ? size - at : 8;
    char where[64];
    std::snprintf(where, sizeof where, "%s+%zX ", what, base + at);
    compare::Record(id, false, where + Hex(game + at, n), where + Hex(ours + at, n));
    return false;
}

// The builder compared: the game's runs first, on the sign as it is, and
// its result is taken (the object and the text's vertices). Then the
// builder's inputs are put back (the transform, and the vector's size, in
// the block the game's run left), what it writes is filled with 0xCD (the
// boxes, the prop's box, the quads, the board, the caret while edited and
// the vertices), and ours runs. Both tell the occupant manager and the
// renderer; the renderer takes a reference on the occupant for each change
// it queues, so the reference count (the object + 0x18) is left out. When
// they differ the game's output is put back (what the builder writes, not
// the whole object).
void CompareBuild(char* self)
{
    constexpr std::size_t kSize = sign::kObjectSize, kReferences = 0x18;
    std::vector<char> before(self, self + kSize);
    const std::size_t heldBefore = static_cast<std::size_t>(At<char*>(self, sign::kGlyphVertices + 4) - At<char*>(self, sign::kGlyphVertices));
    ++g_depth;
    g_build(self, nullptr);
    std::vector<char> game(self, self + kSize);
    std::vector<char> gameVertices(At<char*>(self, sign::kGlyphVertices), At<char*>(self, sign::kGlyphVertices + 4));
    std::memcpy(self + sign::kTransformCount, before.data() + sign::kTransformCount, sign::kBoxes - sign::kTransformCount);
    char* const begin = At<char*>(self, sign::kGlyphVertices);
    const std::size_t capacity = static_cast<std::size_t>(At<char*>(self, sign::kGlyphVertices + 8) - begin);
    if (heldBefore <= capacity)
        At<char*>(self, sign::kGlyphVertices + 4) = begin + heldBefore;
    std::memset(self + layout::kMin, 0xCD, 24);
    std::memset(self + sign::kBoxes, 0xCD, sign::kGlyphVertices - sign::kBoxes);
    std::memset(self + sign::kBoard, 0xCD, sign::kCaretQuad - sign::kBoard);
    if (At<uint8_t>(self, sign::kEditing))
        std::memset(self + sign::kCaretQuad, 0xCD, 0x40);
    std::memset(begin, 0xCD, At<char*>(self, sign::kGlyphVertices + 4) - begin);
    BuildGeometry(self, nullptr);
    --g_depth;
    std::vector<char> mine(self, self + kSize);
    std::vector<char> ourVertices(At<char*>(self, sign::kGlyphVertices), At<char*>(self, sign::kGlyphVertices + 4));
    std::memcpy(game.data() + kReferences, mine.data() + kReferences, 4);
    bool same = RecordBytes("propkinds.sign.geometry", "sign", game.data(), mine.data(), kSize);
    if (gameVertices.size() != ourVertices.size()) {
        compare::Record("propkinds.sign.geometry.vertices", false, std::to_string(gameVertices.size()), std::to_string(ourVertices.size()));
        same = false;
    } else if (!gameVertices.empty()) {
        same = RecordBytes("propkinds.sign.geometry.vertices", "vertices", gameVertices.data(), ourVertices.data(), gameVertices.size()) && same;
    }
    if (!same) {
        std::memcpy(self + layout::kMin, game.data() + layout::kMin, 24);
        std::memcpy(self + sign::kTransformCount, game.data() + sign::kTransformCount, sign::kGlyphVertices - sign::kTransformCount);
        std::memcpy(self + sign::kBoard, game.data() + sign::kBoard, sign::kBinding - sign::kBoard);
        if (At<char*>(self, sign::kGlyphVertices + 4) - At<char*>(self, sign::kGlyphVertices) ==
            static_cast<std::ptrdiff_t>(gameVertices.size()))
            std::memcpy(At<char*>(self, sign::kGlyphVertices), gameVertices.data(), gameVertices.size());
    }
}

void __fastcall DBuildGeometry(char* self, void*)
{
    if (!g_compare)
        return BuildGeometry(self, nullptr);
    if (g_depth > 0)
        return g_build(self, nullptr);
    CompareBuild(self);
}

// The helpers: in compare mode the game's builder reaches the detours and
// gets the game's functions; ours calls its own directly.
float* __cdecl DMakeBox(float* out, float ax, float ay, float az, float bx, float by, float bz)
{
    if (g_compare)
        return g_makeBox(out, ax, ay, az, bx, by, bz);
    return MakeBox(out, ax, ay, az, bx, by, bz);
}

void __fastcall DInsertVertices(char* vector, void*, char* at, uint32_t n, const uint32_t* value)
{
    if (g_compare)
        return g_insert(vector, nullptr, at, n, value);
    InsertVertices(vector, nullptr, at, n, value);
}

// The small slots: the game's into one buffer, ours into another, compared.
void* __fastcall DGetOccupant(char* model, void*)
{
    if (!g_compare)
        return GetOccupant(model, nullptr);
    return compare::Call("propkinds.sign.model.occupant", [&] { return g_getOccupant(model, nullptr); },
                         [&] { return GetOccupant(model, nullptr); });
}

template <std::size_t N, typename Ours>
void CompareOut(const char* id, OutFn game, Ours&& ours, char* model, void* out)
{
    char mine[N];
    std::memcpy(mine, out, N);
    game(model, nullptr, out);
    ours(model, nullptr, static_cast<void*>(mine));
    RecordBytes(id, "out", static_cast<const char*>(out), mine, N);
}

void __fastcall DGetLocalBox(char* model, void*, uint32_t* out)
{
    if (!g_compare)
        return GetLocalBox(model, nullptr, out);
    CompareOut<24>("propkinds.sign.model.localbox", g_getLocalBox,
                   [](char* m, void*, void* o) { GetLocalBox(m, nullptr, static_cast<uint32_t*>(o)); }, model, out);
}

void __fastcall DGetWorldBox(char* model, void*, uint32_t* out)
{
    if (!g_compare)
        return GetWorldBox(model, nullptr, out);
    CompareOut<24>("propkinds.sign.model.worldbox", g_getWorldBox,
                   [](char* m, void*, void* o) { GetWorldBox(m, nullptr, static_cast<uint32_t*>(o)); }, model, out);
}

void __fastcall DGetTransform(char* model, void*, uint8_t* out)
{
    if (!g_compare)
        return GetTransform(model, nullptr, out);
    CompareOut<0x38>("propkinds.sign.model.gettransform", g_getTransform,
                     [](char* m, void*, void* o) { GetTransform(m, nullptr, static_cast<uint8_t*>(o)); }, model, out);
}

// SetTransform writes the sign: ours on the sign, the result kept, the
// fields put back, the game's, compared.
void __fastcall DSetTransform(char* model, void*, const uint8_t* in)
{
    if (!g_compare)
        return SetTransform(model, nullptr, in);
    char* const self = SelfOf(model);
    constexpr std::size_t kFrom = sign::kTransformCount, kSize = sign::kBoxes - sign::kTransformCount;
    char before[kSize], mine[kSize];
    std::memcpy(before, self + kFrom, kSize);
    SetTransform(model, nullptr, in);
    std::memcpy(mine, self + kFrom, kSize);
    std::memcpy(self + kFrom, before, kSize);
    g_setTransform(model, nullptr, const_cast<uint8_t*>(in));
    RecordBytes("propkinds.sign.model.settransform", "sign", self + kFrom, mine, kSize, kFrom);
}

// The hit test writes the ray's distance: ours on a copy of the ray.
uint32_t __fastcall DHitTest(char* model, void*, char* ray)
{
    if (!g_compare)
        return HitTest(model, nullptr, ray);
    char copy[0x2C];
    std::memcpy(copy, ray, sizeof copy);
    const uint32_t mine = HitTest(model, nullptr, copy) & 0xFF;
    const uint32_t result = g_hitTest(model, nullptr, ray);
    compare::Record("propkinds.sign.model.hittest", (result & 0xFF) == mine && std::memcmp(copy, ray, sizeof copy) == 0,
                    std::to_string(result & 0xFF) + " " + Hex(ray + 0x28, 4), std::to_string(mine) + " " + Hex(copy + 0x28, 4));
    return result;
}

void __fastcall DGetTexture(char* model, void*, uint32_t* out)
{
    if (!g_compare)
        return GetTexture(model, nullptr, out);
    CompareOut<4>("propkinds.sign.model.texture", g_getTexture,
                  [](char* m, void*, void* o) { GetTexture(m, nullptr, static_cast<uint32_t*>(o)); }, model, out);
}

// The draw compared in the same frame (docs/renderer.md, "Models and
// lots"): with our D3D11 driver a quiet run of the game's first (the
// context, its texture stages and the driver's state put back after it),
// then ours with a checksum per draw kept off the screen, everything put
// back, then the game's with its draws shown; the draws, the driver's state
// after and the context are compared. Without our driver the game's runs.
struct Region {
    const char* name;
    char* data;
    std::size_t size;
};

std::vector<Region> ContextRegions(void* context)
{
    char* const c = static_cast<char*>(context);
    std::vector<Region> regions{{"context", c, 0x200}};
    if (char* const render = At<char*>(c, 0x34))
        if (char* const camera = At<char*>(render, 0xD8))
            regions.push_back({"camera", camera, 0x1A8});
    if (char* const stages = At<char*>(c, 0xE0))
        regions.push_back({"stages", stages, static_cast<std::size_t>(At<char*>(c, 0xE4) - stages)});
    return regions;
}

uint32_t __fastcall DDraw(char* model, void*, void* context, uint32_t pass)
{
    if (!g_compare)
        return Draw(model, nullptr, context, pass);
    const std::vector<char> pipeline = gd::SaveState();
    if (pipeline.empty() || g_depth > 0)
        return g_draw(model, nullptr, context, pass);
    const std::vector<Region> regions = ContextRegions(context);
    std::vector<std::vector<char>> before;
    for (const Region& r : regions)
        before.emplace_back(r.data, r.data + r.size);
    const auto putBack = [&] {
        for (std::size_t i = 0; i < regions.size(); ++i)
            std::memcpy(regions[i].data, before[i].data(), regions[i].size);
    };
    ++g_depth;
    gd::BeginCapture(false);
    g_draw(model, nullptr, context, pass);
    gd::EndCapture();
    putBack();
    gd::RestoreState(pipeline);
    gd::BeginCapture(false);
    const uint32_t mine = Draw(model, nullptr, context, pass);
    const std::vector<uint32_t> ourDraws = gd::EndCapture();
    const std::vector<char> ourPipeline = gd::SaveState();
    std::vector<std::vector<char>> after;
    for (const Region& r : regions)
        after.emplace_back(r.data, r.data + r.size);
    putBack();
    gd::RestoreState(pipeline);
    gd::BeginCapture(true);
    const uint32_t result = g_draw(model, nullptr, context, pass);
    const std::vector<uint32_t> gameDraws = gd::EndCapture();
    --g_depth;
    const char* const id = "propkinds.sign.draw";
    if (ourDraws != gameDraws) {
        std::size_t at = 0;
        while (at < ourDraws.size() && at < gameDraws.size() && ourDraws[at] == gameDraws[at])
            ++at;
        char game[64], ours[64];
        std::snprintf(game, sizeof game, "draw %zu of %zu: %08X", at, gameDraws.size(), at < gameDraws.size() ? gameDraws[at] : 0u);
        std::snprintf(ours, sizeof ours, "draw %zu of %zu: %08X", at, ourDraws.size(), at < ourDraws.size() ? ourDraws[at] : 0u);
        compare::Record(id, false, game, ours);
        return result;
    }
    const std::vector<char> gamePipeline = gd::SaveState();
    if (gamePipeline != ourPipeline) {
        RecordBytes(id, "driver state", gamePipeline.data(), ourPipeline.data(), gamePipeline.size());
        return result;
    }
    for (std::size_t i = 0; i < regions.size(); ++i)
        if (std::memcmp(regions[i].data, after[i].data(), regions[i].size) != 0) {
            RecordBytes(id, regions[i].name, regions[i].data, after[i].data(), regions[i].size);
            return result;
        }
    compare::Record(id, result == mine, std::to_string(result), std::to_string(mine));
    static uint64_t compared = 0, draws = 0;
    ++compared;
    draws += gameDraws.size();
    if ((compared & (compared - 1)) == 0)
        log::Write("propkinds sign draw: %llu draws of the sign compared, %llu draw calls", static_cast<unsigned long long>(compared),
                   static_cast<unsigned long long>(draws));
    return result;
}

// ====================================================================================
// Installation
// ====================================================================================

struct Hooked {
    std::uintptr_t address;
    void* ours;
    void** original;
};

// GetTexture's first two instructions are ten bytes, GetTransform's six and
// the others' first instruction five or more: the detours' jumps cover whole
// instructions.
const Hooked kSignDrawDetours[] = {
    {addr::cSC4PropOccupantSign_BuildGeometry, reinterpret_cast<void*>(&DBuildGeometry), reinterpret_cast<void**>(&g_build)},
    {addr::cSC4PropOccupantSign_MakeBox, reinterpret_cast<void*>(&DMakeBox), reinterpret_cast<void**>(&g_makeBox)},
    {addr::cSC4PropOccupantSign_InsertVertices, reinterpret_cast<void*>(&DInsertVertices), reinterpret_cast<void**>(&g_insert)},
    {addr::cSC4PropOccupantSign_GetOccupant, reinterpret_cast<void*>(&DGetOccupant), reinterpret_cast<void**>(&g_getOccupant)},
    {addr::cSC4PropOccupantSign_GetLocalBox, reinterpret_cast<void*>(&DGetLocalBox), reinterpret_cast<void**>(&g_getLocalBox)},
    {addr::cSC4PropOccupantSign_GetWorldBox, reinterpret_cast<void*>(&DGetWorldBox), reinterpret_cast<void**>(&g_getWorldBox)},
    {addr::cSC4PropOccupantSign_GetTransform, reinterpret_cast<void*>(&DGetTransform), reinterpret_cast<void**>(&g_getTransform)},
    {addr::cSC4PropOccupantSign_SetTransform, reinterpret_cast<void*>(&DSetTransform), reinterpret_cast<void**>(&g_setTransform)},
    {addr::cSC4PropOccupantSign_HitTest, reinterpret_cast<void*>(&DHitTest), reinterpret_cast<void**>(&g_hitTest)},
    {addr::cSC4PropOccupantSign_Draw, reinterpret_cast<void*>(&DDraw), reinterpret_cast<void**>(&g_draw)},
    {addr::cSC4PropOccupantSign_GetTexture, reinterpret_cast<void*>(&DGetTexture), reinterpret_cast<void**>(&g_getTexture)},
};

bool InstallSignDraw(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Hooked& h : kSignDrawDetours)
        if (!addr_check::Find(h.address)) {
            log::Write("propkinds sign draw: no address entry for 0x%08X", static_cast<unsigned>(h.address));
            return false;
        }
    for (const Hooked& h : kSignDrawDetours)
        if (!hook::Install(*addr_check::Find(h.address), h.ours, h.original))
            return false;
    g_compare = mode == registry::Mode::Compare;
    log::Write("propkinds sign draw: %u detours%s", static_cast<unsigned>(std::size(kSignDrawDetours)),
               g_compare ? ", compared with the game's" : "");
    return true;
}

const bool g_registered = registry::Register({
    "simulators.propkinds.sign_draw",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantSign_BuildGeometry,
    "Our cSC4PropOccupantSign's drawing: the geometry builder and its two helpers, and the model interface's occupant, "
    "boxes, transform, hit test, draw and texture (11 detours; compare mode checks the geometry, the slots and the draw "
    "against the game's; src/core/propkinds_sign_draw.cpp)",
    true,
    InstallSignDraw,
});

} // namespace
} // namespace opensc4::propkinds
