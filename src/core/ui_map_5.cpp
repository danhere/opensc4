// The map window's controls, part 5 (0x007A61E0 to 0x007A79B0). Two classes
// meet in this range:
//
//   - the map window (interface vtable 0x00AB8150 at +0, a second one
//     0x00AB8140 at +4, its window part 0x00AB7EE0 at +8): the three
//     interface methods that keep its list of forced map kinds, and the
//     window part's "controls made ready" and "controls hidden" methods;
//   - the mini map window, cSC4WinMiniMap (class 0xCA318388, window vtable
//     0x00AB83B8, its interface 0xCA318385 with vtable 0x00AB838C at +0xD8,
//     a message target 0x00AB837C at +0xDC): its interface's small setters,
//     the mouse handlers that move the camera to the clicked spot, the
//     marks of changed cells, the colour ramps read from an exemplar, the
//     picture buffer's size, the message registrations, the shutdown;
//   - the occupant filter the mini map uses to pick network occupants
//     (vtables 0x00AB8340 and 0x00AB8358).
//
// Replacement, off by default:
//
//   ui.map.part5: the functions above.
//
// The pixel work in this range (0x007A65D0 to 0x007A7080 without the three
// window methods at 0x007A6650 to 0x007A66C0) is drawing and stays the
// game's; so does the map picture's drawing at 0x007A2F60, which the map
// window's refresh (0x007A5610, another part) may reach.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::ui {
namespace {

// The map window, offsets from the object's start.
namespace map {
constexpr std::size_t kWindow = 0x8;     // the window part
constexpr std::size_t kForced = 0x9C8;   // list of forced map kinds (sentinel node)
constexpr std::size_t kKind = 0x9D4;     // the map kind asked for through the interface
constexpr std::size_t kReady = 0x9D8;    // byte: the controls are made ready
constexpr std::size_t kLastMode = 0x9A0; // the mode button last chosen
} // namespace map

// The mini map window, offsets from the window's start.
namespace mini {
constexpr std::size_t kInterface = 0xD8;
constexpr std::size_t kTarget = 0xDC;    // the message target
constexpr std::size_t kFlags = 0xE0;     // bit 2: shows occupants
constexpr std::size_t kSize = 0xE4;      // the picture's side in pixels
constexpr std::size_t kE8 = 0xE8;
constexpr std::size_t kEC = 0xEC;
constexpr std::size_t kHeld = 0xF0;      // a counted object, shut down with the window
constexpr std::size_t kActive = 0xFC;    // byte
constexpr std::size_t kChanged = 0xFD;   // byte
constexpr std::size_t kChanged2 = 0xFE;  // byte
constexpr std::size_t kDragging = 0x100; // byte: the mouse is held down
constexpr std::size_t kListening = 0x101; // byte: registered for occupant messages
constexpr std::size_t kShift = 0x104;    // city cells to picture pixels, as a shift
constexpr std::size_t kPicture = 0x114;  // Picture
constexpr std::size_t kMarks = 0x120;    // 16 rows of 16 bits: changed blocks
constexpr std::size_t kRamp1 = 0x160;    // 0x100 colours
constexpr std::size_t kRamp2 = 0x560;    // 0x100 colours
} // namespace mini

constexpr uint32_t kMiniMapClass = 0xCA318388;
constexpr uint32_t kMiniMapIid = 0xCA318385;
constexpr uint32_t kButtonIid = 0x8810;
constexpr uint32_t kOccupantInserted = 0x99EF1142;
constexpr uint32_t kOccupantRemoved = 0x99EF1143;
constexpr uint32_t kOccupantFilterIid = 0x46A80026;
constexpr uint32_t kNetworkOccupantIid = 0xA821EF94;
constexpr uint32_t kMapControlCount = 82;

struct ListNode {
    ListNode* next;
    ListNode* previous;
    uint32_t value;
};

// The mini map's picture buffer.
struct Picture {
    void* pixels;
    uint32_t width;
    uint32_t height;
};

struct ResourceKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};

struct Vector3 {
    float x;
    float y;
    float z;
};

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

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void ShowChild(void* window, uint32_t id, uint32_t show)
{
    Cdecl<uint8_t>(addr::SC4Win_ShowChild, window, id, show, uint32_t{0});
}

// The map window's refresh (another part's): it works out the map kind on
// show and may run the picture's drawing.
void RefreshMap(char* object)
{
    This<void>(addr::SC4UI_F7A5610, object);
}

// The game's 0x0079E5A0 takes the window in EDI and the first control's ID in
// EBX, and the count and the value on the stack (caller cleans): it sets
// state 2 of `count` controls in a row to `value`.
void* g_setControlRange = reinterpret_cast<void*>(addr::SC4UI_F79E5A0);

__declspec(naked) void __cdecl SetControlRange(void* /*window*/, uint32_t /*first*/, uint32_t /*count*/,
                                               uint32_t /*value*/)
{
    __asm push ebx
    __asm push edi
    __asm mov edi, [esp + 12]
    __asm mov ebx, [esp + 16]
    __asm push dword ptr [esp + 24]
    __asm push dword ptr [esp + 24]
    __asm call dword ptr [g_setControlRange]
    __asm add esp, 8
    __asm pop edi
    __asm pop ebx
    __asm ret
}

// ---------------------------------------------------------------------------
// The map window.
// ---------------------------------------------------------------------------

// The game's 0x007A61E0. Interface slot 4: a map kind put at the front of the
// forced list, then the refresh.
void __fastcall PushForcedKind(char* self, void*, uint32_t kind)
{
    ListNode* const first = At<ListNode*>(self, map::kForced)->next;
    ListNode* const node = Cdecl<ListNode*>(addr::SC4_stl_allocate, uint32_t{0xC});
    node->value = kind;
    ListNode* const before = first->previous;
    node->previous = before;
    node->next = first;
    before->next = node;
    first->previous = node;
    RefreshMap(self);
}

// The game's 0x007A6220. Interface slot 5: the first entry with this map kind
// taken out of the forced list, then the refresh; nothing when it is not there.
void __fastcall RemoveForcedKind(char* self, void*, uint32_t kind)
{
    ListNode* const sentinel = At<ListNode*>(self, map::kForced);
    ListNode* node = sentinel->next;
    while (node != sentinel && node->value != kind)
        node = node->next;
    if (node == At<ListNode*>(self, map::kForced))
        return;
    ListNode* const next = node->next;
    ListNode* const previous = node->previous;
    previous->next = next;
    next->previous = previous;
    Cdecl<void>(addr::SC4_Free, node);
    RefreshMap(self);
}

// The game's 0x007A6270. Interface slot 3.
void __fastcall SetKind(char* self, void*, uint32_t kind)
{
    At<uint32_t>(self, map::kKind) = kind;
    RefreshMap(self);
}

// The game's 0x007A6290. Slot 0x114 of the window part: the frame shown, the
// window made the target of its 82 controls, the first buttons pressed the
// first time, then the refresh.
void __fastcall ReadyControls(char* self, void*)
{
    char* const object = self - map::kWindow;
    void* const parent = Virtual<void*>(self, 0x2C);
    if (!Cdecl<uint8_t>(addr::SC4Win_IsChildVisible, parent, uint32_t{0x8A2871C3}))
        ShowChild(parent, 0x8A2871B1, 1);
    ShowChild(parent, 0x8A2871B2, 1);

    const uint32_t* const ids = reinterpret_cast<const uint32_t*>(addr::SC4UI_MapControlIds);
    for (uint32_t i = 0; i < kMapControlCount; ++i) {
        if (void* const child = Virtual<void*>(parent, 0x8C, ids[i]))
            Virtual<void>(child, 0x158, object ? self : nullptr);
    }
    This<void>(addr::SC4UI_F7A04F0, object, uint32_t{0});

    if (At<uint32_t>(object, map::kLastMode) == 0x63) {
        void* button = nullptr;
        static const uint32_t pressed[] = {0x5001, 0x6001, 0x8001, 0x8100, 0x8003};
        for (const uint32_t id : pressed) {
            void* const owner = Virtual<void*>(self, 0x2C);
            if (void* const held = button) {
                button = nullptr;
                Release(held);
            }
            if (Virtual<uint8_t>(owner, 0x94, id, kButtonIid, &button))
                Virtual<void>(button, 0x18);
        }
        SetControlRange(Virtual<void*>(self, 0x2C), 0x8003, 0xA, 0);
        At<uint32_t>(object, map::kLastMode) = 0x28;
        if (button)
            Release(button);
    }
    At<uint8_t>(object, map::kReady) = 1;
    RefreshMap(object);
}

// The game's 0x007A64D0. Slot 0x118 of the window part: the frame's windows
// hidden, then the refresh; nothing when the frame is not loaded.
void __fastcall HideControls(char* self, void*)
{
    void* const parent = Virtual<void*>(self, 0x2C);
    if (!Virtual<void*>(parent, 0x8C, uint32_t{0x8A2871B1}))
        return;
    static const uint32_t hidden[] = {
        0x4200, 0x8A2871B1, 0x8A2871B2, 0xAA32BCD4, 0xAA32BCD5, 0x8A2871C3, 0x8A2871C4,
    };
    for (const uint32_t id : hidden)
        ShowChild(parent, id, 0);
    char* const object = self - map::kWindow;
    At<uint8_t>(object, map::kReady) = 0;
    RefreshMap(object);
}

// ---------------------------------------------------------------------------
// The mini map window.
// ---------------------------------------------------------------------------

// The game's 0x007A6580.
uint32_t __cdecl MiniMapClassId()
{
    return kMiniMapClass;
}

// The game's 0x007A6590. Whether a window and every window above it answer
// yes at slot 0x11C.
uint8_t __cdecl ShownWithParents(void* window)
{
    while (window) {
        if (!Virtual<uint8_t>(window, 0x11C))
            return 0;
        window = Virtual<void*>(window, 0x2C);
    }
    return 1;
}

// The game's 0x007A6650. A null window is not checked for before the count
// is raised, as in the original.
uint8_t __fastcall MiniQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != kMiniMapIid)
        return This<uint8_t>(addr::SC4Win_QueryInterface, self, iid, out);
    *out = self ? self + mini::kInterface : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x007A6690. Window slot 136: the mouse button let go.
uint8_t __fastcall MiniMouseUp(char* self, void*, int32_t, int32_t, uint32_t)
{
    At<uint8_t>(self, mini::kDragging) = 0;
    This<void>(addr::SC4Win_F99BE1E, self);
    return 1;
}

// The game's 0x007A66B0. Window slots 141 and 142.
uint8_t __fastcall MiniStopDragging(char* self, void*, uint32_t)
{
    At<uint8_t>(self, mini::kDragging) = 0;
    return 1;
}

void MarkAll(char* self)
{
    Cdecl<void>(addr::SC4_FillUint32, self + mini::kMarks, uint32_t{0xFFFFFFFF}, uint32_t{0x10});
    At<uint8_t>(self, mini::kChanged) = 1;
    At<uint8_t>(self, mini::kChanged2) = 1;
}

// The game's 0x007A66C0. Every block marked as changed.
void __fastcall MiniMarkAll(char* self, void*)
{
    MarkAll(self);
}

// The game's 0x007A7080. Interface slot 4: the counted source object.
void __fastcall MiniSetSource(char* self, void*, void* source)
{
    void* const old = At<void*>(self, 0x1C);
    if (source != old) {
        if (source)
            Virtual<uint32_t>(source, 0x4);
        At<void*>(self, 0x1C) = source;
        if (old)
            Release(old);
    }
    At<uint8_t>(self, 0x26) = 1;
}

// The game's 0x007A70C0. Interface slot 6: every colour cleared.
void __fastcall MiniClearColors(char* self, void*)
{
    uint32_t* const end = At<uint32_t*>(self, 0x34);
    for (uint32_t* color = At<uint32_t*>(self, 0x30); color != end; ++color)
        *color = 0;
}

// The game's 0x007A70E0. Interface slot 5: every colour set, opaque, and the
// first one cleared (written even when the table is empty).
void __fastcall MiniFillColors(char* self, void*, uint32_t color)
{
    uint32_t* const end = At<uint32_t*>(self, 0x34);
    color |= 0xFF000000;
    for (uint32_t* entry = At<uint32_t*>(self, 0x30); entry != end; ++entry)
        *entry = color;
    *At<uint32_t*>(self, 0x30) = 0;
}

// The game's 0x007A7120. Interface slot 7: one of the sixteen colours; zero
// stays clear, any other colour is made opaque.
void __fastcall MiniSetColor(char* self, void*, uint32_t index, uint32_t color)
{
    if (index >= 0x10)
        return;
    if (color)
        color |= 0xFF000000;
    At<uint32_t*>(self, 0x30)[index] = color;
}

// The game's 0x007A7140. Registers for the occupant messages once.
void __fastcall MiniListen(char* self, void*)
{
    if (At<uint8_t>(self, mini::kListening) || !At<uint8_t>(self, mini::kActive))
        return;
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (At<uint8_t>(self, mini::kFlags) & 2) {
        void* const target = self + mini::kTarget;
        Virtual<uint8_t>(server, 0x14, target, kOccupantInserted);
        Virtual<uint8_t>(server, 0x14, target, kOccupantRemoved);
    }
    At<uint8_t>(self, mini::kListening) = 1;
}

// The game's 0x007A71A0, also inlined in 0x007A7770 with the interface part
// as `this`: `target` is the message target, `flags` and `listening` the two
// fields.
void StopListening(void* target, uint32_t flags, uint8_t& listening)
{
    if (!listening)
        return;
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (flags & 2) {
        Virtual<uint8_t>(server, 0x18, target, kOccupantRemoved);
        Virtual<uint8_t>(server, 0x18, target, kOccupantInserted);
    }
    listening = 0;
}

void __fastcall MiniStopListening(char* self, void*)
{
    StopListening(self + mini::kTarget, At<uint8_t>(self, mini::kFlags), At<uint8_t>(self, mini::kListening));
}

// The game's 0x007A71F0. The blocks of 16 pixels a rectangle touches marked
// as changed; right and bottom are cut at block 15, left and top are not.
uint8_t __fastcall MiniMarkRect(char* self, void*, const int32_t* rect)
{
    const int32_t left = rect[0] >> 4;
    const int32_t top = rect[1] >> 4;
    int32_t right = rect[2] >> 4;
    int32_t bottom = rect[3] >> 4;
    if (right > 0xF)
        right = 0xF;
    if (bottom > 0xF)
        bottom = 0xF;
    uint32_t* const rows = &At<uint32_t>(self, mini::kMarks);
    for (int32_t y = top; y <= bottom; ++y) {
        for (int32_t x = left; x <= right; ++x) {
            rows[(x >> 5) + y] |= uint32_t{1} << (static_cast<uint32_t>(x) & 0x1F);
        }
    }
    At<uint8_t>(self, mini::kChanged) = 1;
    At<uint8_t>(self, mini::kChanged2) = 1;
    return 1;
}

// The game's 0x007A72B0. The camera centred on the spot of the city under a
// point of the mini map. The second coordinate's fraction is compared with
// one before it is rounded to a float when the view is not turned a quarter.
void __fastcall MiniCenterCamera(char* self, void*, int32_t x, int32_t y)
{
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    void* const city = Global<void*>(addr::SC4_City);
    void* const camera = Virtual<void*>(Global<void*>(addr::SC4_Lighting43DD0), 0x40);
    const uint32_t rotation = At<uint32_t>(camera, 0x10C);

    const float fx = static_cast<float>(x);
    const int32_t width = Virtual<int32_t>(self, 0xA4);
    const float u = static_cast<float>(static_cast<double>(fx) / static_cast<double>(width) - 0.5);
    const float fy = static_cast<float>(y);
    const int32_t height = Virtual<int32_t>(self, 0xA8);
    const double v = static_cast<double>(fy) / static_cast<double>(height) - 0.5;

    double first;
    double second;
    if (rotation & 1) {
        first = -static_cast<double>(static_cast<float>(v));
        second = static_cast<double>(u);
    } else {
        first = static_cast<double>(u);
        second = v;
    }
    if (rotation & 2) {
        first = -first;
        second = -second;
    }

    const float one = 1.0f;
    const float zero = 0.0f;

    const double firstSum = first + 0.5;
    const float firstRounded = static_cast<float>(firstSum);
    const float* pick = firstSum < 1.0 ? &firstRounded : &one;
    if (!(0.0f < *pick))
        pick = &zero;
    const float across = *pick;

    const double secondSum = second + 0.5;
    const float secondRounded = static_cast<float>(secondSum);
    pick = secondSum < 1.0 ? &secondRounded : &one;
    if (!(0.0f < *pick))
        pick = &zero;
    const float along = *pick;

    Vector3 spot;
    spot.x = spot.y = spot.z = Global<float>(addr::SC4UI_FloatB4E1BC);
    spot.x = static_cast<float>(Virtual<double>(city, 0x164) * static_cast<double>(across));
    spot.z = static_cast<float>(Virtual<double>(city, 0x168) * static_cast<double>(along));
    spot.y = static_cast<float>(Virtual<double>(terrain, 0x34, spot.x, spot.z));
    This<void>(addr::cSC4CameraControl_SetCenter, Virtual<void*>(Global<void*>(addr::SC4_Lighting43DD0), 0x40),
               &spot);
}

// One colour ramp read from a property of the exemplar.
bool LoadRamp(void* properties, uint32_t property, uint32_t* ramp)
{
    void* const found = Virtual<void*>(properties, 0x24, property);
    if (!found)
        return false;
    void* const value = Virtual<void*>(found, 0x14);
    const uint32_t* const values = Virtual<const uint32_t*>(value, 0x19C);
    const uint32_t count = Virtual<uint32_t>(value, 0x10);
    Cdecl<void>(addr::SC4UI_F78D520, ramp, int32_t{0x100}, values, count);
    return true;
}

// The game's 0x007A7450. The two colour ramps read from the exemplar
// 0x6534284A / 0x690F693F / 0x8A356F2C (the first with its alpha cleared),
// then every block marked as changed.
void __fastcall MiniLoadRamps(char* self, void*)
{
    ResourceKey key;
    key.type = 0x6534284A;
    key.group = 0x690F693F;
    key.instance = 0x8A356F2C;
    void* exemplar; // not set before the call, as in the original
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, &key, uint32_t{0xA52160F5}, &exemplar,
                         uint32_t{0}, uint32_t{0})) {
        void* const properties = Virtual<void*>(exemplar, 0x1C);
        if (LoadRamp(properties, 0xEA3573AF, &At<uint32_t>(self, mini::kRamp1))) {
            for (std::size_t i = 0; i < 0x100; ++i)
                At<uint8_t>(self, mini::kRamp1 + 3 + i * 4) = 0;
        }
        LoadRamp(properties, 0x6B733233, &At<uint32_t>(self, mini::kRamp2));
        Release(exemplar);
    }
    MarkAll(self);
}

// The game's 0x007A7570. `this` is the picture buffer: kept when it has the
// size asked for, else freed and allocated anew.
uint8_t __fastcall PictureResize(Picture* self, void*, uint32_t width, uint32_t height)
{
    if (self->pixels && self->width == width && self->height == height)
        return 1;
    Cdecl<void>(addr::SC4_operator_delete, self->pixels);
    self->width = width;
    self->pixels = nullptr;
    self->height = height;
    void* const pixels = Cdecl<void*>(addr::SC4_operator_new, width * height * 4);
    self->pixels = pixels;
    return pixels != nullptr;
}

// The game's 0x007A75D0. Window slot 5: the registrations dropped, the held
// object shut down and let go, then the window's own shutdown.
uint8_t __fastcall MiniShutdown(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self)) {
        void* const server = Global<void*>(addr::SC4_MessageServer);
        MiniStopListening(self, nullptr);
        static const uint32_t messages[] = {
            0xC53D10AA, 0x26D31EC2, 0x26D31EC1, 0xA6B79602, 0xA6B79612,
            0x07699455, 0x278E6003, 0x678E6E3A, 0x89171496,
        };
        for (const uint32_t message : messages)
            Virtual<uint8_t>(server, 0x18, self ? self + mini::kTarget : nullptr, message);
        if (void* const held = At<void*>(self, mini::kHeld)) {
            Virtual<void>(held, 0x10);
            if (void* const again = At<void*>(self, mini::kHeld)) {
                At<void*>(self, mini::kHeld) = nullptr;
                Release(again);
            }
        }
        This<uint8_t>(addr::SC4Win_F99D2FE, self);
    }
    return 1;
}

// The game's 0x007A7720. Window slot 134: the mouse button pressed.
uint8_t __fastcall MiniMouseDown(char* self, void*, int32_t x, int32_t y, uint32_t)
{
    MiniCenterCamera(self, nullptr, x, y);
    At<uint8_t>(self, mini::kDragging) = 1;
    This<void>(addr::SC4Win_F99BDFE, self);
    return 1;
}

// The game's 0x007A7750. Window slot 138: the mouse moved.
uint8_t __fastcall MiniMouseMove(char* self, void*, int32_t x, int32_t y, uint32_t)
{
    if (At<uint8_t>(self, mini::kDragging))
        MiniCenterCamera(self, nullptr, x, y);
    return 1;
}

// The game's 0x007A7770. Interface slot 8: the flags under `mask` cleared and
// `values` toggled in; `this` is the interface part.
uint32_t __fastcall MiniSetFlags(char* self, void*, uint32_t mask, uint32_t values)
{
    StopListening(self + 0x4, At<uint8_t>(self, 0x8), At<uint8_t>(self, 0x29));
    At<uint32_t>(self, 0x8) = (~mask & At<uint32_t>(self, 0x8)) ^ values;
    Cdecl<void>(addr::SC4_FillUint32, self + 0x48, uint32_t{0xFFFFFFFF}, uint32_t{0x10});
    At<uint8_t>(self, 0x25) = 1;
    At<uint8_t>(self, 0x26) = 1;
    return At<uint32_t>(self, 0x8);
}

// The game's 0x007A77E0. The mini map taken out of use: registrations and
// the picture buffer dropped.
void __fastcall MiniDeactivate(char* self, void*)
{
    if (!At<uint8_t>(self, mini::kActive))
        return;
    MiniStopListening(self, nullptr);
    void* const service = Cdecl<void*>(addr::SC4_GetService86AD10EC);
    Virtual<uint8_t>(service, 0x18, self + mini::kTarget, uint32_t{0});
    Cdecl<void>(addr::SC4_operator_delete, At<void*>(self, mini::kPicture));
    At<void*>(self, mini::kPicture) = nullptr;
    At<uint8_t>(self, mini::kActive) = 0;
    At<uint32_t>(self, mini::kE8) = 0;
    At<uint32_t>(self, mini::kEC) = 0;
}

// The game's 0x007A7840. The picture buffer sized and the shift between the
// city's width in cells and the picture's side worked out.
void __fastcall MiniFitCity(char* self, void*)
{
    void* const city = Global<void*>(addr::SC4_City);
    if (!city)
        return;
    int32_t width = Virtual<int32_t>(city, 0x174);
    const int32_t depth = Virtual<int32_t>(city, 0x178);
    if (width <= 0 || depth <= 0)
        return;
    const uint32_t side = At<uint32_t>(self, mini::kSize);
    PictureResize(&At<Picture>(self, mini::kPicture), nullptr, side, side);
    At<int32_t>(self, mini::kShift) = 0;
    if (static_cast<uint32_t>(width) > At<uint32_t>(self, mini::kSize)) {
        do {
            width >>= 1;
            ++At<int32_t>(self, mini::kShift);
        } while (static_cast<uint32_t>(width) > At<uint32_t>(self, mini::kSize));
    } else if (static_cast<uint32_t>(width) < At<uint32_t>(self, mini::kSize)) {
        do {
            width <<= 1;
            --At<int32_t>(self, mini::kShift);
        } while (static_cast<uint32_t>(width) < At<uint32_t>(self, mini::kSize));
    }
    MarkAll(self);
}

// ---------------------------------------------------------------------------
// The mini map's occupant filter.
// ---------------------------------------------------------------------------

// The game's 0x007A7900.
char* __fastcall FilterConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_OccupantFilterBaseVtable;
    This<void*>(addr::cRZBaseRefCount_Construct, self + 0x4);
    At<std::uintptr_t>(self, 0x4) = addr::SC4UI_MiniMapFilterCountVtable;
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_MiniMapFilterVtable;
    return self;
}

// The game's 0x007A7930.
uint8_t __fastcall FilterQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != 1 && iid != kOccupantFilterIid)
        return 0;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x007A7970. Whether an occupant is a network occupant with any
// of the kinds in 0x1F4F. The occupant is used after its count is dropped,
// as in the original.
uint32_t __fastcall FilterAccepts(char*, void*, void* occupant)
{
    void* network = occupant;
    if (!Virtual<uint8_t>(occupant, 0x0, kNetworkOccupantIid, &network))
        return 0;
    Release(network);
    return (Virtual<uint32_t>(network, 0x30) & 0x1F4F) != 0;
}

bool InstallMap5(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[30] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7A61E0, reinterpret_cast<void*>(&PushForcedKind)},
        {addr::SC4UI_F7A6220, reinterpret_cast<void*>(&RemoveForcedKind)},
        {addr::SC4UI_F7A6270, reinterpret_cast<void*>(&SetKind)},
        {addr::SC4UI_F7A6290, reinterpret_cast<void*>(&ReadyControls)},
        {addr::SC4UI_F7A64D0, reinterpret_cast<void*>(&HideControls)},
        {addr::SC4UI_F7A6580, reinterpret_cast<void*>(&MiniMapClassId)},
        {addr::SC4UI_F7A6590, reinterpret_cast<void*>(&ShownWithParents)},
        {addr::SC4UI_F7A6650, reinterpret_cast<void*>(&MiniQueryInterface)},
        {addr::SC4UI_F7A6690, reinterpret_cast<void*>(&MiniMouseUp)},
        {addr::SC4UI_F7A66B0, reinterpret_cast<void*>(&MiniStopDragging)},
        {addr::SC4UI_F7A66C0, reinterpret_cast<void*>(&MiniMarkAll)},
        {addr::SC4UI_F7A7080, reinterpret_cast<void*>(&MiniSetSource)},
        {addr::SC4UI_F7A70C0, reinterpret_cast<void*>(&MiniClearColors)},
        {addr::SC4UI_F7A70E0, reinterpret_cast<void*>(&MiniFillColors)},
        {addr::SC4UI_F7A7120, reinterpret_cast<void*>(&MiniSetColor)},
        {addr::SC4UI_F7A7140, reinterpret_cast<void*>(&MiniListen)},
        {addr::SC4UI_F7A71A0, reinterpret_cast<void*>(&MiniStopListening)},
        {addr::SC4UI_F7A71F0, reinterpret_cast<void*>(&MiniMarkRect)},
        {addr::SC4UI_F7A72B0, reinterpret_cast<void*>(&MiniCenterCamera)},
        {addr::SC4UI_F7A7450, reinterpret_cast<void*>(&MiniLoadRamps)},
        {addr::SC4UI_F7A7570, reinterpret_cast<void*>(&PictureResize)},
        {addr::SC4UI_F7A75D0, reinterpret_cast<void*>(&MiniShutdown)},
        {addr::SC4UI_F7A7720, reinterpret_cast<void*>(&MiniMouseDown)},
        {addr::SC4UI_F7A7750, reinterpret_cast<void*>(&MiniMouseMove)},
        {addr::SC4UI_F7A7770, reinterpret_cast<void*>(&MiniSetFlags)},
        {addr::SC4UI_F7A77E0, reinterpret_cast<void*>(&MiniDeactivate)},
        {addr::SC4UI_F7A7840, reinterpret_cast<void*>(&MiniFitCity)},
        {addr::SC4UI_F7A7900, reinterpret_cast<void*>(&FilterConstruct)},
        {addr::SC4UI_F7A7930, reinterpret_cast<void*>(&FilterQueryInterface)},
        {addr::SC4UI_F7A7970, reinterpret_cast<void*>(&FilterAccepts)},
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

const bool g_map5Registered = registry::Register({
    "ui.map.part5",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7A61E0,
    "Our map window list and control methods, and the mini map window's setters, mouse handling, marks, colour "
    "ramps and shutdown (docs/ui.md)",
    false,
    InstallMap5,
});

} // namespace
} // namespace opensc4::ui
