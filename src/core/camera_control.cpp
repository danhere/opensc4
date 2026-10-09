// Our cSC4CameraControl (build 641), the city view's camera control on top
// of the S3D camera (docs/renderer.md, "The camera control"): zoom, rotation,
// scrolling, the view centre, the orthographic box, the screen-space bounds
// that keep the view on the city, and the messages that announce changes.
// Every function the rest of the game calls is a detour; the class's
// internal helpers are ours behind them. In compare mode each call runs
// ours first, without posting messages, notes the result, puts the object
// (with its S3D camera and bounds) back, runs the game's and records whether
// the two came out bit for bit the same; the game's result is what the game
// sees.
//
// The game computes on the x87 at 53-bit precision and stores floats; here
// register values are doubles and each store to memory is a float, at the
// same points and in the same order of operations as the game's code.
#include "core/camera_control.h"

#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include "core/gd/gd.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

namespace opensc4::camera_control {
namespace {

constexpr std::size_t kSize = 0x160;
constexpr std::size_t kCameraSize = 0x1A8; // the S3D camera

namespace field {
constexpr std::size_t kInitialised = 0x4;    // uint8: Init done
constexpr std::size_t kLoaded = 0x5;         // uint8: read from a save
constexpr std::size_t kRefs = 0x8;
constexpr std::size_t kCamera = 0xC;         // cS3DCamera*
constexpr std::size_t kPosition = 0x10;      // float[3]: the camera, corner + offset
constexpr std::size_t kCorner = 0x1C;        // float[3]: the city corner the camera hangs from, by rotation
constexpr std::size_t kTarget = 0x38;        // float[3]: the view centre moved onto the camera's axis
constexpr std::size_t kCenter = 0x44;        // float[3]: the view centre (saved)
constexpr std::size_t kCenterOffset = 0x50;  // float[3]
constexpr std::size_t kDirection = 0x5C;     // float[3]: minus the view's third row
constexpr std::size_t kUpRow = 0x68;         // float[3]: the view's second row
constexpr std::size_t kRightRow = 0x74;      // float[3]: the view's first row
constexpr std::size_t kPlane = 0x80;         // float[4]: the plane through the target, normal kDirection
constexpr std::size_t kScreen = 0x90;        // float[4]: the target on screen, then in pixels with the pixel offset
constexpr std::size_t kPreviousScreen = 0xA0;
constexpr std::size_t kShake = 0xB0;         // float[2]: pixel offsets (earthquakes)
constexpr std::size_t kScrollSpeeds = 0xB8;  // float[5] by zoom
constexpr std::size_t kFineZoom = 0xF0;
constexpr std::size_t kMinZoom = 0xF4;
constexpr std::size_t kMaxZoom = 0xF8;
constexpr std::size_t kMaxFineZoom = 0xFC;   // zooms past kMaxZoom up to this enlarge by 2^(z - max)
constexpr std::size_t kOld = 0x100;          // int32 zoom, rotation before a change (saved)
constexpr std::size_t kZoom = 0x108;
constexpr std::size_t kRotation = 0x10C;
constexpr std::size_t kNew = 0x110;          // int32 zoom, rotation after a change (saved)
constexpr std::size_t kAngleOffset = 0x118;
constexpr std::size_t kPitch = 0x11C;
constexpr std::size_t kDistance = 0x120;
constexpr std::size_t kNear = 0x124;
constexpr std::size_t kFar = 0x128;
constexpr std::size_t kViewWidth = 0x12C;    // int32 pixels
constexpr std::size_t kViewHeight = 0x130;
constexpr std::size_t kMetresPerPixel = 0x134;
constexpr std::size_t kPixelsPerMetre = 0x138;
constexpr std::size_t kOffset = 0x13C;       // float[3]: the camera from the corner
constexpr std::size_t kBoundsOn = 0x148;     // uint8
constexpr std::size_t kBounds = 0x14C;       // vector of float pairs: begin, end, capacity
constexpr std::size_t kCitySize = 0x158;     // float x, z
} // namespace field

float& F(char* self, std::size_t offset) { return At<float>(self, offset); }
int32_t& I(char* self, std::size_t offset) { return At<int32_t>(self, offset); }
uint8_t& B(char* self, std::size_t offset) { return At<uint8_t>(self, offset); }
float* V(char* self, std::size_t offset) { return &At<float>(self, offset); }

float*& BoundsBegin(char* self) { return At<float*>(self, field::kBounds); }
float*& BoundsEnd(char* self) { return At<float*>(self, field::kBounds + 4); }
float*& BoundsCapacity(char* self) { return At<float*>(self, field::kBounds + 8); }

// The game's "not set" float (0x00B4E268), which locals start out as; copied
// as bits (a float load could change a NaN's).
void Unset(float* out, int count)
{
    for (int i = 0; i < count; ++i)
        std::memcpy(out + i, &Global<uint32_t>(addr::S3D_UnsetFloat), 4);
}

double X87Cos(double x)
{
    double result;
    __asm {
        fld x
        fcos
        fstp result
    }
    return result;
}

double X87Sin(double x)
{
    double result;
    __asm {
        fld x
        fsin
        fstp result
    }
    return result;
}

// a + b as the game's FLD a / FADD b: when both are NaN (a camera the
// projection fails for), the x87 and SSE keep different ones.
float X87Add(float a, float b)
{
    float result;
    __asm {
        fld a
        fadd b
        fstp result
    }
    return result;
}

// -a + b as the game's FCHS / FADD b (a compiler may turn it into b - a,
// which keeps a NaN's sign where FCHS flips it).
float X87NegateAdd(double a, float b)
{
    float result;
    __asm {
        fld a
        fchs
        fadd b
        fstp result
    }
    return result;
}

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

// --- What the camera control uses of the rest of the game --------------------------

void* Camera(char* self) { return At<void*>(self, field::kCamera); }

void CameraViewport(void* camera, float width, float height)
{
    CallThis<void>(addr::cS3DCamera_SetViewport, camera, width, height);
}

const char* CameraView(void* camera) { return CallThis<const char*>(addr::cS3DCamera_GetView, camera); }

float M(const char* view, std::size_t offset) { return *reinterpret_cast<const float*>(view + offset); }

// The view volume (0x00B43DD8): the rest of the game's picture of what is in
// view, rebuilt from the camera control after every change.
void* ViewVolume() { return Global<void*>(addr::SC4_CameraView); }

void UpdateViewVolume(char* self) { CallThis<void>(addr::SC4CameraView_Update, ViewVolume(), self); }

// Set while a compare-mode run of ours is under way: nothing is posted.
bool g_quiet = false;

void Send(void* server, void* message) { Virtual<uint8_t>(server, 0xC, message); }

// The framework's message server, and a new message (cGZMessage), as the
// game's posting code gets them.
void* MessageServer()
{
    using GetFrameworkFn = void*(__cdecl*)();
    void* server = nullptr;
    if (void* framework = reinterpret_cast<GetFrameworkFn>(addr::RZGetFramework)())
        Virtual<uint8_t>(framework, 0x14, 0x04FA845Bu, 0x652294C7u, &server);
    return server;
}

void* NewMessage()
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? CallThis<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    if (message)
        Virtual<uint32_t>(message, 0x4);
    return message;
}

void Done(void* server, void* message)
{
    Virtual<uint32_t>(message, 0x8);
    if (server)
        Virtual<uint32_t>(server, 0x8);
}

void SetChange(void* message, char* self, uint32_t type)
{
    Virtual<uint32_t>(message, 0x14, type);
    Virtual<uint32_t>(message, 0x30, V(self, field::kOld));
    Virtual<uint32_t>(message, 0x44, V(self, field::kNew));
}

// 0x007CC000: before a zoom or rotation change, 0xA6B79600 with the old and
// new zoom and rotation.
void PostBeginChange(char* self, int32_t mode)
{
    if (mode != 2 || g_quiet)
        return;
    void* const server = MessageServer();
    void* const message = NewMessage();
    SetChange(message, self, 0xA6B79600u);
    Send(server, message);
    Done(server, message);
}

// 0x007CC0B0: after a change. Mode 0 (set up) posts 0xA6B79620, 4 (fine zoom)
// 0xA6B79621; 2 (zoom or rotation) and 3 (moved) first post 0x4A9F2FFE with
// the camera control, then 0xA6B79601 and 0xA6B79602, or 0xA6B79611 and
// 0xA6B79612. Mode 1 posts nothing.
void PostChanged(char* self, int32_t mode)
{
    if ((mode != 0 && mode != 2 && mode != 3 && mode != 4) || g_quiet)
        return;
    void* const server = MessageServer();
    void* const message = NewMessage();
    if (mode == 2 || mode == 3) {
        Virtual<uint32_t>(message, 0x14, 0x4A9F2FFEu);
        Virtual<uint32_t>(message, 0x30, self);
        Send(server, message);
    }
    const uint32_t type = mode == 0 ? 0xA6B79620u : mode == 2 ? 0xA6B79601u : mode == 3 ? 0xA6B79611u : 0xA6B79621u;
    SetChange(message, self, type);
    Send(server, message);
    if (mode == 2 || mode == 3) {
        Virtual<uint32_t>(message, 0x14, mode == 2 ? 0xA6B79602u : 0xA6B79612u);
        Send(server, message);
    }
    Done(server, message);
}

// --- Geometry ------------------------------------------------------------------------

// 0x007CBA10: the city corner the camera hangs from, by rotation.
void CityCorner(char* self, float* out, int32_t rotation)
{
    At<uint32_t>(out, 4) = 0;
    switch (rotation) {
    case 0: At<uint32_t>(out, 0) = 0, At<uint32_t>(out, 8) = 0; break;
    case 1: At<uint32_t>(out, 0) = At<uint32_t>(self, field::kCitySize), At<uint32_t>(out, 8) = 0; break;
    case 2:
        At<uint32_t>(out, 0) = At<uint32_t>(self, field::kCitySize);
        At<uint32_t>(out, 8) = At<uint32_t>(self, field::kCitySize + 4);
        break;
    case 3: At<uint32_t>(out, 0) = 0, At<uint32_t>(out, 8) = At<uint32_t>(self, field::kCitySize + 4); break;
    default: break;
    }
}

// 0x007CBA90: where a ray from `point` along `direction` meets the plane
// y = 0; false when it runs parallel or away from it.
bool RayToGround(const float* direction, const float* point, float* out)
{
    if (std::fabs(static_cast<double>(direction[1])) < 1e-4f)
        return false;
    const double t = (-1.0 / static_cast<double>(direction[1])) * point[1];
    if (t < 0.0)
        return false;
    const double x = t * direction[0];
    const float y = static_cast<float>(t * direction[1]);
    const float z = static_cast<float>(t * direction[2]);
    out[2] = static_cast<float>(static_cast<double>(z) + point[2]);
    out[0] = static_cast<float>(x + point[0]);
    out[1] = static_cast<float>(static_cast<double>(y) + point[1]);
    return true;
}

// 0x007CBCE0: a world point to view units (x right, y up) relative to the camera.
void WorldToScreen(char* self, const float* point, float* out)
{
    const float d0 = static_cast<float>(static_cast<double>(point[0]) - F(self, field::kPosition));
    const float d1 = static_cast<float>(static_cast<double>(point[1]) - F(self, field::kPosition + 4));
    const float d2 = static_cast<float>(static_cast<double>(point[2]) - F(self, field::kPosition + 8));
    const char* const m = CameraView(Camera(self));
    const double x = (static_cast<double>(d2) * M(m, 0xC) + static_cast<double>(d1) * M(m, 0x8)) +
                     static_cast<double>(d0) * M(m, 0x4);
    const double y = (static_cast<double>(d2) * M(m, 0x18) + static_cast<double>(d1) * M(m, 0x14)) +
                     static_cast<double>(d0) * M(m, 0x10);
    const float yf = static_cast<float>(y);
    const float scale = M(m, 0x34);
    out[0] = static_cast<float>(x * scale);
    out[1] = static_cast<float>(static_cast<double>(yf) * scale);
}

// 0x007CBD70: view units back to a world point (on the camera's plane).
void ScreenToWorld(char* self, const float* screen, float* out)
{
    const float sx = screen[0], sy = screen[1];
    const char* const m = CameraView(Camera(self));
    const double wx = (static_cast<double>(sy) * M(m, 0x10) + static_cast<double>(sx) * M(m, 0x4)) +
                      static_cast<double>(M(m, 0x1C)) * 0.0f;
    const double wy = (static_cast<double>(sy) * M(m, 0x14) + static_cast<double>(sx) * M(m, 0x8)) +
                      static_cast<double>(M(m, 0x20)) * 0.0f;
    const double wz = (static_cast<double>(sy) * M(m, 0x18) + static_cast<double>(sx) * M(m, 0xC)) +
                      static_cast<double>(M(m, 0x24)) * 0.0f;
    const float wzf = static_cast<float>(wz);
    const float inverse = static_cast<float>(1.0f / static_cast<double>(M(m, 0x34)));
    const float x = static_cast<float>(wx * inverse);
    const double y = wy * inverse;
    const float z = static_cast<float>(static_cast<double>(wzf) * inverse);
    const float x2 = static_cast<float>(static_cast<double>(x) + F(self, field::kPosition));
    const double y2 = y + F(self, field::kPosition + 4);
    const double z2 = static_cast<double>(z) + F(self, field::kPosition + 8);
    out[0] = x2;
    out[1] = static_cast<float>(y2);
    out[2] = static_cast<float>(z2);
}

// 0x007CBE40: the orthographic box (the tile size for the zoom times the fine
// zoom, the pitch, near and far from the city's diagonal) and the camera's
// offset from its corner.
void BuildProjection(char* self)
{
    const int32_t zoom = I(self, field::kZoom);
    const auto* const tileSizes = &Global<int32_t>(addr::cSC4CameraControl_TileSizes);
    const auto* const pitches = &Global<float>(addr::cSC4CameraControl_Pitches);
    const float pixelsPerTile = static_cast<float>(static_cast<double>(tileSizes[zoom]) * F(self, field::kFineZoom));
    using TileExtentFn = double(__cdecl*)(float size, int32_t zoom);
    const double extent = reinterpret_cast<TileExtentFn>(addr::SC4CameraView_TileExtent)(16.0f, zoom);
    const float metresPerPixel = static_cast<float>(extent / pixelsPerTile);
    F(self, field::kMetresPerPixel) = metresPerPixel;
    F(self, field::kPixelsPerMetre) = static_cast<float>(static_cast<double>(pixelsPerTile) / extent);
    const float angle = static_cast<float>((static_cast<double>(I(self, field::kRotation)) + 1.0f) * 1.57079637f +
                                           F(self, field::kAngleOffset));
    const float pitch = pitches[zoom];
    const float width = 4096.0f < F(self, field::kCitySize) ? F(self, field::kCitySize) : 4096.0f;
    const float depth = 4096.0f < F(self, field::kCitySize + 4) ? F(self, field::kCitySize + 4) : 4096.0f;
    F(self, field::kPitch) = pitch;
    const double cosine = X87Cos(pitch);
    const float range = static_cast<float>(static_cast<double>(F(self, field::kDistance)) / cosine);
    const double diagonal =
        std::sqrt(static_cast<double>(depth) * depth + static_cast<double>(width) * width);
    const float nearZ = static_cast<float>((static_cast<double>(range) - diagonal / cosine) * 0.95);
    F(self, field::kNear) = nearZ;
    const float farZ = static_cast<float>(static_cast<double>(range) * 1.5);
    F(self, field::kFar) = farZ;
    const float halfWidth =
        static_cast<float>(static_cast<double>(I(self, field::kViewWidth)) * metresPerPixel * 0.5);
    const float halfHeight =
        static_cast<float>(static_cast<double>(I(self, field::kViewHeight)) * metresPerPixel * 0.5);
    CallThis<void>(addr::cS3DCamera_SetFrustum, Camera(self), -halfWidth, halfWidth, -halfHeight, halfHeight, nearZ,
                   farZ);
    const double distance = F(self, field::kDistance);
    const double x = X87Cos(angle) * distance;
    const double y = X87Sin(pitch) * range;
    F(self, field::kOffset + 8) = static_cast<float>(X87Sin(angle) * distance);
    F(self, field::kOffset) = static_cast<float>(x);
    F(self, field::kOffset + 4) = static_cast<float>(y);
}

// With [renderer] scale_city = no the fine zoom is divided by the render
// scale while the box is built, as the D3D11 driver's wrapper of the game's
// function does (src/core/gd/driver.cpp).
void UpdateProjection(char* self)
{
    float& fine = F(self, field::kFineZoom);
    const float saved = fine;
    fine = saved / gd::CityZoomDivisor();
    BuildProjection(self);
    fine = saved;
}

// The integer below a coordinate the game's way: the 1.5 * 2^23 rounding trick
// on x - 0.5, one up when that lands exactly one below.
double SnapDown(float value)
{
    const float rounded = static_cast<float>((static_cast<double>(value) - 0.5f) + 12582912.0f);
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof bits);
    const int32_t below = static_cast<int32_t>(bits + 0xB4C00000u);
    const int32_t above = below + 1;
    return static_cast<double>(value) == static_cast<double>(above) ? above : below;
}

// 0x007CC290: the camera placed and aimed, snapped to whole pixels (plus any
// shake), and the view rows, plane and screen positions derived from it.
void UpdateView(char* self)
{
    float* const corner = V(self, field::kCorner);
    CityCorner(self, corner, I(self, field::kRotation));
    float* const offset = V(self, field::kOffset);
    const float pz = static_cast<float>(static_cast<double>(corner[2]) + offset[2]);
    F(self, field::kPosition + 8) = pz;
    F(self, field::kPosition) = static_cast<float>(static_cast<double>(corner[0]) + offset[0]);
    F(self, field::kPosition + 4) = static_cast<float>(static_cast<double>(corner[1]) + offset[1]);
    float* const centre = V(self, field::kCenter);
    const float ex = static_cast<float>(static_cast<double>(centre[0]) - corner[0]);
    const float ey = static_cast<float>(static_cast<double>(centre[1]) - corner[1]);
    const float ez = static_cast<float>(static_cast<double>(centre[2]) - corner[2]);
    const double ox = offset[0], oy = offset[1], oz = offset[2];
    const double lengthSquared = (ox * ox + oy * oy) + oz * oz;
    const float lengthSquaredF = static_cast<float>(lengthSquared);
    double k = 0.0f;
    if (lengthSquared > 0.0f)
        k = 1.0f - ((static_cast<double>(ez) * oz + static_cast<double>(ey) * oy) + static_cast<double>(ex) * ox) /
                       lengthSquaredF;
    const float kx = static_cast<float>(k * ox);
    const float ky = static_cast<float>(k * oy);
    const double kz = k * oz;
    float* const target = V(self, field::kTarget);
    target[2] = static_cast<float>(kz + centre[2]);
    target[0] = static_cast<float>(static_cast<double>(kx) + centre[0]);
    target[1] = static_cast<float>(static_cast<double>(ky) + centre[1]);
    void* const camera = Camera(self);
    CallThis<void>(addr::cS3DCamera_SetPosition, camera, static_cast<const float*>(target));
    const float up[3] = {0.0f, 1.0f, 0.0f};
    CallThis<void>(addr::cS3DCamera_LookAt, camera, static_cast<const float*>(centre), up);
    float noOffset[2];
    Unset(noOffset, 2);
    CallThis<void>(addr::cS3DCamera_SetPixelOffset, camera, static_cast<const float*>(noOffset));
    float projected[3];
    Unset(projected, 3);
    CallThis<uint8_t>(addr::cS3DCamera_Project, camera, static_cast<const float*>(corner), projected);
    const double fx = static_cast<double>(projected[0]) - SnapDown(projected[0]);
    const double fy = static_cast<double>(projected[1]) - SnapDown(projected[1]);
    const float pixelOffset[2] = {static_cast<float>(fx + F(self, field::kShake)),
                                  X87NegateAdd(fy, F(self, field::kShake + 4))};
    CallThis<void>(addr::cS3DCamera_SetPixelOffset, camera, pixelOffset);
    const char* const m = CameraView(camera);
    float* const direction = V(self, field::kDirection);
    direction[2] = -M(m, 0x24);
    direction[0] = -M(m, 0x1C);
    direction[1] = -M(m, 0x20);
    std::memcpy(V(self, field::kRightRow), m + 0x4, 12);
    std::memcpy(V(self, field::kUpRow), m + 0x10, 12);
    float* const plane = V(self, field::kPlane);
    plane[0] = direction[0], plane[1] = direction[1], plane[2] = direction[2];
    plane[3] = static_cast<float>(-((static_cast<double>(target[1]) * direction[1] +
                                     static_cast<double>(target[2]) * direction[2]) +
                                    static_cast<double>(target[0]) * direction[0]));
    std::memcpy(V(self, field::kPreviousScreen), V(self, field::kScreen), 16);
    WorldToScreen(self, target, V(self, field::kScreen));
    const double pixelsPerMetre = F(self, field::kPixelsPerMetre);
    const float a = static_cast<float>(pixelsPerMetre * F(self, field::kScreen));
    const float b = static_cast<float>(pixelsPerMetre * F(self, field::kScreen + 4));
    F(self, field::kScreen + 8) = X87Add(a, pixelOffset[0]);
    F(self, field::kScreen + 12) = X87Add(b, pixelOffset[1]);
}

// 0x007CC650: the point `b` moved inside the bounds polygon, sliding along
// the edges it crosses on the way from `a`; at most one pass per edge, after
// which `a` comes back.
void ClipToBounds(char* self, const float* a, const float* b, float* out)
{
    const float* const begin = BoundsBegin(self);
    const float* const end = BoundsEnd(self);
    if (!B(self, field::kBoundsOn) || begin == end) {
        out[0] = b[0], out[1] = b[1];
        return;
    }
    float ax = a[0], ay = a[1];
    float bx = b[0];
    double by = b[1];
    bool sideA = false, sideB = false;
    uint32_t passes = 0;
    const uint32_t count = static_cast<uint32_t>(end - begin) / 2;
    const float* const last = end - 2;
    for (;;) {
        const float* hitPrevious = nullptr;
        const float* hitCurrent = nullptr;
        float best = 1.0f;
        const float* previous = last;
        for (uint32_t j = 0; j < count; ++j) {
            const float* const current = begin + 2 * j;
            const float ex = static_cast<float>(static_cast<double>(previous[0]) - current[0]);
            const double ey = static_cast<double>(previous[1]) - current[1];
            const double length = std::sqrt(ey * ey + static_cast<double>(ex) * ex);
            const float lengthF = static_cast<float>(length);
            if (!(length < 1e-8)) {
                const double n0 = ey / lengthF;
                const double n1 = (-1.0f / static_cast<double>(lengthF)) * ex;
                const double distance = n1 * previous[1] + n0 * previous[0];
                const float sideOfB = static_cast<float>((n1 * by + n0 * bx) - distance);
                if (!(sideOfB > 0.0f)) {
                    const double sideOfA = (n1 * ay + n0 * ax) - distance;
                    if (!(sideOfA <= sideOfB)) {
                        const double t = sideOfA / (sideOfA - sideOfB);
                        if (t < best) {
                            best = static_cast<float>(t);
                            hitPrevious = previous;
                            hitCurrent = current;
                        }
                    }
                }
            }
            previous = current;
        }
        if (!hitPrevious) {
            out[1] = static_cast<float>(by);
            out[0] = bx;
            return;
        }
        const double t = best >= 0.001f ? static_cast<double>(best) - 1e-4f : 0.0f;
        const double dx = static_cast<double>(bx) - ax;
        const float dy = static_cast<float>(by - ay);
        const float keep = static_cast<float>(1.0 - t);
        const float mx = static_cast<float>(dx * keep);
        const float my = static_cast<float>(static_cast<double>(dy) * keep);
        const float nx = static_cast<float>(static_cast<double>(bx) - mx);
        ax = nx;
        const float ny = static_cast<float>(by - my);
        ay = ny;
        const float gx = static_cast<float>(static_cast<double>(hitCurrent[0]) - hitPrevious[0]);
        const double gy = static_cast<double>(hitCurrent[1]) - hitPrevious[1];
        const double travelX = static_cast<double>(b[0]) - a[0];
        const double travelY = static_cast<double>(b[1]) - a[1];
        bool other;
        if (!(travelY * gy + travelX * gx < 0.0f)) {
            other = sideB;
            sideA = true;
        } else {
            other = sideA;
            sideB = true;
        }
        if (other) {
            out[0] = nx, out[1] = ny;
            return;
        }
        const double inverse = 1.0f / std::sqrt(gy * gy + static_cast<double>(gx) * gx);
        const double ux = static_cast<double>(gx) * inverse;
        const double uy = gy * inverse;
        const float uyF = static_cast<float>(uy);
        const float along = static_cast<float>(uy * my + ux * mx);
        const double slideX = ux * along;
        const float slideY = static_cast<float>(static_cast<double>(uyF) * along);
        bx = static_cast<float>(slideX + nx);
        by = static_cast<double>(slideY) + ny;
        if (++passes >= count) {
            out[0] = a[0], out[1] = a[1];
            return;
        }
    }
}

void PushBound(char* self, float x, float y)
{
    float*& end = BoundsEnd(self);
    if (end != BoundsCapacity(self)) {
        if (end)
            end[0] = x, end[1] = y;
        end += 2;
        return;
    }
    using InsertFn = void(__thiscall*)(void* vector, float* where, const float* value, void* allocator,
                                       uint32_t count, uint8_t atEnd);
    const float value[2] = {x, y};
    uint8_t allocator = 0;
    reinterpret_cast<InsertFn>(addr::SC4PointVector_InsertN)(self + field::kBounds, end, value, &allocator, 1, 1);
}

// 0x007CCC60: the bounds polygon in view units, from the view volume's
// ground corners: the sides widened by 500 and the top raised by the
// height the camera sees past the far corners.
void BuildBounds(char* self)
{
    if (!B(self, field::kBoundsOn))
        return;
    BoundsEnd(self) = BoundsBegin(self);
    void* const view = ViewVolume();
    float c1[3], c0[3], c2[3], c3[3];
    Unset(c1, 3), Unset(c0, 3), Unset(c2, 3), Unset(c3, 3);
    CallThis<void>(addr::SC4CameraView_GroundCorner, view, 1, c1);
    CallThis<void>(addr::SC4CameraView_GroundCorner, view, 0, c0);
    CallThis<void>(addr::SC4CameraView_GroundCorner, view, 2, c2);
    CallThis<void>(addr::SC4CameraView_GroundCorner, view, 3, c3);
    float a[2] = {0.0f, 0.0f}, b[2] = {0.0f, 0.0f}, c[2] = {0.0f, 0.0f}, d[2] = {0.0f, 0.0f};
    WorldToScreen(self, c1, a);
    WorldToScreen(self, c0, b);
    WorldToScreen(self, c2, c);
    WorldToScreen(self, c3, d);
    const float rise = static_cast<float>(
        static_cast<double>(F(self, field::kOffset + 4)) * 0.48 / F(self, field::kUpRow + 4) -
        static_cast<double>(F(self, field::kMetresPerPixel)) * I(self, field::kViewHeight) * 0.5);
    b[0] = static_cast<float>(static_cast<double>(b[0]) - 500.0f);
    c[0] = static_cast<float>(static_cast<double>(c[0]) + 500.0f);
    PushBound(self, a[0], static_cast<float>(static_cast<double>(rise) + a[1]));
    PushBound(self, b[0], static_cast<float>(static_cast<double>(rise) + b[1]));
    PushBound(self, b[0], b[1]);
    PushBound(self, d[0], d[1]);
    PushBound(self, c[0], c[1]);
    PushBound(self, c[0], static_cast<float>(static_cast<double>(rise) + c[1]));
}

// The view volume rebuilt and, when `always` or there are none yet, the bounds.
void Refresh(char* self, bool always)
{
    if (!ViewVolume())
        return;
    UpdateViewVolume(self);
    if (always || BoundsBegin(self) == BoundsEnd(self))
        BuildBounds(self);
}

// 0x007CD000: the view centre pulled back inside the bounds (then onto the
// ground) when it has strayed, with mode 3's messages.
void KeepCenterInBounds(char* self)
{
    float a[2] = {0.0f, 0.0f};
    WorldToScreen(self, V(self, field::kCorner), a);
    float* const centre = V(self, field::kCenter);
    float c[2] = {0.0f, 0.0f};
    WorldToScreen(self, centre, c);
    float clipped[2] = {0.0f, 0.0f};
    ClipToBounds(self, a, c, clipped);
    if (std::fabs(static_cast<double>(clipped[0]) - c[0]) < 1e-4f &&
        std::fabs(static_cast<double>(clipped[1]) - c[1]) < 1e-4f)
        return;
    ScreenToWorld(self, clipped, centre);
    float hit[3];
    Unset(hit, 3);
    if (RayToGround(V(self, field::kDirection), centre, hit)) {
        centre[0] = hit[0], centre[1] = hit[1], centre[2] = hit[2];
        I(self, field::kCenterOffset) = 0, I(self, field::kCenterOffset + 4) = 0, I(self, field::kCenterOffset + 8) = 0;
    }
    if (!Camera(self))
        return;
    UpdateView(self);
    Refresh(self, false);
    PostChanged(self, 3);
}

// --- The class's functions -----------------------------------------------------------

// 0x007CC990
void Construct(char* self)
{
    const uint32_t unset = Global<uint32_t>(addr::S3D_UnsetFloat);
    At<uint32_t>(self, 0) = addr::cSC4CameraControl_vftable;
    B(self, field::kInitialised) = 0;
    B(self, field::kLoaded) = 0;
    for (std::size_t at = field::kRefs; at < 0x28; at += 4)
        At<uint32_t>(self, at) = 0;
    for (std::size_t at = 0x28; at < 0x38; at += 4)
        At<uint32_t>(self, at) = unset;
    for (std::size_t at = 0x38; at < 0xB8; at += 4)
        At<uint32_t>(self, at) = 0;
    F(self, 0x6C) = 1.0f;
    F(self, 0xCC) = 0.0f;
    F(self, 0xD0) = 0.0f;
    F(self, 0xD4) = 0.0f;
    At<uint32_t>(self, 0xD8) = 0x7F7FFFFF;
    At<uint32_t>(self, 0xDC) = 0x7F7FFFFF;
    At<uint32_t>(self, 0xE0) = 0x7F7FFFFF;
    At<uint32_t>(self, 0xE4) = 0x7F7FFFFF;
    At<uint32_t>(self, 0xE8) = 0x7F7FFFFF;
    At<uint32_t>(self, 0xEC) = 0x7F7FFFFF;
    F(self, field::kFineZoom) = 1.0f;
    F(self, field::kMinZoom) = 0.0f;
    F(self, field::kMaxZoom) = 4.0f;
    F(self, field::kMaxFineZoom) = 5.0f;
    for (std::size_t at = field::kOld; at < field::kAngleOffset; at += 4)
        At<uint32_t>(self, at) = 0;
    At<uint32_t>(self, field::kAngleOffset) = 0xBEC90FDB; // -pi/8
    At<uint32_t>(self, field::kPitch) = 0x3F060A92;
    F(self, field::kDistance) = 8192.0f;
    F(self, field::kNear) = 0.0f;
    F(self, field::kFar) = 0.0f;
    I(self, field::kViewWidth) = 800;
    I(self, field::kViewHeight) = 600;
    F(self, field::kMetresPerPixel) = 1.0f;
    F(self, field::kPixelsPerMetre) = 1.0f;
    At<uint32_t>(self, field::kOffset) = unset;
    At<uint32_t>(self, field::kOffset + 4) = unset;
    At<uint32_t>(self, field::kOffset + 8) = unset;
    B(self, field::kBoundsOn) = 1;
    BoundsBegin(self) = nullptr;
    BoundsEnd(self) = nullptr;
    BoundsCapacity(self) = nullptr;
    F(self, field::kCitySize) = 4096.0f;
    F(self, field::kCitySize + 4) = 4096.0f;
    F(self, field::kScrollSpeeds) = 100.0f;
    F(self, field::kScrollSpeeds + 4) = 50.0f;
    F(self, field::kScrollSpeeds + 8) = 25.0f;
    F(self, field::kScrollSpeeds + 12) = 12.5f;
    F(self, field::kScrollSpeeds + 16) = 6.25f;
}

// 0x007CCC20, the scalar deleting destructor.
void Destroy(char* self, uint32_t flags)
{
    using FreeFn = void(__cdecl*)(void* p);
    At<uint32_t>(self, 0) = addr::cSC4CameraControl_vftable;
    if (float* const begin = BoundsBegin(self))
        reinterpret_cast<FreeFn>(addr::SC4_Free)(begin);
    if (flags & 1)
        reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(self);
}

// 0x007CB970
void ReleaseCamera(char* self)
{
    if (void* const camera = Camera(self)) {
        Virtual<uint32_t>(camera, 0x8);
        At<void*>(self, field::kCamera) = nullptr;
    }
}

// 0x007CB990
void GetViewSize(char* self, int32_t* width, int32_t* height)
{
    *width = I(self, field::kViewWidth);
    *height = I(self, field::kViewHeight);
}

// 0x007CB9B0
void SetViewSize(char* self, int32_t width, int32_t height)
{
    I(self, field::kViewWidth) = width;
    I(self, field::kViewHeight) = height;
    if (void* const camera = Camera(self))
        CameraViewport(camera, static_cast<float>(width), static_cast<float>(height));
}

// 0x007CB9F0
void GetNearFar(char* self, float* nearZ, float* farZ)
{
    *nearZ = F(self, field::kNear);
    *farZ = F(self, field::kFar);
}

// 0x007CBB10: version 1, the view centre, the old and new zoom and rotation.
uint32_t Write(char* self, void* stream)
{
    Virtual<uint8_t>(stream, 0x1C, 1u);
    for (int i = 0; i < 3; ++i)
        Virtual<uint8_t>(stream, 0x30, F(self, field::kCenter + 4 * i));
    for (std::size_t at = field::kOld; at < field::kAngleOffset; at += 4)
        Virtual<uint8_t>(stream, 0x20, I(self, at));
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1u : 0u;
}

// 0x007CBBA0
uint32_t Read(char* self, void* stream)
{
    B(self, field::kLoaded) = 1;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    for (int i = 0; i < 3; ++i)
        Virtual<uint8_t>(stream, 0x30, V(self, field::kCenter + 4 * i));
    for (std::size_t at = field::kOld; at < field::kAngleOffset; at += 4)
        Virtual<uint8_t>(stream, 0x20, &I(self, at));
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1u : 0u;
}

// 0x007CBC40: the zoom, one more when the fine zoom is past 1.5.
int32_t EffectiveZoom(char* self)
{
    return I(self, field::kZoom) + (F(self, field::kFineZoom) > 1.5 ? 1 : 0);
}

bool IsNaN(const float* value)
{
    uint32_t bits;
    std::memcpy(&bits, value, sizeof bits);
    return (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x7FFFFFu) != 0;
}

// 0x007CBC60
uint8_t SetCenterOffset(char* self, const float* offset)
{
    if (IsNaN(offset) || IsNaN(offset + 1) || IsNaN(offset + 2))
        return 0;
    std::memcpy(V(self, field::kCenterOffset), offset, 12);
    return 1;
}

// 0x007CC5B0
void PostSetUp(char* self) { PostChanged(self, 0); }

// 0x007CCF80: everything rebuilt after a change of the given kind (see
// PostChanged); 3 keeps the box.
uint8_t Update(char* self, int32_t mode)
{
    if (!Camera(self))
        return 0;
    PostBeginChange(self, mode);
    if (mode != 3)
        UpdateProjection(self);
    UpdateView(self);
    if (ViewVolume()) {
        UpdateViewVolume(self);
        if (mode == 2 || mode == 1 || BoundsBegin(self) == BoundsEnd(self))
            BuildBounds(self);
    }
    PostChanged(self, mode);
    if (mode == 2)
        KeepCenterInBounds(self);
    return 1;
}

// 0x007CD330: the view centre moved `distance` (times the zoom's scroll
// speed) in the screen direction `angle`, kept in bounds and put on the
// terrain.
uint8_t Scroll(char* self, float angle, float distance)
{
    const double cosine = X87Cos(angle);
    const double sine = -X87Sin(angle);
    const double step = static_cast<double>(distance) * F(self, field::kScrollSpeeds + 4 * I(self, field::kZoom));
    const float mx = static_cast<float>(cosine * step);
    const float my = static_cast<float>(sine * step);
    float* const centre = V(self, field::kCenter);
    float s[2] = {0.0f, 0.0f};
    WorldToScreen(self, centre, s);
    const float t[2] = {static_cast<float>(static_cast<double>(s[0]) + mx),
                        static_cast<float>(static_cast<double>(s[1]) + my)};
    float moved[2] = {0.0f, 0.0f};
    ClipToBounds(self, s, t, moved);
    ScreenToWorld(self, moved, centre);
    float hit[3];
    Unset(hit, 3);
    using PickerFn = void*(__cdecl*)();
    void* const picker = reinterpret_cast<PickerFn>(addr::SC4_TerrainPicker)();
    float* const direction = V(self, field::kDirection);
    if (Virtual<uint8_t>(picker, 0xC, static_cast<const float*>(centre), static_cast<const float*>(direction), 0u,
                         hit, 1u) ||
        RayToGround(direction, centre, hit))
        centre[0] = hit[0], centre[1] = hit[1], centre[2] = hit[2];
    if (!Camera(self))
        return 0;
    UpdateView(self);
    Refresh(self, false);
    PostChanged(self, 3);
    return 1;
}

// 0x007CD6E0: the fine zoom, changed only from a value in (0, 1000).
void SetZoomFactor(char* self, float factor)
{
    const float current = F(self, field::kFineZoom);
    if (current == factor || !(current > 0.0f) || !(current < 1000.0f))
        return;
    F(self, field::kFineZoom) = factor;
    if (!Camera(self))
        return;
    UpdateProjection(self);
    UpdateView(self);
    Refresh(self, false);
    PostChanged(self, 4);
}

// The common tail of the zoom and rotation setters.
uint8_t ZoomOrRotationChanged(char* self)
{
    if (!Camera(self))
        return 0;
    PostBeginChange(self, 2);
    UpdateProjection(self);
    UpdateView(self);
    Refresh(self, true);
    PostChanged(self, 2);
    KeepCenterInBounds(self);
    return 1;
}

// 0x007CD540
uint8_t SetRotation(char* self, int32_t rotation)
{
    if (rotation < 0 || rotation > 3 || rotation == I(self, field::kRotation))
        return 1;
    const int32_t zoom = I(self, field::kZoom), old = I(self, field::kRotation);
    I(self, field::kRotation) = rotation;
    I(self, field::kNew) = zoom;
    I(self, field::kOld) = zoom;
    I(self, field::kOld + 4) = old;
    I(self, field::kNew + 4) = rotation;
    return ZoomOrRotationChanged(self);
}

// 0x007CD810: the view centre, each coordinate within +-100,000.
uint8_t SetCenter(char* self, const float* centre)
{
    if (IsNaN(centre) || IsNaN(centre + 1) || IsNaN(centre + 2))
        return 0;
    for (int i = 0; i < 3; ++i)
        if (centre[i] < -100000.0f || centre[i] > 100000.0f)
            return 0;
    std::memcpy(V(self, field::kCenter), centre, 12);
    I(self, field::kCenterOffset) = 0, I(self, field::kCenterOffset + 4) = 0, I(self, field::kCenterOffset + 8) = 0;
    if (Camera(self)) {
        UpdateView(self);
        Refresh(self, false);
        PostChanged(self, 3);
    }
    return 1;
}

// 0x007CDA20
void SetShake(char* self, float x, float y)
{
    if (x == F(self, field::kShake) && y == F(self, field::kShake + 4))
        return;
    F(self, field::kShake) = x;
    F(self, field::kShake + 4) = y;
    if (!Camera(self))
        return;
    UpdateProjection(self);
    UpdateView(self);
    Refresh(self, false);
}

// 0x007CDAA0
void SetCitySize(char* self, float width, float depth)
{
    F(self, field::kCitySize) = width;
    F(self, field::kCitySize + 4) = depth;
    if (!B(self, field::kInitialised))
        return;
    BoundsEnd(self) = BoundsBegin(self);
    if (!Camera(self))
        return;
    UpdateProjection(self);
    UpdateView(self);
    Refresh(self, true);
}

// 0x007CD200
void SetCamera(char* self, void* camera)
{
    if (camera == Camera(self))
        return;
    if (camera)
        Virtual<uint32_t>(camera, 0x4);
    if (void* const old = Camera(self))
        Virtual<uint32_t>(old, 0x8);
    At<void*>(self, field::kCamera) = camera;
    CameraViewport(camera, static_cast<float>(I(self, field::kViewWidth)),
                   static_cast<float>(I(self, field::kViewHeight)));
    if (!B(self, field::kInitialised) || !Camera(self))
        return;
    UpdateProjection(self);
    UpdateView(self);
    Refresh(self, false);
    PostChanged(self, 0);
}

// 0x007CDB20: once, the centre (unless read from a save) and the camera.
uint8_t Init(char* self, const float* centre, void* camera)
{
    if (B(self, field::kInitialised))
        return 1;
    if (B(self, field::kLoaded)) {
        I(self, field::kNew) = I(self, field::kZoom);
        I(self, field::kNew + 4) = I(self, field::kRotation);
    } else {
        std::memcpy(V(self, field::kCenter), centre, 12);
    }
    SetCamera(self, camera);
    if (Camera(self)) {
        UpdateProjection(self);
        UpdateView(self);
        Refresh(self, false);
        PostChanged(self, 0);
    }
    B(self, field::kInitialised) = 1;
    return 1;
}

// Zooms past the maximum (up to kMaxFineZoom) stay at the maximum and
// enlarge by 2^(zoom - maximum) instead.
float FineZoomFor(char* self, double zoom)
{
    return static_cast<float>(std::pow(2.0, zoom - F(self, field::kMaxZoom)));
}

// 0x007CDC50
uint8_t SetZoom(char* self, float zoom)
{
    float factor = 1.0f;
    if (zoom >= F(self, field::kMinZoom) && zoom <= F(self, field::kMaxFineZoom) && zoom > F(self, field::kMaxZoom)) {
        factor = FineZoomFor(self, zoom);
        zoom = F(self, field::kMaxZoom);
    }
    if (!(zoom >= F(self, field::kMinZoom)) || !(zoom <= F(self, field::kMaxZoom)))
        return 0;
    if (static_cast<double>(zoom) == static_cast<double>(I(self, field::kZoom)) &&
        factor == F(self, field::kFineZoom))
        return 0;
    SetZoomFactor(self, factor);
    I(self, field::kOld) = I(self, field::kZoom);
    I(self, field::kOld + 4) = I(self, field::kRotation);
    const int32_t level = static_cast<int32_t>(zoom);
    I(self, field::kZoom) = level;
    I(self, field::kNew) = level;
    I(self, field::kNew + 4) = I(self, field::kRotation);
    return ZoomOrRotationChanged(self);
}

// 0x007CDE60
uint8_t SetZoomAndRotation(char* self, int32_t zoom, int32_t rotation)
{
    float factor = 1.0f;
    const float zoomF = static_cast<float>(zoom);
    if (static_cast<double>(zoom) >= F(self, field::kMinZoom) && zoomF <= F(self, field::kMaxFineZoom) &&
        zoomF > F(self, field::kMaxZoom)) {
        factor = FineZoomFor(self, zoomF);
        zoom = static_cast<int32_t>(F(self, field::kMaxZoom));
    }
    if (!(static_cast<double>(zoom) >= F(self, field::kMinZoom)) ||
        !(static_cast<double>(zoom) <= F(self, field::kMaxZoom)))
        return 0;
    if (rotation < 0 || rotation > 3)
        return 0;
    SetZoomFactor(self, factor);
    if (zoom == I(self, field::kZoom) && rotation == I(self, field::kRotation))
        return 0;
    I(self, field::kOld) = I(self, field::kZoom);
    I(self, field::kOld + 4) = I(self, field::kRotation);
    I(self, field::kZoom) = zoom;
    I(self, field::kNew) = zoom;
    I(self, field::kRotation) = rotation;
    I(self, field::kNew + 4) = rotation;
    return ZoomOrRotationChanged(self);
}

// --- Compare mode ----------------------------------------------------------------------

bool g_compare = false;
int g_depth = 0; // > 0 while compare mode runs either side: nested calls go to the game's

constexpr uint32_t kMaxPoints = 32;

struct Snapshot {
    char control[kSize];
    char camera[kCameraSize];
    bool hasCamera;
    uint32_t points;
    float bounds[2 * kMaxPoints];
};

void Take(char* self, Snapshot& s)
{
    std::memcpy(s.control, self, kSize);
    void* const camera = Camera(self);
    s.hasCamera = camera != nullptr;
    if (camera)
        std::memcpy(s.camera, camera, kCameraSize);
    const float* const begin = BoundsBegin(self);
    s.points = begin ? static_cast<uint32_t>(BoundsEnd(self) - begin) / 2 : 0;
    if (s.points <= kMaxPoints && s.points)
        std::memcpy(s.bounds, begin, 8 * s.points);
}

// The object, its camera and its bounds as before; the bounds keep the
// buffer they have now (ours may have grown it).
void Restore(char* self, const Snapshot& s)
{
    char vector[12];
    std::memcpy(vector, self + field::kBounds, sizeof vector);
    std::memcpy(self, s.control, kSize);
    std::memcpy(self + field::kBounds, vector, sizeof vector);
    if (s.hasCamera)
        std::memcpy(Camera(self), s.camera, kCameraSize);
    float* const begin = BoundsBegin(self);
    const uint32_t capacity = begin ? static_cast<uint32_t>(BoundsCapacity(self) - begin) / 2 : 0;
    if (s.points > capacity || s.points > kMaxPoints) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            log::Write("camera control: compare could not restore %u bounds points (capacity %u)", s.points, capacity);
        }
        return;
    }
    if (s.points)
        std::memcpy(begin, s.bounds, 8 * s.points);
    if (begin)
        BoundsEnd(self) = begin + 2 * s.points;
}

std::string Hex(const void* data, std::size_t size)
{
    std::string text;
    char hex[4];
    const auto* b = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        std::snprintf(hex, sizeof hex, "%02X", b[i]);
        text += hex;
    }
    return text;
}

// The camera's view and projection are caches, built when first asked for
// after a change; the game's run has the messages it posts answered (and so
// the caches rebuilt), ours has not. Snapshots are compared with both built.
void BuildCaches(Snapshot& s)
{
    if (!s.hasCamera)
        return;
    CallThis<const char*>(addr::cS3DCamera_GetView, s.camera);
    CallThis<const char*>(addr::cS3DCamera_GetProjection, s.camera);
}

// Where two snapshots part ("" when they do not): the control's bytes but
// the bounds' pointers, the camera, the bounds points.
bool Differ(const Snapshot& game, const Snapshot& ours, std::string& gameText, std::string& oursText)
{
    char where[48];
    for (std::size_t at = 0; at < kSize; ++at) {
        if (at >= field::kBounds && at < field::kBounds + 12)
            continue;
        if (game.control[at] != ours.control[at]) {
            const std::size_t from = at & ~std::size_t{3};
            std::snprintf(where, sizeof where, "control+%zX ", from);
            gameText = where + Hex(game.control + from, 8);
            oursText = where + Hex(ours.control + from, 8);
            return true;
        }
    }
    if (game.hasCamera && ours.hasCamera) {
        for (std::size_t at = 0; at < kCameraSize; ++at) {
            if (game.camera[at] != ours.camera[at]) {
                const std::size_t from = at & ~std::size_t{3};
                std::snprintf(where, sizeof where, "camera+%zX ", from);
                gameText = where + Hex(game.camera + from, 8);
                oursText = where + Hex(ours.camera + from, 8);
                return true;
            }
        }
    }
    if (game.points != ours.points ||
        (game.points <= kMaxPoints && std::memcmp(game.bounds, ours.bounds, 8 * game.points) != 0)) {
        std::snprintf(where, sizeof where, "bounds %u ", game.points);
        gameText = where + Hex(game.bounds, 8 * (game.points < 2 ? game.points : 2));
        std::snprintf(where, sizeof where, "bounds %u ", ours.points);
        oursText = where + Hex(ours.bounds, 8 * (ours.points < 2 ? ours.points : 2));
        return true;
    }
    return false;
}

struct Nothing {
    bool operator==(const Nothing&) const { return true; }
};

Snapshot g_before, g_ours, g_game;

// Ours first (quiet), then the object put back and the game's; the game's
// result stands.
template <typename R, typename Ours, typename Original>
R Compared(const char* id, char* self, Ours&& ours, Original&& original)
{
    Take(self, g_before);
    ++g_depth;
    g_quiet = true;
    const R ourResult = ours();
    g_quiet = false;
    Take(self, g_ours);
    Restore(self, g_before);
    const R result = original();
    --g_depth;
    Take(self, g_game);
    BuildCaches(g_ours);
    BuildCaches(g_game);
    std::string gameText, oursText;
    bool differ = Differ(g_game, g_ours, gameText, oursText);
    if (!differ && !(ourResult == result)) {
        differ = true;
        gameText = "result " + Hex(&result, sizeof(R));
        oursText = "result " + Hex(&ourResult, sizeof(R));
    }
    compare::Record(id, !differ, gameText, oursText);
    return result;
}

// --- The detours ---------------------------------------------------------------------

using VoidFn = void(__fastcall*)(char*, void*);
using SizeFn = void(__fastcall*)(char*, void*, int32_t*, int32_t*);
using SetSizeFn = void(__fastcall*)(char*, void*, int32_t, int32_t);
using NearFarFn = void(__fastcall*)(char*, void*, float*, float*);
using StreamFn = uint32_t(__fastcall*)(char*, void*, void*);
using IntFn = int32_t(__fastcall*)(char*, void*);
using VectorFn = uint8_t(__fastcall*)(char*, void*, const float*);
using ConstructFn = char*(__fastcall*)(char*, void*);
using DestroyFn = char*(__fastcall*)(char*, void*, uint32_t);
using ModeFn = uint8_t(__fastcall*)(char*, void*, int32_t);
using ScrollFn = uint8_t(__fastcall*)(char*, void*, float, float);
using FloatFn = void(__fastcall*)(char*, void*, float);
using TwoFloatFn = void(__fastcall*)(char*, void*, float, float);
using InitFn = uint8_t(__fastcall*)(char*, void*, const float*, void*);
using ZoomFn = uint8_t(__fastcall*)(char*, void*, float);
using ZoomRotationFn = uint8_t(__fastcall*)(char*, void*, int32_t, int32_t);

VoidFn g_releaseCamera, g_postSetUp;
SizeFn g_getViewSize;
SetSizeFn g_setViewSize;
NearFarFn g_getNearFar;
StreamFn g_write, g_read;
IntFn g_effectiveZoom;
VectorFn g_setCenterOffset, g_setCenter;
ConstructFn g_construct;
DestroyFn g_destroy;
ModeFn g_update, g_setRotation;
ScrollFn g_scroll;
FloatFn g_setZoomFactor;
TwoFloatFn g_setShake, g_setCitySize;
InitFn g_init;
ZoomFn g_setZoom;
ZoomRotationFn g_setZoomAndRotation;

// The game's in compare mode for what cannot run twice (references, streams,
// the destructor), and inside a comparison.
bool Game() { return g_compare || g_depth > 0; }

void __fastcall DReleaseCamera(char* s, void*)
{
    if (Game())
        return g_releaseCamera(s, nullptr);
    ReleaseCamera(s);
}

uint32_t __fastcall DWrite(char* s, void*, void* stream)
{
    return Game() ? g_write(s, nullptr, stream) : Write(s, stream);
}

uint32_t __fastcall DRead(char* s, void*, void* stream)
{
    return Game() ? g_read(s, nullptr, stream) : Read(s, stream);
}

char* __fastcall DDestroy(char* s, void*, uint32_t flags)
{
    if (Game())
        return g_destroy(s, nullptr, flags);
    Destroy(s, flags);
    return s;
}

uint8_t __fastcall DInit(char* s, void*, const float* centre, void* camera)
{
    return Game() ? g_init(s, nullptr, centre, camera) : Init(s, centre, camera);
}

char* __fastcall DConstruct(char* s, void*)
{
    if (g_depth > 0)
        return g_construct(s, nullptr);
    if (!g_compare) {
        Construct(s);
        return s;
    }
    static char ours[kSize];
    std::memcpy(ours, s, kSize); // what the constructor leaves alone
    Construct(ours);
    char* const result = g_construct(s, nullptr);
    std::size_t at = 0;
    while (at < kSize && ours[at] == s[at])
        ++at;
    char where[32];
    std::snprintf(where, sizeof where, "@%zX ", at);
    compare::Record("cameracontrol.construct", at == kSize, at == kSize ? std::string() : where + Hex(s + at, 4),
                    at == kSize ? std::string() : where + Hex(ours + at, 4));
    return result;
}

void __fastcall DGetViewSize(char* s, void*, int32_t* width, int32_t* height)
{
    if (g_depth > 0 || !g_compare)
        return g_depth > 0 ? g_getViewSize(s, nullptr, width, height) : GetViewSize(s, width, height);
    int32_t w = 0, h = 0;
    GetViewSize(s, &w, &h);
    g_getViewSize(s, nullptr, width, height);
    compare::Record("cameracontrol.getviewsize", w == *width && h == *height, std::string(), std::string());
}

void __fastcall DGetNearFar(char* s, void*, float* nearZ, float* farZ)
{
    if (g_depth > 0 || !g_compare)
        return g_depth > 0 ? g_getNearFar(s, nullptr, nearZ, farZ) : GetNearFar(s, nearZ, farZ);
    float n = 0.0f, f = 0.0f;
    GetNearFar(s, &n, &f);
    g_getNearFar(s, nullptr, nearZ, farZ);
    compare::Record("cameracontrol.getnearfar", std::memcmp(&n, nearZ, 4) == 0 && std::memcmp(&f, farZ, 4) == 0,
                    std::string(), std::string());
}

int32_t __fastcall DEffectiveZoom(char* s, void*)
{
    if (g_depth > 0)
        return g_effectiveZoom(s, nullptr);
    if (!g_compare)
        return EffectiveZoom(s);
    const int32_t ours = EffectiveZoom(s);
    const int32_t result = g_effectiveZoom(s, nullptr);
    compare::Record("cameracontrol.effectivezoom", ours == result, std::to_string(result), std::to_string(ours));
    return result;
}

// The rest change the object: compared with Compared.
#define OPENSC4_COMPARED(R, id, ours, original)                                                                  \
    if (g_depth > 0)                                                                                             \
        return original;                                                                                         \
    if (!g_compare)                                                                                              \
        return ours;                                                                                             \
    return Compared<R>(id, s, [&] { return ours; }, [&] { return original; })

#define OPENSC4_COMPARED_VOID(id, ours, original)                                                                \
    if (g_depth > 0)                                                                                             \
        return original;                                                                                         \
    if (!g_compare)                                                                                              \
        return ours;                                                                                             \
    Compared<Nothing>(id, s, [&] { ours; return Nothing{}; }, [&] { original; return Nothing{}; })

void __fastcall DSetViewSize(char* s, void*, int32_t width, int32_t height)
{
    OPENSC4_COMPARED_VOID("cameracontrol.setviewsize", SetViewSize(s, width, height),
                          g_setViewSize(s, nullptr, width, height));
}

uint8_t __fastcall DSetCenterOffset(char* s, void*, const float* offset)
{
    OPENSC4_COMPARED(uint8_t, "cameracontrol.setcenteroffset", SetCenterOffset(s, offset),
                     g_setCenterOffset(s, nullptr, offset));
}

void __fastcall DPostSetUp(char* s, void*)
{
    OPENSC4_COMPARED_VOID("cameracontrol.postsetup", PostSetUp(s), g_postSetUp(s, nullptr));
}

uint8_t __fastcall DUpdate(char* s, void*, int32_t mode)
{
    OPENSC4_COMPARED(uint8_t, "cameracontrol.update", Update(s, mode), g_update(s, nullptr, mode));
}

uint8_t __fastcall DScroll(char* s, void*, float angle, float distance)
{
    OPENSC4_COMPARED(uint8_t, "cameracontrol.scroll", Scroll(s, angle, distance), g_scroll(s, nullptr, angle, distance));
}

uint8_t __fastcall DSetRotation(char* s, void*, int32_t rotation)
{
    OPENSC4_COMPARED(uint8_t, "cameracontrol.setrotation", SetRotation(s, rotation),
                     g_setRotation(s, nullptr, rotation));
}

void __fastcall DSetZoomFactor(char* s, void*, float factor)
{
    OPENSC4_COMPARED_VOID("cameracontrol.setzoomfactor", SetZoomFactor(s, factor),
                          g_setZoomFactor(s, nullptr, factor));
}

uint8_t __fastcall DSetCenter(char* s, void*, const float* centre)
{
    OPENSC4_COMPARED(uint8_t, "cameracontrol.setcenter", SetCenter(s, centre), g_setCenter(s, nullptr, centre));
}

void __fastcall DSetShake(char* s, void*, float x, float y)
{
    OPENSC4_COMPARED_VOID("cameracontrol.setshake", SetShake(s, x, y), g_setShake(s, nullptr, x, y));
}

void __fastcall DSetCitySize(char* s, void*, float width, float depth)
{
    OPENSC4_COMPARED_VOID("cameracontrol.setcitysize", SetCitySize(s, width, depth),
                          g_setCitySize(s, nullptr, width, depth));
}

uint8_t __fastcall DSetZoom(char* s, void*, float zoom)
{
    OPENSC4_COMPARED(uint8_t, "cameracontrol.setzoom", SetZoom(s, zoom), g_setZoom(s, nullptr, zoom));
}

uint8_t __fastcall DSetZoomAndRotation(char* s, void*, int32_t zoom, int32_t rotation)
{
    OPENSC4_COMPARED(uint8_t, "cameracontrol.setzoomandrotation", SetZoomAndRotation(s, zoom, rotation),
                     g_setZoomAndRotation(s, nullptr, zoom, rotation));
}

#undef OPENSC4_COMPARED
#undef OPENSC4_COMPARED_VOID

OPENSC4_THUNK(TReleaseCamera, DReleaseCamera, 0)
OPENSC4_THUNK(TGetViewSize, DGetViewSize, 8)
OPENSC4_THUNK(TSetViewSize, DSetViewSize, 8)
OPENSC4_THUNK(TGetNearFar, DGetNearFar, 8)
OPENSC4_THUNK(TWrite, DWrite, 4)
OPENSC4_THUNK(TRead, DRead, 4)
OPENSC4_THUNK(TEffectiveZoom, DEffectiveZoom, 0)
OPENSC4_THUNK(TSetCenterOffset, DSetCenterOffset, 4)
OPENSC4_THUNK(TPostSetUp, DPostSetUp, 0)
OPENSC4_THUNK(TConstruct, DConstruct, 0)
OPENSC4_THUNK(TDestroy, DDestroy, 4)
OPENSC4_THUNK(TUpdate, DUpdate, 4)
OPENSC4_THUNK(TScroll, DScroll, 8)
OPENSC4_THUNK(TSetRotation, DSetRotation, 4)
OPENSC4_THUNK(TSetZoomFactor, DSetZoomFactor, 4)
OPENSC4_THUNK(TSetCenter, DSetCenter, 4)
OPENSC4_THUNK(TSetShake, DSetShake, 8)
OPENSC4_THUNK(TSetCitySize, DSetCitySize, 8)
OPENSC4_THUNK(TInit, DInit, 8)
OPENSC4_THUNK(TSetZoom, DSetZoom, 4)
OPENSC4_THUNK(TSetZoomAndRotation, DSetZoomAndRotation, 8)

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
        Detour(addr::cSC4CameraControl_ReleaseCamera, &TReleaseCamera, &g_releaseCamera) &&
        Detour(addr::cSC4CameraControl_GetViewSize, &TGetViewSize, &g_getViewSize) &&
        Detour(addr::cSC4CameraControl_SetViewSize, &TSetViewSize, &g_setViewSize) &&
        Detour(addr::cSC4CameraControl_GetNearFar, &TGetNearFar, &g_getNearFar) &&
        Detour(addr::cSC4CameraControl_Write, &TWrite, &g_write) &&
        Detour(addr::cSC4CameraControl_Read, &TRead, &g_read) &&
        Detour(addr::cSC4CameraControl_EffectiveZoom, &TEffectiveZoom, &g_effectiveZoom) &&
        Detour(addr::cSC4CameraControl_SetCenterOffset, &TSetCenterOffset, &g_setCenterOffset) &&
        Detour(addr::cSC4CameraControl_PostSetUp, &TPostSetUp, &g_postSetUp) &&
        Detour(addr::cSC4CameraControl_cSC4CameraControl, &TConstruct, &g_construct) &&
        Detour(addr::cSC4CameraControl_DeletingDestructor, &TDestroy, &g_destroy) &&
        Detour(addr::cSC4CameraControl_Update, &TUpdate, &g_update) &&
        Detour(addr::cSC4CameraControl_Scroll, &TScroll, &g_scroll) &&
        Detour(addr::cSC4CameraControl_SetRotation, &TSetRotation, &g_setRotation) &&
        Detour(addr::cSC4CameraControl_SetZoomFactor, &TSetZoomFactor, &g_setZoomFactor) &&
        Detour(addr::cSC4CameraControl_SetCenter, &TSetCenter, &g_setCenter) &&
        Detour(addr::cSC4CameraControl_SetShake, &TSetShake, &g_setShake) &&
        Detour(addr::cSC4CameraControl_SetCitySize, &TSetCitySize, &g_setCitySize) &&
        Detour(addr::cSC4CameraControl_Init, &TInit, &g_init) &&
        Detour(addr::cSC4CameraControl_SetZoom, &TSetZoom, &g_setZoom) &&
        Detour(addr::cSC4CameraControl_SetZoomAndRotation, &TSetZoomAndRotation, &g_setZoomAndRotation);
    if (ok)
        log::Write("camera control: 21 functions %s", g_compare ? "compared with the game's" : "ours");
    return ok;
}

const bool g_registered = registry::Register({
    "renderer.camera.control",
    "renderer/camera",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4CameraControl_Update,
    "Our cSC4CameraControl: zoom, rotation, scrolling, the view centre and its bounds, the orthographic box, the "
    "change messages (docs/renderer.md, \"The camera control\"); compare mode runs both on every call",
    true,
    Install,
});

} // namespace

void Exercise(void* control, int step)
{
    auto* const c = static_cast<char*>(control);
    for (int k = 0; k < 8; ++k)
        CallThis<uint8_t>(addr::cSC4CameraControl_Scroll, c, 0.785398185f * k + 0.1f * step, 2.5f + 4.0f * k);
    CallThis<uint8_t>(addr::cSC4CameraControl_SetRotation, c, (step + 1) % 4);
    CallThis<uint8_t>(addr::cSC4CameraControl_SetZoom, c, 4.5f);
    CallThis<uint8_t>(addr::cSC4CameraControl_SetZoom, c, static_cast<float>(step % 5));
    CallThis<void>(addr::cSC4CameraControl_SetShake, c, 1.5f, -2.25f);
    CallThis<void>(addr::cSC4CameraControl_SetShake, c, 0.0f, 0.0f);
    const float far[3] = {9000.0f + 100.0f * step, 0.0f, -4000.0f};
    CallThis<uint8_t>(addr::cSC4CameraControl_SetCenter, c, far);
    for (int mode = 0; mode < 5; ++mode)
        CallThis<uint8_t>(addr::cSC4CameraControl_Update, c, mode);
    CallThis<void>(addr::cSC4CameraControl_SetZoomFactor, c, 1.25f);
    CallThis<void>(addr::cSC4CameraControl_SetZoomFactor, c, 1.0f);
    const float offset[3] = {0.5f, 0.0f, 0.25f};
    CallThis<uint8_t>(addr::cSC4CameraControl_SetCenterOffset, c, offset);
    CallThis<int32_t>(addr::cSC4CameraControl_EffectiveZoom, c);
    const float centre[3] = {512.0f + 64.0f * step, 0.0f, 512.0f};
    CallThis<uint8_t>(addr::cSC4CameraControl_SetCenter, c, centre);
}

} // namespace opensc4::camera_control
