// Our cS3DCamera (build 641), the camera math under the city view
// (docs/renderer.md, "The camera"): viewport, frustum, orientation, the view
// and projection matrices and point projection, each a detour on the game's
// function. In compare mode each call runs ours on a copy of the camera (and
// of any output) and the game's on the real one, and records whether the two
// cameras and outputs came out bit for bit the same; the game's result is
// what the game sees.
//
// The game computes on the x87 at 53-bit precision and stores floats; here
// register values are doubles and each store to memory is a float, at the
// same points and in the same order of operations as the game's code.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <utility>

namespace opensc4::s3d_camera {
namespace {

constexpr std::size_t kSize = 0x1A8;

// Offsets in cS3DCamera.
namespace field {
constexpr std::size_t kViewportWidth = 0x8;   // float
constexpr std::size_t kViewportHeight = 0xC;
constexpr std::size_t kZoomScale = 0x10;      // float
constexpr std::size_t kHasSub = 0x14;         // uint8: a sub-viewport is set
constexpr std::size_t kSub = 0x18;            // float x, y, width, height
constexpr std::size_t kOrientationVersion = 0x28; // uint8
constexpr std::size_t kOrientationFlags = 0x29;   // uint8: 1 position set, 4 rotated
constexpr std::size_t kOrientation = 0x2C;    // float[3][3] rows, then position (+0x50) and scale (+0x5C)
constexpr std::size_t kPosition = 0x50;
constexpr std::size_t kViewVersion = 0x60;    // uint8
constexpr std::size_t kViewFlags = 0x61;      // uint8
constexpr std::size_t kView = 0x64;           // float[3][3], translation (+0x88), scale (+0x94)
constexpr std::size_t kViewBuiltFor = 0x98;   // uint32: kOrientationVersion it was built for
constexpr std::size_t kProjection = 0x9C;     // float[4][4], then its inverse (+0x11C)
constexpr std::size_t kProjectionBuiltFor = 0x15C;
constexpr std::size_t kTarget = 0x160;        // float[3]: LookAt's target
constexpr std::size_t kTargetVersion = 0x16C;
constexpr std::size_t kUp = 0x170;            // float[3]
constexpr std::size_t kProjectionVersion = 0x17C;
constexpr std::size_t kMode = 0x180;          // 1 orthographic (SetFrustum), 2 perspective
constexpr std::size_t kNear = 0x184;
constexpr std::size_t kFar = 0x188;
constexpr std::size_t kLeft = 0x18C;
constexpr std::size_t kRight = 0x190;
constexpr std::size_t kTop = 0x194;
constexpr std::size_t kBottom = 0x198;
constexpr std::size_t kPixelOffset = 0x19C;   // float x, y
constexpr std::size_t kDepthOffset = 0x1A4;
} // namespace field

float& F(char* self, std::size_t offset) { return At<float>(self, offset); }
uint32_t& U(char* self, std::size_t offset) { return At<uint32_t>(self, offset); }
uint8_t& B(char* self, std::size_t offset) { return At<uint8_t>(self, offset); }

double One() { return Global<float>(0x00B0E354); }       // 1.0
double Epsilon() { return Global<float>(0x00ABD2BC); }   // 0.0001
double Half() { return Global<float>(0x00A84D2C); }      // 0.5
double MinusTwo() { return Global<float>(0x00ABD2D4); }  // -2.0
double Unit() { return Global<float>(0x00A81228); }      // 1.0

// --- Our functions --------------------------------------------------------------

void SetZoomScale(char* self, float scale)
{
    ++U(self, field::kProjectionVersion);
    F(self, field::kZoomScale) = scale;
}

void SetSubViewport(char* self, float x, float y, float width, float height)
{
    F(self, field::kSub) = x;
    F(self, field::kSub + 8) = width;
    F(self, field::kSub + 4) = y;
    F(self, field::kSub + 12) = height;
    B(self, field::kHasSub) = 1;
    ++U(self, field::kProjectionVersion);
}

void ResetSubViewport(char* self)
{
    U(self, field::kSub) = 0;
    U(self, field::kSub + 4) = 0;
    B(self, field::kHasSub) = 0;
    U(self, field::kSub + 8) = U(self, field::kViewportWidth);
    U(self, field::kSub + 12) = U(self, field::kViewportHeight);
    ++U(self, field::kProjectionVersion);
}

void SetDepthOffset(char* self, float offset)
{
    ++U(self, field::kProjectionVersion);
    F(self, field::kDepthOffset) = offset;
}

void SetViewport(char* self, float width, float height)
{
    F(self, field::kViewportWidth) = width;
    F(self, field::kViewportHeight) = height;
    U(self, field::kSub) = 0;
    U(self, field::kSub + 4) = 0;
    B(self, field::kHasSub) = 0;
    F(self, field::kSub + 8) = width;
    F(self, field::kSub + 12) = height;
    U(self, field::kProjectionVersion) += 2;
}

void Identity3(char* self, std::size_t at)
{
    // Rows of 3, then a translation of 3 and a scale.
    const uint32_t one = 0x3F800000;
    U(self, at + 0x0) = one, U(self, at + 0xC) = 0, U(self, at + 0x18) = 0;
    U(self, at + 0x4) = 0, U(self, at + 0x10) = one, U(self, at + 0x1C) = 0;
    U(self, at + 0x8) = 0, U(self, at + 0x14) = 0, U(self, at + 0x20) = one;
    U(self, at + 0x30) = one;
    U(self, at + 0x24) = 0, U(self, at + 0x28) = 0, U(self, at + 0x2C) = 0;
}

void ResetOrientation(char* self)
{
    if (B(self, field::kOrientationFlags) != 0) {
        Identity3(self, field::kOrientation);
        B(self, field::kOrientationFlags) = 0;
        ++B(self, field::kOrientationVersion);
    }
    if (B(self, field::kViewFlags) != 0) {
        Identity3(self, field::kView);
        B(self, field::kViewFlags) = 0;
        ++B(self, field::kViewVersion);
    }
    U(self, field::kViewBuiltFor) = B(self, field::kOrientationVersion);
}

void GetPosition(char* self, float* out)
{
    std::memcpy(out, self + field::kPosition, 12);
}

void SetPosition(char* self, const float* position)
{
    std::memcpy(self + field::kPosition, position, 12);
    B(self, field::kOrientationFlags) |= 1;
    ++B(self, field::kOrientationVersion);
}

void GetTarget(char* self, float* out)
{
    if (U(self, field::kTargetVersion) != B(self, field::kOrientationVersion))
        U(self, field::kTargetVersion) = B(self, field::kOrientationVersion);
    std::memcpy(out, self + field::kTarget, 12);
}

void SetPixelOffset(char* self, const float* offset)
{
    ++U(self, field::kProjectionVersion);
    std::memcpy(self + field::kPixelOffset, offset, 8);
}

void SetFrustum(char* self, float left, float right, float top, float bottom, float nearZ, float farZ)
{
    F(self, field::kLeft) = left;
    F(self, field::kRight) = right;
    F(self, field::kBottom) = top;
    F(self, field::kTop) = bottom;
    F(self, field::kNear) = nearZ;
    F(self, field::kFar) = farZ;
    U(self, field::kMode) = 1;
    ++U(self, field::kProjectionVersion);
}

void GetFrustum(char* self, float* left, float* right, float* top, float* bottom, float* nearZ, float* farZ)
{
    const double dx = (static_cast<double>(F(self, field::kRight)) - F(self, field::kLeft)) /
                      F(self, field::kViewportWidth) * F(self, field::kPixelOffset);
    const double dy = (static_cast<double>(F(self, field::kBottom)) - F(self, field::kTop)) /
                      F(self, field::kViewportHeight) * F(self, field::kPixelOffset + 4);
    *left = static_cast<float>(dx + F(self, field::kLeft));
    *right = static_cast<float>(dx + F(self, field::kRight));
    *top = static_cast<float>(F(self, field::kBottom) - dy);
    *bottom = static_cast<float>(F(self, field::kTop) - dy);
    std::memcpy(nearZ, self + field::kNear, 4);
    std::memcpy(farZ, self + field::kFar, 4);
}

// The view: the orientation copied, its translation negated and, when
// rotated, turned by the transposed rotation; scaled by 1 / scale when
// flagged.
void BuildView(char* self)
{
    ++B(self, field::kViewVersion);
    std::memcpy(self + field::kView, self + field::kOrientation, 0x34);
    B(self, field::kViewFlags) = B(self, field::kOrientationFlags);
    float& t0 = F(self, 0x88);
    float& t1 = F(self, 0x8C);
    float& t2 = F(self, 0x90);
    const double n0 = -static_cast<double>(t0), n1 = -static_cast<double>(t1);
    const float n2 = static_cast<float>(-static_cast<double>(t2));
    t2 = n2;
    t0 = static_cast<float>(n0);
    t1 = static_cast<float>(n1);
    if (B(self, field::kViewFlags) & 4) {
        std::swap(U(self, 0x68), U(self, 0x70));
        std::swap(U(self, 0x6C), U(self, 0x7C));
        std::swap(U(self, 0x78), U(self, 0x80));
        const double x = t0, y = t1, z = t2;
        const double r0 = (F(self, 0x6C) * z + F(self, 0x68) * y) + x * F(self, 0x64);
        const double r1 = (F(self, 0x78) * z + F(self, 0x74) * y) + x * F(self, 0x70);
        const float r2 = static_cast<float>((F(self, 0x84) * z + F(self, 0x80) * y) + x * F(self, 0x7C));
        t2 = r2;
        t0 = static_cast<float>(r0);
        t1 = static_cast<float>(r1);
    }
    if (B(self, field::kViewFlags) & 2) {
        const double s = Unit() / F(self, 0x94);
        F(self, 0x94) = static_cast<float>(s);
        t0 = static_cast<float>(s * t0);
        t1 = static_cast<float>(s * t1);
        t2 = static_cast<float>(s * t2);
    }
    ++B(self, field::kViewVersion);
    U(self, field::kViewBuiltFor) = B(self, field::kOrientationVersion);
}

// The projection (column-major rows of 4 at +0x9C) and its inverse
// (+0x11C): orthographic (mode 1) or perspective (2) over the frustum, the
// sub-viewport and the pixel offset.
void BuildProjection(char* self)
{
    static const std::size_t kZero[] = {0xAC, 0xBC, 0xCC, 0xA0, 0xC0, 0xD0, 0xA4, 0xB4, 0xD4, 0xA8, 0xB8, 0xC8,
                                        0x12C, 0x13C, 0x14C, 0x120, 0x140, 0x150, 0x124, 0x134, 0x154, 0x128,
                                        0x138, 0x148};
    static const std::size_t kOne[] = {0x9C, 0xB0, 0xC4, 0xD8, 0x11C, 0x130, 0x144, 0x158};
    for (const std::size_t at : kZero)
        U(self, at) = 0;
    for (const std::size_t at : kOne)
        U(self, at) = 0x3F800000;
    double a = Unit() / (static_cast<double>(F(self, field::kRight)) - F(self, field::kLeft));
    float b = static_cast<float>(Unit() / (static_cast<double>(F(self, field::kTop)) - F(self, field::kBottom)));
    const float c = static_cast<float>(Unit() / (static_cast<double>(F(self, field::kFar)) - F(self, field::kNear)));
    const double d = Unit() / F(self, field::kSub + 8);
    const float df = static_cast<float>(d);
    double e = d * F(self, field::kPixelOffset) * MinusTwo();
    const double f = Unit() / F(self, field::kSub + 12);
    float g = static_cast<float>(f * F(self, field::kPixelOffset + 4) * MinusTwo());
    const float h = static_cast<float>(-(static_cast<double>(F(self, field::kDepthOffset)) / F(self, field::kZoomScale)));
    if (B(self, field::kHasSub)) {
        const double i = static_cast<double>(df) * F(self, field::kViewportWidth);
        const float j = static_cast<float>(f * F(self, field::kViewportHeight));
        a = a * i;
        b = static_cast<float>(static_cast<double>(j) * b);
        e = e + ((i - Unit()) - 2.0 * (static_cast<double>(df) * F(self, field::kSub)));
        const double l = (static_cast<double>(j) - Unit()) - 2.0 * (f * F(self, field::kSub + 4));
        g = static_cast<float>(l + g);
    }
    if (U(self, field::kMode) == 2) {
        const double n = 2.0 * F(self, field::kNear);
        F(self, 0x9C) = static_cast<float>(n * a);
        F(self, 0xB0) = static_cast<float>(b * n);
        F(self, 0xC8) = static_cast<float>(-(n * F(self, field::kFar) * c));
        F(self, 0xA4) = static_cast<float>((static_cast<double>(F(self, field::kLeft)) + F(self, field::kRight)) * a - e);
        F(self, 0xB4) =
            static_cast<float>((static_cast<double>(F(self, field::kBottom)) + F(self, field::kTop)) * b - g);
        F(self, 0xC4) =
            static_cast<float>(-((static_cast<double>(F(self, field::kNear)) + F(self, field::kFar)) * c) - h);
        U(self, 0xD4) = 0xBF800000;
        U(self, 0xD8) = 0;
        F(self, 0x11C) = static_cast<float>(Unit() / F(self, 0x9C));
        F(self, 0x130) = static_cast<float>(Unit() / F(self, 0xB0));
        F(self, 0x154) = static_cast<float>(Unit() / F(self, 0xC8));
        F(self, 0x128) = static_cast<float>(static_cast<double>(F(self, 0xA4)) * F(self, 0x11C));
        F(self, 0x138) = static_cast<float>(static_cast<double>(F(self, 0x130)) * F(self, 0xB4));
        U(self, 0x148) = 0xBF800000;
        U(self, 0x158) = 0;
    } else {
        F(self, 0x9C) = static_cast<float>(a + a);
        F(self, 0xB0) = static_cast<float>(static_cast<double>(b) + b);
        F(self, 0xC4) = static_cast<float>(static_cast<double>(c) * MinusTwo());
        F(self, 0xA8) = static_cast<float>(e - (static_cast<double>(F(self, field::kLeft)) + F(self, field::kRight)) * a);
        F(self, 0xB8) =
            static_cast<float>(g - (static_cast<double>(F(self, field::kBottom)) + F(self, field::kTop)) * b);
        F(self, 0xC8) =
            static_cast<float>(h - (static_cast<double>(F(self, field::kNear)) + F(self, field::kFar)) * c);
        F(self, 0x11C) = static_cast<float>(Unit() / F(self, 0x9C));
        F(self, 0x130) = static_cast<float>(Unit() / F(self, 0xB0));
        F(self, 0x144) = static_cast<float>(Unit() / F(self, 0xC4));
        F(self, 0x128) = static_cast<float>(-(static_cast<double>(F(self, 0xA8)) * F(self, 0x11C)));
        F(self, 0x138) = static_cast<float>(-(static_cast<double>(F(self, 0x130)) * F(self, 0xB8)));
        F(self, 0x148) = static_cast<float>(-(static_cast<double>(F(self, 0x144)) * F(self, 0xC8)));
    }
    U(self, field::kProjectionBuiltFor) = U(self, field::kProjectionVersion);
}

// The orientation looking from the position at `target` with `up`.
void LookAt(char* self, const float* target, const float* up)
{
    const double a = static_cast<double>(F(self, 0x50)) - target[0];
    const float bf = static_cast<float>(static_cast<double>(F(self, 0x54)) - target[1]);
    const double c = static_cast<double>(F(self, 0x58)) - target[2];
    const float cf = static_cast<float>(c);
    const double s = (c * cf + a * a) + static_cast<double>(bf) * bf;
    const double inv = One() / std::sqrt(s);
    const float invf = static_cast<float>(inv);
    const double x = a * inv;
    const double y = static_cast<double>(invf) * bf;
    const float zf = static_cast<float>(static_cast<double>(cf) * invf);
    const double r0 = static_cast<double>(zf) * up[1] - y * up[2];
    const float r1f = static_cast<float>(x * up[2] - static_cast<double>(zf) * up[0]);
    const double r2 = y * up[0] - x * up[1];
    const float r2f = static_cast<float>(r2);
    const double s2 = (r2 * r2f + static_cast<double>(r1f) * r1f) + r0 * r0;
    const float inv2f = static_cast<float>(One() / std::sqrt(s2));
    const double R0 = r0 * inv2f;
    const double R1 = static_cast<double>(r1f) * inv2f;
    const double R2 = static_cast<double>(r2f) * inv2f;
    const float R2f = static_cast<float>(R2);
    const float U0f = static_cast<float>(R2 * y - R1 * zf);
    const float U1f = static_cast<float>(static_cast<double>(zf) * R0 - static_cast<double>(R2f) * x);
    const float U2f = static_cast<float>(R1 * x - R0 * y);
    F(self, 0x44) = R2f;
    F(self, 0x2C) = static_cast<float>(R0);
    F(self, 0x38) = static_cast<float>(R1);
    F(self, 0x30) = U0f;
    F(self, 0x3C) = U1f;
    F(self, 0x48) = U2f;
    F(self, 0x34) = static_cast<float>(x);
    F(self, 0x4C) = zf;
    F(self, 0x40) = static_cast<float>(y);
    B(self, field::kOrientationFlags) |= 4;
    B(self, field::kOrientationVersion) += 2;
    std::memcpy(self + field::kTarget, target, 12);
    std::memcpy(self + field::kUp, up, 12);
    U(self, field::kTargetVersion) = B(self, field::kOrientationVersion);
}

char* GetView(char* self)
{
    if (U(self, field::kViewBuiltFor) != B(self, field::kOrientationVersion))
        BuildView(self);
    return self + 0x60;
}

char* GetProjection(char* self)
{
    if (U(self, field::kProjectionBuiltFor) != U(self, field::kProjectionVersion))
        BuildProjection(self);
    return self + field::kProjection;
}

// A world point to the screen (x, y in pixels of the sub-viewport, z the
// depth); false when it projects to w near 0.
uint8_t Project(char* self, const float* point, float* out)
{
    const float px = point[0], py = point[1], pz = point[2];
    if (U(self, field::kViewBuiltFor) != B(self, field::kOrientationVersion))
        BuildView(self);
    const float scale = F(self, 0x94);
    const double e0 = (static_cast<double>(pz) * F(self, 0x6C) + static_cast<double>(py) * F(self, 0x68)) +
                      static_cast<double>(px) * F(self, 0x64);
    const double e1 = (static_cast<double>(pz) * F(self, 0x78) + static_cast<double>(py) * F(self, 0x74)) +
                      static_cast<double>(px) * F(self, 0x70);
    const float e2f = static_cast<float>(
        (static_cast<double>(pz) * F(self, 0x84) + static_cast<double>(py) * F(self, 0x80)) +
        static_cast<double>(px) * F(self, 0x7C));
    float vx = static_cast<float>(e0 * scale);
    const double e1s = e1 * scale;
    const double e2s = static_cast<double>(e2f) * scale;
    vx = static_cast<float>(static_cast<double>(vx) + F(self, 0x88));
    const float vy = static_cast<float>(e1s + F(self, 0x8C));
    const float vz = static_cast<float>(e2s + F(self, 0x90));
    if (U(self, field::kProjectionBuiltFor) != U(self, field::kProjectionVersion))
        BuildProjection(self);
    auto row = [&](std::size_t a, std::size_t b, std::size_t c, std::size_t d) {
        return ((static_cast<double>(vz) * F(self, a) + static_cast<double>(vy) * F(self, b)) + One() * F(self, c)) +
               static_cast<double>(vx) * F(self, d);
    };
    const double c0 = row(0xA4, 0xA0, 0xA8, 0x9C);
    const float c1f = static_cast<float>(row(0xB4, 0xB0, 0xB8, 0xAC));
    const double c2 = row(0xC4, 0xC0, 0xC8, 0xBC);
    const double c3 = row(0xD4, 0xD0, 0xD8, 0xCC);
    if (std::fabs(c3) < Epsilon())
        return 0;
    const double inv = One() / c3;
    const float nx = static_cast<float>(inv * c0);
    const float ny = static_cast<float>(static_cast<double>(c1f) * inv);
    const double nz = c2 * inv;
    out[0] = static_cast<float>(((static_cast<double>(nx) + Unit()) * F(self, field::kSub + 8)) * Half() +
                                F(self, field::kSub));
    out[1] = static_cast<float>(((Unit() - ny) * F(self, field::kSub + 12)) * Half() + F(self, field::kSub + 4));
    out[2] = static_cast<float>(nz);
    return 1;
}

// A screen point and depth back to the world.
uint8_t Unproject(char* self, const float* point, float* out)
{
    double v = (static_cast<double>(point[0]) - F(self, field::kSub)) / F(self, field::kSub + 8);
    const float ax = static_cast<float>((v + v) - Unit());
    v = ((static_cast<double>(F(self, field::kSub + 4)) + F(self, field::kSub + 12)) - point[1]) /
        F(self, field::kSub + 12);
    const float ay = static_cast<float>((v + v) - Unit());
    const float az = point[2];
    if (U(self, field::kProjectionBuiltFor) != U(self, field::kProjectionVersion))
        BuildProjection(self);
    auto row = [&](std::size_t a, std::size_t b, std::size_t c, std::size_t d) {
        return ((static_cast<double>(az) * F(self, a) + static_cast<double>(ay) * F(self, b)) +
                static_cast<double>(ax) * F(self, c)) +
               F(self, d);
    };
    const double r0 = row(0x124, 0x120, 0x11C, 0x128);
    const float r1f = static_cast<float>(row(0x134, 0x130, 0x12C, 0x138));
    const float r2f = static_cast<float>(row(0x144, 0x140, 0x13C, 0x148));
    const double r3 = row(0x154, 0x150, 0x14C, 0x158);
    if (std::fabs(r3) < Epsilon())
        return 0;
    const float invf = static_cast<float>(One() / r3);
    out[0] = static_cast<float>(r0 * invf);
    out[1] = static_cast<float>(static_cast<double>(r1f) * invf);
    out[2] = static_cast<float>(static_cast<double>(r2f) * invf);
    if (U(self, field::kViewBuiltFor) != B(self, field::kOrientationVersion))
        BuildView(self);
    out[0] = static_cast<float>(static_cast<double>(out[0]) - F(self, 0x88));
    out[1] = static_cast<float>(static_cast<double>(out[1]) - F(self, 0x8C));
    out[2] = static_cast<float>(static_cast<double>(out[2]) - F(self, 0x90));
    const double s = One() / F(self, 0x94);
    out[0] = static_cast<float>(s * out[0]);
    out[1] = static_cast<float>(s * out[1]);
    out[2] = static_cast<float>(s * out[2]);
    const double o0 = (static_cast<double>(F(self, 0x7C)) * out[2] + static_cast<double>(F(self, 0x70)) * out[1]) +
                      static_cast<double>(F(self, 0x64)) * out[0];
    const double o1 = (static_cast<double>(F(self, 0x68)) * out[0] + static_cast<double>(F(self, 0x80)) * out[2]) +
                      static_cast<double>(F(self, 0x74)) * out[1];
    const float o2f = static_cast<float>(
        (static_cast<double>(F(self, 0x6C)) * out[0] + static_cast<double>(F(self, 0x84)) * out[2]) +
        static_cast<double>(F(self, 0x78)) * out[1]);
    out[0] = static_cast<float>(o0);
    out[2] = o2f;
    out[1] = static_cast<float>(o1);
    return 1;
}

// A pick ray from a screen point: origin and direction.
uint8_t PickRay(char* self, const float* point, float* origin, float* direction)
{
    if (U(self, field::kMode) == 2) {
        const double sx = (static_cast<double>(point[0]) / F(self, field::kViewportWidth)) * 2.0 - Unit();
        const double sy =
            ((static_cast<double>(F(self, field::kViewportHeight)) - point[1]) / F(self, field::kViewportHeight)) * 2.0 -
            Unit();
        const double n = -static_cast<double>(F(self, field::kNear));
        std::memcpy(origin, self + field::kPosition, 12);
        const float r0f = static_cast<float>((n * F(self, 0x34) + sy * F(self, 0x30)) + sx * F(self, 0x2C));
        const float r1f = static_cast<float>((n * F(self, 0x40) + sy * F(self, 0x3C)) + sx * F(self, 0x38));
        const float r2f = static_cast<float>((n * F(self, 0x4C) + sy * F(self, 0x48)) + sx * F(self, 0x44));
        direction[0] = -r0f;
        direction[1] = -r1f;
        direction[2] = -r2f;
        return 1;
    }
    const float garbage = Global<float>(0x00B4E660);
    float unprojected[3] = {garbage, garbage, garbage};
    std::memcpy(origin, self + field::kPosition, 12);
    if (U(self, field::kTargetVersion) != B(self, field::kOrientationVersion))
        U(self, field::kTargetVersion) = B(self, field::kOrientationVersion);
    const double a = static_cast<double>(F(self, field::kTarget)) - origin[0];
    const double b = static_cast<double>(F(self, field::kTarget + 4)) - origin[1];
    const double c = static_cast<double>(F(self, field::kTarget + 8)) - origin[2];
    direction[0] = static_cast<float>(a);
    direction[1] = static_cast<float>(b);
    direction[2] = static_cast<float>(c);
    const float screen[3] = {point[0], point[1], 1.0f};
    Unproject(self, screen, unprojected);
    const double d0 = direction[0], d1 = direction[1], d2 = direction[2];
    const double s = (d0 * d0 + d1 * d1) + d2 * d2;
    if (s > 0.0) {
        const double m = F(self, field::kFar) / std::sqrt(s);
        direction[0] = static_cast<float>(m * direction[0]);
        direction[1] = static_cast<float>(m * direction[1]);
        direction[2] = static_cast<float>(m * direction[2]);
    }
    const double p0 = static_cast<double>(origin[0]) + direction[0];
    const double p1 = static_cast<double>(origin[1]) + direction[1];
    const float p2f = static_cast<float>(static_cast<double>(origin[2]) + direction[2]);
    const float q0f = static_cast<float>(static_cast<double>(unprojected[0]) - p0);
    const double q1 = static_cast<double>(unprojected[1]) - p1;
    const double q2 = static_cast<double>(unprojected[2]) - p2f;
    origin[0] = static_cast<float>(static_cast<double>(q0f) + origin[0]);
    origin[1] = static_cast<float>(q1 + origin[1]);
    origin[2] = static_cast<float>(q2 + origin[2]);
    return 1;
}

// --- The detours, with compare mode ---------------------------------------------------

bool g_compare = false;

std::string Bytes(const void* data, std::size_t size)
{
    std::string text;
    char hex[4];
    const auto* b = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < size && i < 32; ++i) {
        std::snprintf(hex, sizeof hex, "%02X", b[i]);
        text += hex;
    }
    return text;
}

// Runs `ours` on a copy of the camera (and outputs) and `original` on the
// real one, and records whether cameras, outputs and results all match.
// `outputs` are (pointer, size) pairs the functions write.
struct Output {
    void* data;
    std::size_t size;
};

// Void functions return a dummy for the comparison.
struct Nothing {
    bool operator==(const Nothing&) const { return true; }
};

template <typename R, typename Original, typename Ours>
R Compared(const char* id, char* self, std::initializer_list<Output> outputs, Original&& original, Ours&& ours)
{
    alignas(16) char copy[kSize];
    std::memcpy(copy, self, kSize);
    char saved[4][64];
    int k = 0;
    for (const Output& o : outputs)
        std::memcpy(saved[k++], o.data, o.size);
    // Ours first, on the copy, with outputs into the saved buffers.
    k = 0;
    void* ourOutputs[4];
    for (k = 0; k < static_cast<int>(outputs.size()); ++k)
        ourOutputs[k] = saved[k];
    const R ourResult = ours(copy, ourOutputs);
    const R result = original();
    const bool sameObject = std::memcmp(copy, self, kSize) == 0;
    bool sameResult = true;
    if constexpr (!std::is_same_v<R, Nothing>)
        sameResult = std::memcmp(&ourResult, &result, sizeof(R)) == 0;
    int differingOutput = -1;
    k = 0;
    for (const Output& o : outputs) {
        if (differingOutput < 0 && std::memcmp(saved[k], o.data, o.size) != 0)
            differingOutput = k;
        ++k;
    }
    const bool same = sameObject && sameResult && differingOutput < 0;
    if (same) {
        compare::Record(id, true, std::string(), std::string());
    } else {
        char where[32];
        if (!sameObject) {
            std::size_t at = 0;
            while (at < kSize && copy[at] == self[at])
                ++at;
            std::snprintf(where, sizeof where, "@%zX ", at);
            compare::Record(id, false, where + Bytes(self + at, 16), where + Bytes(copy + at, 16));
        } else if (differingOutput >= 0) {
            const Output& o = outputs.begin()[differingOutput];
            std::snprintf(where, sizeof where, "out%d ", differingOutput);
            compare::Record(id, false, where + Bytes(o.data, o.size), where + Bytes(saved[differingOutput], o.size));
        } else {
            compare::Record(id, false, "result " + Bytes(&result, sizeof(R)), "result " + Bytes(&ourResult, sizeof(R)));
        }
    }
    return result;
}

using SetFloatFn = void(__fastcall*)(char*, void*, float);
using SubFn = void(__fastcall*)(char*, void*, float, float, float, float);
using SelfFn = void(__fastcall*)(char*, void*);
using ViewportFn = void(__fastcall*)(char*, void*, float, float);
using VectorFn = void(__fastcall*)(char*, void*, float*);
using ConstVectorFn = void(__fastcall*)(char*, void*, const float*);
using FrustumFn = void(__fastcall*)(char*, void*, float, float, float, float, float, float);
using GetFrustumFn = void(__fastcall*)(char*, void*, float*, float*, float*, float*, float*, float*);
using LookAtFn = void(__fastcall*)(char*, void*, const float*, const float*);
using MatrixFn = char*(__fastcall*)(char*, void*);
using ProjectFn = uint8_t(__fastcall*)(char*, void*, const float*, float*);
using PickFn = uint8_t(__fastcall*)(char*, void*, const float*, float*, float*);

SetFloatFn g_zoomScale, g_depthOffset;
SubFn g_subViewport;
SelfFn g_resetSub, g_resetOrientation, g_buildView, g_buildProjection;
ViewportFn g_viewport;
VectorFn g_getPosition, g_getTarget;
ConstVectorFn g_setPosition, g_pixelOffset;
FrustumFn g_frustum;
GetFrustumFn g_getFrustum;
LookAtFn g_lookAt;
MatrixFn g_getView, g_getProjection;
ProjectFn g_project, g_unproject;
PickFn g_pick;

void __fastcall DSetZoomScale(char* s, void*, float v)
{
    if (!g_compare)
        return SetZoomScale(s, v);
    Compared<Nothing>("s3dcamera.zoomscale", s, {}, [&] { g_zoomScale(s, nullptr, v); return Nothing{}; },
                      [&](char* c, void**) { SetZoomScale(c, v); return Nothing{}; });
}

void __fastcall DSetDepthOffset(char* s, void*, float v)
{
    if (!g_compare)
        return SetDepthOffset(s, v);
    Compared<Nothing>("s3dcamera.depthoffset", s, {}, [&] { g_depthOffset(s, nullptr, v); return Nothing{}; },
                      [&](char* c, void**) { SetDepthOffset(c, v); return Nothing{}; });
}

void __fastcall DSetSubViewport(char* s, void*, float x, float y, float w, float h)
{
    if (!g_compare)
        return SetSubViewport(s, x, y, w, h);
    Compared<Nothing>("s3dcamera.subviewport", s, {}, [&] { g_subViewport(s, nullptr, x, y, w, h); return Nothing{}; },
                      [&](char* c, void**) { SetSubViewport(c, x, y, w, h); return Nothing{}; });
}

#define OPENSC4_SELF_DETOUR(Name, Ours, Original, id)                                                             \
    void __fastcall Name(char* s, void*)                                                                         \
    {                                                                                                            \
        if (!g_compare)                                                                                          \
            return Ours(s);                                                                                      \
        Compared<Nothing>(id, s, {}, [&] { Original(s, nullptr); return Nothing{}; },                            \
                          [&](char* c, void**) { Ours(c); return Nothing{}; });                                  \
    }

OPENSC4_SELF_DETOUR(DResetSubViewport, ResetSubViewport, g_resetSub, "s3dcamera.resetsub")
OPENSC4_SELF_DETOUR(DResetOrientation, ResetOrientation, g_resetOrientation, "s3dcamera.resetorientation")
OPENSC4_SELF_DETOUR(DBuildView, BuildView, g_buildView, "s3dcamera.buildview")
OPENSC4_SELF_DETOUR(DBuildProjection, BuildProjection, g_buildProjection, "s3dcamera.buildprojection")

#undef OPENSC4_SELF_DETOUR

void __fastcall DSetViewport(char* s, void*, float w, float h)
{
    if (!g_compare)
        return SetViewport(s, w, h);
    Compared<Nothing>("s3dcamera.viewport", s, {}, [&] { g_viewport(s, nullptr, w, h); return Nothing{}; },
                      [&](char* c, void**) { SetViewport(c, w, h); return Nothing{}; });
}

void __fastcall DGetPosition(char* s, void*, float* out)
{
    if (!g_compare)
        return GetPosition(s, out);
    Compared<Nothing>("s3dcamera.getposition", s, {{out, 12}}, [&] { g_getPosition(s, nullptr, out); return Nothing{}; },
                      [&](char* c, void** o) { GetPosition(c, static_cast<float*>(o[0])); return Nothing{}; });
}

void __fastcall DGetTarget(char* s, void*, float* out)
{
    if (!g_compare)
        return GetTarget(s, out);
    Compared<Nothing>("s3dcamera.gettarget", s, {{out, 12}}, [&] { g_getTarget(s, nullptr, out); return Nothing{}; },
                      [&](char* c, void** o) { GetTarget(c, static_cast<float*>(o[0])); return Nothing{}; });
}

void __fastcall DSetPosition(char* s, void*, const float* v)
{
    if (!g_compare)
        return SetPosition(s, v);
    Compared<Nothing>("s3dcamera.setposition", s, {}, [&] { g_setPosition(s, nullptr, v); return Nothing{}; },
                      [&](char* c, void**) { SetPosition(c, v); return Nothing{}; });
}

void __fastcall DSetPixelOffset(char* s, void*, const float* v)
{
    if (!g_compare)
        return SetPixelOffset(s, v);
    Compared<Nothing>("s3dcamera.pixeloffset", s, {}, [&] { g_pixelOffset(s, nullptr, v); return Nothing{}; },
                      [&](char* c, void**) { SetPixelOffset(c, v); return Nothing{}; });
}

void __fastcall DSetFrustum(char* s, void*, float l, float r, float t, float b, float n, float f)
{
    if (!g_compare)
        return SetFrustum(s, l, r, t, b, n, f);
    Compared<Nothing>("s3dcamera.frustum", s, {}, [&] { g_frustum(s, nullptr, l, r, t, b, n, f); return Nothing{}; },
                      [&](char* c, void**) { SetFrustum(c, l, r, t, b, n, f); return Nothing{}; });
}

void __fastcall DGetFrustum(char* s, void*, float* l, float* r, float* t, float* b, float* n, float* f)
{
    if (!g_compare)
        return GetFrustum(s, l, r, t, b, n, f);
    Compared<Nothing>("s3dcamera.getfrustum", s, {{l, 4}, {r, 4}, {t, 4}, {b, 4}},
                      [&] { g_getFrustum(s, nullptr, l, r, t, b, n, f); return Nothing{}; },
                      [&](char* c, void** o) {
                          float nn, ff;
                          GetFrustum(c, static_cast<float*>(o[0]), static_cast<float*>(o[1]), static_cast<float*>(o[2]),
                                     static_cast<float*>(o[3]), &nn, &ff);
                          return Nothing{};
                      });
}

void __fastcall DLookAt(char* s, void*, const float* target, const float* up)
{
    if (!g_compare)
        return LookAt(s, target, up);
    Compared<Nothing>("s3dcamera.lookat", s, {}, [&] { g_lookAt(s, nullptr, target, up); return Nothing{}; },
                      [&](char* c, void**) { LookAt(c, target, up); return Nothing{}; });
}

char* __fastcall DGetView(char* s, void*)
{
    if (!g_compare)
        return GetView(s);
    // The result points into the object: compared as an offset.
    const std::ptrdiff_t at = Compared<std::ptrdiff_t>(
        "s3dcamera.getview", s, {}, [&] { return g_getView(s, nullptr) - s; },
        [&](char* c, void**) { return GetView(c) - c; });
    return s + at;
}

char* __fastcall DGetProjection(char* s, void*)
{
    if (!g_compare)
        return GetProjection(s);
    const std::ptrdiff_t at = Compared<std::ptrdiff_t>(
        "s3dcamera.getprojection", s, {}, [&] { return g_getProjection(s, nullptr) - s; },
        [&](char* c, void**) { return GetProjection(c) - c; });
    return s + at;
}

uint8_t __fastcall DProject(char* s, void*, const float* p, float* out)
{
    if (!g_compare)
        return Project(s, p, out);
    return Compared<uint8_t>("s3dcamera.project", s, {{out, 12}}, [&] { return g_project(s, nullptr, p, out); },
                             [&](char* c, void** o) { return Project(c, p, static_cast<float*>(o[0])); });
}

uint8_t __fastcall DUnproject(char* s, void*, const float* p, float* out)
{
    if (!g_compare)
        return Unproject(s, p, out);
    return Compared<uint8_t>("s3dcamera.unproject", s, {{out, 12}}, [&] { return g_unproject(s, nullptr, p, out); },
                             [&](char* c, void** o) { return Unproject(c, p, static_cast<float*>(o[0])); });
}

uint8_t __fastcall DPickRay(char* s, void*, const float* p, float* origin, float* direction)
{
    if (!g_compare)
        return PickRay(s, p, origin, direction);
    return Compared<uint8_t>(
        "s3dcamera.pickray", s, {{origin, 12}, {direction, 12}},
        [&] { return g_pick(s, nullptr, p, origin, direction); },
        [&](char* c, void** o) { return PickRay(c, p, static_cast<float*>(o[0]), static_cast<float*>(o[1])); });
}

// The game's callers keep values in ECX (and EDX) across these calls, which
// the game's functions leave alone (the constructor 0x007FF930 uses ECX
// after calling ResetOrientation, Project after BuildView). Each detour is
// entered through a thunk that saves both, passes the stack arguments on and
// restores them.
#define OPENSC4_THUNK(Name, Target, ArgBytes)                                                                  \
    __declspec(naked) void Name()                                                                              \
    {                                                                                                          \
        __asm push ecx                                                                                         \
        __asm push edx                                                                                         \
        OPENSC4_REPUSH_##ArgBytes                                                                              \
        __asm call Target                                                                                      \
        __asm pop edx                                                                                          \
        __asm pop ecx                                                                                          \
        __asm ret ArgBytes                                                                                     \
    }

#define OPENSC4_PUSH(Bytes) __asm push dword ptr [esp + 8 + Bytes]
#define OPENSC4_REPUSH_0
#define OPENSC4_REPUSH_4 OPENSC4_PUSH(4)
#define OPENSC4_REPUSH_8 OPENSC4_PUSH(8) OPENSC4_PUSH(8)
#define OPENSC4_REPUSH_12 OPENSC4_PUSH(12) OPENSC4_PUSH(12) OPENSC4_PUSH(12)
#define OPENSC4_REPUSH_16 OPENSC4_PUSH(16) OPENSC4_PUSH(16) OPENSC4_PUSH(16) OPENSC4_PUSH(16)
#define OPENSC4_REPUSH_24 OPENSC4_PUSH(24) OPENSC4_PUSH(24) OPENSC4_PUSH(24) OPENSC4_PUSH(24) OPENSC4_PUSH(24) OPENSC4_PUSH(24)

OPENSC4_THUNK(TSetZoomScale, DSetZoomScale, 4)
OPENSC4_THUNK(TSetSubViewport, DSetSubViewport, 16)
OPENSC4_THUNK(TResetSubViewport, DResetSubViewport, 0)
OPENSC4_THUNK(TSetDepthOffset, DSetDepthOffset, 4)
OPENSC4_THUNK(TSetViewport, DSetViewport, 8)
OPENSC4_THUNK(TResetOrientation, DResetOrientation, 0)
OPENSC4_THUNK(TGetPosition, DGetPosition, 4)
OPENSC4_THUNK(TSetPosition, DSetPosition, 4)
OPENSC4_THUNK(TGetTarget, DGetTarget, 4)
OPENSC4_THUNK(TSetPixelOffset, DSetPixelOffset, 4)
OPENSC4_THUNK(TSetFrustum, DSetFrustum, 24)
OPENSC4_THUNK(TGetFrustum, DGetFrustum, 24)
OPENSC4_THUNK(TBuildView, DBuildView, 0)
OPENSC4_THUNK(TBuildProjection, DBuildProjection, 0)
OPENSC4_THUNK(TLookAt, DLookAt, 8)
OPENSC4_THUNK(TGetView, DGetView, 0)
OPENSC4_THUNK(TGetProjection, DGetProjection, 0)
OPENSC4_THUNK(TProject, DProject, 8)
OPENSC4_THUNK(TUnproject, DUnproject, 8)
OPENSC4_THUNK(TPickRay, DPickRay, 12)

template <typename Fn>
bool Detour(std::uintptr_t address, void (*thunk)(), Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, reinterpret_cast<Fn>(thunk), original);
}

bool Install(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compare = mode == registry::Mode::Compare;
    const bool ok =
        Detour(addr::cS3DCamera_SetZoomScale, &TSetZoomScale, &g_zoomScale) &&
        Detour(addr::cS3DCamera_SetSubViewport, &TSetSubViewport, &g_subViewport) &&
        Detour(addr::cS3DCamera_ResetSubViewport, &TResetSubViewport, &g_resetSub) &&
        Detour(addr::cS3DCamera_SetDepthOffset, &TSetDepthOffset, &g_depthOffset) &&
        Detour(addr::cS3DCamera_SetViewport, &TSetViewport, &g_viewport) &&
        Detour(addr::cS3DCamera_ResetOrientation, &TResetOrientation, &g_resetOrientation) &&
        Detour(addr::cS3DCamera_GetPosition, &TGetPosition, &g_getPosition) &&
        Detour(addr::cS3DCamera_SetPosition, &TSetPosition, &g_setPosition) &&
        Detour(addr::cS3DCamera_GetTarget, &TGetTarget, &g_getTarget) &&
        Detour(addr::cS3DCamera_SetPixelOffset, &TSetPixelOffset, &g_pixelOffset) &&
        Detour(addr::cS3DCamera_SetFrustum, &TSetFrustum, &g_frustum) &&
        Detour(addr::cS3DCamera_GetFrustum, &TGetFrustum, &g_getFrustum) &&
        Detour(addr::cS3DCamera_BuildView, &TBuildView, &g_buildView) &&
        Detour(addr::cS3DCamera_BuildProjection, &TBuildProjection, &g_buildProjection) &&
        Detour(addr::cS3DCamera_LookAt, &TLookAt, &g_lookAt) &&
        Detour(addr::cS3DCamera_GetView, &TGetView, &g_getView) &&
        Detour(addr::cS3DCamera_GetProjection, &TGetProjection, &g_getProjection) &&
        Detour(addr::cS3DCamera_Project, &TProject, &g_project) &&
        Detour(addr::cS3DCamera_Unproject, &TUnproject, &g_unproject) &&
        Detour(addr::cS3DCamera_PickRay, &TPickRay, &g_pick);
    if (ok)
        log::Write("s3d camera: 20 functions %s", g_compare ? "compared with the game's" : "ours");
    return ok;
}

const bool g_registered = registry::Register({
    "renderer.camera.s3d",
    "renderer/camera",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cS3DCamera_BuildProjection,
    "Our cS3DCamera: viewport, frustum, orientation, view and projection matrices, point projection "
    "(docs/renderer.md, \"The camera\"); compare mode runs both on every call",
    true,
    Install,
});

} // namespace
} // namespace opensc4::s3d_camera
