// The plop tool, last third: cSC4ViewInputControlPlaceLot (vtable 0x00A90738,
// message target 0x00A90728 at +0x28, object 0x158 bytes), the tool the
// player places a building or lot from a menu with. Replacement, off by
// default:
//
//   ui.tools.plop3: the message target's DoMessage (0x004C2F20), the key
//     handler (0x004C3060; the rotate commands), the left mouse button
//     (0x004C3120; places the lot), the mouse move (0x004C3300; picks the
//     cell, tests the placement, makes the cursor text and the cost) and the
//     mouse exit (0x004C36C0).
//
// Left the game's, called by address: the preview update 0x004C2660 and the
// footprint update 0x004C2090. Both build and move the cursor's overlay
// objects (the arrow, the footprint rectangles and their tint) through the
// effects service 0x00B43D1C with inline vertex arithmetic: drawing.
//
// Fields of the object (from its start; up to +0x24 the base class):
//   +0x1C the city view            +0x20 an object released after a move
//   +0x30 the lot configuration    +0x38 the rotation chosen (keys)
//   +0x3C the rotation in use      +0x40 the rotation at the last update
//   +0x44, +0x48 the last mouse position (screen)
//   +0x4C the lot configuration manager
//   +0x78 .. +0x84 the placed lot's bounds as ints (x0, z0, x1, z1)
//   +0x88 .. +0x90 the picked point (3 floats)
//   +0x94 byte: the placement is allowed; +0x95 byte: the preview is shown
//   +0x98 the cursor text (a game string: vtable, body at +0x9C)
//   +0xAC an overlay holder (first dword a pointer)
//   +0xDC, +0xE0 overlay objects (released and cleared on a placement)
//   +0xE8, +0xF4 vectors of overlay objects (the footprint)
//   +0x118 byte; +0x138 int64 cost; +0x150 byte; +0x154 the count of
//   placements left (a placement counts it down; below 1 the tool ends).
//
// Random draws: none. Clocks: none. The picking goes through the view's
// PickTerrain (slot 0x90, camera and terrain: a pure function of both).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/tool_plop.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4 {
namespace tool_plop {
namespace {
std::uintptr_t g_callSite = 0;
} // namespace

std::uintptr_t PlaceLotCallSite()
{
    return g_callSite;
}

bool InToolPlaceLot()
{
    return g_callSite == kPlaceLotCallSite;
}

} // namespace tool_plop

namespace {

namespace field {
constexpr std::size_t kView = 0x1C;
constexpr std::size_t kLastObject = 0x20;
constexpr std::size_t kConfig = 0x30;
constexpr std::size_t kRotationWanted = 0x38;
constexpr std::size_t kRotation = 0x3C;
constexpr std::size_t kRotationSeen = 0x40;
constexpr std::size_t kMouseX = 0x44;
constexpr std::size_t kMouseZ = 0x48;
constexpr std::size_t kConfigManager = 0x4C;
constexpr std::size_t kBounds = 0x78;
constexpr std::size_t kPoint = 0x88;
constexpr std::size_t kAllowed = 0x94;
constexpr std::size_t kShown = 0x95;
constexpr std::size_t kText = 0x98;
constexpr std::size_t kTextBody = 0x9C;
constexpr std::size_t kTextEnd = 0xA0;
constexpr std::size_t kHolder = 0xAC;
constexpr std::size_t kOverlayA = 0xDC;
constexpr std::size_t kOverlayB = 0xE0;
constexpr std::size_t kByte118 = 0x118;
constexpr std::size_t kCost = 0x138;
constexpr std::size_t kByte150 = 0x150;
constexpr std::size_t kPlacementsLeft = 0x154;
constexpr std::size_t kMessagePart = 0x28;
} // namespace field

// Functions of the other parts of the tool and of the base class, called by
// address (their parts name them).
constexpr std::uintptr_t kRotationRect = 0x004BE750;   // ret 0x18: (rotation, x, z, int*, int*, int*) -> bool
constexpr std::uintptr_t kFootprintFree = 0x004BFB30;  // ret 0x10: four pointers -> bool
constexpr std::uintptr_t kSelectConfig = 0x004C1360;   // ret 4: (config)
constexpr std::uintptr_t kPickFromConfig = 0x004BEB70; // ret 0x10: (config object, 3 pointers) -> bool
constexpr std::uintptr_t kBuildText = 0x004C1110;      // ret 0x1C: seven pointers and the rotation -> bool
constexpr std::uintptr_t kClearHover = 0x004BE0A0;     // clears +0x118 and the view's helper
constexpr std::uintptr_t kEmptyMap = 0x007D20E0;       // thiscall on the view's slot 0xC4 result

constexpr uint32_t kCursorTextId = 0xE9C877FA;
constexpr uint32_t kCommandRotateCw = 0x6A935CD5;
constexpr uint32_t kCommandRotateCcw = 0x6A935CD1;

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

// The game's ftol (0x009EEF04): the low 32 bits of the truncated value.
int32_t ToInt(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The preview update (0x004C2660, ret 0xC; left the game's, drawing).
void UpdatePreview(void* self, uint32_t allowed, uint32_t shown, void* point)
{
    This<void>(addr::cSC4ViewInputControlPlaceLot_UpdatePreview, self, allowed, shown, point);
}

// The game's inlined reference holder code (the overlay objects): when the
// object says its slot 0x14, ask it to stop (slot 0x10), then let go.
void StopAndRelease(char* tool, std::size_t offset)
{
    void*& holder = At<void*>(tool, offset);
    if (holder && Virtual<uint8_t>(holder, 0x14))
        Virtual<void>(holder, 0x10, uint32_t{0});
    if (holder) {
        void* const object = holder;
        holder = nullptr;
        Virtual<void>(object, 0x8);
    }
}

// PlaceLot for the tool. The wrapper game.quiet.plop recognises the tool's
// call by the address the game's PlaceLot returns to (0x004C3190, after the
// call at 0x004C318B); a call from here returns into core.dll, so the call
// site is kept for tool_plop::PlaceLotCallSite().
uint8_t PlaceLotFromTool(void* self, void* point, uint32_t rotation, uint32_t smoke, void* bounds,
                         std::uintptr_t callSite)
{
    const std::uintptr_t saved = tool_plop::g_callSite;
    tool_plop::g_callSite = callSite;
    const uint8_t placed =
        This<uint8_t>(addr::cSC4ViewInputControlPlaceLot_PlaceLot, self, point, rotation, smoke, bounds);
    tool_plop::g_callSite = saved;
    return placed;
}

// --- DoMessage (message target, this = object + 0x28) ---------------------------------

// 0x004C2F20, ret 4, always false.
//   0x49D5B83F: 0x004BE0A0 on the tool.
//   0x277A9364: the holder at +0xAC shown or hidden by the service 0x00B43CF4's
//     object (its slot 0x108, then slot 0x50: hidden when that says yes).
//   0x99EF1142, 0x99EF1143: when +0x118 is clear and there is a view, empty
//     the map of the view's slot 0xC4 result.
//   0xA6B79602, 0xC9F775BB: both overlay objects stopped and let go, then the
//     preview update with the last state.
uint8_t __fastcall DoMessage(char* self, void*, void* message)
{
    char* const tool = self - field::kMessagePart;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type > 0x99EF1143u) {
        if (type == 0xA6B79602u || type == 0xC9F775BBu) {
            StopAndRelease(tool, field::kOverlayA);
            StopAndRelease(tool, field::kOverlayB);
            UpdatePreview(tool, At<uint8_t>(tool, field::kAllowed), At<uint8_t>(tool, field::kShown),
                          tool + field::kPoint);
        }
        return 0;
    }
    if (type >= 0x99EF1142u) {
        if (!At<uint8_t>(tool, field::kByte118)) {
            if (void* const view = At<void*>(tool, field::kView))
                This<void>(kEmptyMap, Virtual<void*>(view, 0xC4));
        }
        return 0;
    }
    if (type == 0x277A9364u) {
        void* const service = Global<void*>(addr::SC4_Terrain);
        void* const object = Virtual<void*>(service, 0x108);
        const uint8_t flag = Virtual<uint8_t>(object, 0x50);
        This<void>(addr::SC4ViewModel_Show, tool + field::kHolder, uint32_t{flag ? 0u : 1u});
        return 0;
    }
    if (type == 0x49D5B83Fu)
        This<void>(kClearHover, tool);
    return 0;
}

// --- OnKeyDown --------------------------------------------------------------------------

// 0x004C3060, ret 8 (virtual key, modifiers): when the tool is the view's
// current one, the key accelerator turns the key into a command; the two
// rotate commands turn the lot a quarter (+1 or +3, kept in 0..3), then the
// mouse move runs again for the last position (modifier 0x100) and the
// preview is updated.
uint8_t __fastcall OnKeyDown(char* self, void*, int32_t key, uint32_t modifiers)
{
    if (!This<uint8_t>(addr::SC4ViewInputControl_IsCurrent, self))
        return 0;
    void* const accelerator = Virtual<void*>(At<void*>(self, field::kView), 0x7C);
    if (!accelerator)
        return 0;
    uint32_t command = 0;
    if (!Virtual<uint8_t>(accelerator, 0x28, key, modifiers, uint32_t{1}, &command))
        return 0;
    uint32_t turned;
    if (command == kCommandRotateCw)
        turned = At<uint32_t>(self, field::kRotation) + 1;
    else if (command == kCommandRotateCcw)
        turned = At<uint32_t>(self, field::kRotation) + 3;
    else
        return 0;
    turned &= 0x80000003u;
    if (static_cast<int32_t>(turned) < 0)
        turned = ((turned - 1) | 0xFFFFFFFCu) + 1;
    At<uint32_t>(self, field::kRotation) = turned;
    At<uint32_t>(self, field::kRotationWanted) = turned;
    Virtual<uint8_t>(self, 0x50, At<int32_t>(self, field::kMouseX), At<int32_t>(self, field::kMouseZ),
                     uint32_t{0x100});
    UpdatePreview(self, At<uint8_t>(self, field::kAllowed), At<uint8_t>(self, field::kShown),
                  self + field::kPoint);
    return 1;
}

// --- OnMouseDownL -----------------------------------------------------------------------

// 0x004C3120, ret 0xC (x, z, modifiers). Picks the terrain under the cursor
// and, when the preview says the lot may go there, asks PlaceLot (smoke 1) to
// place it. After a placement: the bounds in world units kept at +0x78..+0x84
// (from the cell size, slot 0x18 of the service 0x00B43CF4), the preview
// updated, both overlay objects let go, the cost indicator cleared, +0x94
// clear. A placement counts +0x154 down and ends the tool below 1 (slot
// 0x6C); no placement plays the sound 0xAA62C9D5 of the sound service.
uint8_t __fastcall OnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    uint32_t point[3];
    int32_t bounds[4];
    uint8_t placed = 0;
    std::memset(bounds, 0, sizeof(bounds));
    if (void* const view = At<void*>(self, field::kView)) {
        point[0] = point[1] = point[2] = Global<uint32_t>(addr::SC4_UnsetFloat);
        if (Virtual<uint8_t>(view, 0x90, x, z, reinterpret_cast<float*>(point), uint32_t{0}) &&
            At<uint8_t>(self, field::kAllowed)) {
            placed = PlaceLotFromTool(self, point, At<uint32_t>(self, field::kRotation), 1,
                                      bounds, tool_plop::kPlaceLotCallSite);
            if (placed) {
                void* const terrain = Global<void*>(addr::SC4_Terrain);
                const double cell = Virtual<double>(terrain, 0x18);
                const double twoTo32 = Global<float>(addr::SC4_FloatTwoTo32);
                At<int32_t>(self, field::kBounds + 0x0) = ToInt(static_cast<double>(bounds[0]) * cell - twoTo32);
                At<int32_t>(self, field::kBounds + 0x4) = ToInt(static_cast<double>(bounds[1]) * cell - twoTo32);
                At<int32_t>(self, field::kBounds + 0x8) =
                    ToInt(static_cast<double>(bounds[2] + 1) * cell + twoTo32);
                At<int32_t>(self, field::kBounds + 0xC) =
                    ToInt(static_cast<double>(bounds[3] + 1) * cell + twoTo32);
                UpdatePreview(self, 1, 0, point);
                StopAndRelease(self, field::kOverlayA);
                StopAndRelease(self, field::kOverlayB);
                Virtual<void>(At<void*>(self, field::kView), 0xB0, uint32_t{1});
                At<uint8_t>(self, field::kAllowed) = 0;
            }
        }
    }
    if (placed) {
        const int32_t left = At<int32_t>(self, field::kPlacementsLeft) - 1;
        At<int32_t>(self, field::kPlacementsLeft) = left;
        if (left < 1)
            Virtual<void>(self, 0x6C);
    } else if (void* const sound = Global<void*>(addr::SC4_Sound43CB8)) {
        Virtual<void>(sound, 0x2C, uint32_t{0xEA451903}, uint32_t{0xAA62C9D5}, uint32_t{0}, uint32_t{0}, uint32_t{0});
    }
    return placed;
}

// --- OnMouseMove ------------------------------------------------------------------------

// The frame of the original, offsets from its stack pointer after the
// prologue (the callee-saved registers pushed). The game hands pointers into
// it to four callees, so its layout is kept.
namespace frame {
constexpr std::size_t kAllowed = 0x0C; // the placement flag (a byte)
constexpr std::size_t kShown = 0x10;   // the shown flag (a byte)
constexpr std::size_t kPoint = 0x14;   // three floats, the old point and then the picked one
constexpr std::size_t kOut = 0x20;     // an out value; in the picking failure path the localized string (0x20)
constexpr std::size_t kRect = 0x20;    // four ints, -1000 (the same place as kOut: the rectangle out)
constexpr std::size_t kName = 0x40;    // a game string (0x14): the cursor text built this time
constexpr std::size_t kNameBody = 0x44;
constexpr std::size_t kNameEnd = 0x48;
constexpr std::size_t kNameTag = 0x50;
constexpr std::size_t kRotation = 0x58; // the caller's x slot, reused for the rotation
constexpr std::size_t kZ = 0x5C;
constexpr std::size_t kMods = 0x60;
constexpr std::size_t kSize = 0x64;
} // namespace frame

// 0x004C3300, ret 0xC (x, z, modifiers).
//   The cursor text is copied to a local; the mouse position kept. With a
//   view: the point under the cursor is picked (failing leaves the old point
//   in the frame). On a pick: with modifier 0x100 the rotation and cell are
//   worked out by 0x004BE750; else with +0x150 set by 0x004BFB30 (and a
//   changed configuration selected through 0x004C1360), else by the
//   configuration manager's object and 0x004BEB70. 0x004C1110 then builds the
//   text and the cost; the cost indicator is set (cost above 0) or cleared.
//   Without a pick the text is the localized string 0x0A419226/0xCA711914 and
//   the placement flag 0. A changed text is set as the cursor text 0xE9C877FA.
//   When anything changed the preview is updated and the state kept.
//   Always true.
uint8_t __fastcall OnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    alignas(8) uint8_t memory[frame::kSize];
    // The original leaves most of the frame as the stack had it (the
    // out value at 0x20 among them); here it starts at zero.
    std::memset(memory, 0, sizeof(memory));
    auto dword = [&](std::size_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(memory + offset); };
    uint8_t* const allowedSlot = memory + frame::kAllowed;
    uint8_t* const shownSlot = memory + frame::kShown;

    uint8_t allowed = At<uint8_t>(self, field::kAllowed);
    *shownSlot = At<uint8_t>(self, field::kShown);
    dword(frame::kPoint + 0x0) = At<uint32_t>(self, field::kPoint + 0x0);
    dword(frame::kPoint + 0x4) = At<uint32_t>(self, field::kPoint + 0x4);
    dword(frame::kPoint + 0x8) = At<uint32_t>(self, field::kPoint + 0x8);
    *allowedSlot = allowed;
    dword(frame::kRotation) = static_cast<uint32_t>(x);
    dword(frame::kZ) = static_cast<uint32_t>(z);
    dword(frame::kMods) = modifiers;

    // The name: a copy of the tool's cursor text body, as a game string.
    char* const text = self + field::kText;
    This<void*>(addr::SC4String_CopyBuffer, memory + frame::kNameBody, text + 4, uint32_t{0}, uint32_t{0xFFFFFFFF},
                memory + frame::kRotation);
    uint32_t edi = addr::SC4String_Vtable;
    dword(frame::kName) = edi;
    dword(frame::kNameTag) = 0;
    At<int32_t>(self, field::kMouseX) = x;
    At<int32_t>(self, field::kMouseZ) = z;

    void* const view = At<void*>(self, field::kView);
    if (view) {
        At<uint8_t>(self, field::kShown) = 0;
        float* const point = reinterpret_cast<float*>(memory + frame::kPoint);
        if (Virtual<uint8_t>(view, 0x90, x, z, point, uint32_t{0})) {
            edi = At<uint32_t>(self, field::kRotationWanted);
            for (std::size_t i = 0; i < 4; ++i)
                dword(frame::kRect + 4 * i) = 0xFFFFFC18u;
            dword(frame::kRotation) = edi;
            bool textPath = false;
            if (modifiers & 0x100u) {
                allowed = This<uint8_t>(kRotationRect, self, edi, dword(frame::kPoint), dword(frame::kPoint + 0x8),
                                        memory + frame::kPoint, memory + frame::kPoint + 0x8,
                                        memory + frame::kOut);
                *allowedSlot = allowed;
                textPath = allowed != 0;
            } else if (At<uint8_t>(self, field::kByte150)) {
                allowed = This<uint8_t>(kFootprintFree, self, memory + frame::kZ, memory + frame::kRotation,
                                        memory + frame::kPoint, memory + frame::kOut);
                if (dword(frame::kZ) != At<uint32_t>(self, field::kConfig))
                    This<void>(kSelectConfig, self, dword(frame::kZ));
                *allowedSlot = allowed;
                edi = dword(frame::kRotation);
                At<uint32_t>(self, field::kRotation) = edi;
                textPath = allowed != 0;
            } else {
                void* const configuration =
                    Virtual<void*>(At<void*>(self, field::kConfigManager), 0x20, At<uint32_t>(self, field::kConfig));
                bool picked = false;
                if (configuration) {
                    picked = This<uint8_t>(kPickFromConfig, self, configuration, memory + frame::kPoint,
                                           memory + frame::kRotation, memory + frame::kOut) != 0;
                    edi = dword(frame::kRotation);
                }
                if (picked) {
                    At<uint32_t>(self, field::kRotation) = edi;
                    textPath = true;
                } else {
                    allowed = *allowedSlot;
                    textPath = allowed != 0;
                }
            }
            int64_t* const cost = reinterpret_cast<int64_t*>(self + field::kCost);
            auto buildText = [&]() -> uint8_t {
                return This<uint8_t>(kBuildText, self, memory + frame::kPoint, edi, memory + frame::kOut,
                                     memory + frame::kShown, self + field::kCost, memory + frame::kRotation,
                                     memory + frame::kName);
            };
            if (textPath) {
                allowed = buildText();
                *allowedSlot = allowed;
                if (allowed) {
                    At<int32_t>(self, field::kBounds + 0x0) = 0;
                    At<int32_t>(self, field::kBounds + 0x4) = 0;
                    At<int32_t>(self, field::kBounds + 0x8) = -1;
                    At<int32_t>(self, field::kBounds + 0xC) = -1;
                } else {
                    At<uint32_t>(self, field::kRotation) = At<uint32_t>(self, field::kRotationWanted);
                }
                if (*cost > 0)
                    Virtual<void>(view, 0xAC, static_cast<uint32_t>(*cost));
                else
                    Virtual<void>(view, 0xB0, uint32_t{0});
            } else {
                buildText();
                At<uint32_t>(self, field::kRotation) = edi;
                Virtual<void>(At<void*>(self, field::kView), 0xB0, uint32_t{0});
            }
        } else {
            // No point under the cursor: the text is a localized string.
            allowed = 0;
            *shownSlot = 0;
            *allowedSlot = 0;
            // The game's localized string is 0x20 bytes (frame 0x20 to 0x3F).
            char* const localized = This<char*>(addr::SC4LocalizedString_SC4LocalizedString, memory + frame::kOut,
                                                uint32_t{0x0A419226}, uint32_t{0xCA711914}, uint32_t{1});
            char* const body = localized ? localized + 4 : nullptr;
            if (body != reinterpret_cast<char*>(memory + frame::kNameBody)) {
                This<void>(addr::SC4String_AssignRange, memory + frame::kNameBody, At<char*>(body, 0),
                           At<char*>(body, 4), memory + frame::kRotation);
            }
            char* const localizedBody = *reinterpret_cast<char**>(memory + frame::kOut + 4);
            dword(frame::kOut) = edi;
            if (localizedBody)
                Cdecl<void>(addr::SC4_Free, static_cast<void*>(localizedBody));
            Virtual<void>(At<void*>(self, field::kView), 0xB0, uint32_t{0});
        }
    }

    // The text: set as the cursor text when it differs from the one kept.
    char* const nameBegin = *reinterpret_cast<char**>(memory + frame::kNameBody);
    if (Cdecl<uint8_t>(addr::SC4UI_F454BB0, static_cast<void*>(text), static_cast<void*>(memory + frame::kName))) {
        This<void>(addr::SC4String_AssignRange, self + field::kTextBody, nameBegin,
                   *reinterpret_cast<char**>(memory + frame::kNameEnd), memory + frame::kRotation);
        Virtual<void>(At<void*>(self, field::kView), 0xA0, kCursorTextId, uint32_t{0}, uint32_t{0}, text, uint32_t{1});
    }

    // Anything changed (flags, point beyond 1e-4, rotation): update the preview.
    const double epsilon = Global<float>(addr::cSC4ViewInputControlPlaceLot_PositionEpsilon);
    auto same = [&](std::size_t selfOffset, std::size_t frameOffset) {
        float a, b;
        std::memcpy(&a, self + selfOffset, sizeof(a));
        std::memcpy(&b, memory + frameOffset, sizeof(b));
        return std::fabs(static_cast<double>(a) - static_cast<double>(b)) < epsilon;
    };
    const bool unchanged = At<uint8_t>(self, field::kAllowed) == allowed &&
                           At<uint8_t>(self, field::kShown) == *shownSlot && same(field::kPoint + 0x0, frame::kPoint) &&
                           same(field::kPoint + 0x4, frame::kPoint + 0x4) &&
                           same(field::kPoint + 0x8, frame::kPoint + 0x8) &&
                           At<uint32_t>(self, field::kRotationSeen) == At<uint32_t>(self, field::kRotationWanted);
    if (!unchanged) {
        UpdatePreview(self, dword(frame::kAllowed), dword(frame::kShown), memory + frame::kPoint);
        At<uint8_t>(self, field::kShown) = *shownSlot;
        At<uint8_t>(self, field::kAllowed) = allowed;
        At<uint32_t>(self, field::kPoint + 0x0) = dword(frame::kPoint + 0x0);
        At<uint32_t>(self, field::kPoint + 0x4) = dword(frame::kPoint + 0x4);
        At<uint32_t>(self, field::kPoint + 0x8) = dword(frame::kPoint + 0x8);
        At<uint32_t>(self, field::kRotationSeen) = At<uint32_t>(self, field::kRotationWanted);
    }
    if (void* const last = At<void*>(self, field::kLastObject))
        Virtual<void>(last, 0x4C);
    if (nameBegin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(nameBegin));
    return 1;
}

// --- OnMouseExit ------------------------------------------------------------------------

// 0x004C36C0, no arguments: the preview hidden, the cursor text emptied and
// cleared on the view, the cost indicator cleared. Always true; the view is
// not checked for null.
uint8_t __fastcall OnMouseExit(char* self, void*)
{
    At<uint8_t>(self, field::kShown) = 0;
    UpdatePreview(self, At<uint8_t>(self, field::kAllowed), 0, self + field::kPoint);
    char* const body = At<char*>(self, field::kTextBody);
    if (body != At<char*>(self, field::kTextEnd)) {
        *body = 0;
        At<char*>(self, field::kTextEnd) = At<char*>(self, field::kTextBody);
    }
    void* const view = At<void*>(self, field::kView);
    Virtual<void>(view, 0xA4, kCursorTextId);
    Virtual<void>(At<void*>(self, field::kView), 0xB0, uint32_t{0});
    return 1;
}

// --- Registration -----------------------------------------------------------------------

bool InstallPlop3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4ViewInputControlPlaceLot_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::cSC4ViewInputControlPlaceLot_OnKeyDown, reinterpret_cast<void*>(&OnKeyDown)},
        {addr::cSC4ViewInputControlPlaceLot_OnMouseDownL, reinterpret_cast<void*>(&OnMouseDownL)},
        {addr::cSC4ViewInputControlPlaceLot_OnMouseMove, reinterpret_cast<void*>(&OnMouseMove)},
        {addr::cSC4ViewInputControlPlaceLot_OnMouseExit, reinterpret_cast<void*>(&OnMouseExit)},
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

const bool g_plop3Registered = registry::Register({
    "ui.tools.plop3",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4ViewInputControlPlaceLot_OnMouseMove,
    "Our plop tool: message target, rotate keys, mouse down, move and exit (docs/ui.md)",
    false,
    InstallPlop3,
});

} // namespace
} // namespace opensc4
