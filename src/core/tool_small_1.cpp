// Two small view input controls of the city view (0x004B7DC0 to 0x004B8822).
// Replacement, off by default:
//
//   ui.tools.small1: the drag-to-scroll control (vtable 0x00A90040, 0x48
//     bytes, made by the view's WindowMouseDownR 0x007E6A00 and started at
//     the point of the press) and the view's mission icon control (vtable
//     0x00A900D8, message target 0x00A900C4 at +0x28, 0x3C bytes, ID
//     0xABEC6CFE, made at city load by 0x007ED0F0 as a persistent control).
//
// The drag-to-scroll control shows a small picture where the right button
// went down and, while the button is held, scrolls the view by how far the
// pointer is from that point: nothing within the inner distance (12 pixels
// of half the offset), faster up to the outer one (80), in the direction of
// the offset. The cursor is one of eight arrows. It reads the button's state
// from Windows on every move and puts itself away when the button is up.
//
// The mission icon control is always there under the tools. In view level 1
// it lets the player click the icons the dispatch manager's vehicle view
// (dispatch manager slot 22) draws over buildings: the icon under the
// pointer is lit, a press marks it, and letting go on the same icon starts
// the situation for its occupant (the city situation manager's slot 22).
//
// The shared slots (the deleting destructor 0x004B7ED0, the QueryInterface
// 0x004B8480, the message target's thunks) are the base's (tool_base.cpp).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace opensc4::tool_small {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kInited = 0x04; // byte
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kCursor = 0x10; // cursor ID
constexpr std::size_t kView = 0x1C;   // the city view
} // namespace base

// The drag-to-scroll control's fields.
namespace scroll {
constexpr std::size_t kAnchorX = 0x28;      // the screen point the drag began at
constexpr std::size_t kAnchorZ = 0x2C;
constexpr std::size_t kRateX = 0x30;        // float: the offset, held to the outer distance (written only)
constexpr std::size_t kRateZ = 0x34;
constexpr std::size_t kMoved = 0x38;        // byte: an arrow cursor has been shown
constexpr std::size_t kWasCapturing = 0x39; // byte: the mouse was captured before Init
constexpr std::size_t kImage = 0x3C;        // the anchor picture (referenced)
constexpr std::size_t kInner = 0x40;        // 12: no scrolling within this
constexpr std::size_t kOuter = 0x44;        // 80: full speed from here
} // namespace scroll

// The mission icon control's fields.
namespace icons {
constexpr std::size_t kTarget = 0x28;     // the message target part (vtable 0x00A900C4)
constexpr std::size_t kSituations = 0x2C; // the city situation manager (referenced)
constexpr std::size_t kDispatch = 0x30;   // the dispatch manager (referenced)
constexpr std::size_t kHover = 0x34;      // the icon under the pointer, 0 for none
constexpr std::size_t kPressed = 0x38;    // the icon the button went down on
} // namespace icons

constexpr uint32_t kScrollId = 0x48E2A1F8;
constexpr uint32_t kCursorNeutral = 0xC2A676AC;
constexpr uint32_t kAnchorWindow = 0x48E945B4; // the ID of the anchor picture's window
constexpr uint32_t kExemplarIid = 0xA52160F5;
constexpr uint32_t kMessageViewZR = 0xA6B79602;     // kSC4MessageViewZRChange3
constexpr uint32_t kMessageViewT = 0xA6B79612;      // kSC4MessageViewTChange3
constexpr uint32_t kMessageViewFilter = 0x06CDB65B; // kSC4MessagePostViewFilterChange

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

// An image by resource key (0x30 bytes: the frame of 0x004B82F0 keeps it in
// the 0x30 bytes below the return address).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    char rest[0x28];
};
static_assert(sizeof(ImageLoader) == 0x30);

// The angle of an offset as the game's x87 code leaves it in a float: FILD
// dz, FDIV by the float dx, FLD1, FPATAN, FSTP dword. One rounding, from the
// instruction's own result.
float X87Angle(int32_t dz, float fdx)
{
    float result;
    __asm {
        fild dz
        fdiv fdx
        fld1
        fpatan
        fstp result
    }
    return result;
}

// The same with the float at 0x00A90034 (pi) added before the store.
float X87AnglePlusPi(int32_t dz, float fdx)
{
    float pi = 3.14159274f;
    float result;
    __asm {
        fild dz
        fdiv fdx
        fld1
        fpatan
        fadd pi
        fstp result
    }
    return result;
}

// |value| as CDQ, XOR, SUB give it.
uint32_t Magnitude(int32_t value)
{
    const uint32_t sign = static_cast<uint32_t>(value >> 31);
    return (static_cast<uint32_t>(value) ^ sign) - sign;
}

// FILD of a dword read as unsigned (2 to the 32 added when the sign is set).
double Unsigned(uint32_t value)
{
    return static_cast<double>(value);
}

// --- The drag-to-scroll control --------------------------------------------------------

// The game's 0x004B7DC0: the constructor; the caller allocated 0x48 bytes.
char* __fastcall ScrollConstruct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<int32_t>(self, scroll::kAnchorX) = 0;
    At<int32_t>(self, scroll::kAnchorZ) = 0;
    At<uint32_t>(self, scroll::kRateX) = 0;
    At<uint32_t>(self, scroll::kRateZ) = 0;
    At<uint8_t>(self, scroll::kMoved) = 0;
    At<uint8_t>(self, scroll::kWasCapturing) = 0;
    At<void*>(self, scroll::kImage) = nullptr;
    At<std::uintptr_t>(self, 0) = addr::SC4ScrollDragTool_Vtable;
    At<uint32_t>(self, scroll::kInner) = 0xC;
    At<uint32_t>(self, scroll::kOuter) = 0x50;
    return self;
}

// The anchor picture's window among the view's children, or null.
void* AnchorWindow(char* self)
{
    void* const window = Virtual<void*>(At<void*>(self, base::kView), 0xC); // AsIGZWin
    return Virtual<void*>(window, 0x88, kAnchorWindow);                     // the child of that ID
}

// Takes the anchor picture's window off the view's window (window slot 16).
void RemoveAnchor(char* self, void* anchor)
{
    void* const window = Virtual<void*>(At<void*>(self, base::kView), 0xC);
    Virtual<void>(window, 0x40, anchor);
}

// The game's 0x004B7E00 (slot 4): Shutdown. Lets go of the mouse unless it
// was captured before Init, stops the scroll, takes the anchor picture's
// window away and lets go of the picture.
uint8_t __fastcall ScrollShutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    void* const anchor = AnchorWindow(self);
    if (!At<uint8_t>(self, scroll::kWasCapturing))
        This<uint8_t>(addr::SC4ViewInputControl_EndCapture, self);
    Virtual<void>(At<void*>(self, base::kView), 0x88); // ScrollStop
    if (anchor)
        RemoveAnchor(self, anchor);
    if (void* const image = At<void*>(self, scroll::kImage)) {
        Release(image);
        At<void*>(self, scroll::kImage) = nullptr;
    }
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x004B7E70 (slot 27): puts the control away. Stops the scroll,
// takes the anchor picture's window away, then the base's slot 27 (the view
// drops its current control), whose answer is the answer.
uint32_t __fastcall ScrollPutAway(char* self, void*)
{
    void* const anchor = AnchorWindow(self);
    Virtual<void>(At<void*>(self, base::kView), 0x88); // ScrollStop
    if (anchor)
        RemoveAnchor(self, anchor);
    return This<uint32_t>(addr::SC4ViewInputControl_PutAway, self);
}

// The game's 0x004B7EC0 (slot 19, OnMouseUpR, ret 0xC): slot 27. True.
uint8_t __fastcall ScrollOnMouseUpR(char* self, void*, int32_t, int32_t, uint32_t)
{
    Virtual<void>(self, 0x6C);
    return 1;
}

// The cursor for an offset: neutral when both parts are within the inner
// distance, else one of eight arrows.
uint32_t ArrowCursor(int32_t dx, int32_t dz, uint32_t innerBits)
{
    const int32_t inner = static_cast<int32_t>(innerBits);
    const int32_t minusInner = static_cast<int32_t>(0u - innerBits);
    if (dz >= inner) {
        if (dx >= inner)
            return 0x22A676A0;
        return dx > minusInner ? 0x62A6769B : 0xA2A67698;
    }
    if (dz <= minusInner) {
        if (dx >= inner)
            return 0x82A676A8;
        return dx > minusInner ? 0x62A67606 : 0x02A67691;
    }
    if (dx >= inner)
        return 0xC2A676A4;
    return dx > minusInner ? kCursorNeutral : 0x62A67694;
}

// The game's 0x004B7EF0 (slot 20, OnMouseMove, ret 0xC). Asks Windows
// whether the right button (the left one when the buttons are swapped) is
// still down; when it is not, this is OnMouseUpR (slot 19) and its answer.
// Otherwise the offset is half the way from the anchor to the pointer; the
// rates at +0x30 and +0x34, the cursor, and the view's SetScrolling with the
// direction (an angle, 0 along +x, a quarter turn along +z) and the speed
// (the offset's length over outer less inner, 1 at most). False.
uint8_t __fastcall ScrollOnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    using MetricsFn = int(__stdcall*)(int);
    using KeyStateFn = int16_t(__stdcall*)(int);
    const int swapped = Global<MetricsFn>(addr::User32_GetSystemMetrics)(0x17); // SM_SWAPBUTTON
    if (Global<KeyStateFn>(addr::User32_GetAsyncKeyState)(swapped ? 1 : 2) == 0)
        return Virtual<uint8_t>(self, 0x4C, x, z, modifiers);

    const int32_t dx = -((At<int32_t>(self, scroll::kAnchorX) - x) / 2);
    const int32_t dz = -((At<int32_t>(self, scroll::kAnchorZ) - z) / 2);
    const uint32_t outer = At<uint32_t>(self, scroll::kOuter);
    const uint32_t inner = At<uint32_t>(self, scroll::kInner);
    const bool farX = Magnitude(dx) > outer;
    const bool farZ = Magnitude(dz) > outer;
    const bool outX = Magnitude(dx) > inner;
    const bool outZ = Magnitude(dz) > inner;

    if (farX)
        At<float>(self, scroll::kRateX) = static_cast<float>(dx > 0 ? Unsigned(outer) : -Unsigned(outer));
    if (farZ)
        At<float>(self, scroll::kRateZ) = static_cast<float>(dz > 0 ? Unsigned(outer) : -Unsigned(outer));
    if (outX && !farX)
        At<float>(self, scroll::kRateX) = static_cast<float>(dx);
    if (outZ && !farZ)
        At<float>(self, scroll::kRateZ) = static_cast<float>(dz);
    if (!farX && !outX)
        At<uint32_t>(self, scroll::kRateX) = 0;
    if (!farZ && !outZ)
        At<uint32_t>(self, scroll::kRateZ) = 0;

    uint32_t cursor;
    if (!farX && !outX && !farZ && !outZ) {
        cursor = kCursorNeutral;
        if (At<uint32_t>(self, base::kCursor) != cursor)
            Virtual<void>(self, 0x20, cursor); // SetCursor by ID
    } else {
        cursor = ArrowCursor(dx, dz, inner);
    }
    if (!At<uint8_t>(self, scroll::kMoved) && cursor != kCursorNeutral)
        At<uint8_t>(self, scroll::kMoved) = 1;
    if (At<uint32_t>(self, base::kCursor) != cursor)
        Virtual<void>(self, 0x20, cursor);

    // The length of the offset over the span between the two distances, read
    // again from the object.
    const float fdx = static_cast<float>(dx);
    const int32_t dzSquared = static_cast<int32_t>(static_cast<uint32_t>(dz) * static_cast<uint32_t>(dz));
    const double squares = static_cast<double>(dzSquared) + static_cast<double>(fdx) * static_cast<double>(fdx);
    const uint32_t span = At<uint32_t>(self, scroll::kOuter) - At<uint32_t>(self, scroll::kInner);
    const float ratio = static_cast<float>(std::sqrt(squares) / Unsigned(span));
    const float speed = 1.0f < ratio ? 1.0f : ratio;

    float angle = 0.0f;
    uint32_t scrolling = 0;
    if (cursor != kCursorNeutral) {
        if (dx > 0) {
            if (dz != 0)
                angle = X87Angle(dz, fdx);
        } else if (dx < 0) {
            angle = dz != 0 ? X87AnglePlusPi(dz, fdx) : 3.14159274f; // 0x40490FDB
        } else {
            angle = dz < 0 ? 4.71238899f : 1.57079637f; // 0x4096CBE4, 0x3FC90FDB
        }
        scrolling = 1;
    }
    // The flag is the low byte of the stack slot the modifiers came in.
    Virtual<uint8_t>(At<void*>(self, base::kView), 0x84, (modifiers & 0xFFFFFF00u) | scrolling, angle, speed);
    return 0;
}

// The game's 0x004B8220: the two distances from the exemplar (0x6534284A,
// 0x690F693F, 0xE90F6C84): properties 0xE90F5DE4 (inner) and 0xA90F5DEC
// (outer). They keep their values when the exemplar or a property is not
// there.
void __fastcall ScrollLoadTuning(char* self, void*)
{
    void* exemplar; // not set before the call, as in the game
    uint32_t key[3] = {0x6534284A, 0x690F693F, 0xE90F6C84};
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, &key[0], kExemplarIid, &exemplar, uint32_t{0},
                          uint32_t{0}))
        return;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0xE90F5DE4}, &At<uint32_t>(self, scroll::kInner));
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0xA90F5DEC}, &At<uint32_t>(self, scroll::kOuter));
    Release(exemplar);
}

// The game's 0x004B82A0 (slot 3): Init. The ID and the neutral cursor, the
// tuning, the base Init, and the mouse captured unless it is already (slot
// 25 says; the answer is kept for Shutdown).
uint8_t __fastcall ScrollInit(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    At<uint32_t>(self, base::kId) = kScrollId;
    At<uint32_t>(self, base::kCursor) = kCursorNeutral;
    At<uint8_t>(self, scroll::kMoved) = 0;
    ScrollLoadTuning(self, nullptr);
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    const uint8_t capturing = Virtual<uint8_t>(self, 0x64); // AmCapturing
    At<uint8_t>(self, scroll::kWasCapturing) = capturing;
    if (!capturing)
        This<uint8_t>(addr::SC4ViewInputControl_BeginCapture, self);
    return 1;
}

// The game's 0x004B82F0 (ret 8): the drag begins at the screen point. Keeps
// the point; the first time it also loads the anchor picture (PNG
// 0x094AC89A of the interface group, magenta see-through), has the window
// factory make a window of it with the ID 0x48E945B4, centres that on the
// point and gives it to the view's window as a child (when that fails the
// window manager is told to drop it; otherwise the view's window slot 26
// gets it).
void __fastcall ScrollStart(char* self, void*, int32_t x, int32_t z)
{
    At<int32_t>(self, scroll::kAnchorX) = x;
    At<int32_t>(self, scroll::kAnchorZ) = z;
    if (At<void*>(self, scroll::kImage))
        return;
    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0x856DDBAC}, uint32_t{0x46A006B0},
                uint32_t{0x094AC89A}, uint32_t{1}, uint32_t{0});
    if (This<uint8_t>(addr::SC4ImageLoader_Load, &loader)) {
        void* const image = loader.image;
        At<void*>(self, scroll::kImage) = image;
        if (image) {
            AddRef(image);
            const uint32_t clear =
                Virtual<uint32_t>(At<void*>(self, scroll::kImage), 0x78, uint32_t{0xFF}, uint32_t{0}, uint32_t{0xFF});
            Virtual<void>(At<void*>(self, scroll::kImage), 0x5C, clear);
            const int32_t* const area = Virtual<int32_t*>(At<void*>(self, scroll::kImage), 0x30);
            const int32_t halfHeight = (area[3] - area[1]) / 2;
            const int32_t halfWidth = (area[2] - area[0]) / 2;
            const int32_t left = area[0] - halfWidth + At<int32_t>(self, scroll::kAnchorX);
            const int32_t right = area[2] - halfWidth + At<int32_t>(self, scroll::kAnchorX);
            const int32_t top = area[1] - halfHeight + At<int32_t>(self, scroll::kAnchorZ);
            const int32_t bottom = area[3] - halfHeight + At<int32_t>(self, scroll::kAnchorZ);
            void* const factory = Cdecl<void*>(addr::SC4_Singleton913C72);
            void* const made =
                Virtual<void*>(factory, 0x3C, kAnchorWindow, At<void*>(self, scroll::kImage), uint32_t{1});
            if (made) {
                void* const window = Virtual<void*>(made, 0xC);
                Virtual<void>(window, 0xDC, left, top, right, bottom);
                Virtual<void>(window, 0x110, uint32_t{2}, uint32_t{0});
                Virtual<void>(window, 0x110, uint32_t{4}, uint32_t{1});
                Virtual<void>(window, 0x114);
                void* const parent = Virtual<void*>(At<void*>(self, base::kView), 0xC);
                if (!Virtual<uint8_t>(parent, 0x38, window)) { // ChildAdd
                    void* const again = Virtual<void*>(At<void*>(self, base::kView), 0xC);
                    Virtual<void>(Virtual<void*>(again, 0x18), 0x5C, window); // the window manager drops it
                } else {
                    void* const again = Virtual<void*>(At<void*>(self, base::kView), 0xC);
                    Virtual<void>(again, 0x68, window);
                }
                Release(made);
            }
        }
    }
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
}

// --- The mission icon control ----------------------------------------------------------

// The vehicle view's functions (the object the dispatch manager's slot 22
// gives): the icon at a screen point (0 for none), an icon lit or not, an
// icon made the active one with a state, the active icon's occupant.
uint32_t PickIcon(void* vehicleView, int32_t x, int32_t z)
{
    return This<uint32_t>(addr::SC4DispatchVehicleView_PickAt, vehicleView, x, z);
}

void LightIcon(void* vehicleView, uint32_t icon, uint32_t lit)
{
    This<void>(addr::SC4DispatchVehicleView_SetHover, vehicleView, icon, lit);
}

void ActivateIcon(void* vehicleView, uint32_t icon, uint32_t state)
{
    This<void>(addr::SC4DispatchVehicleView_SetActive, vehicleView, icon, state);
}

// Whether the view (the global, not the control's own pointer) shows level 1.
bool ViewLevelIsOne()
{
    void* const view = Global<void*>(addr::SC4_Service43DC4);
    return view && Virtual<int32_t>(view, 0x58) == 1; // GetCurrentViewLevel
}

// Puts a service into a counted field: the new one referenced first, the old
// one released after. Nothing when they are the same.
void Hold(char* self, std::size_t field, void* service)
{
    void* const old = At<void*>(self, field);
    if (service == old)
        return;
    if (service)
        AddRef(service);
    At<void*>(self, field) = service;
    if (old)
        Release(old);
}

// The game's 0x004B84B0 (slot 3): Init. The base Init, the city's situation
// manager (slot 49) and dispatch manager (slot 39), and the view's zoom and
// rotation message for the target at +0x28.
uint8_t __fastcall IconsInit(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    if (void* const city = Global<void*>(addr::SC4_City)) {
        Hold(self, icons::kSituations, Virtual<void*>(city, 0xC4));
        Hold(self, icons::kDispatch, Virtual<void*>(city, 0x9C));
    }
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<void>(server, 0x14, static_cast<void*>(self + icons::kTarget), kMessageViewZR); // AddNotification
    return 1;
}

// The game's 0x004B8550 (slot 4): Shutdown. Takes the message back, lets go
// of the two managers (each field cleared first), then the base.
uint8_t __fastcall IconsShutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<void>(server, 0x18, static_cast<void*>(self + icons::kTarget), kMessageViewZR); // RemoveNotification
    for (const std::size_t field : {icons::kSituations, icons::kDispatch}) {
        if (void* const held = At<void*>(self, field)) {
            At<void*>(self, field) = nullptr;
            Release(held);
        }
    }
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x004B85B0 (slot 3 of the message target at +0x28, ret 4).
// `this` is the target part. When the view was zoomed, turned, moved or its
// filter changed: the lit icon goes dark, no icon is active, and both the
// lit and the pressed icon are forgotten. True always.
uint8_t __fastcall IconsDoMessage(char* target, void*, void* message)
{
    char* const self = target - icons::kTarget;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type != kMessageViewFilter && type != kMessageViewZR && type != kMessageViewT)
        return 1;
    void* const view = Virtual<void*>(At<void*>(self, icons::kDispatch), 0x58);
    if (const uint32_t hover = At<uint32_t>(self, icons::kHover); hover && view) {
        LightIcon(view, hover, 0);
        ActivateIcon(view, 0, 1);
    }
    At<uint32_t>(self, icons::kHover) = 0;
    At<uint32_t>(self, icons::kPressed) = 0;
    return 1;
}

// The game's 0x004B8620 (slot 20, OnMouseMove, ret 0xC). With no lit icon,
// the icon under the pointer (0 when there is none, all the same) is lit,
// kept and made active with state 3. With one, nothing when the pointer is
// still on it; else it goes dark and is forgotten, and the next move lights
// the new one. False always.
uint8_t __fastcall IconsOnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    void* const dispatch = At<void*>(self, icons::kDispatch);
    if (!dispatch)
        return 0;
    void* const view = Virtual<void*>(dispatch, 0x58);
    if (!view)
        return 0;
    const uint32_t picked = PickIcon(view, x, z);
    const uint32_t hover = At<uint32_t>(self, icons::kHover);
    if (hover) {
        if (hover == picked)
            return 0;
        LightIcon(view, hover, 0);
        At<uint32_t>(self, icons::kHover) = 0;
        return 0;
    }
    LightIcon(view, picked, 1);
    At<uint32_t>(self, icons::kHover) = picked;
    ActivateIcon(view, picked, 3);
    return 0;
}

// The game's 0x004B8690: the constructor; the caller allocated 0x3C bytes.
char* __fastcall IconsConstruct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::SC4MissionIconControl_Vtable;
    At<std::uintptr_t>(self, icons::kTarget) = addr::SC4MissionIconControl_TargetVtable;
    At<void*>(self, icons::kSituations) = nullptr;
    At<void*>(self, icons::kDispatch) = nullptr;
    At<uint32_t>(self, icons::kHover) = 0;
    At<uint32_t>(self, icons::kPressed) = 0;
    At<uint32_t>(self, base::kId) = 0xABEC6CFE;
    At<uint32_t>(self, base::kCursor) = 0x43401C71;
    return self;
}

// The game's 0x004B86F0: the destructor's body. Lets go of the dispatch
// manager, then the situation manager, then the base destructor.
void __fastcall IconsDestruct(char* self, void*)
{
    if (void* const dispatch = At<void*>(self, icons::kDispatch))
        Release(dispatch);
    if (void* const situations = At<void*>(self, icons::kSituations))
        Release(situations);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// The game's 0x004B86D0 (slot 26, ret 4): the deleting destructor.
char* __fastcall IconsDeletingDestruct(char* self, void*, uint32_t flags)
{
    IconsDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x004B8720 (slot 16, OnMouseDownL, ret 0xC). In view level 1,
// the icon under the pointer becomes the active one with state 2 and is
// kept as the pressed one; true then. False (and no pressed icon) when
// there is none; false with the pressed icon untouched outside level 1.
uint8_t __fastcall IconsOnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    if (!ViewLevelIsOne())
        return 0;
    At<uint32_t>(self, icons::kPressed) = 0;
    void* const dispatch = At<void*>(self, icons::kDispatch);
    if (!dispatch)
        return 0;
    void* const view = Virtual<void*>(dispatch, 0x58);
    if (!view)
        return 0;
    const uint32_t picked = PickIcon(view, x, z);
    if (!picked)
        return 0;
    ActivateIcon(view, picked, 2);
    At<uint32_t>(self, icons::kPressed) = picked;
    return 1;
}

// The game's 0x004B8790 (slot 18, OnMouseUpL, ret 0xC). In view level 1, the
// icon under the pointer becomes the active one with state 1; when it is the
// pressed one and the vehicle view has an occupant for the active icon, the
// situation manager's slot 22 (InitiateSituationForOccupant) gets it with a
// null second argument, and the answer is true with the pressed icon kept.
// Everything else forgets the pressed icon and answers false.
uint8_t __fastcall IconsOnMouseUpL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    if (ViewLevelIsOne()) {
        void* const dispatch = At<void*>(self, icons::kDispatch);
        if (dispatch && At<void*>(self, icons::kSituations)) {
            if (void* const view = Virtual<void*>(dispatch, 0x58)) {
                if (const uint32_t picked = PickIcon(view, x, z)) {
                    ActivateIcon(view, picked, 1);
                    if (picked == At<uint32_t>(self, icons::kPressed)) {
                        if (void* const occupant = This<void*>(addr::SC4DispatchVehicleView_ActiveOccupant, view)) {
                            Virtual<void>(At<void*>(self, icons::kSituations), 0x58, occupant, uint32_t{0});
                            return 1;
                        }
                    }
                }
            }
        }
    }
    At<uint32_t>(self, icons::kPressed) = 0;
    return 0;
}

// --- Registration ----------------------------------------------------------------------

bool InstallSmallTools1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4ScrollDragTool_Construct, reinterpret_cast<void*>(&ScrollConstruct)},
        {addr::SC4ScrollDragTool_Shutdown, reinterpret_cast<void*>(&ScrollShutdown)},
        {addr::SC4ScrollDragTool_PutAway, reinterpret_cast<void*>(&ScrollPutAway)},
        {addr::SC4ScrollDragTool_OnMouseUpR, reinterpret_cast<void*>(&ScrollOnMouseUpR)},
        {addr::SC4ScrollDragTool_OnMouseMove, reinterpret_cast<void*>(&ScrollOnMouseMove)},
        {addr::SC4ScrollDragTool_LoadTuning, reinterpret_cast<void*>(&ScrollLoadTuning)},
        {addr::SC4ScrollDragTool_Init, reinterpret_cast<void*>(&ScrollInit)},
        {addr::SC4ScrollDragTool_Start, reinterpret_cast<void*>(&ScrollStart)},
        {addr::SC4MissionIconControl_Init, reinterpret_cast<void*>(&IconsInit)},
        {addr::SC4MissionIconControl_Shutdown, reinterpret_cast<void*>(&IconsShutdown)},
        {addr::SC4MissionIconControl_DoMessage, reinterpret_cast<void*>(&IconsDoMessage)},
        {addr::SC4MissionIconControl_OnMouseMove, reinterpret_cast<void*>(&IconsOnMouseMove)},
        {addr::SC4MissionIconControl_Construct, reinterpret_cast<void*>(&IconsConstruct)},
        {addr::SC4MissionIconControl_DeletingDestructor, reinterpret_cast<void*>(&IconsDeletingDestruct)},
        {addr::SC4MissionIconControl_Destruct, reinterpret_cast<void*>(&IconsDestruct)},
        {addr::SC4MissionIconControl_OnMouseDownL, reinterpret_cast<void*>(&IconsOnMouseDownL)},
        {addr::SC4MissionIconControl_OnMouseUpL, reinterpret_cast<void*>(&IconsOnMouseUpL)},
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
    "ui.tools.small1",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4ScrollDragTool_OnMouseMove,
    "Our drag-to-scroll control and the view's mission icon control (docs/tools.md)",
    false,
    InstallSmallTools1,
});

} // namespace
} // namespace opensc4::tool_small
