// The terraform tool of the city view (0x004D8AC0 to 0x004D9E50).
// Replacement, off by default:
//
//   ui.tools.brush: the view input control of 0x130 bytes (vtable 0x00A92A10,
//     message target 0x00A92A00 at +0x28, ID 0x68E29401) the view makes for
//     its terrain brush commands (0x007E5910) with a brush's exemplar ID and
//     whether the brush is free: 1 for the god mode menus (command
//     0x4A2B2462 and the layout button 0x899231E6), 0 for the mayor's
//     (command 0x8A313C52 and the others).
//
// The tool scrubs a terrain brush over the ground while the left button is
// held. A press takes the ground under the pointer (PickTerrain) and its
// altitude, begins the scrub (the terrain's slot 59), captures the mouse and
// asks the framework's frame service (0x86AD10EC, under 0xC0) for its
// message. Every pointer move while the button is held queues the stretch
// from the last point to the new one and applies it (the terrain's
// ApplyBrush, slot 57, with the tool's parameter block at +0x74); every
// third frame message without a move queues the last point again, so a
// brush held still keeps working. The release applies what is queued and
// ends the scrub (slot 60).
//
// Real time. The strength of every application after the first of a scrub
// is the brush's strength times the time since the application before it:
// microseconds of the stopwatch at +0xC0 (mode 3) times 1e-6 times the rate
// (the tuning value 0x69DB8D05 inverted, 10 a second as shipped), at most 2.
// So what a scrub does to the ground depends on how fast the frames and the
// pointer moves come, in the game as here. The stopwatch is read from this
// code, which is not one of sim_timers' ranges: it stays on the clock.
//
// Money. A brush that is not free costs, at every application, the brush's
// cost (slot 62) times the strength of that application, rounded; the
// application is skipped when the budget refuses the withdrawal. The cost
// indicator shows the sum of the scrub and the last cost.
//
// Keys, when the tool is the view's current one: with the modifier bit 0,
// F1 to F10 set the strength to 0.1 to 1.0 and the digits 1 to 9 and 0 the
// radius scaling to 0.1 to 1.0; the keys 0x10 and 0x11 going down or up set
// the two from the modifier bits (bit 1: the tuning values 0x69DB8D01 and
// 02; bit 0: 03 and 04). With Caps Lock on, a scrub moves on the level plane
// of the altitude it began at instead of on the ground.
//
// The pointer's looks come from the brush (slot 63, ten IDs): two visual
// effects and a cursor for a scrub, two effects for the idle tool and its
// cursor. UpdateCursor (0x004D9160) places the effects (the first scaled
// with the brush's radius, the second with the zoom) and sets the cursor;
// everything it draws is a call on an effect, so it is ours too.
//
// Left the game's: 0x004D8EF0, a power with a whole exponent that only
// UpdateCursor calls (for the zoom scaling; called at its address), and the
// shared slots, which are the base's (tool_base.cpp).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::tool_brush {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kInited = 0x04; // byte
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kCursor = 0x10; // cursor ID
constexpr std::size_t kView = 0x1C;   // the city view
constexpr std::size_t kSaved = 0x24;  // byte: the terrain view's cell cursor state before the scrub
} // namespace base

// The tool's fields.
namespace field {
constexpr std::size_t kTarget = 0x28;         // the message target part (vtable 0x00A92A00)
constexpr std::size_t kBrush = 0x2C;          // the brush's exemplar ID (the constructor's first argument)
constexpr std::size_t kElements = 0x30;       // ten IDs from the terrain's GetUIElementsForBrush
constexpr std::size_t kFrames = 0x58;         // frame messages since the last application
constexpr std::size_t kTerrain = 0x5C;        // the terrain (referenced)
constexpr std::size_t kTerrainView = 0x60;    // the terrain view (terrain slot 66, referenced)
constexpr std::size_t kScrubbing = 0x64;      // byte: the button is down
constexpr std::size_t kInside = 0x65;         // byte: scrubbing and the pointer has not left the view
constexpr std::size_t kLast = 0x68;           // two floats: the scrub's last point (x, z)
constexpr std::size_t kWinManager = 0x70;     // the window manager (referenced)
constexpr std::size_t kParameters = 0x74;     // tBrushParameters, 0x38 bytes
constexpr std::size_t kMoved = 0xAC;          // byte: a move was applied since the last message
constexpr std::size_t kApplications = 0xB0;   // applications since the texture updates were let out
constexpr std::size_t kFree = 0xB4;           // byte: the brush costs nothing (the second argument)
constexpr std::size_t kSpent = 0xB8;          // int64: the sum of this scrub
constexpr std::size_t kPaceWatch = 0xC0;      // a stopwatch of 0x18 bytes, mode 3: between applications
constexpr std::size_t kFirst = 0xD8;          // byte: the scrub has not applied anything yet
constexpr std::size_t kUnusedOne = 0xDC;      // float 1.0, read nowhere
constexpr std::size_t kStrength = 0xE0;       // float: the strength the keys chose
constexpr std::size_t kRadius = 0xE4;         // float: the radius scaling the keys chose
constexpr std::size_t kEffect = 0xE8;         // the first effect (referenced)
constexpr std::size_t kZoomEffect = 0xEC;     // the second effect (referenced)
constexpr std::size_t kPoint = 0xF0;          // three floats: where the pointer's looks were last put
constexpr std::size_t kShownCursor = 0xFC;    // the cursor while the looks are shown
constexpr std::size_t kHiddenCursor = 0x100;  // the cursor while they are not
constexpr std::size_t kQueueWatch = 0x108;    // a stopwatch of 0x18 bytes, mode 4: the queue's times
constexpr std::size_t kQueueDelay = 0x120;    // added to a queued stretch's time; always 0
constexpr std::size_t kPressAltitude = 0x124; // float: the ground's altitude where the button went down
constexpr std::size_t kQueue = 0x128;         // the list of queued stretches (its sentinel)
} // namespace field

// tBrushParameters (0x38 bytes; gzcom-dll cISTETerrain.h) as the tool fills it.
namespace parameters {
constexpr std::size_t kFrom = 0x00;        // four floats: the stretch's first point (x, z) and its last
constexpr std::size_t kZoom = 0x10;        // -1
constexpr std::size_t kRedisplay = 0x14;   // byte: 1
constexpr std::size_t kStrength = 0x18;    // float
constexpr std::size_t kRadius = 0x1C;      // float: the radius scaling
constexpr std::size_t kMessageData = 0x20; // 3 until a scrub: then 1 for a free brush, 2 for one that costs
constexpr std::size_t kAltitude = 0x34;    // float: 100 until a scrub, then the pressed point's y
} // namespace parameters

// A node of the queue, 0x1C bytes.
namespace node {
constexpr std::size_t kNext = 0x00;
constexpr std::size_t kPrevious = 0x04;
constexpr std::size_t kTime = 0x08; // milliseconds of the stopwatch at +0x108
constexpr std::size_t kFrom = 0x0C; // four floats: the stretch's two points
constexpr std::size_t kSize = 0x1C;
} // namespace node

// The IDs at +0x30.
namespace element {
constexpr std::size_t kScrubEffect = 0;     // two IDs
constexpr std::size_t kIdleEffect = 2;      // two IDs
constexpr std::size_t kScrubZoomEffect = 4; // two IDs
constexpr std::size_t kIdleZoomEffect = 6;  // two IDs
constexpr std::size_t kScrubCursor = 8;
constexpr std::size_t kIdleCursor = 9;
} // namespace element

constexpr uint32_t kToolId = 0x68E29401;
constexpr uint32_t kMessageFrame = 0x86AD10EE;    // the frame service's
constexpr uint32_t kMessageViewA = 0xA6B79602;    // the view changed
constexpr uint32_t kMessageViewB = 0xA6B79612;
constexpr uint32_t kFrameCode = 0xC0;             // what the tool asks the frame service under
constexpr uint32_t kKeyCapsLock = 0x14;

// What an effect is told where it is: 0x38 bytes on the stack of 0x004D9160
// (its 0x38 bytes of locals). A count of changes, flags (1 the position,
// 2 a scale other than 1), a rotation, the position, the scale.
struct EffectTransform {
    uint8_t changes;
    uint8_t flags;
    uint8_t unused[2];
    float rotation[9];
    float position[3];
    float scale;
};
static_assert(sizeof(EffectTransform) == 0x38);

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
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// The transform as both halves of 0x004D9160 begin it.
void Identity(EffectTransform& transform)
{
    std::memset(&transform, 0, sizeof(transform));
    transform.rotation[0] = 1.0f;
    transform.rotation[4] = 1.0f;
    transform.rotation[8] = 1.0f;
    transform.scale = 1.0f;
}

// The scale and the position into a transform: the scale flag is set when
// the unrounded scale is not 1.
void Place(EffectTransform& transform, double scale, const float* point)
{
    transform.scale = static_cast<float>(scale);
    if (scale == 1.0)
        transform.flags &= 0xFD;
    else
        transform.flags |= 0x02;
    std::memcpy(&transform.position[0], point, sizeof(transform.position));
    transform.flags |= 0x01;
    transform.changes = static_cast<uint8_t>(transform.changes + 2);
}

void __fastcall UpdateCursor(char* self, void*, const float* point, uint8_t show);

// --- Strength and radius ---------------------------------------------------------------

// The game's 0x004D8AC0 (ret 4): the strength and the radius scaling of the
// parameter block are the keys' choice, or the tuning's pair for modifier
// bit 1, or else its pair for bit 0.
void __fastcall ApplyModifiers(char* self, void*, uint32_t modifiers)
{
    char* const block = self + field::kParameters;
    At<uint32_t>(block, parameters::kStrength) = At<uint32_t>(self, field::kStrength);
    At<uint32_t>(block, parameters::kRadius) = At<uint32_t>(self, field::kRadius);
    if ((modifiers >> 1) & 1) {
        At<uint32_t>(block, parameters::kStrength) = Global<uint32_t>(addr::SC4TerraformTool_Bit1Strength);
        At<uint32_t>(block, parameters::kRadius) = Global<uint32_t>(addr::SC4TerraformTool_Bit1Radius);
    } else if (modifiers & 1) {
        At<uint32_t>(block, parameters::kStrength) = Global<uint32_t>(addr::SC4TerraformTool_Bit0Strength);
        At<uint32_t>(block, parameters::kRadius) = Global<uint32_t>(addr::SC4TerraformTool_Bit0Radius);
    }
}

// The hook of 0x004D8AC0 keeps ECX as the game's function does: the game's
// two key handlers use it after the call (0x004D96C1, 0x004D9702).
__declspec(naked) void ApplyModifiersThunk()
{
    __asm push ecx
    __asm push dword ptr [esp + 8]
    __asm call ApplyModifiers
    __asm pop ecx
    __asm ret 4
}

// The game's 0x004D8B20: one more application. Past the tuning's count
// (0x69DB8D00, 100 as shipped) the terrain lets its gathered texture updates
// out (slot 79) and, the count not being 0, gathers again (slot 78).
void __fastcall CountApplication(char* self, void*)
{
    At<uint32_t>(self, field::kFrames) = 0;
    const uint32_t count = ++At<uint32_t>(self, field::kApplications);
    if (count <= Global<uint32_t>(addr::SC4TerraformTool_UpdatesPerFlush))
        return;
    Virtual<void>(At<void*>(self, field::kTerrain), 0x13C);
    At<uint32_t>(self, field::kApplications) = 0;
    if (Global<uint32_t>(addr::SC4TerraformTool_UpdatesPerFlush) > 0)
        Virtual<void>(At<void*>(self, field::kTerrain), 0x138);
}

// --- Tuning ----------------------------------------------------------------------------

// The game's 0x004D8B70 (cdecl): what the application calls back (its slot
// 69): the tuning is to be loaded again by the next Init. True.
uint8_t __cdecl TuningReset()
{
    if (Global<uint8_t>(addr::SC4TerraformTool_TuningLoaded))
        Global<uint8_t>(addr::SC4TerraformTool_TuningLoaded) = 0;
    return 1;
}

// The game's 0x004D8BC0 (cdecl): the tuning exemplar (group 0x6534284A,
// instance 0x88CD66E9 and 1): the count of applications between texture
// updates, the two pairs of strength and radius scaling of the modifiers,
// and the seconds whose inverse is the strength's rate (only when above 0).
void __cdecl LoadTuning()
{
    float seconds = 0.0f;
    void* exemplar = nullptr;
    uint32_t key[3] = {0x6534284A, 0x88CD66E9, 1};
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, &key[0], uint32_t{0xA52160F5}, &exemplar,
                          uint32_t{0}, uint32_t{0}))
        return;
    void* const properties = Virtual<void*>(exemplar, 0x1C);
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, properties, uint32_t{0x69DB8D00},
                   &Global<uint32_t>(addr::SC4TerraformTool_UpdatesPerFlush));
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, properties, uint32_t{0x69DB8D01},
                   &Global<float>(addr::SC4TerraformTool_Bit1Strength));
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, properties, uint32_t{0x69DB8D02},
                   &Global<float>(addr::SC4TerraformTool_Bit1Radius));
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, properties, uint32_t{0x69DB8D03},
                   &Global<float>(addr::SC4TerraformTool_Bit0Strength));
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, properties, uint32_t{0x69DB8D04},
                   &Global<float>(addr::SC4TerraformTool_Bit0Radius));
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, properties, uint32_t{0x69DB8D05}, &seconds);
    if (seconds > 0.0f)
        Global<float>(addr::SC4TerraformTool_StrengthRate) = static_cast<float>(1.0 / static_cast<double>(seconds));
    Release(exemplar);
}

// --- Picking ---------------------------------------------------------------------------

// The game's 0x004D8CB0 (ret 8): the altitude of the ground under a screen
// point (PickTerrain with the flag 1, then the terrain's GetAltitude at the
// picked x and z) into +0x124. False when nothing is picked.
uint8_t __fastcall PressAltitude(char* self, void*, int32_t x, int32_t z)
{
    float picked[3];
    if (!Virtual<uint8_t>(At<void*>(self, base::kView), 0x90, x, z, &picked[0], uint32_t{1}))
        return 0;
    At<float>(self, field::kPressAltitude) =
        static_cast<float>(Virtual<double>(Global<void*>(addr::SC4_Terrain), 0x34, picked[0], picked[2]));
    return 1;
}

// The game's 0x004D8D10 (ret 0xC): the world point under a screen point.
// The ground (PickTerrain with the flag 0), true; unless a scrub is on and
// the keyboard part of the view's window manager (the window's slot 6, the
// manager's slot 38) says Caps Lock (its slot 1 with 0x14 and 0). Then, and
// when no ground is picked (false), the point is where the camera's ray
// through the screen point meets the level plane of the pressed altitude
// (the ray's origin lowered by it, 0x007F6AB0), put through the camera's
// 0x007F7710.
uint8_t __fastcall CursorPoint(char* self, void*, int32_t x, int32_t z, float* point)
{
    uint8_t picked = 1;
    bool level = false;
    if (At<uint8_t>(self, field::kScrubbing)) {
        void* const window = Virtual<void*>(At<void*>(self, base::kView), 0xC);
        void* const manager = Virtual<void*>(window, 0x18);
        void* const keyboard = Virtual<void*>(manager, 0x98);
        level = Virtual<uint8_t>(keyboard, 0x4, kKeyCapsLock, uint32_t{0}) != 0;
    }
    if (!level) {
        if (Virtual<uint8_t>(At<void*>(self, base::kView), 0x90, x, z, point, uint32_t{0}))
            return 1;
        picked = 0;
    }
    float origin[3];
    float direction[3];
    void* const camera = Global<void*>(addr::SC4_CameraView);
    This<uint8_t>(addr::SC4CameraView_ScreenRay, camera, x, z, &origin[0], &direction[0]);
    origin[1] = static_cast<float>(static_cast<double>(origin[1]) -
                                   static_cast<double>(At<float>(self, field::kPressAltitude)));
    Cdecl<void>(addr::SC4CameraView_RayToLevel, &origin[0], &direction[0], point);
    This<void>(addr::SC4CameraView_F7F7710, camera, point);
    return picked;
}

// --- The pointer's looks ---------------------------------------------------------------

// An effect of the tool hidden (slot 4 with `how` when it is shown) and let
// go.
void DropEffect(char* self, std::size_t offset, uint32_t how)
{
    void* const effect = At<void*>(self, offset);
    if (!effect)
        return;
    if (Virtual<uint8_t>(effect, 0x14))
        Virtual<void>(At<void*>(self, offset), 0x10, how);
    if (void* const held = At<void*>(self, offset)) {
        At<void*>(self, offset) = nullptr;
        Release(held);
    }
}

// The game's 0x004D8F50 (ret 4): the looks for a scrub or for the idle
// tool. The cursor ID is kept at +0xFC, both effects are dropped and, when
// the preferences' byte +0x23C is set, made anew by the effects manager
// (0x00B43D1C, slot 6) from the brush's IDs.
void __fastcall SetLooks(char* self, void*, uint8_t scrubbing)
{
    const uint32_t* const ids = &At<uint32_t>(self, field::kElements);
    const std::size_t first = scrubbing ? element::kScrubEffect : element::kIdleEffect;
    const std::size_t second = scrubbing ? element::kScrubZoomEffect : element::kIdleZoomEffect;
    const uint32_t effectA = ids[first], effectB = ids[first + 1];
    const uint32_t zoomA = ids[second], zoomB = ids[second + 1];
    At<uint32_t>(self, field::kShownCursor) = ids[scrubbing ? element::kScrubCursor : element::kIdleCursor];
    DropEffect(self, field::kEffect, 0);
    DropEffect(self, field::kZoomEffect, 1);
    if (!At<uint8_t>(At<char*>(Global<char*>(addr::SC4_Preferences43CC4), 0xC), 0x23C))
        return;
    void* effects = Global<void*>(addr::SC4_Service43D1C);
    if (void* const held = At<void*>(self, field::kEffect)) {
        At<void*>(self, field::kEffect) = nullptr;
        Release(held);
    }
    Virtual<uint8_t>(effects, 0x18, effectA, effectB, &At<void*>(self, field::kEffect));
    effects = Global<void*>(addr::SC4_Service43D1C);
    if (void* const held = At<void*>(self, field::kZoomEffect)) {
        At<void*>(self, field::kZoomEffect) = nullptr;
        Release(held);
    }
    Virtual<uint8_t>(effects, 0x18, zoomA, zoomB, &At<void*>(self, field::kZoomEffect));
}

// The game's 0x004D9160 (ret 8): the pointer's looks at a world point,
// shown or hidden. The point is kept at +0xF0. The first effect gets the
// point and the brush's radius (slot 61) times the radius scaling as its
// scale (slot 8), the second the point and 3 over 1.7 to the camera's zoom
// (slot 7); each is started (slot 3 with 0) or stopped (slot 4, with 0 and
// 1) when its state (slot 5) is not the one asked for. Then the cursor: the
// one at +0xFC when shown, the one at +0x100 when not.
void __fastcall UpdateCursor(char* self, void*, const float* point, uint8_t show)
{
    std::memmove(&At<float>(self, field::kPoint), point, 3 * sizeof(float));
    if (At<void*>(self, field::kEffect)) {
        EffectTransform transform;
        Identity(transform);
        const double radius =
            Virtual<double>(At<void*>(self, field::kTerrain), 0xF4, At<uint32_t>(self, field::kBrush));
        Place(transform, radius * static_cast<double>(At<float>(self, field::kParameters + parameters::kRadius)),
              point);
        Virtual<void>(At<void*>(self, field::kEffect), 0x20, &transform);
        const uint8_t shown = Virtual<uint8_t>(At<void*>(self, field::kEffect), 0x14);
        if (show) {
            if (!shown)
                Virtual<void>(At<void*>(self, field::kEffect), 0xC, uint32_t{0});
        } else if (shown) {
            Virtual<void>(At<void*>(self, field::kEffect), 0x10, uint32_t{0});
        }
    }
    if (At<void*>(self, field::kZoomEffect)) {
        EffectTransform transform;
        Identity(transform);
        int32_t zoom = 0;
        int32_t rotation = 0;
        This<void>(addr::SC4CameraView_ZoomAndRotation, Global<void*>(addr::SC4_CameraView), &zoom, &rotation);
        // 1.7 and 3.0 are the doubles at 0x00A929F8 and 0x00A929F0; the game
        // pushes the zoom twice.
        const double power = Cdecl<double>(addr::SC4TerraformTool_PowInt, 1.7, zoom, zoom);
        Place(transform, 3.0 / power, point);
        Virtual<void>(At<void*>(self, field::kZoomEffect), 0x1C, &transform);
        const uint8_t shown = Virtual<uint8_t>(At<void*>(self, field::kZoomEffect), 0x14);
        if (show) {
            if (!shown)
                Virtual<void>(At<void*>(self, field::kZoomEffect), 0xC, uint32_t{0});
        } else if (shown) {
            Virtual<void>(At<void*>(self, field::kZoomEffect), 0x10, uint32_t{1});
        }
    }
    Virtual<void>(self, 0x20, At<uint32_t>(self, show ? field::kShownCursor : field::kHiddenCursor));
}

// --- The queue and the applications ----------------------------------------------------

// The game's 0x004D9A30 (ret 8): a stretch joins the end of the queue with
// the queue stopwatch's milliseconds (plus +0x120) as its time.
void __fastcall Queue(char* self, void*, const float* from, const float* to)
{
    const uint32_t time =
        This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, self + field::kQueueWatch) +
        At<uint32_t>(self, field::kQueueDelay);
    char* const head = At<char*>(self, field::kQueue);
    uint32_t stretch[4];
    std::memcpy(&stretch[0], from, 2 * sizeof(float));
    std::memcpy(&stretch[2], to, 2 * sizeof(float));
    char* const added = Cdecl<char*>(addr::SC4_stl_allocate, std::size_t{node::kSize});
    At<uint32_t>(added, node::kTime) = time;
    std::memcpy(added + node::kFrom, &stretch[0], sizeof(stretch));
    char* const last = At<char*>(head, node::kPrevious);
    At<char*>(added, node::kNext) = head;
    At<char*>(added, node::kPrevious) = last;
    At<char*>(last, node::kNext) = added;
    At<char*>(head, node::kPrevious) = added;
}

// The cost of one application: the brush's cost times the strength, rounded
// as the game does it (0x004D9891): a half and the float 12582912.0
// (0x00A929E8) added, the sum's float bits less the bits of that float, and
// one less when that is the product itself.
int32_t RoundCost(double product)
{
    float biased = static_cast<float>(0.5 + product + 12582912.0);
    int32_t whole = static_cast<int32_t>(At<uint32_t>(&biased, 0) + 0xB4C00000u);
    const int32_t below = whole - 1;
    if (static_cast<double>(below) == product)
        whole = below;
    return whole;
}

// The game's 0x004D9740 (ret 4): applies the brush once, along the last of
// the queued stretches whose time has come (all of them when flushing); the
// others taken with it are dropped. Nothing when none is due. But for the
// first application of a scrub the strength is scaled by the time since the
// application before (the top of this file). A brush that costs is paid for
// first and not applied when the budget refuses. The strength of the block
// is put back afterwards.
void __fastcall Apply(char* self, void*, uint8_t flush)
{
    const uint32_t now =
        flush ? 0xFFFFFFFFu : This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, self + field::kQueueWatch);
    bool taken = false;
    uint32_t stretch[4] = {};
    for (;;) {
        char* const head = At<char*>(self, field::kQueue);
        char* const first = At<char*>(head, node::kNext);
        if (first == head || At<uint32_t>(first, node::kTime) > now)
            break;
        std::memcpy(&stretch[0], first + node::kFrom, sizeof(stretch));
        char* const next = At<char*>(first, node::kNext);
        char* const previous = At<char*>(first, node::kPrevious);
        At<char*>(previous, node::kNext) = next;
        At<char*>(next, node::kPrevious) = previous;
        taken = true;
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(first));
    }
    if (!taken)
        return;
    char* const block = self + field::kParameters;
    const uint32_t strength = At<uint32_t>(block, parameters::kStrength);
    if (!At<uint8_t>(self, field::kFirst)) {
        const uint32_t elapsed = This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, self + field::kPaceWatch);
        // 1e-6f is the float at 0x00A86510, 2.0f the one at 0x00A8825C.
        double factor = static_cast<double>(elapsed) * static_cast<double>(1e-6f) *
                        static_cast<double>(Global<float>(addr::SC4TerraformTool_StrengthRate));
        if (factor > 2.0)
            factor = 2.0;
        At<float>(block, parameters::kStrength) =
            static_cast<float>(factor * static_cast<double>(At<float>(block, parameters::kStrength)));
    }
    This<void>(addr::GZStopwatch_Start, self + field::kPaceWatch);
    std::memcpy(block + parameters::kFrom, &stretch[0], sizeof(stretch));
    At<uint8_t>(block, parameters::kRedisplay) = 1;
    bool paid = true;
    if (!At<uint8_t>(self, field::kFree)) {
        void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
        const int64_t each =
            Virtual<int64_t>(At<void*>(self, field::kTerrain), 0xF8, At<uint32_t>(self, field::kBrush));
        const int32_t cost =
            RoundCost(static_cast<double>(each) * static_cast<double>(At<float>(block, parameters::kStrength)));
        // SetCostIndicator, two numbers: the sum with this cost, and the cost.
        const uint32_t sum = At<uint32_t>(self, field::kSpent) + static_cast<uint32_t>(cost);
        Virtual<void>(At<void*>(self, base::kView), 0xA8, sum, static_cast<uint32_t>(cost));
        if (Virtual<uint8_t>(budget, 0x20, static_cast<int64_t>(cost))) // WithdrawFunds
            At<int64_t>(self, field::kSpent) += cost;
        else
            paid = false;
    }
    if (paid)
        Virtual<uint8_t>(At<void*>(self, field::kTerrain), 0xE4, At<uint32_t>(self, field::kBrush),
                         static_cast<void*>(block));
    At<uint8_t>(self, field::kFirst) = 0;
    CountApplication(self, nullptr);
    At<uint32_t>(block, parameters::kStrength) = strength;
}

// --- The object ------------------------------------------------------------------------

// The game's 0x004D9430 (ret 8): the constructor with the brush and whether
// it is free; the caller allocated 0x130 bytes. The parameter block starts
// as the terrain's own defaults (0x004D8B90).
char* __fastcall Construct(char* self, void*, uint32_t brush, uint32_t free)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, field::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0) = addr::SC4TerraformTool_Vtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4TerraformTool_TargetVtable;
    At<uint32_t>(self, field::kBrush) = brush;
    At<uint32_t>(self, field::kFrames) = 0;
    At<void*>(self, field::kTerrain) = nullptr;
    At<void*>(self, field::kTerrainView) = nullptr;
    At<uint8_t>(self, field::kScrubbing) = 0;
    At<uint8_t>(self, field::kInside) = 0;
    At<void*>(self, field::kWinManager) = nullptr;
    char* const block = self + field::kParameters;
    At<int32_t>(block, parameters::kZoom) = -1;
    At<uint8_t>(block, parameters::kRedisplay) = 1;
    At<float>(block, parameters::kStrength) = 1.0f;
    At<float>(block, parameters::kRadius) = 1.0f;
    At<uint32_t>(block, parameters::kMessageData) = 3;
    At<float>(block, parameters::kAltitude) = 100.0f;
    At<uint8_t>(self, field::kFree) = static_cast<uint8_t>(free);
    At<uint8_t>(self, field::kMoved) = 0;
    This<void*>(addr::SC4Stopwatch_Construct, self + field::kPaceWatch, uint32_t{3});
    At<uint8_t>(self, field::kFirst) = 1;
    At<float>(self, field::kUnusedOne) = 1.0f;
    At<float>(self, field::kStrength) = 1.0f;
    At<float>(self, field::kRadius) = 1.0f;
    At<void*>(self, field::kEffect) = nullptr;
    At<void*>(self, field::kZoomEffect) = nullptr;
    At<uint32_t>(self, field::kPoint) = Global<uint32_t>(addr::SC4TerraformTool_Zero);
    At<uint32_t>(self, field::kPoint + 0x4) = Global<uint32_t>(addr::SC4TerraformTool_Zero);
    At<uint32_t>(self, field::kPoint + 0x8) = Global<uint32_t>(addr::SC4TerraformTool_Zero);
    At<uint32_t>(self, field::kShownCursor) = 0x071E5705;
    At<uint32_t>(self, field::kHiddenCursor) = 1;
    This<void*>(addr::SC4Stopwatch_Construct, self + field::kQueueWatch, uint32_t{4});
    At<uint32_t>(self, field::kQueueDelay) = 0;
    At<char*>(self, field::kQueue) = nullptr;
    char* const head = Cdecl<char*>(addr::SC4_stl_allocate, std::size_t{node::kSize});
    At<char*>(head, node::kNext) = head;
    At<char*>(head, node::kPrevious) = head;
    At<char*>(self, field::kQueue) = head;
    At<uint32_t>(self, base::kId) = kToolId;
    At<uint32_t>(self, base::kCursor) = 0x071E5703;
    return self;
}

// The game's 0x004D8DD0 (slot 4): Shutdown. A scrub that is on is ended
// (slot 60) and the mouse let go; the two view messages are given up; the
// effects are hidden; the window manager, the terrain view (its marks
// cleared) and the terrain (its texture updates let out) are let go; then
// the base. As in the game an effect is let go here only when it is shown,
// the second effect's turn lets go of the first again, and the frame service
// is not told (OnMouseUpL does that).
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    if (At<uint8_t>(self, field::kScrubbing)) {
        Virtual<void>(At<void*>(self, field::kTerrain), 0xF0, At<uint32_t>(self, field::kBrush),
                      At<uint32_t>(self, field::kParameters + parameters::kMessageData));
        At<uint8_t>(self, field::kInside) = 0;
        At<uint8_t>(self, field::kScrubbing) = 0;
        This<uint8_t>(addr::SC4ViewInputControl_EndCapture, self);
    }
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        void* const target = self + field::kTarget;
        Virtual<uint8_t>(server, 0x18, target, kMessageViewA);
        Virtual<uint8_t>(server, 0x18, target, kMessageViewB);
    }
    if (void* const effect = At<void*>(self, field::kEffect)) {
        if (Virtual<uint8_t>(effect, 0x14)) {
            Virtual<void>(At<void*>(self, field::kEffect), 0x10, uint32_t{0});
            if (void* const held = At<void*>(self, field::kEffect)) {
                At<void*>(self, field::kEffect) = nullptr;
                Release(held);
            }
        }
    }
    if (void* const effect = At<void*>(self, field::kZoomEffect)) {
        if (Virtual<uint8_t>(effect, 0x14)) {
            Virtual<void>(At<void*>(self, field::kZoomEffect), 0x10, uint32_t{1});
            if (void* const held = At<void*>(self, field::kEffect)) {
                At<void*>(self, field::kEffect) = nullptr;
                Release(held);
            }
        }
    }
    if (void* const manager = At<void*>(self, field::kWinManager)) {
        Release(manager);
        At<void*>(self, field::kWinManager) = nullptr;
    }
    if (void* const terrainView = At<void*>(self, field::kTerrainView)) {
        Virtual<void>(terrainView, 0x24); // ClearCurrentSelections
        Release(At<void*>(self, field::kTerrainView));
        At<void*>(self, field::kTerrainView) = nullptr;
    }
    if (void* const terrain = At<void*>(self, field::kTerrain)) {
        Virtual<void>(terrain, 0x13C);
        Release(At<void*>(self, field::kTerrain));
        At<void*>(self, field::kTerrain) = nullptr;
    }
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x004D9080 (slot 3): Init. The tuning on the first Init since
// the application last asked for it again (the callback is registered with
// the application's slot 69); the base Init; the terrain, the brush's looks
// (slot 63) and the terrain view; the window manager; the two view
// messages; the idle looks and the cursor at +0x100.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    if (!Global<uint8_t>(addr::SC4TerraformTool_TuningLoaded)) {
        Global<uint8_t>(addr::SC4TerraformTool_TuningLoaded) = 1;
        LoadTuning();
        Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0x114,
                      reinterpret_cast<void*>(addr::SC4TerraformTool_TuningReset), uint32_t{0});
    }
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    At<void*>(self, field::kTerrain) = terrain;
    if (terrain) {
        AddRef(terrain);
        Virtual<uint8_t>(At<void*>(self, field::kTerrain), 0xFC, At<uint32_t>(self, field::kBrush),
                         &At<uint32_t>(self, field::kElements));
        void* const terrainView = Virtual<void*>(At<void*>(self, field::kTerrain), 0x108);
        At<void*>(self, field::kTerrainView) = terrainView;
        if (terrainView)
            AddRef(terrainView);
    }
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    At<void*>(self, field::kWinManager) = manager;
    if (manager)
        AddRef(manager);
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        void* const target = self + field::kTarget;
        Virtual<uint8_t>(server, 0x14, target, kMessageViewA);
        Virtual<uint8_t>(server, 0x14, target, kMessageViewB);
    }
    SetLooks(self, nullptr, 0);
    Virtual<void>(self, 0x20, At<uint32_t>(self, field::kHiddenCursor));
    return 1;
}

// The game's 0x004D9560: the destructor. Shutdown when still inited, the
// queue, the stopwatches, the effects, the base.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4TerraformTool_Vtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4TerraformTool_TargetVtable;
    if (At<uint8_t>(self, base::kInited))
        Shutdown(self, nullptr);
    This<void>(addr::SC4PairList_Destroy, self + field::kQueue);
    This<void>(addr::SC4Stopwatch_Destruct, self + field::kQueueWatch);
    if (void* const effect = At<void*>(self, field::kZoomEffect))
        Release(effect);
    if (void* const effect = At<void*>(self, field::kEffect))
        Release(effect);
    This<void>(addr::SC4Stopwatch_Destruct, self + field::kPaceWatch);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// The game's 0x004D9950 (slot 26, ret 4): the deleting destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// --- Keys ------------------------------------------------------------------------------

// The looks again where they were, shown, after a key changed the brush.
void Refresh(char* self)
{
    UpdateCursor(self, nullptr, &At<float>(self, field::kPoint), 1);
}

// A key's number times the float 0.1 (0x00A8C950), as a float.
float Tenths(uint32_t number)
{
    return static_cast<float>(static_cast<double>(number) * static_cast<double>(0.1f));
}

// The game's 0x004D95D0 (slot 14, OnKeyDown, ret 8). Nothing, false, when
// the tool is not the view's current one. With modifier bit 0: F1 to F10
// (0x70 to 0x79) set the strength, the digits 1 to 9 and 0 the radius
// scaling, to a tenth of the key's number, and the looks are refreshed when
// there is a first effect: true. Else the keys 0x10 and 0x11 set both from
// the modifiers and refresh the looks, when there is a first effect: false.
uint8_t __fastcall OnKeyDown(char* self, void*, uint32_t key, uint32_t modifiers)
{
    if (!This<uint8_t>(addr::SC4ViewInputControl_IsCurrent, self))
        return 0;
    char* const block = self + field::kParameters;
    if (modifiers & 1) {
        if (key >= 0x70 && key <= 0x79) {
            const float strength = Tenths(key - 0x6F);
            At<float>(self, field::kStrength) = strength;
            At<float>(block, parameters::kStrength) = strength;
            if (At<void*>(self, field::kEffect))
                Refresh(self);
            return 1;
        }
        if (key >= 0x30 && key <= 0x39) {
            const float radius = Tenths((key == 0x30 ? 0x39 : key - 1) - 0x2F);
            At<float>(self, field::kRadius) = radius;
            At<float>(block, parameters::kRadius) = radius;
            if (At<void*>(self, field::kEffect))
                Refresh(self);
            return 1;
        }
    }
    if ((key == 0x10 || key == 0x11) && At<void*>(self, field::kEffect)) {
        ApplyModifiers(self, nullptr, modifiers);
        Refresh(self);
    }
    return 0;
}

// The game's 0x004D96E0 (slot 15, OnKeyUp, ret 8): the keys 0x10 and 0x11
// as in OnKeyDown, without the test for the current tool. False.
uint8_t __fastcall OnKeyUp(char* self, void*, uint32_t key, uint32_t modifiers)
{
    if ((key == 0x10 || key == 0x11) && At<void*>(self, field::kEffect)) {
        ApplyModifiers(self, nullptr, modifiers);
        Refresh(self);
    }
    return 0;
}

// --- The mouse and the messages --------------------------------------------------------

// The game's 0x004D9720 (slot 22, OnMouseExit): the pointer left the view;
// the looks are hidden when there is a first effect. True.
uint8_t __fastcall OnMouseExit(char* self, void*)
{
    At<uint8_t>(self, field::kInside) = 0;
    if (At<void*>(self, field::kEffect))
        UpdateCursor(self, nullptr, &At<float>(self, field::kPoint), 0);
    return 1;
}

// The three floats the handlers hand CursorPoint, set to the dword at
// 0x00B21928 (zero) first.
void UnsetPoint(float* point)
{
    const uint32_t unset = Global<uint32_t>(addr::SC4TerraformTool_Zero);
    At<uint32_t>(point, 0x0) = unset;
    At<uint32_t>(point, 0x4) = unset;
    At<uint32_t>(point, 0x8) = unset;
}

// The game's 0x004D9B90 (slot 16, OnMouseDownL, ret 0xC). False without a
// view. Without ground under the pointer (PressAltitude, then CursorPoint)
// the looks are hidden: true. A brush that costs (cost above 0, with a
// budget) does nothing when the funds are not above 0: true. Else the scrub
// begins: the block's strength, radius scaling, message data (1 free, 2
// not) and altitude (the point's y); the terrain's BeginBrushScrub; the
// point kept; keyboard scrolling stopped; the mouse captured; the terrain
// view's cell cursor off (what it was at +0x24); the texture updates
// gathered when the tuning's count is not 0; the scrub's looks; the queue's
// stopwatch started and the point queued and applied; the frame service
// asked for its message. True.
uint8_t __fastcall OnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    float point[3];
    UnsetPoint(point);
    if (!At<void*>(self, base::kView))
        return 0;
    if (!PressAltitude(self, nullptr, x, z) || !CursorPoint(self, nullptr, x, z, point)) {
        UpdateCursor(self, nullptr, point, 0);
        return 1;
    }
    const uint32_t brush = At<uint32_t>(self, field::kBrush);
    if (!At<uint8_t>(self, field::kFree)) {
        void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
        const int64_t each = Virtual<int64_t>(At<void*>(self, field::kTerrain), 0xF8, brush);
        if (budget && each > 0 && Virtual<int64_t>(budget, 0x18) <= 0) // GetTotalFunds
            return 1;
    }
    char* const block = self + field::kParameters;
    At<uint8_t>(self, field::kScrubbing) = 1;
    At<uint8_t>(self, field::kInside) = 1;
    At<uint8_t>(self, field::kFirst) = 1;
    At<uint8_t>(block, parameters::kRedisplay) = 1;
    At<int32_t>(block, parameters::kZoom) = -1;
    At<uint32_t>(block, parameters::kMessageData) = At<uint8_t>(self, field::kFree) ? 1 : 2;
    std::memcpy(block + parameters::kAltitude, &point[1], sizeof(float));
    ApplyModifiers(self, nullptr, modifiers);
    Virtual<void>(At<void*>(self, field::kTerrain), 0xEC, brush, At<uint32_t>(block, parameters::kMessageData));
    float* const last = &At<float>(self, field::kLast);
    std::memcpy(&last[0], &point[0], sizeof(float));
    std::memcpy(&last[1], &point[2], sizeof(float));
    Virtual<void>(At<void*>(self, base::kView), 0x8C); // KillKeyboardScrolling
    This<uint8_t>(addr::SC4ViewInputControl_BeginCapture, self);
    At<uint8_t>(self, base::kSaved) = Virtual<uint8_t>(At<void*>(self, field::kTerrainView), 0x28, uint32_t{0});
    At<uint32_t>(self, field::kApplications) = 0;
    if (Global<uint32_t>(addr::SC4TerraformTool_UpdatesPerFlush) > 0)
        Virtual<void>(At<void*>(self, field::kTerrain), 0x138);
    SetLooks(self, nullptr, 1);
    UpdateCursor(self, nullptr, point, 1);
    At<int64_t>(self, field::kSpent) = 0;
    This<void>(addr::GZStopwatch_Start, self + field::kQueueWatch);
    Queue(self, nullptr, last, last);
    Apply(self, nullptr, 0);
    if (void* const frames = Cdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<uint8_t>(frames, 0x14, static_cast<void*>(self + field::kTarget), kFrameCode);
    return 1;
}

// The game's 0x004D9970 (slot 18, OnMouseUpL, ret 0xC). False when no scrub
// is on. Everything queued is applied; the cell cursor is put back; the
// idle looks, shown as the first effect was; the cost indicator cleared
// (with 1); the mouse let go; the frame service told; the terrain's
// EndBrushScrub; the texture updates let out. True.
uint8_t __fastcall OnMouseUpL(char* self, void*, int32_t, int32_t, uint32_t)
{
    if (!At<uint8_t>(self, field::kScrubbing))
        return 0;
    Apply(self, nullptr, 1);
    Virtual<uint8_t>(At<void*>(self, field::kTerrainView), 0x28, uint32_t{At<uint8_t>(self, base::kSaved)});
    SetLooks(self, nullptr, 0);
    if (void* const effect = At<void*>(self, field::kEffect)) {
        const uint8_t shown = Virtual<uint8_t>(effect, 0x14);
        UpdateCursor(self, nullptr, &At<float>(self, field::kPoint), shown);
    }
    Virtual<void>(At<void*>(self, base::kView), 0xB0, uint32_t{1}); // ClearCostIndicator
    This<uint8_t>(addr::SC4ViewInputControl_EndCapture, self);
    if (void* const frames = Cdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<uint8_t>(frames, 0x18, static_cast<void*>(self + field::kTarget), kFrameCode);
    Virtual<void>(At<void*>(self, field::kTerrain), 0xF0, At<uint32_t>(self, field::kBrush),
                  At<uint32_t>(self, field::kParameters + parameters::kMessageData));
    At<uint8_t>(self, field::kInside) = 0;
    At<uint8_t>(self, field::kScrubbing) = 0;
    At<uint8_t>(self, field::kMoved) = 0;
    Virtual<void>(At<void*>(self, field::kTerrain), 0x13C);
    return 1;
}

// The game's 0x004D9D80 (slot 20, OnMouseMove, ret 0xC). Nothing while a
// move was applied since the last message. Without a point under the
// pointer and without a scrub the looks are hidden. In a scrub the stretch
// from the last point to this one is queued and applied, the point becomes
// the last one, the window manager's slot 19 is called and no further move
// is taken until a message comes. The looks go to the point, shown. True.
uint8_t __fastcall OnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    float point[3];
    UnsetPoint(point);
    At<uint8_t>(self, field::kInside) = At<uint8_t>(self, field::kScrubbing);
    if (At<uint8_t>(self, field::kMoved))
        return 1;
    if (!CursorPoint(self, nullptr, x, z, point) && !At<uint8_t>(self, field::kScrubbing)) {
        UpdateCursor(self, nullptr, point, 0);
        return 1;
    }
    float to[2];
    std::memcpy(&to[0], &point[0], sizeof(float));
    std::memcpy(&to[1], &point[2], sizeof(float));
    if (At<uint8_t>(self, field::kScrubbing)) {
        float* const last = &At<float>(self, field::kLast);
        Queue(self, nullptr, last, to);
        Apply(self, nullptr, 0);
        std::memcpy(last, &to[0], sizeof(to));
        if (void* const manager = At<void*>(self, field::kWinManager))
            Virtual<void>(manager, 0x4C);
        At<uint8_t>(self, field::kMoved) = 1;
    }
    UpdateCursor(self, nullptr, point, 1);
    return 1;
}

// The game's 0x004D9AC0 (slot 3 of the message target at +0x28, ret 4).
// `this` is the target part. The frame service's message, in a scrub: moves
// are taken again, and every third one since the last application queues
// the last point once more; what is due is applied. The two view messages,
// when the tool is the view's current one: moves are taken again and the
// tool is told the pointer moved to where the window manager says it is
// (its slot 33), then the looks go where they were, shown as the first
// effect is. True.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const self = target - field::kTarget;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == kMessageFrame) {
        if (!At<uint8_t>(self, field::kScrubbing))
            return 1;
        const int32_t frames = At<int32_t>(self, field::kFrames) + 1;
        At<uint8_t>(self, field::kMoved) = 0;
        At<int32_t>(self, field::kFrames) = frames;
        if (frames > 2) {
            float* const last = &At<float>(self, field::kLast);
            Queue(self, nullptr, last, last);
            At<int32_t>(self, field::kFrames) = 0;
        }
        Apply(self, nullptr, 0);
        return 1;
    }
    if (type != kMessageViewA && type != kMessageViewB)
        return 1;
    if (Virtual<char*>(At<void*>(self, base::kView), 0x60) != self)
        return 1;
    At<uint8_t>(self, field::kMoved) = 0;
    // The game leaves the first number unset before the call; the second is
    // its own argument slot.
    int32_t x = 0;
    int32_t z = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(message));
    Virtual<uint8_t>(At<void*>(self, field::kWinManager), 0x84, &x, &z);
    Virtual<uint8_t>(self, 0x50, x, z, uint32_t{0});
    if (void* const effect = At<void*>(self, field::kEffect)) {
        const uint8_t shown = Virtual<uint8_t>(effect, 0x14);
        UpdateCursor(self, nullptr, &At<float>(self, field::kPoint), shown);
    }
    return 1;
}

// --- Registration ----------------------------------------------------------------------

bool InstallBrushTool(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4TerraformTool_ApplyModifiers, reinterpret_cast<void*>(&ApplyModifiersThunk)},
        {addr::SC4TerraformTool_CountApplication, reinterpret_cast<void*>(&CountApplication)},
        {addr::SC4TerraformTool_TuningReset, reinterpret_cast<void*>(&TuningReset)},
        {addr::SC4TerraformTool_LoadTuning, reinterpret_cast<void*>(&LoadTuning)},
        {addr::SC4TerraformTool_PressAltitude, reinterpret_cast<void*>(&PressAltitude)},
        {addr::SC4TerraformTool_CursorPoint, reinterpret_cast<void*>(&CursorPoint)},
        {addr::SC4TerraformTool_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4TerraformTool_SetLooks, reinterpret_cast<void*>(&SetLooks)},
        {addr::SC4TerraformTool_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4TerraformTool_UpdateCursor, reinterpret_cast<void*>(&UpdateCursor)},
        {addr::SC4TerraformTool_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4TerraformTool_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4TerraformTool_OnKeyDown, reinterpret_cast<void*>(&OnKeyDown)},
        {addr::SC4TerraformTool_OnKeyUp, reinterpret_cast<void*>(&OnKeyUp)},
        {addr::SC4TerraformTool_OnMouseExit, reinterpret_cast<void*>(&OnMouseExit)},
        {addr::SC4TerraformTool_Apply, reinterpret_cast<void*>(&Apply)},
        {addr::SC4TerraformTool_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4TerraformTool_OnMouseUpL, reinterpret_cast<void*>(&OnMouseUpL)},
        {addr::SC4TerraformTool_Queue, reinterpret_cast<void*>(&Queue)},
        {addr::SC4TerraformTool_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::SC4TerraformTool_OnMouseDownL, reinterpret_cast<void*>(&OnMouseDownL)},
        {addr::SC4TerraformTool_OnMouseMove, reinterpret_cast<void*>(&OnMouseMove)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.brush",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4TerraformTool_OnMouseDownL,
    "Our terraform (terrain brush) tool of the city view (docs/tools.md)",
    false,
    InstallBrushTool,
});

} // namespace
} // namespace opensc4::tool_brush
