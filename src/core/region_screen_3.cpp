// The region screen's windows (docs/region.md), part 3: 0x007AC830 to
// 0x007ADE50. Replacement, off by default:
//
//   region.screen.part3: the region screen's paint slot (the view glides to
//     its target and the map window and overlay follow), its left button
//     slot (picking a tile, the double click), the set-up of its 3D view
//     (tick manager, terrain, camera, camera control, renderer, effects
//     manager), going to a region by name, opening the picked city; the
//     right button slot of the scroll window; the runs of a region bitmap's
//     channel; the destructor of an import record's members; and the five
//     instances of the STL sort that carry the ordering of layout tiles.
//
// The region screen (cSC4WinRegionScreen, window base class, main vtable
// 0x00AB9260) as far as this part shows it: +0xE0 the region map window,
// +0xE4 the overlay window, +0x158 the animation tick manager, +0x15C the
// effects manager, +0x160 the S3D camera, +0x164 the camera control, +0x168
// the 3D renderer, +0x16C the region's terrain object, +0x178 and +0x17C the
// view position (floats), +0x180 its limits (left, top, right, bottom),
// +0x1A0 whether a tile is picked, +0x1A4 the region's index in the nation,
// +0x1A8 the map mode, +0x1B8 the stopwatch between paints, +0x1D0 the
// stopwatch since the last pick, +0x1E8 a count of paints after which window
// flag 0x200000 is cleared, +0x1F4 and +0x1F8 the position the view glides
// to, +0x1FC whether it is gliding.
//
// An import record (0x80 bytes, a vector of them at the region screen's
// +0x118): +0x1C to +0x30 six counted pointers, +0x38, +0x44, +0x50 and
// +0x5C vectors of dwords (the last three are the runs of the bitmap at
// +0x20, by channel), +0x70 a list of counted pointers, +0x74 a vector of
// layout tiles.
//
// A layout tile is 12 bytes: x, y, log2 of the side in km. The ordering: by
// (1 << size) + x + y, compared through the wrapped difference, then by x.
//
// Still the game's: 0x007AD2C0 and 0x007ADD80 (erase and assignment of a
// list of counted pointers), 0x007AD7A0 (assignment of a vector of 12-byte
// records, shared with the region, the regional city and two simulators).
// 0x007ACC40, 0x007AD6D0, 0x007AD720, 0x007AD800 and 0x007ADC10 are not
// functions: they are labels inside 0x007ACAD0, 0x007AD400, 0x007AD7A0 and
// 0x007ADAC0.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::region {
namespace {

namespace screen { // the region screen, by offset
constexpr std::size_t kMapWindow = 0xE0;
constexpr std::size_t kOverlay = 0xE4;
constexpr std::size_t kTicks = 0x158;
constexpr std::size_t kEffects = 0x15C;
constexpr std::size_t kCamera = 0x160;
constexpr std::size_t kControl = 0x164;
constexpr std::size_t kRender = 0x168;
constexpr std::size_t kTerrain = 0x16C;
constexpr std::size_t kViewX = 0x178;
constexpr std::size_t kViewY = 0x17C;
constexpr std::size_t kLimits = 0x180; // left, top, right, bottom
constexpr std::size_t kPicked = 0x1A0;
constexpr std::size_t kRegionIndex = 0x1A4;
constexpr std::size_t kMode = 0x1A8;
constexpr std::size_t kPaintWatch = 0x1B8;
constexpr std::size_t kPickWatch = 0x1D0;
constexpr std::size_t kFlagCountdown = 0x1E8;
constexpr std::size_t kTargetX = 0x1F4;
constexpr std::size_t kTargetY = 0x1F8;
constexpr std::size_t kGliding = 0x1FC;
} // namespace screen

namespace win { // the window base class, by vtable offset
constexpr std::size_t kAddRef = 0x4;
constexpr std::size_t kRelease = 0x8;
constexpr std::size_t kChildDelete = 0x40;  // (window)
constexpr std::size_t kChildFromId = 0x88;  // (id)
constexpr std::size_t kWidth = 0xA4;
constexpr std::size_t kHeight = 0xA8;
constexpr std::size_t kSetFlag = 0x110;     // (flag, on)
constexpr std::size_t kOnCommand = 0x23C;   // (command, data)
} // namespace win

namespace render { // cISC43DRender, by vtable offset
constexpr std::size_t kInit = 0xC;
constexpr std::size_t kSetMapScaleFactor = 0x14;
constexpr std::size_t kSetGraphicSystem = 0x18;
constexpr std::size_t kSetDriver = 0x1C;
constexpr std::size_t kViewUtilities = 0x20;
constexpr std::size_t kLightingManager = 0x24;
constexpr std::size_t kSetViewportSize = 0x30;
constexpr std::size_t kSetCamera = 0x3C;
constexpr std::size_t kSetCameraControl = 0x44;
constexpr std::size_t kAddViewObject = 0x80;          // (object, 0, 0)
constexpr std::size_t kGetShakeOffsets = 0xB4;        // (float* x, float* z)
constexpr std::size_t kTransparentBackground = 0xDC;  // (on)
} // namespace render

namespace image { // the game's image buffer, by vtable offset
constexpr std::size_t kLock = 0x18; // (flags)
constexpr std::size_t kArea = 0x30; // -> int32_t[4]: left, top, right, bottom
constexpr std::size_t kBits = 0x88;
constexpr std::size_t kStride = 0x8C;
} // namespace image

namespace record { // an import record, by offset
constexpr std::size_t kPointers = 0x1C; // six, to +0x30
constexpr std::size_t kVector38 = 0x38;
constexpr std::size_t kVector44 = 0x44;
constexpr std::size_t kVector50 = 0x50;
constexpr std::size_t kVector5C = 0x5C;
constexpr std::size_t kList = 0x70;
constexpr std::size_t kTiles = 0x74;
} // namespace record

constexpr uint32_t kTickManagerClass = 0xA9C73857;
constexpr uint32_t kTickManagerIid = 0x86AD10ED;
constexpr uint32_t kEffectsClass = 0x49822F75;
constexpr uint32_t kEffectsIid = 0xC97CF5CD;
constexpr uint32_t kCameraClass = 0xE9C6262A;
constexpr uint32_t kControlClass = 0xC9C628EC;
constexpr uint32_t kGraphicSystem2Iid = 0xC47B747C;

struct Tile {
    int32_t x;
    int32_t y;
    int32_t size;
};
static_assert(sizeof(Tile) == 0xC);

struct DwordVector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};
static_assert(sizeof(DwordVector) == 0xC);

// The game's string.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

void AddRef(void* object)
{
    Virtual<uint32_t>(object, win::kAddRef);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, win::kRelease);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// A counted pointer emptied: cleared first, then released.
void Reset(void*& slot)
{
    if (void* const old = slot) {
        slot = nullptr;
        Release(old);
    }
}

// A counted pointer assigned.
void Assign(void*& slot, void* fresh)
{
    void* const old = slot;
    if (fresh == old)
        return;
    if (fresh)
        AddRef(fresh);
    slot = fresh;
    if (old)
        Release(old);
}

void* App()
{
    return Global<void*>(addr::SC4_Service43C94);
}

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// The game's 0x007AC830, the region screen's paint slot (0x160; no
// arguments, returns 1). Counts the flag countdown down, moves the view
// position towards its target by the time since the last paint, keeps it
// inside the limits, and hands the map window and the overlay the view's
// place on screen.
uint8_t __fastcall ScreenPaint(char* self, void*)
{
    uint32_t countdown = At<uint32_t>(self, screen::kFlagCountdown);
    if (countdown != 0) {
        --countdown;
        At<uint32_t>(self, screen::kFlagCountdown) = countdown;
        if (countdown == 0)
            Virtual<void>(self, win::kSetFlag, uint32_t{0x200000}, uint32_t{0});
    }
    char* const watch = self + screen::kPaintWatch;
    if (!This<uint8_t>(addr::GZStopwatch_IsRunning, watch)) {
        This<void>(addr::SC4Stopwatch_Start, watch);
    } else {
        int32_t elapsed = This<int32_t>(addr::GZStopwatch_ElapsedMilliseconds, watch);
        This<void>(addr::GZStopwatch_Start, watch);
        if (elapsed > 200)
            elapsed = 200;
        if (At<uint8_t>(self, screen::kGliding)) {
            const float dx = static_cast<float>(static_cast<double>(At<float>(self, screen::kTargetX)) -
                                                static_cast<double>(At<float>(self, screen::kViewX)));
            const double dyWide = static_cast<double>(At<float>(self, screen::kTargetY)) -
                                  static_cast<double>(At<float>(self, screen::kViewY));
            const float dy = static_cast<float>(dyWide);
            const double distance =
                std::sqrt(dyWide * static_cast<double>(dy) + static_cast<double>(dx) * static_cast<double>(dx));
            double speed = static_cast<double>(5.0f) * distance;
            if (speed > static_cast<double>(1200.0f))
                speed = static_cast<double>(1200.0f);
            const double step = static_cast<double>(elapsed) * static_cast<double>(0.001f);
            const double factor = (step / distance) * speed;
            const double x = static_cast<double>(dx) * factor + static_cast<double>(At<float>(self, screen::kViewX));
            At<float>(self, screen::kViewX) = static_cast<float>(x);
            const double y = factor * static_cast<double>(dy) + static_cast<double>(At<float>(self, screen::kViewY));
            At<float>(self, screen::kViewY) = static_cast<float>(y);
            const double offY = std::fabs(y - static_cast<double>(At<float>(self, screen::kTargetY)));
            const double offX = std::fabs(x - static_cast<double>(At<float>(self, screen::kTargetX)));
            if (offY + offX <= static_cast<double>(2.0f))
                At<uint8_t>(self, screen::kGliding) = 0;
        }
    }

    const int32_t left = At<int32_t>(self, screen::kLimits);
    const int32_t right = At<int32_t>(self, screen::kLimits + 8);
    const int32_t top = At<int32_t>(self, screen::kLimits + 4);
    const int32_t bottom = At<int32_t>(self, screen::kLimits + 0xC);
    if (static_cast<double>(left) > static_cast<double>(At<float>(self, screen::kViewX))) {
        At<float>(self, screen::kViewX) = static_cast<float>(left);
        At<uint8_t>(self, screen::kGliding) = 0;
    } else if (static_cast<double>(right) < static_cast<double>(At<float>(self, screen::kViewX))) {
        At<float>(self, screen::kViewX) = static_cast<float>(right);
        At<uint8_t>(self, screen::kGliding) = 0;
    }
    if (static_cast<double>(top) > static_cast<double>(At<float>(self, screen::kViewY))) {
        At<float>(self, screen::kViewY) = static_cast<float>(top);
        At<uint8_t>(self, screen::kGliding) = 0;
    } else if (static_cast<double>(bottom) < static_cast<double>(At<float>(self, screen::kViewY))) {
        At<float>(self, screen::kViewY) = static_cast<float>(bottom);
        At<uint8_t>(self, screen::kGliding) = 0;
    }

    This<void>(addr::cSC4WinRegionScreen_CenterCamera, self);
    float shakeZ; // neither is read
    float shakeX;
    Virtual<uint8_t>(At<void*>(self, screen::kRender), render::kGetShakeOffsets, &shakeX, &shakeZ);

    // The camera control's target on screen, in pixels (+0x98, +0x9C),
    // rounded by adding 1.5 * 2^23 and taking the float's bits.
    const float wideX = static_cast<float>(
        static_cast<double>(At<float>(At<char*>(self, screen::kControl), 0x98)) + static_cast<double>(12582912.0f));
    const int32_t width = Virtual<int32_t>(self, win::kWidth);
    const int32_t x = static_cast<int32_t>(Bits(wideX) - static_cast<uint32_t>(width / 2) - 0x4B400000u);
    const float wideY = static_cast<float>(
        static_cast<double>(12582912.0f) - static_cast<double>(At<float>(At<char*>(self, screen::kControl), 0x9C)));
    const int32_t height = Virtual<int32_t>(self, win::kHeight);
    const int32_t y = static_cast<int32_t>(Bits(wideY) - static_cast<uint32_t>(height / 2) - 0x4B400000u);

    if (void* const map = At<void*>(self, screen::kMapWindow))
        This<void>(addr::cSC4WinRegionView_SetViewPlace, map, x, y);
    if (void* const overlay = At<void*>(self, screen::kOverlay)) {
        int32_t point[2];
        point[0] = x;
        point[1] = y;
        This<void>(addr::SC4RegionOverlayWin_SetOffset, overlay, &point[0]);
    }
    Virtual<void>(At<void*>(self, screen::kTicks), 0x24);
    return 1;
}

// The game's 0x007ACAD0, the region screen's left button slot (0x218; (x, y,
// modifiers), ret 0xC, returns 1). With a tile picked: a second click on it
// within the double click time is command 0x287259F6 (open the city); else
// the tile under the pointer is picked, and when there is none the picked
// state is dropped. Then, with a tile picked now: the click sound, the
// buttons, the map window's flags and frame colours (by whether the city is
// established), the pick stopwatch started.
uint8_t __fastcall ScreenMouseDownL(char* self, void*, int32_t x, int32_t y, uint32_t)
{
    if (At<uint8_t>(self, screen::kPicked)) {
        void* const hit = This<void*>(addr::cSC4WinRegionView_TileAt, At<void*>(self, screen::kMapWindow), x, y);
        if (hit && hit == This<void*>(addr::SC4Folded_GetFieldE4, At<void*>(self, screen::kMapWindow))) {
            void* const input = Cdecl<void*>(addr::SC4_Singleton913C72);
            const uint32_t elapsed =
                This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, static_cast<void*>(self + screen::kPickWatch));
            if (elapsed < Virtual<uint32_t>(input, 0xA0)) {
                Virtual<uint8_t>(self, win::kOnCommand, uint32_t{0x287259F6}, uint32_t{0x4A560000});
                return 1;
            }
        }
        This<void>(addr::cSC4WinRegionView_PickAt, At<void*>(self, screen::kMapWindow), x, y);
        if (!This<void*>(addr::SC4Folded_GetFieldE4, At<void*>(self, screen::kMapWindow))) {
            This<void>(addr::cSC4WinRegionScreen_ApplyMode, self);
            return 1;
        }
    }

    char* const picked = This<char*>(addr::SC4Folded_GetFieldE4, At<void*>(self, screen::kMapWindow));
    if (!picked)
        return 1;
    if (void* const sound = Global<void*>(addr::SC4_Sound43CB8))
        Virtual<void>(sound, 0x2C, uint32_t{1}, uint32_t{0x2A5C322B}, uint32_t{0}, uint32_t{0}, uint32_t{0});
    void* const nation = Virtual<void*>(App(), 0x88);
    void* const region = Virtual<void*>(nation, 0x20); // the active region
    This<void>(addr::cSC4WinRegionScreen_SelectPanel, self, int32_t{-1});
    This<void>(addr::cSC4WinRegionView_SetFlag116, At<void*>(self, screen::kMapWindow), uint32_t{1});
    This<void>(addr::cSC4WinRegionView_SetFlags111, At<void*>(self, screen::kMapWindow), uint32_t{0}, uint32_t{1});
    // The original pushes the mode with its low byte replaced by the flag.
    const uint32_t mode = At<uint32_t>(self, screen::kMode);
    const uint32_t flag = (mode & 0xFFFFFF00u) | (mode == 1 ? 1u : 0u);
    This<void>(addr::cSC4WinRegionView_SetFlags114, At<void*>(self, screen::kMapWindow), flag, uint32_t{0});
    // GetCity
    void** const holder = Virtual<void**>(region, 0x2C, At<uint32_t>(picked, 0x8), At<uint32_t>(picked, 0xC));
    uint32_t colour = 0x0A8CD184;
    if (holder && Virtual<uint8_t>(*holder, 0xAC)) // GetEstablished
        colour = 0xCA539340;
    This<void>(addr::cSC4WinRegionView_SetLabelLayout, At<void*>(self, screen::kMapWindow), colour, uint32_t{0x0A551C50});
    At<uint8_t>(self, screen::kPicked) = 1;
    This<void>(addr::GZStopwatch_Start, static_cast<void*>(self + screen::kPickWatch));
    int32_t area[4]; // filled by the map window, not read here
    This<void>(addr::cSC4WinRegionView_TileArea, At<void*>(self, screen::kMapWindow), picked, &area[0]);
    return 1;
}

// A message of the given type, with the reference the original takes.
void* NewMessage(uint32_t type)
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    if (message)
        AddRef(message);
    Virtual<void>(message, 0x14, type);
    return message;
}

// The game's 0x007ACC90 (this is the region screen, no arguments; called by
// its Init, 0x007B1900). Sets the screen's 3D view up: the animation tick
// manager, a terrain object of the region's size (16 cells per km, at most
// 32 km a side), the camera, the camera control and the renderer with its
// view object, the effects manager; fills the game's globals for them and
// posts messages 0x2B96B3EA and 0xAB96B05F.
void __fastcall SetUpView(char* self, void*)
{
    void* com = Cdecl<void*>(addr::RZGetCOM);
    void*& ticks = At<void*>(self, screen::kTicks);
    Reset(ticks);
    Virtual<uint8_t>(com, 0xC, kTickManagerClass, kTickManagerIid, &ticks);
    Virtual<uint8_t>(ticks, 0xC);

    void* const nation = Virtual<void*>(App(), 0x88);
    void* const region = Virtual<void*>(nation, 0x2C, At<uint32_t>(self, screen::kRegionIndex));
    int32_t box[4]; // left, top, right, bottom, in km
    Virtual<int32_t>(region, 0x60, &box[0]); // GetBoundingRect
    int32_t across = box[2] - box[0] + 1;
    if (across > 0x20)
        across = 0x20;
    const int32_t cellsX = static_cast<int32_t>(static_cast<uint32_t>(across) << 6) / 4;
    int32_t down = box[3] - box[1] + 1;
    if (down > 0x20)
        down = 0x20;
    const int32_t cellsY = static_cast<int32_t>(static_cast<uint32_t>(down) << 6) / 4;

    void* memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x28});
    void* const terrain = memory ? This<void*>(addr::SC4RegionTerrainMap_Construct, memory, cellsX, cellsY) : nullptr;
    Assign(At<void*>(self, screen::kTerrain), terrain);
    Virtual<uint8_t>(At<void*>(self, screen::kTerrain), 0xC);
    Global<void*>(addr::SC4_RegionTerrain) = At<void*>(self, screen::kTerrain);

    void*& camera = At<void*>(self, screen::kCamera);
    if (!camera) {
        com = Cdecl<void*>(addr::RZGetCOM);
        Reset(camera);
        if (!Virtual<uint8_t>(com, 0xC, kCameraClass, kCameraClass, &camera)) {
            memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1A8});
            void* const fresh = memory ? This<void*>(addr::cS3DCamera_cS3DCamera, memory) : nullptr;
            Assign(camera, fresh);
        }
    }
    This<uint8_t>(addr::SC4_ReturnTrue648F00, camera);

    void*& control = At<void*>(self, screen::kControl);
    if (!control) {
        com = Cdecl<void*>(addr::RZGetCOM);
        Reset(control);
        if (!Virtual<uint8_t>(com, 0xC, kControlClass, kControlClass, &control)) {
            memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x160});
            void* const fresh = memory ? This<void*>(addr::cSC4CameraControl_cSC4CameraControl, memory) : nullptr;
            Assign(control, fresh);
        }
    }
    {
        void* const target = control;
        const int32_t height = Virtual<int32_t>(self, win::kHeight);
        const int32_t width = Virtual<int32_t>(self, win::kWidth);
        This<void>(addr::cSC4CameraControl_SetViewSize, target, width, height);
    }
    {
        const float depth = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(box[3] - box[1] + 1) << 10));
        const float breadth =
            static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(box[2] - box[0] + 1) << 10));
        This<void>(addr::cSC4CameraControl_SetCitySize, control, breadth, depth);
    }
    float centre[3];
    centre[0] = 512.0f;
    centre[1] = 0.0f;
    centre[2] = 512.0f;
    This<void>(addr::cSC4CameraControl_Init, control, &centre[0], camera);

    void* graphics = nullptr;
    Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C9C), 0x0, kGraphicSystem2Iid, &graphics);

    memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x2E8});
    void* const renderer = memory ? This<void*>(addr::cSC43DRender_cSC43DRender, memory) : nullptr;
    Assign(At<void*>(self, screen::kRender), renderer);
    Virtual<uint8_t>(At<void*>(self, screen::kRender), render::kSetGraphicSystem, graphics);
    {
        void* const target = At<void*>(self, screen::kRender);
        const int32_t height = Virtual<int32_t>(self, win::kHeight);
        const int32_t width = Virtual<int32_t>(self, win::kWidth);
        Virtual<uint8_t>(target, render::kSetViewportSize, width, height);
    }
    {
        void* const target = At<void*>(self, screen::kRender);
        void* const driver = Virtual<void*>(graphics, 0xC);
        Virtual<uint8_t>(target, render::kSetDriver, driver);
    }
    Virtual<uint8_t>(At<void*>(self, screen::kRender), render::kSetCamera, At<void*>(self, screen::kCamera));
    Virtual<uint8_t>(At<void*>(self, screen::kRender), render::kSetCameraControl, control);
    Virtual<uint8_t>(At<void*>(self, screen::kRender), render::kTransparentBackground, uint32_t{1});
    {
        void* const target = At<void*>(self, screen::kRender);
        const int32_t tilesX = (cellsX + 0x3F) / 0x40;
        const int32_t tilesY = (cellsY + 0x3F) / 0x40;
        int32_t scale = tilesX < tilesY ? tilesY : tilesX;
        if (scale < 1)
            scale = 1;
        Virtual<uint8_t>(target, render::kSetMapScaleFactor, scale);
    }
    Virtual<uint8_t>(At<void*>(self, screen::kRender), render::kInit);

    memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
    void* const viewObject = memory ? This<void*>(addr::SC4RegionOverlay_Construct, memory) : nullptr;
    {
        void* const target = At<void*>(self, screen::kRender);
        if (viewObject)
            AddRef(viewObject);
        Virtual<uint8_t>(target, render::kAddViewObject, viewObject, uint32_t{0}, uint32_t{0});
        if (viewObject)
            Release(viewObject);
    }
    This<void>(addr::cSC4CameraControl_SetZoomFactor, control, 0.25f);
    Global<void*>(addr::SC4_CameraView) = Virtual<void*>(At<void*>(self, screen::kRender), render::kViewUtilities);
    Global<void*>(addr::SC4_Transform43DDC) =
        Virtual<void*>(At<void*>(self, screen::kRender), render::kLightingManager);
    Global<void*>(addr::SC4_View43DD0) = At<void*>(self, screen::kRender);
    This<void>(addr::cSC4WinRegionScreen_CenterCamera, self);

    com = Cdecl<void*>(addr::RZGetCOM);
    void*& effects = At<void*>(self, screen::kEffects);
    Reset(effects);
    Virtual<uint8_t>(com, 0xC, kEffectsClass, kEffectsIid, &effects);
    Virtual<uint8_t>(effects, 0xC);
    void* const server = Global<void*>(addr::SC4_MessageServer);
    Global<void*>(addr::SC4_Service43D1C) = effects;
    if (server) {
        void* const first = NewMessage(0x2B96B3EA);
        Virtual<void>(server, 0x10, first, uint32_t{0});
        void* const second = NewMessage(0xAB96B05F);
        Virtual<void>(server, 0x10, second, uint32_t{0});
        Release(second);
        Release(first);
    }
    if (graphics)
        Release(graphics);
}

// The ordering of layout tiles.
uint32_t Weight(const Tile& tile)
{
    return (1u << (static_cast<uint32_t>(tile.size) & 31)) + static_cast<uint32_t>(tile.y) +
           static_cast<uint32_t>(tile.x);
}

bool Less(const Tile& a, const Tile& b)
{
    const int32_t difference = static_cast<int32_t>(Weight(a) - Weight(b));
    return difference < 0 || (difference == 0 && a.x < b.x);
}

int32_t CountBetween(const Tile* first, const Tile* last)
{
    return static_cast<int32_t>(reinterpret_cast<const char*>(last) - reinterpret_cast<const char*>(first)) / 0xC;
}

// The heap helper of the same sort (0x007AC4D0, cdecl): (first, hole, count,
// value, compare).
void AdjustHeap(Tile* first, int32_t hole, int32_t count, const Tile& value, uint32_t compare)
{
    Cdecl<void>(addr::SC4RegionTileSort_AdjustHeap, first, hole, count, value.x, value.y, value.size, compare);
}

// The game's 0x007AD1F0 (cdecl: first, last, the pivot by value, compare).
// The partition step of the sort; returns where the halves meet.
Tile* __cdecl PartitionTiles(Tile* first, Tile* last, int32_t x, int32_t y, int32_t size, uint32_t)
{
    const Tile pivot{x, y, size};
    for (;;) {
        while (Less(*first, pivot))
            ++first;
        do {
            --last;
        } while (Less(pivot, *last));
        if (first >= last)
            return first;
        const Tile held = *first;
        *first = *last;
        *last = held;
        ++first;
    }
}

// The game's 0x007AD310 (cdecl: first, last, compare, and two zeros that are
// not read). Makes a heap of the tiles.
void __cdecl MakeTileHeap(Tile* first, Tile* last, uint32_t compare, uint32_t, uint32_t)
{
    const int32_t count = CountBetween(first, last);
    if (count < 2)
        return;
    int32_t parent = (count - 2) / 2;
    Tile* at = first + parent;
    AdjustHeap(first, parent, count, *at, compare);
    while (parent != 0) {
        --at;
        --parent;
        AdjustHeap(first, parent, count, *at, compare);
    }
}

// The game's 0x007AD8D0 (cdecl: first, last, the value by value, compare).
// Puts the value in its place among the sorted tiles before last: at the
// front when it is less than the first (the rest moved up by one), else by
// the unguarded search 0x007AA9C0.
void __cdecl LinearInsertTile(Tile* first, Tile* last, int32_t x, int32_t y, int32_t size, uint32_t compare)
{
    const Tile value{x, y, size};
    if (Less(value, *first)) {
        // The original passes the address of its first argument's slot where
        // the copy takes an unused pointer.
        Cdecl<Tile*>(addr::SC4Vector12_CopyBackward, first, last, last + 1, &first, uint32_t{0});
        first->x = x;
        first->y = y;
        first->size = size;
    } else {
        Cdecl<void>(addr::SC4RegionTileSort_InsertBackward, last, x, y, size, compare);
    }
}

// The game's 0x007AD960 (cdecl: first, last, compare). Sorts a heap of
// tiles.
void __cdecl SortTileHeap(Tile* first, Tile* last, uint32_t compare)
{
    int32_t bytes = static_cast<int32_t>(reinterpret_cast<char*>(last) - reinterpret_cast<char*>(first));
    if (bytes / 0xC <= 1)
        return;
    do {
        Tile* const back = reinterpret_cast<Tile*>(reinterpret_cast<char*>(first) + bytes - 0xC);
        const Tile value = *back;
        *back = *first;
        bytes -= 0xC;
        AdjustHeap(first, 0, bytes / 0xC, value, compare);
    } while (bytes / 0xC > 1);
}

// The game's 0x007ADD30 (cdecl: first, last, compare). Insertion sort.
void __cdecl InsertionSortTiles(Tile* first, Tile* last, uint32_t compare)
{
    if (first == last)
        return;
    Tile* at = first + 1;
    if (at == last)
        return;
    do {
        LinearInsertTile(first, at, at->x, at->y, at->size, compare);
        ++at;
    } while (at != last);
}

// The game's 0x007AD3B0, the right button slot (0x21C; (x, y, modifiers),
// ret 0xC, returns 1) of the scroll window (main vtable 0x00AB8CD0,
// constructor 0x007AAE10): deletes its child 0x48E945B4, clears window flag
// 0x200000, calls its window manager's slot 0x74 with itself and starts the
// drag at the point (0x007AC620).
uint8_t __fastcall ScrollMouseDownR(char* self, void*, int32_t x, int32_t y, uint32_t)
{
    if (void* const child = Virtual<void*>(self, win::kChildFromId, uint32_t{0x48E945B4}))
        Virtual<uint8_t>(self, win::kChildDelete, child);
    Virtual<void>(self, win::kSetFlag, uint32_t{0x200000}, uint32_t{0});
    Virtual<void>(At<void*>(self, 0x4), 0x74, static_cast<void*>(self));
    This<void>(addr::SC4RegionScroller_PlaceMarker, self, x, y);
    return 1;
}

// One dword at the end of a vector, through the game's insert when it is
// full. `unused` is what the original passes where the insert takes an
// allocator: the address of its own first argument's slot.
void PushBack(DwordVector* vector, uint32_t value, void* unused)
{
    uint32_t* const end = vector->end;
    if (end != vector->capacity) {
        if (end)
            *end = value;
        ++vector->end;
        return;
    }
    This<void>(addr::SC4PtrVector_InsertN, vector, end, &value, unused, uint32_t{1}, uint32_t{1});
}

// The same with the growth written out, as the original has it for the
// values of a run.
void PushBackGrowing(DwordVector* vector, uint32_t value)
{
    uint32_t* const end = vector->end;
    if (end != vector->capacity) {
        if (end)
            *end = value;
        ++vector->end;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<char*>(end) - reinterpret_cast<char*>(vector->begin)) >> 2);
    const uint32_t capacity = (size < 1 ? 1u : size) + size;
    uint32_t* const fresh = capacity != 0 ? Cdecl<uint32_t*>(addr::SC4_stl_allocate, capacity * 4) : nullptr;
    uint32_t* at;
    if (end == vector->begin) {
        at = fresh;
    } else {
        void* const old = vector->begin;
        const uint32_t bytes = static_cast<uint32_t>(reinterpret_cast<char*>(end) - static_cast<char*>(old));
        at = reinterpret_cast<uint32_t*>(
            Cdecl<char*>(addr::SC4_memmove, static_cast<void*>(fresh), old, bytes) + bytes);
    }
    *at = value;
    if (vector->begin)
        Free(vector->begin);
    vector->begin = fresh;
    vector->end = at + 1;
    vector->capacity = fresh + capacity;
}

// The game's 0x007AD400 (the vector in ESI; on the stack the image, a shift
// and a byte, cleaned by the caller). Empties the vector and fills it with
// the runs of the image's pixels whose byte at `shift` is not 0, row by row:
// for each run the place of its first pixel (y * 0x10000 + x), the place
// after its last, and, when `values` is set, the byte of every pixel of the
// run. The image is locked (flag 0x800) and not unlocked.
void __cdecl CollectRuns(DwordVector* runs, void* image, uint32_t shift, uint32_t values)
{
    runs->end = runs->begin;
    if (!Virtual<uint8_t>(image, image::kLock, uint32_t{0x800}))
        return;
    const int32_t* const area = Virtual<const int32_t*>(image, image::kArea);
    const int32_t height = area[3] - area[1];
    const int32_t width = area[2] - area[0];
    if (height <= 0)
        return;
    const uint32_t bit = shift & 31;
    const bool withValues = static_cast<uint8_t>(values) != 0;
    uint32_t base = 0;
    int32_t start = 0; // set when a run begins
    int32_t y = 0;
    do {
        const int32_t stride = Virtual<int32_t>(image, image::kStride);
        const uint32_t* const row = reinterpret_cast<const uint32_t*>(
            static_cast<std::uintptr_t>(stride * y + Virtual<int32_t>(image, image::kBits)));
        bool inRun = false;
        int32_t x = 0;
        if (width > 0) {
            do {
                if (static_cast<uint8_t>(row[x] >> bit) != 0) {
                    if (!inRun) {
                        PushBack(runs, base + static_cast<uint32_t>(x), &image);
                        start = x;
                    }
                    inRun = true;
                } else {
                    if (inRun) {
                        PushBack(runs, base + static_cast<uint32_t>(x), &image);
                        if (withValues) {
                            while (start < x) {
                                const uint32_t value = (row[start] >> bit) & 0xFF;
                                ++start;
                                PushBackGrowing(runs, value);
                            }
                        }
                    }
                    inRun = false;
                }
                ++x;
            } while (x < width);
            if (inRun) {
                PushBack(runs, base + static_cast<uint32_t>(x), &image);
                if (withValues) {
                    while (start < x) {
                        const uint32_t value = (row[start] >> bit) & 0xFF;
                        ++start;
                        PushBackGrowing(runs, value);
                    }
                }
            }
        }
        ++y;
        base += 0x10000;
    } while (y < height);
}

__declspec(naked) void CollectRunsThunk()
{
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm push esi
    __asm call CollectRuns
    __asm add esp, 16
    __asm ret
}

// The game's 0x007ADA00 (this is an import record, no arguments): destroys
// the record's members, last to first.
void __fastcall DestroyRecordMembers(char* self, void*)
{
    if (void* const tiles = At<void*>(self, record::kTiles))
        Free(tiles);
    This<void>(addr::SC4RefList_Clear, static_cast<void*>(self + record::kList));
    if (void* const node = At<void*>(self, record::kList))
        Free(node);
    if (void* const memory = At<void*>(self, record::kVector5C))
        Free(memory);
    if (void* const memory = At<void*>(self, record::kVector50))
        Free(memory);
    if (void* const memory = At<void*>(self, record::kVector44))
        Free(memory);
    if (void* const memory = At<void*>(self, record::kVector38))
        Free(memory);
    for (std::size_t offset = record::kPointers + 0x14;; offset -= 4) {
        if (void* const object = At<void*>(self, offset))
            Release(object);
        if (offset == record::kPointers)
            break;
    }
}

// The game's 0x007ADAC0 (this is the region screen; (const SC4String* name),
// ret 4). Looks the nation's regions up by name. The region found is made
// the active one and the application asked to go to the region view (its
// answer is returned), unless it is the screen's own region (1). 0 when no
// region has the name.
uint8_t __fastcall GoToRegionNamed(char* self, void*, char* name)
{
    void* const nation = Virtual<void*>(App(), 0x88);
    const uint32_t count = Virtual<uint32_t>(nation, 0x18);
    if (count == 0)
        return 0;
    uint32_t index = 0;
    for (;;) {
        void* const region = Virtual<void*>(nation, 0x2C, index);
        void* const text = Virtual<void*>(region, 0xC); // GetName
        const uint32_t length = Virtual<uint32_t>(text, 0x1C);
        const char* const data = Virtual<const char*>(text, 0x18);
        const char* const dataEnd = data + length;
        char* buffer = nullptr;
        if (length + 1 != 0)
            buffer = Cdecl<char*>(addr::SC4_stl_allocate, length + 1);
        else
            Cdecl<void>(addr::STL_ThrowLengthError, "basic_string");
        char* tail;
        if (dataEnd == data)
            tail = buffer;
        else
            tail = Cdecl<char*>(addr::SC4_memmove, static_cast<void*>(buffer), static_cast<const void*>(data), length) +
                   length;
        *tail = 0;
        // A null name is read at address 0 by the original as well.
        char** const body = reinterpret_cast<char**>(name ? name + 4 : nullptr);
        const char* const wanted = body[0];
        const uint32_t wantedLength = static_cast<uint32_t>(body[1] - wanted);
        const uint32_t copied = static_cast<uint32_t>(tail - buffer);
        bool same = wantedLength == copied;
        for (uint32_t i = 0; same && i < copied; ++i)
            same = wanted[i] == buffer[i];
        if (same) {
            if (index == At<uint32_t>(self, screen::kRegionIndex)) {
                if (buffer)
                    Free(buffer);
                return 1;
            }
            Virtual<void>(nation, 0x24, index);
            const uint8_t result = Virtual<uint8_t>(App(), 0x30, uint32_t{0}); // RequestGoToRegionView
            if (buffer)
                Free(buffer);
            return result;
        }
        if (buffer)
            Free(buffer);
        ++index;
        if (index >= count)
            return 0;
    }
}

// The game's 0x007ADC20 (this is the region screen; (holder of a regional
// city), ret 4). Opens the city: with a save file path the application loads
// it (slot 0x34); without one, or when loading fails, the new city
// specification's pointer at +0x200 is dropped and the application asked for
// a new city on the tile (slot 0x18). Returns the application's answer.
uint8_t __fastcall OpenCity(char* self, void*, void** holder)
{
    Virtual<uint32_t>(self, win::kAddRef);
    This<void>(addr::cSC4WinRegionScreen_SaveViewPosition, self);
    void* const app = App();
    Virtual<void*>(Virtual<void*>(app, 0x88), 0x20);
    void* const city = *holder;
    if (city)
        AddRef(city);

    GameString path;
    path.begin = nullptr;
    path.end = nullptr;
    path.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    path.begin = buffer;
    path.end = buffer;
    path.capacity = buffer + 8;
    *buffer = 0;
    path.vtable = addr::SC4String_Vtable;
    path.zero = 0;
    Virtual<void>(city, 0x7C, &path); // GetCitySaveFilePath

    uint8_t result;
    if (path.begin != path.end && Virtual<uint8_t>(app, 0x34, &path, holder)) {
        result = 1;
    } else {
        char* const specification = Virtual<char*>(app, 0x9C);
        Reset(At<void*>(specification, 0x200));
        result = Virtual<uint8_t>(app, 0x18, holder);
    }
    Virtual<uint32_t>(self, win::kRelease);
    path.vtable = addr::SC4String_Vtable;
    if (path.begin)
        Free(path.begin);
    Release(city);
    return result;
}

bool InstallRegionScreen3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4WinRegionScreen_Paint, reinterpret_cast<void*>(&ScreenPaint)},
        {addr::cSC4WinRegionScreen_OnMouseDownL, reinterpret_cast<void*>(&ScreenMouseDownL)},
        {addr::cSC4WinRegionScreen_SetUpView, reinterpret_cast<void*>(&SetUpView)},
        {addr::SC4RegionTileSort_Partition, reinterpret_cast<void*>(&PartitionTiles)},
        {addr::SC4RegionTileSort_MakeHeap, reinterpret_cast<void*>(&MakeTileHeap)},
        {addr::SC4RegionScroller_OnMouseDownR, reinterpret_cast<void*>(&ScrollMouseDownR)},
        {addr::SC4RegionPictures_CollectRuns, reinterpret_cast<void*>(&CollectRunsThunk)},
        {addr::SC4RegionTileSort_LinearInsert, reinterpret_cast<void*>(&LinearInsertTile)},
        {addr::SC4RegionTileSort_SortHeap, reinterpret_cast<void*>(&SortTileHeap)},
        {addr::SC4RegionTileRecord_Destruct, reinterpret_cast<void*>(&DestroyRecordMembers)},
        {addr::cSC4WinRegionScreen_GoToRegionNamed, reinterpret_cast<void*>(&GoToRegionNamed)},
        {addr::cSC4WinRegionScreen_OpenCity, reinterpret_cast<void*>(&OpenCity)},
        {addr::SC4RegionTileSort_InsertionSort, reinterpret_cast<void*>(&InsertionSortTiles)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_regionScreen3Registered = registry::Register({
    "region.screen.part3",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WinRegionScreen_SetUpView,
    "Our region screen, part 3: paint and left button slots, the 3D view's set-up, region by name, opening a "
    "city, the bitmap's runs, the tile sort",
    false,
    InstallRegionScreen3,
});

} // namespace
} // namespace opensc4::region
