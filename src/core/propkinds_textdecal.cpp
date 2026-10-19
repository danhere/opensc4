// Our cSC4PropOccupantTextDecal (build 641; the class map, the vtables and
// the fields are in propkinds.h): the label, a prop that is a line of text
// on the ground. It keeps the text, a size, a colour, an opacity, a place
// and a direction; while the prop is shown, a texture of the text (from the
// service at 0x00B43CE4) is laid on the ground by two of the terrain view's
// overlay managers (0 and 1, or 2 and 3 while the label is being moved).
// The label tool (src/core/tool_decal.cpp) and the prop manager's TextDecal
// command (src/core/propmgr_2.cpp) make them. The prop underneath
// (cSC4PropOccupant) is called at its addresses.
//
// Nothing here draws: the overlay managers (the Renderer's) are told
// through their interface what to show.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`, or, where the callee is behind a service the code does not name,
// from the caller's stack (it reads its own frame at fixed offsets after
// the call, so the callee took what was pushed):
//   - the object's primary vtable 0x00AA5680: 0x04 and 0x08 none;
//   - the prop part 0x00AA5620: 0x20 SetPropType one (0x005EF4C0);
//   - the occupant part 0x00AA5588: 0x14 IsInitialized none (0x00458A40),
//     0x74 SetFlag one (0x005E8B70), 0x7C ClearFlag one (0x005E8BA0), 0x80
//     IsFlagSet one (0x005E8BC0);
//   - the overlay manager 0x00AB39E8 (what the terrain view's slot 0x48
//     hands out; all four are made by 0x0073BC20): 0x04, 0x08 none, 0x0C
//     four (0x0073C0B0), 0x30 two (0x0073AD80), 0x3C one (0x007388C0), 0x44
//     two (0x00737F00), 0x48 two (0x00738010);
//   - the terrain view 0x00AB4410: 0x48 one (0x007558A0);
//   - the terrain: 0x34 two (the height at x and z);
//   - the renderer 0x00B43DD0: 0x28 none (the camera; its rotation at
//     +0x18);
//   - the occupant manager 0x00B43D0C: 0x60 two;
//   - the city: 0x17C four;
//   - the message server: 0x14 and 0x18 two;
//   - the service 0x00B43DC4: 0x48 one (the visibility filter, referenced);
//     the filter: 0x08 none, 0x0C one;
//   - the service 0x00B43DB8: 0x28 none (whether signs and labels are
//     shown);
//   - the service 0x00B43CE4: 0x14 three (from the caller's stack); the
//     texture: 0x08 none, 0x0C one (from the caller's stack: two dwords,
//     the width and height in pixels);
//   - cGZMessage 0x00ACEC00: 0x10 none (+0xC), 0x3C none (+0x14);
//   - the string 0x00A80810: 0x1C none (0x0090E4B6), 0x34 one (0x0090E230);
//   - the streams 0x00AA2310 (out) and 0x00AA2280 (in): 0x14, 0x1C, 0x24,
//     0x30, 0x3C one, 0x48 none.
// Direct callees: 0x005E89B0 (ret), 0x005EC960 (ret 8), 0x005ECE90 and
// 0x005EF360 (ret 4), 0x005ECFB0 (ret 4), 0x005EDE00 (ret 4), 0x005EE160
// (ret), 0x005EE050 (ret), 0x005E8C90 (ret), 0x005E8B90 (ret 4), 0x005E8C40
// (ret), 0x0090CF9A (ret), 0x0090D031 and 0x0090CFF9 (ret 4); cdecl:
// 0x00602160 (none), 0x0090CF54, 0x0090CF63 (one), 0x005DA520, 0x005DA580,
// 0x005DA4F0, 0x005DA550 (two).
//
// Callers from outside the class: the class registration (0x00460856,
// which calls GetGZCLSID and keeps the factory's address, before any
// replacement is installed), the prop manager's TextDecal command
// 0x004A363F / 0x004A364D and the label tool's 0x004BD971 / 0x004BD97F
// (allocator, then constructor: each reads only EAX after either call and
// keeps nothing in ECX or EDX across them). The functions that are not
// virtual are called from the class's own functions only. Everything else
// is reached through the vtables and their thunks. The detours are plain
// functions.
#include "core/propkinds.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::propkinds {
namespace {

namespace text = textdecal;

constexpr uint32_t kNone = 0xFFFFFFFF;
constexpr uint32_t kMessageTick = 0xA6B79602;    // the overlays made again
constexpr uint32_t kMessageFilter = 0x06CDB65B;  // a new visibility filter: data 0 is the filter
constexpr uint32_t kMessageCityInit = 0x26D31EC1; // the overlay managers picked again
constexpr uint32_t kTextureKind = 0xCC1FD62C;    // what the service 0x00B43CE4 is asked for
constexpr float kSizeScale = 16.0f;              // 0x00A8D45C
constexpr float kHalf = 0.5f;                    // 0x00A84D2C
constexpr float kHalfSizeScale = 8.0f;           // 0x00A8E178, also SetSize's upper bound
constexpr float kLift = 0.1f;                    // 0x00A8C950: the box's half height
constexpr float kColourEpsilon = 0.0001f;        // 0x38D1B717 at 0x00AA54A0
constexpr std::uintptr_t kPool = addr::cSC4PropOccupantTextDecal_Pool;
constexpr uint32_t kPoolReopenAt = 0x40000041;

uint32_t Bits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// The message target part for the message server (null for a null object,
// as the game computes it).
void* Target(char* self)
{
    return self ? self + layout::kMessageTarget : nullptr;
}

// A reference kept at `field` replaced by `made`, as the game inlines it:
// the new one referenced, stored, the old released; nothing when they are
// the same.
void Keep(char* self, std::size_t field, void* made)
{
    void* const old = At<void*>(self, field);
    if (made == old)
        return;
    if (made)
        Virtual<uint32_t>(made, 0x4);
    At<void*>(self, field) = made;
    if (old)
        Virtual<uint32_t>(old, 0x8);
}

// Whether the city shows signs and labels.
bool SignsShown()
{
    return Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DB8), 0x28) != 0;
}

// The overlay managers told to drop the text (their slot 15), each when it
// has an ID; the managers are not tested for null.
void DropOverlays(char* self)
{
    const uint32_t id = At<uint32_t>(self, text::kOverlayId);
    if (id != kNone) {
        Virtual<void>(At<void*>(self, text::kOverlays), 0x3C, id);
        At<uint32_t>(self, text::kOverlayId) = kNone;
    }
    const uint32_t id2 = At<uint32_t>(self, text::kOverlayId2);
    if (id2 != kNone) {
        Virtual<void>(At<void*>(self, text::kOverlays2), 0x3C, id2);
        At<uint32_t>(self, text::kOverlayId2) = kNone;
    }
}

// ====================================================================================
// Not virtual (`self` is the object)
// ====================================================================================

// 0x005F77C0: the text taken off both overlay managers.
void __fastcall RemoveOverlays(char* self, void*)
{
    static bool logged = false;
    FirstCall("textdecal RemoveOverlays", logged);
    DropOverlays(self);
}

// 0x005F7810: the text taken off both overlay managers and, when there is a
// texture and the prop is shown (base flag 2), laid down again. The width
// is the texture's width over its height (unsigned, on the FPU) times the
// size times 16; the box is centred on the place, half the width one way
// and the size times 8 the other (swapped for direction 0), and from 0.1
// below to 0.1 above the ground's height there. Each manager gets the text
// (its slot 3: the texture, the place, the width, and the turn from the
// table at 0x00B0A978 by the camera's rotation and the direction), and,
// with an ID, the colour (slot 18) and the opacity (slot 17). Then the
// occupant manager is told the box changed (its slot 24 with 0).
void __fastcall MakeOverlays(char* self, void*)
{
    static bool logged = false;
    FirstCall("textdecal MakeOverlays", logged);
    DropOverlays(self);
    // The texture is read again before each call that takes it, as the game
    // does.
    if (!At<void*>(self, text::kTexture))
        return;
    char* const occupant = self + layout::kOccupant;
    if (!Virtual<uint8_t>(occupant, 0x80, 2u))
        return;
    uint32_t pixels[2] = {0, 0};
    Virtual<void>(At<void*>(self, text::kTexture), 0xC, static_cast<uint32_t*>(pixels));
    const double aspect = static_cast<double>(pixels[0]) / static_cast<double>(pixels[1]);
    const double width = aspect * static_cast<double>(At<float>(self, text::kSize)) * static_cast<double>(kSizeScale);
    At<float>(self, text::kWidth) = static_cast<float>(width);
    const float halfWidth = static_cast<float>(width * static_cast<double>(kHalf));
    const float halfHeight = static_cast<float>(static_cast<double>(At<float>(self, text::kSize)) * static_cast<double>(kHalfSizeScale));
    const float x = At<float>(self, text::kPosition);
    const float z = At<float>(self, text::kPosition + 4);
    const float ground = Virtual<float>(Global<void*>(addr::SC4_Terrain), 0x34, x, z);
    // Across z and across x: swapped for direction 0.
    float acrossZ = halfHeight, acrossX = halfWidth;
    const int32_t direction = At<int32_t>(self, text::kDirection);
    if (direction == 0) {
        acrossZ = halfWidth;
        acrossX = halfHeight;
    }
    void* const camera = Virtual<void*>(Global<void*>(addr::SC4_View43DD0), 0x28);
    const int32_t rotation = At<int32_t>(camera, 0x18);
    const uint32_t turn =
        Global<uint32_t>(addr::cSC4PropOccupantTextDecal_Turns + static_cast<std::uintptr_t>(4 * (direction + 4 * rotation)));
    const double px = static_cast<double>(At<float>(self, text::kPosition));
    const double pz = static_cast<double>(At<float>(self, text::kPosition + 4));
    At<float>(self, layout::kMin) = static_cast<float>(px - static_cast<double>(acrossX));
    At<float>(self, layout::kMin + 4) = static_cast<float>(static_cast<double>(ground) - static_cast<double>(kLift));
    At<float>(self, layout::kMin + 8) = static_cast<float>(pz - static_cast<double>(acrossZ));
    At<float>(self, layout::kMax) = static_cast<float>(static_cast<double>(acrossX) + px);
    At<float>(self, layout::kMax + 4) = static_cast<float>(static_cast<double>(ground) + static_cast<double>(kLift));
    At<float>(self, layout::kMax + 8) = static_cast<float>(static_cast<double>(acrossZ) + pz);
    const std::size_t managers[2][2] = {{text::kOverlays, text::kOverlayId}, {text::kOverlays2, text::kOverlayId2}};
    for (const auto& pair : managers) {
        const uint32_t id = Virtual<uint32_t>(At<void*>(self, pair[0]), 0x0C, At<void*>(self, text::kTexture),
                                              static_cast<void*>(self + text::kPosition),
                                              At<uint32_t>(self, text::kWidth), turn);
        At<uint32_t>(self, pair[1]) = id;
        if (id == kNone)
            continue;
        Virtual<void>(At<void*>(self, pair[0]), 0x48, id, static_cast<void*>(self + text::kColour));
        Virtual<void>(At<void*>(self, pair[0]), 0x44, At<uint32_t>(self, pair[1]), At<uint32_t>(self, text::kOpacity));
    }
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0x60, static_cast<void*>(occupant), 0u);
}

// 0x005F7A50: while the label is being moved, each overlay with an ID is
// moved to the place (slot 12). Nothing otherwise.
void __fastcall MoveOverlays(char* self, void*)
{
    static bool logged = false;
    FirstCall("textdecal MoveOverlays", logged);
    if (!At<uint8_t>(self, text::kMoving))
        return;
    if (const uint32_t id = At<uint32_t>(self, text::kOverlayId); id != kNone)
        Virtual<void>(At<void*>(self, text::kOverlays), 0x30, id, static_cast<void*>(self + text::kPosition));
    if (const uint32_t id = At<uint32_t>(self, text::kOverlayId2); id != kNone)
        Virtual<void>(At<void*>(self, text::kOverlays2), 0x30, id, static_cast<void*>(self + text::kPosition));
}

// 0x005F7C80: the text taken off the managers, the managers picked again
// from the terrain view (through 0x00602160, not tested for null): the
// second is manager 3 while moving, else 1; the first 2, else 0. Each is
// kept with a reference. Then the overlays are made again.
void __fastcall PickOverlayManagers(char* self, void*)
{
    static bool logged = false;
    FirstCall("textdecal PickOverlayManagers", logged);
    DropOverlays(self);
    void* const view = Cdecl<void*>(addr::SC4_TerrainPicker);
    Keep(self, text::kOverlays2, Virtual<void*>(view, 0x48, At<uint8_t>(self, text::kMoving) ? 3u : 1u));
    void* const again = Cdecl<void*>(addr::SC4_TerrainPicker);
    Keep(self, text::kOverlays, Virtual<void*>(again, 0x48, At<uint8_t>(self, text::kMoving) ? 2u : 0u));
    MakeOverlays(self, nullptr);
}

// 0x005F8190: the texture let go, a new one asked of the service at
// 0x00B43CE4 (not tested for null) for the text; with one, the overlays
// made again.
void __fastcall MakeText(char* self, void*)
{
    static bool logged = false;
    FirstCall("textdecal MakeText", logged);
    // The game lets go of it twice over (the second finds null).
    for (int i = 0; i < 2; ++i)
        if (void* const old = At<void*>(self, text::kTexture)) {
            At<void*>(self, text::kTexture) = nullptr;
            Virtual<uint32_t>(old, 0x8);
        }
    Virtual<void>(Global<void*>(addr::SC4_Service43CE4), 0x14, kTextureKind, static_cast<void*>(self + text::kText),
                  static_cast<void*>(self + text::kTexture));
    if (At<void*>(self, text::kTexture))
        MakeOverlays(self, nullptr);
}

// ====================================================================================
// The label's own interface (`self` is the object)
// ====================================================================================

// Slot 0 (0x005F7650): the label interface 0x2B3B7D86 gives the object
// with the base's AddRef on the object + 0xC; everything else is the
// prop's (object + 4). The class ID is not answered here. AL only.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("textdecal QueryInterface", logged);
    if (iid == text::kIid) {
        *object = self;
        This<uint32_t>(addr::cSC4Occupant_AddRef, self + layout::kOccupant);
        return 1;
    }
    return This<uint8_t, uint32_t, void**>(addr::cSC4PropOccupant_QueryInterface, self + layout::kProp, iid, object);
}

// Slot 4 (0x005F82E0): the text assigned from the argument (the string's
// slot 13), then the texture made again. No return value.
void __fastcall SetText(char* self, void*, void* source)
{
    static bool logged = false;
    FirstCall("textdecal SetText", logged);
    Virtual<void*>(self + text::kText, 0x34, source);
    MakeText(self, nullptr);
}

// Slot 5 (0x005F8410): the argument assigned from the text (its slot 13, a
// tail jump); what that answers.
void* __fastcall GetText(char* self, void*, void* target)
{
    static bool logged = false;
    FirstCall("textdecal GetText", logged);
    return Virtual<void*>(target, 0x34, static_cast<void*>(self + text::kText));
}

// Slot 6 (0x005F7B10): a size above 0 and below 8 is kept and the overlays
// made again; anything else (not a number too) is ignored. The size is not
// compared with the one kept.
void __fastcall SetSize(char* self, void*, float size)
{
    static bool logged = false;
    FirstCall("textdecal SetSize", logged);
    if (!(size > 0.0f) || !(size < kHalfSizeScale))
        return;
    At<float>(self, text::kSize) = size;
    MakeOverlays(self, nullptr);
}

// Slot 7 (0x005F8170): another value of the moving byte is kept and the
// overlay managers picked again.
void __fastcall SetMoving(char* self, void*, uint32_t moving)
{
    static bool logged = false;
    FirstCall("textdecal SetMoving", logged);
    if (At<uint8_t>(self, text::kMoving) == static_cast<uint8_t>(moving))
        return;
    At<uint8_t>(self, text::kMoving) = static_cast<uint8_t>(moving);
    PickOverlayManagers(self, nullptr);
}

// Slot 8 (0x005F7B50): nothing when each part differs from the one kept by
// less than 0.0001 (on the FPU). Else the colour is kept; while moving each
// overlay with an ID gets it (slot 18), else the overlays are made again.
void __fastcall SetColour(char* self, void*, float red, float green, float blue)
{
    static bool logged = false;
    FirstCall("textdecal SetColour", logged);
    const auto near = [self](float value, std::size_t at) {
        return std::fabs(static_cast<double>(value) - static_cast<double>(At<float>(self, text::kColour + at))) <
               static_cast<double>(kColourEpsilon);
    };
    if (near(red, 0) && near(green, 4) && near(blue, 8))
        return;
    At<float>(self, text::kColour) = red;
    At<float>(self, text::kColour + 4) = green;
    At<float>(self, text::kColour + 8) = blue;
    if (!At<uint8_t>(self, text::kMoving)) {
        MakeOverlays(self, nullptr);
        return;
    }
    if (const uint32_t id = At<uint32_t>(self, text::kOverlayId); id != kNone)
        Virtual<void>(At<void*>(self, text::kOverlays), 0x48, id, static_cast<void*>(self + text::kColour));
    if (const uint32_t id = At<uint32_t>(self, text::kOverlayId2); id != kNone)
        Virtual<void>(At<void*>(self, text::kOverlays2), 0x48, id, static_cast<void*>(self + text::kColour));
}

// Slot 9 (0x005F7D60): another direction is kept and the overlays made
// again.
void __fastcall SetDirection(char* self, void*, int32_t direction)
{
    static bool logged = false;
    FirstCall("textdecal SetDirection", logged);
    if (direction == At<int32_t>(self, text::kDirection))
        return;
    At<int32_t>(self, text::kDirection) = direction;
    MakeOverlays(self, nullptr);
}

// Slot 10 (0x005F7C00): nothing for the opacity kept (a value that is not
// a number always counts as new). Else it is kept; while moving each
// overlay with an ID gets it (slot 17), else the overlays are made again.
void __fastcall SetOpacity(char* self, void*, float opacity)
{
    static bool logged = false;
    FirstCall("textdecal SetOpacity", logged);
    if (At<float>(self, text::kOpacity) == opacity)
        return;
    At<float>(self, text::kOpacity) = opacity;
    if (!At<uint8_t>(self, text::kMoving)) {
        MakeOverlays(self, nullptr);
        return;
    }
    if (const uint32_t id = At<uint32_t>(self, text::kOverlayId); id != kNone)
        Virtual<void>(At<void*>(self, text::kOverlays), 0x44, id, Bits(opacity));
    if (const uint32_t id = At<uint32_t>(self, text::kOverlayId2); id != kNone)
        Virtual<void>(At<void*>(self, text::kOverlays2), 0x44, id, At<uint32_t>(self, text::kOpacity));
}

// ====================================================================================
// The message target (`target` is the object + 8)
// ====================================================================================

// Slot 3 (0x005F8030). 0xA6B79602: the overlays made again. 0x26D31EC1:
// the overlay managers picked again, then the label is shown (SetFlag 2)
// when the visibility filter (the service 0x00B43DC4's slot 18) takes it
// and the city shows signs and labels. 0x06CDB65B (data 0 a new filter):
// with that filter taking it and labels shown, the label is shown unless it
// is; otherwise it is hidden (ClearFlag 2) if it is shown. Every message
// then goes to the prop's DoMessage, whose answer is the answer.
uint32_t __fastcall DoMessage(char* target, void*, void* message)
{
    static bool logged = false;
    FirstCall("textdecal DoMessage", logged);
    char* const self = target - layout::kMessageTarget;
    char* const occupant = self ? self + layout::kOccupant : nullptr;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case kMessageFilter: {
        void* const filter = *Virtual<void**>(message, 0x3C);
        if (Virtual<uint8_t>(filter, 0xC, static_cast<void*>(occupant)) && SignsShown()) {
            if (!Virtual<uint8_t>(self + layout::kOccupant, 0x80, 2u))
                Virtual<void>(self + layout::kOccupant, 0x74, 2u);
        } else if (Virtual<uint8_t>(self + layout::kOccupant, 0x80, 2u)) {
            Virtual<void>(self + layout::kOccupant, 0x7C, 2u);
        }
        break;
    }
    case kMessageCityInit: {
        PickOverlayManagers(self, nullptr);
        void* filter = nullptr;
        Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x48, &filter);
        if (filter) {
            if (Virtual<uint8_t>(filter, 0xC, static_cast<void*>(occupant)) && SignsShown())
                Virtual<void>(self + layout::kOccupant, 0x74, 2u);
            Virtual<uint32_t>(filter, 0x8);
        }
        break;
    }
    case kMessageTick:
        MakeOverlays(self, nullptr);
        break;
    default:
        break;
    }
    return This<uint32_t, void*>(addr::cSC4PropOccupant_DoMessage, target, message);
}

// ====================================================================================
// The occupant part (`occupant` is the object + 0xC)
// ====================================================================================

// Slot 3 (0x005F7F40): nothing when initialized. The prop type 0xCB79919B
// (the prop's slot 8), the base's Init (0x005EC940: it tests again). A label
// not read from a save gets its overlay managers and is shown (SetFlag 2)
// when the visibility filter takes it and the city shows signs and labels.
// Then the three messages are asked for. True.
uint8_t __fastcall Init(char* occupant, void*)
{
    static bool logged = false;
    FirstCall("textdecal Init", logged);
    char* const self = occupant - layout::kOccupant;
    if (Virtual<uint8_t>(occupant, 0x14))
        return 1;
    Virtual<void>(self + layout::kProp, 0x20, text::kPropType);
    if (!Virtual<uint8_t>(occupant, 0x14))
        This<uint8_t>(addr::cSC4Occupant_Init, occupant);
    if (!(At<uint8_t>(self, layout::kBaseState) & 2)) {
        PickOverlayManagers(self, nullptr);
        void* filter = nullptr;
        Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x48, &filter);
        if (filter) {
            if (Virtual<uint8_t>(filter, 0xC, static_cast<void*>(self ? occupant : nullptr)) && SignsShown())
                Virtual<void>(occupant, 0x74, 2u);
            Virtual<uint32_t>(filter, 0x8);
        }
    }
    void* const server = Global<void*>(addr::SC4_MessageServer);
    for (const uint32_t type : {kMessageTick, kMessageFilter, kMessageCityInit})
        Virtual<uint8_t>(server, 0x14, Target(self), type);
    return 1;
}

// Slot 4 (0x005F76C0): nothing unless initialized. The three messages
// given up, the text taken off each overlay manager there is (when it has
// an ID) and the manager let go, the texture let go, then the prop's own
// Shutdown (0x005EE160). True.
uint8_t __fastcall Shutdown(char* occupant, void*)
{
    static bool logged = false;
    FirstCall("textdecal Shutdown", logged);
    char* const self = occupant - layout::kOccupant;
    if (!Virtual<uint8_t>(occupant, 0x14))
        return 1;
    void* const server = Global<void*>(addr::SC4_MessageServer);
    for (const uint32_t type : {kMessageTick, kMessageFilter, kMessageCityInit})
        Virtual<uint8_t>(server, 0x18, Target(self), type);
    const std::size_t managers[2][2] = {{text::kOverlays, text::kOverlayId}, {text::kOverlays2, text::kOverlayId2}};
    for (const auto& pair : managers) {
        if (!At<void*>(self, pair[0]))
            continue;
        if (const uint32_t id = At<uint32_t>(self, pair[1]); id != kNone) {
            Virtual<void>(At<void*>(self, pair[0]), 0x3C, id);
            At<uint32_t>(self, pair[1]) = kNone;
        }
        if (void* const held = At<void*>(self, pair[0])) {
            At<void*>(self, pair[0]) = nullptr;
            Virtual<uint32_t>(held, 0x8);
        }
    }
    if (void* const texture = At<void*>(self, text::kTexture)) {
        At<void*>(self, text::kTexture) = nullptr;
        Virtual<uint32_t>(texture, 0x8);
    }
    This<uint8_t>(addr::cSC4PropOccupant_Shutdown, occupant);
    return 1;
}

// Slot 9 (0x005F7AA0): the place kept (x and z of the position), the prop's
// SetPosition (the box moved), the city asked for the cell (its slot 95;
// the answer is not used), then the overlays moved while moving, else made
// again. No return value.
void __fastcall SetPosition(char* occupant, void*, const float* position)
{
    static bool logged = false;
    FirstCall("textdecal SetPosition", logged);
    char* const self = occupant - layout::kOccupant;
    uint32_t x = 0, z = 0;
    std::memcpy(&x, &position[0], 4);
    std::memcpy(&z, &position[2], 4);
    At<uint32_t>(self, text::kPosition) = x;
    At<uint32_t>(self, text::kPosition + 4) = z;
    This<void, const float*>(addr::cSC4PropOccupant_SetPosition, occupant, position);
    int32_t cellX = 0, cellZ = 0;
    Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, At<uint32_t>(self, text::kPosition), At<uint32_t>(self, text::kPosition + 4),
                  &cellX, &cellZ);
    if (At<uint8_t>(self, text::kMoving))
        MoveOverlays(self, nullptr);
    else
        MakeOverlays(self, nullptr);
}

// Slot 30 (0x005F81F0): a change of base flag 2 to set is dropped (the
// flag stays clear) while the city does not show signs and labels. Then
// the base's SetAllFlags. When flag 2 changed: shown, the overlays are made
// again (with both overlay managers there) or the managers are picked
// again, and a label with text but no texture gets one; hidden, the text is
// taken off the managers. AL is the flags' byte, from the base. (Where flag
// 2 changed the game returns what the last call left in EAX; ours answers
// the base's byte there too.)
uint32_t __fastcall SetAllFlags(char* occupant, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("textdecal SetAllFlags", logged);
    char* const self = occupant - layout::kOccupant;
    uint32_t changed = At<uint8_t>(self, layout::kFlags) ^ flags;
    if ((changed & 2) && (flags & 2) && !SignsShown()) {
        flags &= ~2u;
        changed = At<uint8_t>(self, layout::kFlags) ^ flags;
    }
    const uint32_t result = This<uint32_t>(addr::cSC4Occupant_SetAllFlags, occupant, flags);
    if (!(changed & 2))
        return result;
    if (!(flags & 2)) {
        RemoveOverlays(self, nullptr);
        return result;
    }
    if (At<void*>(self, text::kOverlays) && At<void*>(self, text::kOverlays2))
        MakeOverlays(self, nullptr);
    else
        PickOverlayManagers(self, nullptr);
    if (!At<void*>(self, text::kTexture) && Virtual<uint32_t>(self + text::kText, 0x1C) > 0)
        MakeText(self, nullptr);
    return result;
}

// ====================================================================================
// Save (`part` is the object + 0x10)
// ====================================================================================

// Slot 3 (0x005F7DB0): version 3, the prop's record (its Write, with the
// same `this`), then the text, the place, the size, the width, the place
// again, the colour, the opacity and the direction's low byte. When the
// prop's Write fails nothing more is written; the answer is in both cases
// whether the stream is free of errors.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("textdecal Write", logged);
    char* const self = part - layout::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 3u);
    if (This<uint8_t>(addr::cSC4PropOccupant_Write, part, stream)) {
        Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(self + text::kText));
        Cdecl<void*>(addr::SC4Stream_WriteVector2, stream, static_cast<void*>(self + text::kPosition));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, text::kSize));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, text::kWidth));
        Cdecl<void*>(addr::SC4Stream_WriteVector2, stream, static_cast<void*>(self + text::kPosition));
        Cdecl<void*>(addr::SC4Stream_WriteVector3, stream, static_cast<void*>(self + text::kColour));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, text::kOpacity));
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, text::kDirection)});
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 4 (0x005F7E50): the version (16 bits), the prop's record; when that
// is read, the text, before version 3 a dword that is dropped, the place,
// the size (version 1: an unsigned integer, made a float), the width, the
// place again, the colour, the opacity and the direction (a byte, kept as
// an integer). No version is refused. The answer is whether the stream is
// free of errors. (The game reads the version into a stack slot it never
// set; ours starts it at 0, which only matters when the read itself fails.)
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("textdecal Read", logged);
    char* const self = part - layout::kSerializable;
    uint32_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (This<uint8_t>(addr::cSC4PropOccupant_Read, part, stream)) {
        Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(self + text::kText));
        uint32_t dropped = 0;
        if (static_cast<uint16_t>(version) < 3)
            Virtual<uint8_t>(stream, 0x24, &dropped);
        Cdecl<void*>(addr::SC4Stream_ReadVector2, stream, static_cast<void*>(self + text::kPosition));
        if (static_cast<uint16_t>(version) == 1) {
            Virtual<uint8_t>(stream, 0x24, &dropped);
            At<float>(self, text::kSize) = static_cast<float>(static_cast<double>(dropped));
        } else {
            Virtual<uint8_t>(stream, 0x30, self + text::kSize);
        }
        Virtual<uint8_t>(stream, 0x30, self + text::kWidth);
        Cdecl<void*>(addr::SC4Stream_ReadVector2, stream, static_cast<void*>(self + text::kPosition));
        Cdecl<void*>(addr::SC4Stream_ReadVector3, stream, static_cast<void*>(self + text::kColour));
        Virtual<uint8_t>(stream, 0x30, self + text::kOpacity);
        uint32_t direction = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
        Virtual<uint8_t>(stream, 0x14, &direction);
        At<uint32_t>(self, text::kDirection) = static_cast<uint8_t>(direction);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 5 (0x005F8430).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("textdecal GetGZCLSID", logged);
    return text::kClassId;
}

// ====================================================================================
// Installation
// ====================================================================================

// GetGZCLSID's code is six bytes (mov eax, the ID; ret) and GetText's first
// two instructions six: the detour's jump covers whole instructions.
const Detour kTextDecalDetours[] = {
    {addr::cSC4PropOccupantTextDecal_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
    {addr::cSC4PropOccupantTextDecal_Shutdown, reinterpret_cast<void*>(&Shutdown)},
    {addr::cSC4PropOccupantTextDecal_RemoveOverlays, reinterpret_cast<void*>(&RemoveOverlays)},
    {addr::cSC4PropOccupantTextDecal_MakeOverlays, reinterpret_cast<void*>(&MakeOverlays)},
    {addr::cSC4PropOccupantTextDecal_MoveOverlays, reinterpret_cast<void*>(&MoveOverlays)},
    {addr::cSC4PropOccupantTextDecal_SetPosition, reinterpret_cast<void*>(&SetPosition)},
    {addr::cSC4PropOccupantTextDecal_SetSize, reinterpret_cast<void*>(&SetSize)},
    {addr::cSC4PropOccupantTextDecal_SetColour, reinterpret_cast<void*>(&SetColour)},
    {addr::cSC4PropOccupantTextDecal_SetOpacity, reinterpret_cast<void*>(&SetOpacity)},
    {addr::cSC4PropOccupantTextDecal_PickOverlayManagers, reinterpret_cast<void*>(&PickOverlayManagers)},
    {addr::cSC4PropOccupantTextDecal_SetDirection, reinterpret_cast<void*>(&SetDirection)},
    {addr::cSC4PropOccupantTextDecal_Write, reinterpret_cast<void*>(&Write)},
    {addr::cSC4PropOccupantTextDecal_Read, reinterpret_cast<void*>(&Read)},
    {addr::cSC4PropOccupantTextDecal_Init, reinterpret_cast<void*>(&Init)},
    {addr::cSC4PropOccupantTextDecal_DoMessage, reinterpret_cast<void*>(&DoMessage)},
    {addr::cSC4PropOccupantTextDecal_SetMoving, reinterpret_cast<void*>(&SetMoving)},
    {addr::cSC4PropOccupantTextDecal_MakeText, reinterpret_cast<void*>(&MakeText)},
    {addr::cSC4PropOccupantTextDecal_SetAllFlags, reinterpret_cast<void*>(&SetAllFlags)},
    {addr::cSC4PropOccupantTextDecal_SetText, reinterpret_cast<void*>(&SetText)},
    {addr::cSC4PropOccupantTextDecal_GetText, reinterpret_cast<void*>(&GetText)},
    {addr::cSC4PropOccupantTextDecal_GetGZCLSID, reinterpret_cast<void*>(&GetGZCLSID)},
};

bool InstallTextDecal(registry::Mode) { return InstallDetours(kTextDecalDetours, "textdecal"); }

const bool g_registered = registry::Register({
    "simulators.propkinds.textdecal",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantTextDecal_QueryInterface,
    "Our cSC4PropOccupantTextDecal (the label): QueryInterface, the text, size, moving byte, colour, direction and "
    "opacity, the overlays and their managers, the texture, the messages, Init, Shutdown, SetPosition, SetAllFlags, "
    "Write, Read and GetGZCLSID (21 detours; src/core/propkinds.h)",
    false,
    InstallTextDecal,
});

// ====================================================================================
// Our object
// ====================================================================================

// Copies of the game's seven vtables, taken at the first construction,
// after every PostAppInit slot swap of the other groups (the prop's
// GetRandomChance in 0x00AA5620).
constexpr int kSlots0 = 11, kSlots4 = 24, kSlots8 = 4, kSlotsC = 34, kSlots10 = 6, kSlots14 = 25, kSlots28 = 5;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable10[kSlots10];
void* g_vtable14[kSlots14];
void* g_vtable28[kSlots28];

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags);

// The holder part's slot 22 (0x005F8490).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - layout::kHolder, nullptr, flags);
}

void BuildVtables()
{
    static bool built = false;
    if (built)
        return;
    built = true;
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4PropOccupantTextDecal_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4PropOccupant_DerivedVftable_AA5620), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4PropOccupantTextDecal_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4PropOccupantTextDecal_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable10, reinterpret_cast<const void*>(addr::cSC4PropOccupantTextDecal_vftable_10), sizeof(g_vtable10));
    std::memcpy(g_vtable14, reinterpret_cast<const void*>(addr::cSC4PropOccupantTextDecal_vftable_14), sizeof(g_vtable14));
    std::memcpy(g_vtable28, reinterpret_cast<const void*>(addr::cSC4PropOccupantTextDecal_vftable_28), sizeof(g_vtable28));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable10[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtable14[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    log::Write("propkinds textdecal object: vtables copied (+0 slots 4/6/10 %p %p %p, +8 slot 3 %p, +C slots 3/4/9/30 %p %p %p %p)",
               g_vtable[4], g_vtable[6], g_vtable[10], g_vtable8[3], g_vtableC[3], g_vtableC[4], g_vtableC[9], g_vtableC[30]);
}

void SetVtables(char* self)
{
    BuildVtables();
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, layout::kProp) = g_vtable4;
    At<void**>(self, layout::kMessageTarget) = g_vtable8;
    At<void**>(self, layout::kOccupant) = g_vtableC;
    At<void**>(self, layout::kSerializable) = g_vtable10;
    At<void**>(self, layout::kHolder) = g_vtable14;
    At<void**>(self, layout::kExemplarPart) = g_vtable28;
}

// The constructor (detour of 0x005F8300): the prop constructed at its
// address on the object + 4, our vtables, an empty text (8 bytes from the
// game's allocator, not tested for null), not moving, width 100, no
// overlay IDs, size 1, place 0, white (255 each), opacity 1, direction 1,
// no texture and no overlay managers, and the prop's box's high corner 0.1
// each way. (The game stores an abstract vtable, 0x00AA54B4, at +0 before
// the prop's constructor; nothing reads it.)
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("textdecal Construct", logged);
    This<void*>(addr::cSC4PropOccupant_cSC4PropOccupant, self + layout::kProp);
    SetVtables(self);
    At<char*>(self, text::kTextBegin) = nullptr;
    At<char*>(self, text::kTextEnd) = nullptr;
    At<char*>(self, text::kTextCapacity) = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    At<char*>(self, text::kTextBegin) = buffer;
    At<char*>(self, text::kTextEnd) = buffer;
    At<char*>(self, text::kTextCapacity) = buffer + 8;
    *buffer = 0;
    At<uint32_t>(self, text::kTextZero) = 0;
    At<std::uintptr_t>(self, text::kText) = addr::SC4String_Vtable;
    At<uint8_t>(self, text::kMoving) = 0;
    At<uint32_t>(self, text::kWidth) = 0x42C80000; // 100
    At<uint32_t>(self, text::kOverlayId) = kNone;
    At<uint32_t>(self, text::kOverlayId2) = kNone;
    At<uint32_t>(self, text::kSize) = 0x3F800000;
    At<uint32_t>(self, text::kPosition) = 0;
    At<uint32_t>(self, text::kPosition + 4) = 0;
    At<uint32_t>(self, text::kColour) = 0x437F0000; // 255
    At<uint32_t>(self, text::kColour + 4) = 0x437F0000;
    At<uint32_t>(self, text::kColour + 8) = 0x437F0000;
    At<uint32_t>(self, text::kOpacity) = 0x3F800000;
    At<int32_t>(self, text::kDirection) = 1;
    At<void*>(self, text::kTexture) = nullptr;
    At<void*>(self, text::kOverlays) = nullptr;
    At<void*>(self, text::kOverlays2) = nullptr;
    At<uint32_t>(self, layout::kMax) = 0x3DCCCCCD; // 0.1
    At<uint32_t>(self, layout::kMax + 4) = 0x3DCCCCCD;
    At<uint32_t>(self, layout::kMax + 8) = 0x3DCCCCCD;
    static int created = 0;
    if (created++ < 3)
        log::Write("propkinds textdecal object: constructed %p", static_cast<void*>(self));
    return self;
}

// The destructor's body (detour of 0x005F8570; the deleting destructor is
// its one caller): the second overlay manager, the first and the texture
// released (the overlays are not taken off: Shutdown does that), the
// text's buffer freed, then the occupant base destroyed. Unlike the other
// classes' it does not store the vtables first. The game passes the base
// null for a null object, which no object has.
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("textdecal Destroy", logged);
    if (void* const held = At<void*>(self, text::kOverlays2))
        Virtual<uint32_t>(held, 0x8);
    if (void* const held = At<void*>(self, text::kOverlays))
        Virtual<uint32_t>(held, 0x8);
    if (void* const texture = At<void*>(self, text::kTexture))
        Virtual<uint32_t>(texture, 0x8);
    At<std::uintptr_t>(self, text::kText) = addr::SC4String_Vtable;
    if (char* const buffer = At<char*>(self, text::kTextBegin))
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
    This<void>(addr::cSC4Occupant_Destroy, self + layout::kOccupant);
}

// The deleting destructor (detour of 0x005F8510): the body, and with flag 1
// the memory back to the pool. A null object is still destroyed first, as
// in the game.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("textdecal DeletingDestructor", logged);
    Destroy(self, nullptr);
    if ((flags & 1) && self)
        PoolFree(kPool, self, kPoolReopenAt);
    return self;
}

// The pool allocator (detour of 0x005F7D80; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("textdecal Allocate", logged);
    return PoolAllocate(kPool);
}

// The factory (detour of 0x005F84D0): an item of the pool, constructed (a
// jump to the constructor). Unlike the other classes' factories it hands
// out the object itself, the label interface, not the object + 4.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("textdecal Create", logged);
    return Construct(static_cast<char*>(PoolAllocate(kPool)), nullptr);
}

const Detour kTextDecalObjectDetours[] = {
    {addr::cSC4PropOccupantTextDecal_Construct, reinterpret_cast<void*>(&Construct)},
    {addr::cSC4PropOccupantTextDecal_Allocate, reinterpret_cast<void*>(&Allocate)},
    {addr::cSC4PropOccupantTextDecal_Create, reinterpret_cast<void*>(&Create)},
    {addr::cSC4PropOccupantTextDecal_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
    {addr::cSC4PropOccupantTextDecal_Destroy, reinterpret_cast<void*>(&Destroy)},
};

// Every construction path: the factory (through the class registration's
// pointer when a save is read) and the allocator and constructor pairs of
// the prop manager's TextDecal command and the label tool (the game's, or
// ours in propmgr_2.cpp and tool_decal.cpp, which call the same
// addresses). The deleting destructor is reached through the holder part's
// slot 22: ours in the copies, and by the detour for the game's thunk.
bool InstallTextDecalObject(registry::Mode) { return InstallDetours(kTextDecalObjectDetours, "textdecal object"); }

const bool g_registeredObject = registry::Register({
    "simulators.propkinds.textdecal_object",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantTextDecal_Construct,
    "Our cSC4PropOccupantTextDecal object: the constructor, the pool allocation, the factory, the destructor and the "
    "deleting destructor (5 detours), identity; methods from copies of the game vtables taken once "
    "(src/core/propkinds.h)",
    false,
    InstallTextDecalObject,
});

} // namespace
} // namespace opensc4::propkinds
