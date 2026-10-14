// Our cSC4PropOccupantTerrainDecal (build 641; the class map, the vtables
// and the fields are in propkinds.h): a prop that is a picture on the
// ground. It keeps a texture key, a size, a centre and an opacity, has the
// terrain view's overlay manager draw the decal while the prop is shown, and
// can fade the opacity over frame time, removing the overlay or itself at
// the end. The prop underneath (cSC4PropOccupant) is called at its
// addresses.
//
// Nothing here draws: the overlay manager (the Renderer's) is told through
// its interface what to show.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the object's primary vtable 0x00AA4790: 0x04 and 0x08 none (thunks to
//     the base's AddRef and Release);
//   - the occupant part 0x00AA4698: 0x10 Shutdown none (0x005EE4B0), 0x14
//     IsInitialized none (0x00458A40);
//   - the overlay manager 0x00AB39E8 (what the terrain view's slot 0x48
//     hands out): 0x08 none, 0x14 four (0x00736150), 0x34 two (0x0073ADE0),
//     0x38 two (0x00736490), 0x3C one (0x007388C0), 0x40 two (0x00737E50),
//     0x44 two (0x00737F00);
//   - the terrain view 0x00AB4410: 0x48 one (0x007558A0);
//   - the frame service 0x00A86760: 0x14 two (0x00448B70), 0x18 two
//     (0x00448C70);
//   - cGZMessage 0x00ACEC00: 0x10 and 0x4C none;
//   - the streams 0x00AA2310 (out) and 0x00AA2280 (in): 0x10, 0x14, 0x1C,
//     0x24, 0x30 one, 0x48 none.
// Direct callees: 0x005E89B0 (ret), 0x005EC960 (ret 8), 0x005ECE90 and
// 0x005EF360 (ret 4), 0x005EE1A0 (ret 4), 0x005EE050 (ret), 0x005E8C90 and
// 0x005E8980 (ret), 0x005E8B90 (ret 4), 0x005E8C40 (ret), 0x005447B0 (ret
// 4), 0x009128BF (ret), 0x0090CF9A (ret), 0x0090D031 and 0x0090CFF9 (ret
// 4); cdecl: 0x00602160, 0x00602600 (none), 0x005E5620 (one), 0x005DA0A0,
// 0x005DA520, 0x005DA070, 0x005DA4F0 (two).
//
// Callers from outside the class: the class registration (0x0046078B, which
// calls GetGZCLSID and keeps the factory's address, before any replacement
// is installed), the prop manager's 0x004A258B / 0x004A2599 and 0x004A30D8 /
// 0x004A30E6 and the terrain code's 0x0075EE44 / 0x0075EE52 and 0x006CC3B1
// / 0x006CC3C3 (allocator, then constructor: each reads only EAX after
// either call and keeps nothing in ECX or EDX across them). Everything else
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

constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kMessageFrame = 0x86AD10EE; // a frame: data 3 is the time in milliseconds as a float
constexpr int32_t kFrameCode = 0xC0;           // what the frame service is asked with
constexpr float kHalf = 0.5f;                  // 0x00A84D2C
constexpr float kMillisecond = 0.001f;         // 0x3A83126F at 0x00A867A4
constexpr double kSlowFade = 0.1;              // 0x3FB999999999999A at 0x00A9EFE0
constexpr std::uintptr_t kPool = addr::cSC4PropOccupantTerrainDecal_Pool;
constexpr uint32_t kPoolReopenAt = 0x4000004C;

// What the overlay manager tells of a decal and takes back (its slots 14
// and 13): 0x24 bytes on the stack of 0x005ED870 and 0x005EE6D0. The first
// three dwords are the centre's x and z and the size. The game zeroes the
// first two before it asks and leaves the rest as the stack was; ours
// zeroes all of it.
struct DecalInfo {
    uint32_t centre[2];
    uint32_t size;
    uint32_t rest[6];
};
static_assert(sizeof(DecalInfo) == 0x24, "decal info size");

float Float(uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, 4);
    return value;
}

uint32_t Bits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

void* FrameService()
{
    return Cdecl<void*>(addr::SC4_GetService86AD10EC);
}

// ====================================================================================
// The decal's own interface (`self` is the object)
// ====================================================================================

// Slot 0 (0x005ECB10): the message target interface gives the object + 8
// (null for a null object, and the reference still taken), the decal
// interface 0xE982078A and the class ID the object; everything else is the
// prop's (object + 4). The first two take their reference with the base's
// AddRef on the object + 0xC, the class ID through the vtable.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("decal QueryInterface", logged);
    if (iid == kIidMessageTarget) {
        *object = self ? self + layout::kMessageTarget : nullptr;
        This<uint32_t>(addr::cSC4Occupant_AddRef, self + layout::kOccupant);
        return 1;
    }
    if (iid == decal::kIid) {
        *object = self;
        This<uint32_t>(addr::cSC4Occupant_AddRef, self + layout::kOccupant);
        return 1;
    }
    if (iid == decal::kClassId) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return This<uint8_t, uint32_t, void**>(addr::cSC4PropOccupant_QueryInterface, self + layout::kProp, iid, object);
}

// Slot 3 (0x005ED7E0): the key's three dwords and the size kept, and the
// prop's box made the size wide and deep around the origin and 0.2 high
// (half the size on the FPU, stored as floats). The overlay is not told.
// No return value.
void __fastcall SetDecalTexture(char* self, void*, const uint32_t* key, uint32_t size)
{
    static bool logged = false;
    FirstCall("decal SetDecalTexture", logged);
    const double half = static_cast<double>(Float(size)) * static_cast<double>(kHalf);
    At<uint32_t>(self, decal::kSize) = size;
    At<uint32_t>(self, decal::kTexture) = key[0];
    At<uint32_t>(self, decal::kTexture + 4) = key[1];
    At<uint32_t>(self, decal::kTexture + 8) = key[2];
    At<float>(self, layout::kMin) = static_cast<float>(-half);
    At<uint32_t>(self, layout::kMin + 4) = 0xBDCCCCCD; // -0.1
    At<float>(self, layout::kMin + 8) = static_cast<float>(-half);
    At<float>(self, layout::kMax) = static_cast<float>(half);
    At<uint32_t>(self, layout::kMax + 4) = 0x3DCCCCCD; // 0.1
    At<float>(self, layout::kMax + 8) = static_cast<float>(half);
}

// Slot 4 (0x005ED840): the opacity kept and, while there is an overlay,
// given to it (the overlay manager's slot 17). No return value.
void __fastcall SetOpacity(char* self, void*, uint32_t opacity)
{
    static bool logged = false;
    FirstCall("decal SetOpacity", logged);
    At<uint32_t>(self, decal::kOpacity) = opacity;
    if (void* const overlays = At<void*>(self, decal::kOverlays))
        Virtual<void>(overlays, 0x44, At<uint32_t>(self, decal::kOverlayId), opacity);
}

// Slot 5 (0x005ECB90): a fade from `from` to `to` in `time` seconds: the
// two kept, the action's low byte, and the rate (to - from) / time (on the
// FPU, stored as a float; time 0 is not tested). Action 2 makes the object
// hold a reference on itself, once. Then the frame service is asked for its
// message for the object + 8, once. No return value.
void __fastcall SetOpacityFade(char* self, void*, float time, uint32_t action, uint32_t from, uint32_t to)
{
    static bool logged = false;
    FirstCall("decal SetOpacityFade", logged);
    At<uint32_t>(self, decal::kFadeNow) = from;
    At<uint32_t>(self, decal::kFadeEnd) = to;
    At<uint8_t>(self, decal::kFadeAction) = static_cast<uint8_t>(action);
    At<float>(self, decal::kFadeRate) = static_cast<float>(
        (static_cast<double>(Float(to)) - static_cast<double>(Float(from))) / static_cast<double>(time));
    if (action == 2 && !At<uint8_t>(self, decal::kSelfReference)) {
        Virtual<uint32_t>(self, 0x4);
        At<uint8_t>(self, decal::kSelfReference) = 1;
    }
    void* const frames = FrameService();
    if (frames && !At<uint8_t>(self, decal::kFrames)) {
        Virtual<void>(frames, 0x14, static_cast<void*>(self + layout::kMessageTarget), kFrameCode);
        At<uint8_t>(self, decal::kFrames) = 1;
    }
}

// Slot 6 (0x005ED870): the size kept and, while there is an overlay, put
// into what the overlay manager has of the decal (asked with slot 14, given
// back with slot 13). No return value.
void __fastcall SetDecalSize(char* self, void*, uint32_t size)
{
    static bool logged = false;
    FirstCall("decal SetDecalSize", logged);
    void* overlays = At<void*>(self, decal::kOverlays);
    At<uint32_t>(self, decal::kSize) = size;
    if (!overlays)
        return;
    DecalInfo info = {};
    Virtual<void>(overlays, 0x38, At<uint32_t>(self, decal::kOverlayId), &info);
    info.size = At<uint32_t>(self, decal::kSize);
    overlays = At<void*>(self, decal::kOverlays);
    Virtual<void>(overlays, 0x34, At<uint32_t>(self, decal::kOverlayId), &info);
}

// ====================================================================================
// The message target (`target` is the object + 8)
// ====================================================================================

// Slot 3 (0x005EE580): false for anything but the frame message. With an
// overlay ID, base flag 2 and a rate that is not zero, the fading opacity
// moves by rate * milliseconds * 0.001 (on the FPU, stored as a float).
// Before the end ((now - end) * rate below zero) the opacity becomes it and
// the overlay is told. From the end on, every frame: action 0 gives the
// overlay the opacity kept (not the end value; the fading opacity goes on
// past the end), action 1 removes the overlay and lets go of the overlay
// manager, action 2 takes an initialized prop out of the occupant manager
// and shuts it down. The overlay manager is not tested for null.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    static bool logged = false;
    FirstCall("decal DoMessage", logged);
    char* const self = target - layout::kMessageTarget;
    if (Virtual<uint32_t>(message, 0x10) != kMessageFrame)
        return 0;
    const float elapsed = Float(Virtual<uint32_t>(message, 0x4C));
    const uint32_t id = At<uint32_t>(self, decal::kOverlayId);
    if (id == 0xFFFFFFFF)
        return 1;
    if (!(At<uint8_t>(self, layout::kFlags) & 2))
        return 1;
    if (At<float>(self, decal::kFadeRate) == 0.0f)
        return 1;
    const float now = static_cast<float>(static_cast<double>(elapsed) * static_cast<double>(At<float>(self, decal::kFadeRate)) *
                                             static_cast<double>(kMillisecond) +
                                         static_cast<double>(At<float>(self, decal::kFadeNow)));
    At<float>(self, decal::kFadeNow) = now;
    // The game's test is "below zero or not a number".
    if (!((static_cast<double>(now) - static_cast<double>(At<float>(self, decal::kFadeEnd))) *
              static_cast<double>(At<float>(self, decal::kFadeRate)) >=
          0.0)) {
        At<float>(self, decal::kOpacity) = now;
        Virtual<void>(At<void*>(self, decal::kOverlays), 0x44, id, Bits(now));
        return 1;
    }
    switch (At<int8_t>(self, decal::kFadeAction)) {
    case decal::kActionNone:
        Virtual<void>(At<void*>(self, decal::kOverlays), 0x44, id, At<uint32_t>(self, decal::kOpacity));
        break;
    case decal::kActionRemoveOverlay:
        Virtual<void>(At<void*>(self, decal::kOverlays), 0x3C, id);
        if (void* const overlays = At<void*>(self, decal::kOverlays)) {
            At<void*>(self, decal::kOverlays) = nullptr;
            Virtual<uint32_t>(overlays, 0x8);
        }
        At<uint32_t>(self, decal::kOverlayId) = 0xFFFFFFFF;
        break;
    case decal::kActionRemoveOccupant: {
        char* const occupant = self + layout::kOccupant;
        if (Virtual<uint8_t>(occupant, 0x14)) {
            This<uint8_t>(addr::cSC4PropOccupant_SetInManager, self + layout::kProp, 0u);
            Virtual<uint8_t>(occupant, 0x10);
        }
        break;
    }
    default:
        break;
    }
    return 1;
}

// ====================================================================================
// The occupant part (`occupant` is the object + 0xC)
// ====================================================================================

// Slot 3 (0x005EE410): nothing when initialized. A decal that was not read
// from a save gets a box of 32 m a side around the origin and rate 0 (no
// fade: what SetDecalTexture and SetOpacityFade set before Init is lost).
// Then the base's Init. A decal read from a save whose frames byte was
// saved set is registered with the frame service again. True.
uint8_t __fastcall Init(char* occupant, void*)
{
    static bool logged = false;
    FirstCall("decal Init", logged);
    char* const self = occupant - layout::kOccupant;
    if (Virtual<uint8_t>(occupant, 0x14))
        return 1;
    if (!(At<uint8_t>(self, layout::kBaseState) & 2)) {
        for (std::size_t i = 0; i < 12; i += 4) {
            At<uint32_t>(self, layout::kMax + i) = 0x41800000;  // 16
            At<uint32_t>(self, layout::kMin + i) = 0xC1800000;  // -16
        }
        At<uint32_t>(self, decal::kFadeRate) = 0;
    }
    if (!Virtual<uint8_t>(occupant, 0x14))
        This<uint8_t>(addr::cSC4Occupant_Init, occupant);
    if (!(At<uint8_t>(self, layout::kBaseState) & 2) || !At<uint8_t>(self, decal::kFrames))
        return 1;
    if (void* const frames = FrameService())
        Virtual<void>(frames, 0x14, static_cast<void*>(self ? self + layout::kMessageTarget : nullptr), kFrameCode);
    return 1;
}

// Slot 4 (0x005EE4B0): nothing unless initialized. The overlay removed and
// the overlay manager let go of, the texture key's third dword zeroed, the
// prop's date range freed, the base's Shutdown (the prop's own is not
// called), the frame service told to stop, and the reference the object
// holds on itself released: when that was the last one the object is gone
// and the byte is written into freed memory, as in the game. True.
uint8_t __fastcall Shutdown(char* occupant, void*)
{
    static bool logged = false;
    FirstCall("decal Shutdown", logged);
    char* const self = occupant - layout::kOccupant;
    if (!Virtual<uint8_t>(occupant, 0x14))
        return 1;
    if (void* const overlays = At<void*>(self, decal::kOverlays)) {
        Virtual<void>(overlays, 0x3C, At<uint32_t>(self, decal::kOverlayId));
        At<uint32_t>(self, decal::kOverlayId) = 0xFFFFFFFF;
        if (void* const held = At<void*>(self, decal::kOverlays)) {
            At<void*>(self, decal::kOverlays) = nullptr;
            Virtual<uint32_t>(held, 0x8);
        }
    }
    At<uint32_t>(self, decal::kTexture + 8) = 0;
    if (char* const range = At<char*>(self, layout::kDates)) {
        This<void>(addr::cRZDate_Destroy, range + 0x18);
        This<void>(addr::cRZDate_Destroy, range + 0x8);
        Cdecl<void, void*>(addr::SC4_operator_delete, range);
        At<char*>(self, layout::kDates) = nullptr;
    }
    This<uint8_t>(addr::cSC4Occupant_Shutdown, occupant);
    if (At<uint8_t>(self, decal::kFrames)) {
        if (void* const frames = FrameService())
            Virtual<void>(frames, 0x18, static_cast<void*>(self ? self + layout::kMessageTarget : nullptr), kFrameCode);
        At<uint8_t>(self, decal::kFrames) = 0;
    }
    if (At<uint8_t>(self, decal::kSelfReference)) {
        Virtual<uint32_t>(self, 0x8);
        At<uint8_t>(self, decal::kSelfReference) = 0;
    }
    return 1;
}

// Slot 9 (0x005EE6D0): the centre kept (x and z of the position), the box
// moved as the prop's SetPosition does it (half the width and the height on
// the FPU, half the depth stored as a float first), and, while there is an
// overlay, the centre and the size put into what the overlay manager has of
// the decal. No return value.
void __fastcall SetPosition(char* occupant, void*, const float* position)
{
    static bool logged = false;
    FirstCall("decal SetPosition", logged);
    char* const self = occupant - layout::kOccupant;
    uint32_t x = 0, y = 0, z = 0;
    std::memcpy(&x, &position[0], 4);
    std::memcpy(&z, &position[2], 4);
    At<uint32_t>(self, decal::kCentre) = x;
    At<uint32_t>(self, decal::kCentre + 4) = z;
    const double halfX =
        (static_cast<double>(At<float>(self, layout::kMax)) - static_cast<double>(At<float>(self, layout::kMin))) *
        static_cast<double>(kHalf);
    const double height =
        static_cast<double>(At<float>(self, layout::kMax + 4)) - static_cast<double>(At<float>(self, layout::kMin + 4));
    const float halfZ = static_cast<float>(
        (static_cast<double>(At<float>(self, layout::kMax + 8)) - static_cast<double>(At<float>(self, layout::kMin + 8))) *
        static_cast<double>(kHalf));
    At<float>(self, layout::kMin) = static_cast<float>(static_cast<double>(position[0]) - halfX);
    At<float>(self, layout::kMax) = static_cast<float>(halfX + static_cast<double>(position[0]));
    std::memcpy(&y, &position[1], 4);
    At<uint32_t>(self, layout::kMin + 4) = y;
    At<float>(self, layout::kMax + 4) = static_cast<float>(height + static_cast<double>(position[1]));
    At<float>(self, layout::kMin + 8) = static_cast<float>(static_cast<double>(position[2]) - static_cast<double>(halfZ));
    At<float>(self, layout::kMax + 8) = static_cast<float>(static_cast<double>(halfZ) + static_cast<double>(position[2]));
    void* overlays = At<void*>(self, decal::kOverlays);
    if (!overlays)
        return;
    DecalInfo info = {};
    Virtual<void>(overlays, 0x38, At<uint32_t>(self, decal::kOverlayId), &info);
    info.size = At<uint32_t>(self, decal::kSize);
    info.centre[0] = At<uint32_t>(self, decal::kCentre);
    info.centre[1] = At<uint32_t>(self, decal::kCentre + 4);
    overlays = At<void*>(self, decal::kOverlays);
    Virtual<void>(overlays, 0x34, At<uint32_t>(self, decal::kOverlayId), &info);
}

// Slot 30 (0x005ED8D0): when base flag 2 changes, the decal is shown or
// hidden, then the base's SetAllFlags. With an overlay manager the overlay
// is switched on or off (its slot 16). Without one, when the flag comes on
// and the texture key's third dword is not zero and the size is above zero:
// the terrain view (the terrain's slot 66, through 0x00602160; not tested
// for null) gives overlay manager 0 for a decal whose rate is below 0.1 in
// magnitude, else manager 2; the decal is added there (slot 5: the key, the
// centre, the size, rotation 0) and given its opacity. AL is the flags'
// byte, from the base.
uint32_t __fastcall SetAllFlags(char* occupant, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("decal SetAllFlags", logged);
    char* const self = occupant - layout::kOccupant;
    if ((At<uint8_t>(self, layout::kFlags) ^ flags) & 2) {
        const uint32_t on = flags & 2;
        if (void* const overlays = At<void*>(self, decal::kOverlays)) {
            Virtual<void>(overlays, 0x40, At<uint32_t>(self, decal::kOverlayId), on ? 1u : 0u);
        } else if (on && At<uint32_t>(self, decal::kTexture + 8) && At<float>(self, decal::kSize) > 0.0f) {
            const uint32_t kind = std::fabs(static_cast<double>(At<float>(self, decal::kFadeRate))) < kSlowFade ? 0u : 2u;
            void* const view = Cdecl<void*>(addr::SC4_TerrainPicker);
            void* const made = Virtual<void*>(view, 0x48, kind);
            This<void>(addr::SC4RefPtr_Assign, self + decal::kOverlays, made);
            const uint32_t id = Virtual<uint32_t>(At<void*>(self, decal::kOverlays), 0x14, static_cast<void*>(self + decal::kTexture),
                                                  static_cast<void*>(self + decal::kCentre), At<uint32_t>(self, decal::kSize), 0u);
            At<uint32_t>(self, decal::kOverlayId) = id;
            Virtual<void>(At<void*>(self, decal::kOverlays), 0x44, id, At<uint32_t>(self, decal::kOpacity));
        }
    }
    return This<uint32_t>(addr::cSC4Occupant_SetAllFlags, occupant, flags);
}

// ====================================================================================
// Save (`part` is the object + 0x10)
// ====================================================================================

// Slot 3 (0x005ED710): version 3, the prop's record (its Write, with the
// same `this`), then the overlay ID as it is, the texture key, the size,
// the centre, the opacity, the fade's opacity, end and rate, the action and
// the two bytes. When the prop's Write fails nothing more is written; the
// answer is in both cases whether the stream is free of errors.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("decal Write", logged);
    char* const self = part - layout::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 3u);
    if (This<uint8_t>(addr::cSC4PropOccupant_Write, part, stream)) {
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, decal::kOverlayId));
        Cdecl<void*>(addr::SC4Stream_WriteResourceKey, stream, static_cast<void*>(self + decal::kTexture));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, decal::kSize));
        Cdecl<void*>(addr::SC4Stream_WriteVector2, stream, static_cast<void*>(self + decal::kCentre));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, decal::kOpacity));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, decal::kFadeNow));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, decal::kFadeEnd));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, decal::kFadeRate));
        Virtual<uint8_t>(stream, 0x10, uint32_t{At<uint8_t>(self, decal::kFadeAction)});
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, decal::kSelfReference)});
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, decal::kFrames)});
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 4 (0x005EF4E0): false at once for a version other than 3 (read into
// the argument's stack slot, the stream pointer until then), and false when
// the prop's Read fails. Else the fields in Write's order; the overlay ID
// is read into a stack slot that holds `this` before (and is then the ID of
// an overlay of the city that was saved: nothing uses it while the overlay
// manager pointer is null). AL only.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("decal Read", logged);
    char* const self = part - layout::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 3)
        return 0;
    if (!This<uint8_t>(addr::cSC4PropOccupant_Read, part, stream))
        return 0;
    uint32_t id = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(part));
    Virtual<uint8_t>(stream, 0x24, &id);
    At<uint32_t>(self, decal::kOverlayId) = id;
    Cdecl<void*>(addr::SC4Stream_ReadResourceKey, stream, static_cast<void*>(self + decal::kTexture));
    Virtual<uint8_t>(stream, 0x30, self + decal::kSize);
    Cdecl<void*>(addr::SC4Stream_ReadVector2, stream, static_cast<void*>(self + decal::kCentre));
    Virtual<uint8_t>(stream, 0x30, self + decal::kOpacity);
    Virtual<uint8_t>(stream, 0x30, self + decal::kFadeNow);
    Virtual<uint8_t>(stream, 0x30, self + decal::kFadeEnd);
    Virtual<uint8_t>(stream, 0x30, self + decal::kFadeRate);
    Virtual<uint8_t>(stream, 0x10, self + decal::kFadeAction);
    Virtual<uint8_t>(stream, 0x14, self + decal::kSelfReference);
    Virtual<uint8_t>(stream, 0x14, self + decal::kFrames);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 5 (0x005ED5A0).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("decal GetGZCLSID", logged);
    return decal::kClassId;
}

// ====================================================================================
// Installation
// ====================================================================================

// GetGZCLSID's code is six bytes (mov eax, the ID; ret): the detour's jump
// covers the first instruction exactly.
const Detour kDecalDetours[] = {
    {addr::cSC4PropOccupantTerrainDecal_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
    {addr::cSC4PropOccupantTerrainDecal_SetDecalTexture, reinterpret_cast<void*>(&SetDecalTexture)},
    {addr::cSC4PropOccupantTerrainDecal_SetOpacity, reinterpret_cast<void*>(&SetOpacity)},
    {addr::cSC4PropOccupantTerrainDecal_SetOpacityFade, reinterpret_cast<void*>(&SetOpacityFade)},
    {addr::cSC4PropOccupantTerrainDecal_SetDecalSize, reinterpret_cast<void*>(&SetDecalSize)},
    {addr::cSC4PropOccupantTerrainDecal_DoMessage, reinterpret_cast<void*>(&DoMessage)},
    {addr::cSC4PropOccupantTerrainDecal_Init, reinterpret_cast<void*>(&Init)},
    {addr::cSC4PropOccupantTerrainDecal_Shutdown, reinterpret_cast<void*>(&Shutdown)},
    {addr::cSC4PropOccupantTerrainDecal_SetPosition, reinterpret_cast<void*>(&SetPosition)},
    {addr::cSC4PropOccupantTerrainDecal_SetAllFlags, reinterpret_cast<void*>(&SetAllFlags)},
    {addr::cSC4PropOccupantTerrainDecal_Write, reinterpret_cast<void*>(&Write)},
    {addr::cSC4PropOccupantTerrainDecal_Read, reinterpret_cast<void*>(&Read)},
    {addr::cSC4PropOccupantTerrainDecal_GetGZCLSID, reinterpret_cast<void*>(&GetGZCLSID)},
};

bool InstallDecal(registry::Mode) { return InstallDetours(kDecalDetours, "decal"); }

const bool g_registered = registry::Register({
    "simulators.propkinds.decal",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantTerrainDecal_QueryInterface,
    "Our cSC4PropOccupantTerrainDecal: QueryInterface, the texture, size, opacity and fade, the frame message, Init, "
    "Shutdown, SetPosition, SetAllFlags, Write, Read and GetGZCLSID (13 detours; src/core/propkinds.h)",
    false,
    InstallDecal,
});

// ====================================================================================
// Our object
// ====================================================================================

// Copies of the game's seven vtables, taken at the first construction,
// after every PostAppInit slot swap of the other groups (the prop's
// GetRandomChance in 0x00AA4730).
constexpr int kSlots0 = 7, kSlots4 = 24, kSlots8 = 4, kSlotsC = 34, kSlots10 = 6, kSlots14 = 25, kSlots28 = 5;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable10[kSlots10];
void* g_vtable14[kSlots14];
void* g_vtable28[kSlots28];

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags);

// The holder part's slot 22 (0x005ED6A0).
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
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4PropOccupantTerrainDecal_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4PropOccupant_DerivedVftable_AA4730), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4PropOccupantTerrainDecal_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4PropOccupantTerrainDecal_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable10, reinterpret_cast<const void*>(addr::cSC4PropOccupantTerrainDecal_vftable_10), sizeof(g_vtable10));
    std::memcpy(g_vtable14, reinterpret_cast<const void*>(addr::cSC4PropOccupantTerrainDecal_vftable_14), sizeof(g_vtable14));
    std::memcpy(g_vtable28, reinterpret_cast<const void*>(addr::cSC4PropOccupantTerrainDecal_vftable_28), sizeof(g_vtable28));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable10[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtable14[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    log::Write("propkinds decal object: vtables copied (+0 slots 3/5 %p %p, +8 slot 3 %p, +C slots 3/4/9/30 %p %p %p %p)",
               g_vtable[3], g_vtable[5], g_vtable8[3], g_vtableC[3], g_vtableC[4], g_vtableC[9], g_vtableC[30]);
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

// The constructor (detour of 0x005EE360): the prop constructed at its
// address on the object + 4, our vtables, no overlay ID, the key and the
// size zeroed, the centre the game's zero float (0x00B231A0), opacity 1, a
// fade from 1 to 0 at rate 1 with no action, no overlay manager. +0x9B is
// left as it is. (The game stores an abstract vtable, 0x00AA45E4, at +0
// before the prop's constructor; nothing reads it.)
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("decal Construct", logged);
    This<void*>(addr::cSC4PropOccupant_cSC4PropOccupant, self + layout::kProp);
    SetVtables(self);
    At<uint32_t>(self, decal::kOverlayId) = 0xFFFFFFFF;
    At<uint32_t>(self, decal::kTexture) = 0;
    At<uint32_t>(self, decal::kTexture + 4) = 0;
    At<uint32_t>(self, decal::kTexture + 8) = 0;
    At<uint32_t>(self, decal::kSize) = 0;
    const uint32_t zero = Global<uint32_t>(addr::cSC4PropOccupant_Zero);
    At<uint32_t>(self, decal::kCentre + 4) = zero;
    At<uint32_t>(self, decal::kCentre) = zero;
    At<uint32_t>(self, decal::kFadeEnd) = 0;
    At<uint8_t>(self, decal::kFadeAction) = 0;
    At<uint8_t>(self, decal::kSelfReference) = 0;
    At<uint8_t>(self, decal::kFrames) = 0;
    At<uint32_t>(self, decal::kOpacity) = 0x3F800000;
    At<uint32_t>(self, decal::kFadeNow) = 0x3F800000;
    At<uint32_t>(self, decal::kFadeRate) = 0x3F800000;
    At<void*>(self, decal::kOverlays) = nullptr;
    static int created = 0;
    if (created++ < 3)
        log::Write("propkinds decal object: constructed %p", static_cast<void*>(self));
    return self;
}

// The destructor's body (detour of 0x005ED500; the deleting destructor is
// its one caller): the class's vtables, the overlay manager released (the
// overlay itself is not removed here: Shutdown does that), then the
// occupant base destroyed. The game passes the base null for a null object
// + 4, which no object has.
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("decal Destroy", logged);
    SetVtables(self);
    if (void* const overlays = At<void*>(self, decal::kOverlays))
        Virtual<uint32_t>(overlays, 0x8);
    This<void>(addr::cSC4Occupant_Destroy, self + layout::kOccupant);
}

// The deleting destructor (detour of 0x005EF1C0): the body, and with flag 1
// the memory back to the pool. A null object is still destroyed first, as
// in the game.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("decal DeletingDestructor", logged);
    Destroy(self, nullptr);
    if ((flags & 1) && self)
        PoolFree(kPool, self, kPoolReopenAt);
    return self;
}

// The pool allocator (detour of 0x005ED6E0; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("decal Allocate", logged);
    return PoolAllocate(kPool);
}

// The factory (detour of 0x005EF180): an item of the pool, constructed; the
// prop interface (object + 4) is what it hands out.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("decal Create", logged);
    char* const self = static_cast<char*>(Construct(static_cast<char*>(PoolAllocate(kPool)), nullptr));
    return self ? self + layout::kProp : nullptr;
}

const Detour kDecalObjectDetours[] = {
    {addr::cSC4PropOccupantTerrainDecal_Construct, reinterpret_cast<void*>(&Construct)},
    {addr::cSC4PropOccupantTerrainDecal_Allocate, reinterpret_cast<void*>(&Allocate)},
    {addr::cSC4PropOccupantTerrainDecal_Create, reinterpret_cast<void*>(&Create)},
    {addr::cSC4PropOccupantTerrainDecal_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
    {addr::cSC4PropOccupantTerrainDecal_Destroy, reinterpret_cast<void*>(&Destroy)},
};

// Every construction path: the factory (through the class registration's
// pointer when a save is read), the prop manager's and the terrain code's
// allocator and constructor calls. The deleting destructor is reached
// through the holder part's slot 22: ours in the copies, and by the detour
// for the game's thunk.
bool InstallDecalObject(registry::Mode) { return InstallDetours(kDecalObjectDetours, "decal object"); }

const bool g_registeredObject = registry::Register({
    "simulators.propkinds.decal_object",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantTerrainDecal_Construct,
    "Our cSC4PropOccupantTerrainDecal object: the constructor, the pool allocation, the factory, the destructor and the "
    "deleting destructor (5 detours), identity; methods from copies of the game vtables taken once "
    "(src/core/propkinds.h)",
    false,
    InstallDecalObject,
});

} // namespace
} // namespace opensc4::propkinds
