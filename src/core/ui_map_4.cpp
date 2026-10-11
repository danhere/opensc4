// The map window (the data view in the city's interface; 0x9E8 bytes,
// constructor 0x007A0D50, interface 0x67C5BEDE on its main vtable
// 0x00AB8150, message target at +0x4 with vtable 0x00AB8140, window part at
// +0x8 with vtable 0x00AB7EE0), four of its functions (0x007A49B0 to
// 0x007A60CC):
//
//   - a data view chosen by ID: the view's exemplar read (which occupants
//     the city view shows, how terrain and zones are drawn, the colours, the
//     data kind and its scaling, the legend, the title), or the plain city
//     view for ID 0;
//   - the message handler: the redraw flag set by the messages the view
//     listens for, and the picture redrawn on the timer;
//   - the view on top applied (a pushed view, else the chosen one) and the
//     two buttons that depend on it;
//   - the window procedure's button handling: the data kind buttons, the
//     zone and legend switches.
//
// Replacement, off by default:
//
//   ui.map.part4: the four functions above.
//
// The picture itself is not ours: its drawing (0x007A2F60, which runs to
// 0x007A4884 with its jump tables up to 0x007A49B0) is called by address
// where the original calls it, after a view change, on the timer and for
// the two redraw buttons. Windows, the city view, the terrain and zone
// services and the exemplar are the game's, called through their vtables.
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

// Offsets from the start of the map window object.
namespace map {
constexpr std::size_t kTarget = 0x4;       // the message target part
constexpr std::size_t kWin = 0x8;          // the window part
constexpr std::size_t kParent = 0x50;      // the window holding the buttons
constexpr std::size_t kOverlay = 0x13C;    // an object with a vector at +0x10 and a flag at +0xF
constexpr std::size_t kActive = 0x142;     // set by message 0x26D31EC1, cleared by 0x26D31EC2
constexpr std::size_t kKind = 0x144;       // the data kind on show
constexpr std::size_t kStyle = 0x148;
constexpr std::size_t kColours = 0x14C;    // 256 colours
constexpr std::size_t kBlended = 0x54C;    // the same with alpha multiplied in
constexpr std::size_t kFlagA = 0x94C;
constexpr std::size_t kHasLimit = 0x94D;
constexpr std::size_t kLimit = 0x950;
constexpr std::size_t kRange = 0x954;
constexpr std::size_t kScale = 0x958;      // 64 bits: 0xFFFF00 / range
constexpr std::size_t kHalfScale = 0x960;  // 64 bits: 0x7FFF00 / range
constexpr std::size_t kLegend = 0x980;
constexpr std::size_t kTitle = 0x984;      // a game string
constexpr std::size_t kTitleBegin = 0x988;
constexpr std::size_t kTitleEnd = 0x98C;
constexpr std::size_t kExtraA = 0x998;
constexpr std::size_t kExtraB = 0x99C;
constexpr std::size_t kHomeKind = 0x9A0;   // the kind the "all" button goes back to
constexpr std::size_t kZoneKind = 0x9A4;   // the kind of the zone page
constexpr std::size_t kLastKind = 0x9A8;
constexpr std::size_t kSwitch = 0x9AC;     // the 0x8100/0x8101 pair
constexpr std::size_t kBytes = 0x9AD;      // up to eight bytes of the view
constexpr std::size_t kByteCount = 0x9B5;
constexpr std::size_t kTypesBegin = 0x9BC; // sorted message types that ask for a redraw
constexpr std::size_t kTypesEnd = 0x9C0;
constexpr std::size_t kPushed = 0x9C8;     // list of pushed views, the top one first
constexpr std::size_t kView = 0x9CC;       // the view on show (exemplar instance)
constexpr std::size_t kSource = 0x9D0;     // the view's data source object
constexpr std::size_t kChosen = 0x9D4;     // the view chosen through the interface
constexpr std::size_t kHasChosen = 0x9D8;
constexpr std::size_t kDirty = 0x9D9;
constexpr std::size_t kDrawnAt = 0x9DC;    // milliseconds, set by the drawing
} // namespace map

// Offsets in the occupant filter the city view gets (0x30 bytes).
namespace filter {
constexpr std::size_t kAll = 0x2C;
constexpr std::size_t kNetworks = 0x2D;
constexpr std::size_t kFlag = 0x2E;
} // namespace filter

constexpr uint32_t kViewInstanceBase = 0x4A0B6800;
constexpr uint32_t kExemplarType = 0x6534284A;
constexpr uint32_t kViewGroup = 0x690F693F;
constexpr uint32_t kExemplarIid = 0xA52160F5;
constexpr uint32_t kButtonIid = 0x8810;

constexpr uint32_t kTickMessage = 0x86AD10EE;
constexpr uint32_t kActivateMessage = 0x26D31EC1;
constexpr uint32_t kDeactivateMessage = 0x26D31EC2;
constexpr uint32_t kMovedMessage = 0xA6B79602;

constexpr uint32_t kKindZones = 9;
constexpr uint32_t kKindTraffic = 0x4C;

// The game's 0x007A2F60: the map's picture drawn. The renderer's; called by
// address only.
constexpr std::uintptr_t kDrawPicture = 0x007A2F60;

struct WinMessage {
    uint32_t type;
    uint32_t a;
    uint32_t b;
};

struct ResourceKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};

struct ListNode {
    ListNode* next;
    ListNode* previous;
    uint32_t value;
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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void DrawPicture(char* self)
{
    This<void>(kDrawPicture, self);
}

// The game's 0x0079E540 takes the window in EDI, the number of buttons in
// EAX, the first button's ID in ECX and the ID to switch on on the stack,
// which the caller removes: (window, count, first, selected, function).
__declspec(naked) void SelectButtonThunk()
{
    __asm push edi
    __asm mov edi, dword ptr [esp + 8]
    __asm mov eax, dword ptr [esp + 12]
    __asm mov ecx, dword ptr [esp + 16]
    __asm push dword ptr [esp + 20]
    __asm call dword ptr [esp + 28]
    __asm add esp, 4
    __asm pop edi
    __asm ret
}

void SelectButton(void* window, uint32_t count, uint32_t first, uint32_t selected)
{
    using ThunkFn = void(__cdecl*)(void*, uint32_t, uint32_t, uint32_t, std::uintptr_t);
    reinterpret_cast<ThunkFn>(&SelectButtonThunk)(window, count, first, selected, addr::SC4UI_F79E540);
}

// The game's 0x0079E5A0 takes the window in EDI, the first child's ID in EBX
// and the number of children and a flag value on the stack, which the caller
// removes: (window, first, count, on, function).
__declspec(naked) void FlagChildrenThunk()
{
    __asm push edi
    __asm push ebx
    __asm mov edi, dword ptr [esp + 12]
    __asm mov ebx, dword ptr [esp + 16]
    __asm push dword ptr [esp + 24]
    __asm push dword ptr [esp + 24]
    __asm call dword ptr [esp + 36]
    __asm add esp, 8
    __asm pop ebx
    __asm pop edi
    __asm ret
}

void FlagChildren(void* window, uint32_t first, uint32_t count, uint32_t on)
{
    using ThunkFn = void(__cdecl*)(void*, uint32_t, uint32_t, uint32_t, std::uintptr_t);
    reinterpret_cast<ThunkFn>(&FlagChildrenThunk)(window, first, count, on, addr::SC4UI_F79E5A0);
}

// A property of the view's exemplar, or null.
void* Property(void* exemplar, uint32_t id)
{
    return Virtual<void*>(exemplar, 0x24, id);
}

void* Value(void* property)
{
    return Virtual<void*>(property, 0x14);
}

uint32_t Uint32Of(void* property)
{
    return *Virtual<uint32_t*>(Value(property), 0x124);
}

uint8_t BoolOf(void* property)
{
    return *Virtual<uint8_t*>(Value(property), 0xFC);
}

// The property's value 0, 1 or 2 as the city view's occupant mode 1, 3 or 2;
// any other value leaves the mode as it is.
void ReadViewMode(void* property, uint32_t& mode)
{
    switch (Uint32Of(property)) {
    case 0:
        mode = 1;
        break;
    case 1:
        mode = 3;
        break;
    case 2:
        mode = 2;
        break;
    default:
        break;
    }
}

void ClearTitle(char* self)
{
    char* const begin = At<char*>(self, map::kTitleBegin);
    if (begin != At<char*>(self, map::kTitleEnd)) {
        *begin = 0;
        At<char*>(self, map::kTitleEnd) = At<char*>(self, map::kTitleBegin);
    }
}

// The game's 0x007A49B0: the view with this ID put on show (0 is the plain
// city view). False when the window is not active or the view's exemplar is
// missing.
uint8_t __fastcall ShowView(char* self, void*, uint32_t id)
{
    if (!At<uint8_t>(self, map::kActive))
        return 0;
    if (id != 0)
        id += kViewInstanceBase;
    if (id == At<uint32_t>(self, map::kView))
        return 1;

    if (void* const source = At<void*>(self, map::kSource)) {
        Virtual<void>(source, 0x10);
        if (void* const held = At<void*>(self, map::kSource)) {
            At<void*>(self, map::kSource) = nullptr;
            Release(held);
        }
    }
    This<void>(addr::SC4UI_F79FB20, self);

    void* const cityView = Global<void*>(addr::SC4_Service43DC4);
    if (id == 0) {
        // A filter that lets every occupant through.
        void* all = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xC});
        if (all) {
            This<void*>(addr::SC4_OccupantFilterBase, all);
            At<std::uintptr_t>(all, 0x0) = addr::SC4OccupantFilterAll_Vtable;
            At<std::uintptr_t>(all, 0x4) = addr::SC4OccupantFilterAll_Vtable2;
            AddRef(all);
        }
        Virtual<void>(cityView, 0x5C, uint32_t{1});
        Virtual<void>(Global<void*>(addr::SC4_Terrain), 0x158, uint32_t{0});
        Virtual<void>(Global<void*>(addr::SC4_Terrain), 0x150, uint32_t{1});
        void* held = all;
        Virtual<void>(cityView, 0x4C, &held);
        void* const zones = Global<void*>(addr::SC4_ZoneManager);
        // The original pushes a dword whose upper three bytes are not set.
        const uint8_t shown = Virtual<uint8_t>(zones, 0x44);
        Virtual<void>(Global<void*>(addr::SC4_ZoneManager), 0x48, uint32_t{shown});
        Virtual<void>(Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x90), 0xBC, uint32_t{0});
        This<void>(addr::SC4UI_OverlayClear, At<void*>(self, map::kOverlay));
        At<uint32_t>(self, map::kKind) = 0;
        At<uint8_t>(self, map::kFlagA) = 0;
        At<uint8_t>(self, map::kHasLimit) = 0;
        At<uint32_t>(self, map::kLegend) = 0;
        ClearTitle(self);
        This<uint8_t>(addr::SC4UI_F7A1A00, self);
        DrawPicture(self);

        void* check = nullptr;
        void* const parent = Virtual<void*>(self + map::kWin, 0x2C);
        if (Virtual<uint8_t>(parent, 0x94, uint32_t{0x4203}, uint32_t{0xCA318385}, &check)) {
            Virtual<void>(check, 0x20, uint32_t{1}, uint32_t{0});
            Virtual<void>(check, 0x24);
            Virtual<void>(Virtual<void*>(check, 0xC), 0x170);
        }
        At<uint32_t>(self, map::kView) = 0;
        if (check)
            Release(check);
        if (all)
            Release(all);
        return 1;
    }

    void* resource = nullptr;
    ResourceKey key{kExemplarType, kViewGroup, id};
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, &key, kExemplarIid, &resource, uint32_t{0},
                          uint32_t{0})) {
        if (resource)
            Release(resource);
        return 0;
    }

    // The occupants the city view shows. Without memory the original goes on
    // with no filter and faults at the first store, as this does.
    char* occupants = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x30});
    if (occupants)
        occupants = This<char*>(addr::SC4UI_F79FE60, occupants);
    if (occupants)
        AddRef(occupants);

    void* const exemplar = Virtual<void*>(resource, 0x1C);
    uint8_t all = 1;
    if (void* const property = Property(exemplar, 0x4A0B47E0)) {
        void* const value = Value(property);
        const uint32_t* types = Virtual<const uint32_t*>(value, 0x19C);
        uint32_t count = Virtual<uint32_t>(value, 0x10);
        if (count != 0) {
            do {
                const uint32_t type = *types++;
                if (type != 0)
                    This<void>(addr::SC4UI_F79FB90, occupants, type);
            } while (--count != 0);
        }
        all = 0;
    }
    if (void* const property = Property(exemplar, 0x4A0B47E1))
        all = BoolOf(property);
    At<uint8_t>(occupants, filter::kAll) = all;
    At<uint8_t>(occupants, filter::kFlag) = 1;
    if (void* const property = Property(exemplar, 0x4A0B47E2)) {
        if (BoolOf(property) != 0)
            At<uint8_t>(occupants, filter::kFlag) = 0;
    }

    // The original pushes dwords whose upper three bytes are left over here
    // and for the zone manager below.
    uint8_t flag = 0;
    if (void* const property = Property(exemplar, 0x4A0B47F0))
        flag = BoolOf(property);
    Virtual<void>(Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x90), 0xBC, uint32_t{flag});

    uint32_t mode = 1;
    if (void* const property = Property(exemplar, 0x4A0B47E3))
        ReadViewMode(property, mode);
    Virtual<void>(cityView, 0x5C, mode);
    Virtual<void>(Global<void*>(addr::SC4_Terrain), 0x158, uint32_t{mode == 3 ? 1u : 0u});
    Virtual<void>(Global<void*>(addr::SC4_Terrain), 0x150, uint32_t{mode == 1 ? 1u : 0u});
    if (void* const property = Property(exemplar, 0x4A0B47F1))
        ReadViewMode(property, mode);
    switch (mode) {
    case 1:
        At<uint8_t>(occupants, filter::kNetworks) = 1;
        This<void>(addr::SC4UI_F7A12D0, occupants, uint32_t{4});
        This<void>(addr::SC4UI_F7A12D0, occupants, uint32_t{7});
        break;
    case 2:
        At<uint8_t>(occupants, filter::kNetworks) = 1;
        This<void>(addr::SC4UI_F7A12D0, occupants, uint32_t{4});
        This<void>(addr::SC4UI_F79E9E0, occupants, uint32_t{1});
        break;
    case 3:
        At<uint8_t>(occupants, filter::kNetworks) = 1;
        This<void>(addr::SC4UI_F7A12D0, occupants, uint32_t{7});
        This<void>(addr::SC4UI_F79E980, occupants, uint32_t{1});
        break;
    default:
        break;
    }

    if (void* const property = Property(exemplar, 0x6B7C5CDD)) {
        void* const value = Value(property);
        const uint32_t* const items = Virtual<const uint32_t*>(value, 0x19C);
        const uint8_t count = static_cast<uint8_t>(Virtual<uint32_t>(value, 0x10));
        At<uint8_t>(self, map::kByteCount) = count;
        if (count > 8)
            At<uint8_t>(self, map::kByteCount) = 8;
        if (At<uint8_t>(self, map::kByteCount) != 0) {
            int32_t index = 0;
            do {
                At<uint8_t>(self, map::kBytes + static_cast<std::size_t>(index)) = static_cast<uint8_t>(items[index]);
                ++index;
            } while (index < static_cast<int32_t>(At<uint8_t>(self, map::kByteCount)));
        }
    }

    void* held = occupants;
    Virtual<void>(cityView, 0x4C, &held);

    uint8_t zonesShown = Virtual<uint8_t>(Global<void*>(addr::SC4_ZoneManager), 0x44);
    if (void* const property = Property(exemplar, 0x4A0B47E6))
        zonesShown = *Virtual<uint8_t*>(Value(property), 0x104);
    Virtual<void>(Global<void*>(addr::SC4_ZoneManager), 0x48, uint32_t{zonesShown});

    if (void* const property = Property(exemplar, 0x4A0B47E4)) {
        void* const value = Value(property);
        uint32_t* const colours = &At<uint32_t>(self, map::kColours);
        uint32_t* const blended = &At<uint32_t>(self, map::kBlended);
        const uint32_t count = Virtual<uint32_t>(value, 0x10);
        const void* const stops = Virtual<const void*>(value, 0x19C);
        Cdecl<void>(addr::SC4UI_F78D520, colours, uint32_t{0x100}, stops, count);
        for (uint32_t index = 0; index < 0x100; ++index) {
            const uint32_t colour = colours[index];
            const uint32_t alpha = colour >> 24;
            const uint32_t red = (((colour & 0xFF0000) * alpha + 0x800000) >> 8) & 0xFF0000;
            const uint32_t blue = (((colour & 0xFF) * alpha + 0x80) >> 8) & 0xFF;
            const uint32_t green = (((colour & 0xFF00) * alpha + 0x8000) >> 8) & 0xFF00;
            blended[index] = red + blue + green + (colour & 0xFF000000);
        }
    }

    This<void>(addr::SC4UI_OverlayClear, At<void*>(self, map::kOverlay));
    At<uint32_t>(self, map::kKind) = 0;
    if (void* const property = Property(exemplar, 0x4A0B47E5))
        At<uint32_t>(self, map::kKind) = Uint32Of(property);
    const uint32_t kind = At<uint32_t>(self, map::kKind);
    This<void>(addr::SC4UI_OverlaySetFlag, At<void*>(self, map::kOverlay),
               uint32_t{kind == kKindZones || kind == kKindTraffic ? 1u : 0u});

    At<uint32_t>(self, map::kStyle) = 1;
    if (void* const property = Property(exemplar, 0x4A0B47ED))
        At<uint32_t>(self, map::kStyle) = Uint32Of(property);
    At<uint8_t>(self, map::kFlagA) = 0;
    if (void* const property = Property(exemplar, 0x4A0B47E7))
        At<uint8_t>(self, map::kFlagA) = BoolOf(property);
    At<uint8_t>(self, map::kHasLimit) = 0;
    if (void* const property = Property(exemplar, 0x4A0B47E8)) {
        At<uint8_t>(self, map::kHasLimit) = 1;
        At<uint32_t>(self, map::kLimit) = Uint32Of(property);
    }
    At<uint8_t>(self, map::kFlagA) = At<uint8_t>(self, map::kFlagA) | At<uint8_t>(self, map::kHasLimit);

    At<uint32_t>(self, map::kScale) = 0x100;
    At<uint32_t>(self, map::kScale + 4) = 0;
    At<uint32_t>(self, map::kHalfScale) = 0x100;
    At<uint32_t>(self, map::kHalfScale + 4) = 0;
    At<uint32_t>(self, map::kRange) = 0x100;
    if (void* const property = Property(exemplar, 0x4A0B47EC)) {
        const uint32_t range = Uint32Of(property);
        if (range > 1 && range < 0x1000000) {
            // 64-bit divisions in the original (0x009F0710 unsigned,
            // 0x009EFEB0 signed); the range is under 2^24 here.
            At<uint32_t>(self, map::kScale) = 0xFFFF00u / range;
            At<uint32_t>(self, map::kScale + 4) = 0;
            At<int64_t>(self, map::kHalfScale) =
                static_cast<int64_t>(int32_t{0x7FFF00} / static_cast<int32_t>(range));
        }
        At<uint32_t>(self, map::kRange) = range;
    }

    At<uint32_t>(self, map::kLegend) = 0;
    if (void* const property = Property(exemplar, 0x4A0B47E9))
        At<uint32_t>(self, map::kLegend) = Uint32Of(property);
    ClearTitle(self);
    if (void* const property = Property(exemplar, 0x4A0B47EA))
        Virtual<void>(Value(property), 0xDC, static_cast<void*>(self + map::kTitle));
    At<uint32_t>(self, map::kExtraA) = 0;
    At<uint32_t>(self, map::kExtraB) = 0;
    if (void* const property = Property(exemplar, 0x4A0B47EF))
        At<uint32_t>(self, map::kExtraA) = Uint32Of(property);
    if (void* const property = Property(exemplar, 0x4A0B47EE))
        At<uint32_t>(self, map::kExtraB) = Uint32Of(property);

    // The view's data source, made by class ID.
    if (void* const property = Property(exemplar, 0x4A0B47EB)) {
        const uint32_t classId = Uint32Of(property);
        void* const com = Cdecl<void*>(addr::RZGetCOM);
        void** const source = &At<void*>(self, map::kSource);
        if (void* const old = *source) {
            *source = nullptr;
            Release(old);
        }
        if (Virtual<uint8_t>(com, 0xC, classId, uint32_t{0x8A175D6B}, source))
            Virtual<void>(*source, 0xC);
    }

    This<uint8_t>(addr::SC4UI_F7A1A00, self);
    DrawPicture(self);

    void* check = nullptr;
    void* const parent = Virtual<void*>(self + map::kWin, 0x2C);
    if (Virtual<uint8_t>(parent, 0x94, uint32_t{0x4203}, uint32_t{0xCA318385}, &check)) {
        Virtual<void>(check, 0x20, uint32_t{1}, uint32_t{0});
        if (void* const property = Property(exemplar, 0x4A0B47F5)) {
            if (BoolOf(property) != 0)
                Virtual<void>(check, 0x20, uint32_t{1}, uint32_t{1});
        }
        Virtual<void>(check, 0x24);
        Virtual<void>(Virtual<void*>(check, 0xC), 0x170);
    }
    At<uint32_t>(self, map::kView) = id;
    This<void>(addr::SC4UI_F7A17B0, self);
    if (check)
        Release(check);
    Release(occupants);
    if (resource)
        Release(resource);
    return 1;
}

// The game's 0x007A54D0 (slot 3 of the message target at +0x4): always
// true. A message type in the view's sorted list marks the picture for a
// redraw; the tick message redraws a marked picture once a second (four
// times a second, always, for the traffic kind).
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    char* const self = part - map::kTarget;
    uint32_t type = Virtual<uint32_t>(message, 0x10);
    const uint32_t* const end = At<const uint32_t*>(self, map::kTypesEnd);
    const uint32_t* const found = Cdecl<const uint32_t*>(addr::SC4_LowerBoundUint32,
                                                         At<const uint32_t*>(self, map::kTypesBegin), end, &type, type,
                                                         uint32_t{0});
    if (found != end && type >= *found)
        At<uint8_t>(self, map::kDirty) = 1;

    switch (type) {
    case kActivateMessage:
    case kDeactivateMessage: {
        // The original passes null for a null object; `part` is never that.
        void* const target = part;
        void* const ticks = Cdecl<void*>(addr::SC4_GetService86AD10EC);
        if (type == kActivateMessage) {
            Virtual<void>(ticks, 0x14, target, uint32_t{0});
            At<uint8_t>(self, map::kActive) = 1;
        } else {
            Virtual<void>(ticks, 0x18, target, uint32_t{0});
            At<uint8_t>(self, map::kActive) = 0;
        }
        return 1;
    }
    case kTickMessage: {
        if (This<uint8_t>(addr::SC4UI_F7A1A00, self))
            At<uint8_t>(self, map::kDirty) = 1;
        uint32_t interval = 1000;
        if (At<uint32_t>(self, map::kKind) == kKindTraffic) {
            interval = 250;
            At<uint8_t>(self, map::kDirty) = 1;
        }
        if (At<uint8_t>(self, map::kDirty)) {
            const uint32_t now = Cdecl<uint32_t>(addr::RZ_TimeMilliseconds);
            if (now - At<uint32_t>(self, map::kDrawnAt) >= interval)
                DrawPicture(self);
        }
        return 1;
    }
    case kMovedMessage: {
        char* const first = Virtual<char*>(message, 0x28);
        char* const second = Virtual<char*>(message, 0x3C);
        if (At<uint32_t>(first, 0x4) != At<uint32_t>(second, 0x4))
            At<uint8_t>(self, map::kDirty) = 1;
        return 1;
    }
    default:
        return 1;
    }
}

// The game's 0x007A5610: the view on top put on show: the last pushed view,
// else the chosen one; the two buttons that tell which, and the legend when
// its window is shown.
void __fastcall ApplyView(char* self, void*)
{
    uint8_t pushed = 1;
    uint32_t id;
    ListNode* const list = At<ListNode*>(self, map::kPushed);
    if (list->next != list) {
        id = list->next->value;
    } else if (At<uint8_t>(self, map::kHasChosen)) {
        id = At<uint32_t>(self, map::kChosen);
        pushed = 0;
    } else {
        id = 0;
    }
    ShowView(self, nullptr, id);

    void* const chosenButton = Virtual<void*>(At<void*>(self, map::kParent), 0x8C, uint32_t{0x8A2871D6});
    void* const pushedButton = Virtual<void*>(At<void*>(self, map::kParent), 0x8C, uint32_t{0x8A2871C5});
    if (chosenButton)
        Virtual<void>(chosenButton, 0x110, uint32_t{1}, uint32_t{pushed == 0 ? 1u : 0u});
    if (pushedButton) {
        // The original pushes the flag byte with three bytes of `this` above it.
        Virtual<void>(pushedButton, 0x110, uint32_t{1}, uint32_t{pushed});
        if (pushed)
            Virtual<void>(pushedButton, 0x5C);
    }
    if (Cdecl<uint8_t>(addr::SC4Win_IsChildVisible, At<void*>(self, map::kParent), uint32_t{0x8A2871C3}))
        This<void>(addr::SC4UI_F7A04F0, self, id);
}

// A data kind put on show through the interface (main slot 3) and in the
// legend.
void SetKind(char* self, uint32_t kind)
{
    Virtual<void>(self, 0xC, kind);
    This<void>(addr::SC4UI_F7A04F0, self, kind);
}

// The same with the kind kept in a field, which the original reads again
// for each call.
void SetKindFrom(char* self, std::size_t field)
{
    Virtual<void>(self, 0xC, At<uint32_t>(self, field));
    This<void>(addr::SC4UI_F7A04F0, self, At<uint32_t>(self, field));
}

// The zone page, or the kind on show when the zone button is up.
void ShowZonesOrKind(char* self, uint32_t button, bool keepHome)
{
    void* holder = nullptr;
    void* const parent = Virtual<void*>(self + map::kWin, 0x2C);
    void** const out = This<void**>(addr::SC4RefPtr_Reset, &holder);
    const bool down = Virtual<uint8_t>(parent, 0x94, button, kButtonIid, out) && Virtual<uint8_t>(holder, 0x20);
    if (keepHome) {
        // Buttons 0x5008 and 0x5108.
        if (down) {
            SetKind(self, kKindZones);
            At<uint32_t>(self, map::kLastKind) = kKindZones;
        } else {
            SetKindFrom(self, map::kZoneKind);
            At<uint32_t>(self, map::kLastKind) = At<uint32_t>(self, map::kZoneKind);
        }
    } else if (down) {
        // Buttons 0x4A32CA92 and 0xEA8B80A6 with 0x5005 down.
        This<void>(addr::SC4UI_F79DF10, self);
        SetKindFrom(self, map::kHomeKind);
    } else if (At<uint32_t>(self, map::kKind) == kKindTraffic) {
        This<void>(addr::SC4UI_F79DFB0, self);
        SetKindFrom(self, map::kZoneKind);
    } else if (At<uint32_t>(self, map::kKind) == kKindZones) {
        This<void>(addr::SC4UI_F79DFB0, self);
        SetKind(self, kKindZones);
    } else {
        This<void>(addr::SC4UI_F79DE70, self);
        SetKindFrom(self, map::kLastKind);
    }
    if (holder)
        Release(holder);
}

// The data kinds of buttons 0x5001 to 0x5011 (0 marks the two with their own
// handling) and of 0x6001 to 0x600C.
constexpr uint32_t kKinds5000[] = {0, 5, 3, 0x1B, 0, 0xA, 7, 0, 0x20, 2, 1, 0x1E, 0x1C, 8, 0x18, 0x19, 0x1A};
constexpr uint32_t kKinds6000[] = {0x28, 0x29, 0x2A, 0x32, 0x33, 0x34, 0x35, 0x36, 0x3C, 0x3D, 0x3E, 0x3F};

// The game's 0x007A56E0 (slot 3 of the window part at +0x8, which runs to
// 0x007A60CC with jump tables up to 0x007A61D4): a button of the map window
// pressed; everything else, and the buttons after their handling, go to the
// window base class.
uint32_t __fastcall WinMessageHandler(char* win, void*, WinMessage* message)
{
    char* const self = win - map::kWin;
    const auto base = [&]() { return This<uint32_t>(addr::SC4Win_DoWinMessage, win, message); };

    if (message->type != 3)
        return base();
    if (!Cdecl<uint8_t>(addr::SC4Tutorial_FilterWinMessage, message))
        return 1;
    if (message->a != 0x287259F6)
        return base();

    uint32_t id = message->b;
    void* const buttons = At<void*>(self, map::kParent);

    // 0x5101 to 0x5111 and 0x6101 to 0x610C do what the buttons 0x100 below
    // them do.
    if ((id >= 0x5101 && id <= 0x5111) || (id >= 0x6101 && id <= 0x610C))
        id -= 0x100;

    if (id >= 0x5001 && id <= 0x5011) {
        if (id == 0x5005) {
            SelectButton(buttons, 0x11, 0x5001, 0x5005);
            This<void>(addr::SC4UI_F79DF10, self);
            SetKindFrom(self, map::kHomeKind);
            At<uint32_t>(self, map::kLastKind) = At<uint32_t>(self, map::kHomeKind);
        } else if (id == 0x5008) {
            SelectButton(buttons, 0x11, 0x5001, 0x5008);
            This<void>(addr::SC4UI_F79DFB0, self);
            ShowZonesOrKind(self, 0x8001, true);
        } else {
            This<void>(addr::SC4UI_F7A0F30, self, id, kKinds5000[id - 0x5001]);
        }
        return base();
    }
    if (id >= 0x6001 && id <= 0x600C) {
        This<void>(addr::SC4UI_F7A0FA0, self, id, kKinds6000[id - 0x6001]);
        return base();
    }
    if (id >= 0x8003 && id <= 0x800C) {
        This<void>(addr::SC4UI_F7A0FE0, self, id, kKindTraffic + (id - 0x8003));
        return base();
    }

    switch (id) {
    case 0x8001:
        SelectButton(buttons, 2, 0x8001, 0x8001);
        FlagChildren(At<void*>(self, map::kParent), 0x8003, 0xA, 0);
        SetKind(self, kKindZones);
        break;
    case 0x8002:
        SelectButton(buttons, 2, 0x8001, 0x8002);
        FlagChildren(At<void*>(self, map::kParent), 0x8003, 0xA, 1);
        SetKindFrom(self, map::kZoneKind);
        break;
    case 0x8100:
        SelectButton(buttons, 2, 0x8100, 0x8100);
        At<uint8_t>(self, map::kSwitch) = 0;
        ApplyView(self, nullptr);
        break;
    case 0x8101:
        SelectButton(buttons, 2, 0x8100, 0x8101);
        At<uint8_t>(self, map::kSwitch) = 1;
        ApplyView(self, nullptr);
        break;
    case 0x0A32CAC3:
    case 0xA32CACD4:
    case 0xEA8B80A7:
    case 0xEA8B80A8:
        This<void>(addr::SC4UI_F79E050, self);
        break;
    case 0x4A32CA92:
    case 0xEA8B80A6:
        ShowZonesOrKind(self, 0x5005, false);
        break;
    case 0x8A2871D5:
        Virtual<void>(Global<void*>(addr::SC4_Commands), 0x50, uint32_t{0x6A935CF1}, uint32_t{0}, uint32_t{0});
        break;
    case 0xCA2871F0:
    case 0xCA2871F1:
        // The two redraw buttons: not passed on.
        This<uint8_t>(addr::SC4UI_F7A1A00, self);
        DrawPicture(self);
        This<void>(addr::SC4UI_F79DE70, self);
        return 1;
    case 0xEA2871D9:
    case 0xEA2871E9:
        Virtual<void>(win, 0x118);
        break;
    default:
        break;
    }
    return base();
}

bool InstallMap4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7A49B0, reinterpret_cast<void*>(&ShowView)},
        {addr::SC4UI_F7A54D0, reinterpret_cast<void*>(&DoMessage)},
        {addr::SC4UI_F7A5610, reinterpret_cast<void*>(&ApplyView)},
        {addr::SC4UI_F7A56E0, reinterpret_cast<void*>(&WinMessageHandler)},
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

const bool g_map4Registered = registry::Register({
    "ui.map.part4",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7A49B0,
    "Our map window: a data view put on show, the redraw messages, the view on top, the buttons (docs/ui.md)",
    false,
    InstallMap4,
});

} // namespace
} // namespace opensc4::ui
