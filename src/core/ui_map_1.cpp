// The map window of the city view, first part (0x0079E400 to 0x007A0280).
// The city view's interface object keeps the window at +0x300 as a cIGZWin
// pointer; the object starts 8 bytes before it (its window part is at +8, a
// message target at +4). Three things live in this range:
//
//   - the window's Init and Shutdown (cIGZWin slots 4 and 5), the removal of
//     its extra notifications, the colours of its legend, and two helpers of
//     its button groups that take their arguments in registers;
//   - the occupant filter the window makes for its layers (0x30 bytes, class
//     0xCA0E3852, vtables 0x00AB7E44 and 0x00AB7E2C): constructor, the
//     occupant test, Write, the class ID, a type added, the two network
//     filters taken or dropped;
//   - the markers the window keeps by occupant (0x18 bytes: the occupant, an
//     object made by the service at 0x00B43D1C, a size and a place): copy,
//     assignment and the object made and started.
//
// Replacement, off by default:
//
//   ui.map.part1: the functions above.
//
// Still the game's in this range: everything that builds the map's picture
// (the scalers and painters called by 0x007A2380 and 0x007A2740, the icon
// blend 0x0079E600 and its loop 0x0079F4D0, the occupant fill 0x0079FEF0 and
// its fenced twin 0x007A00B0), which belongs to the renderer work, and the
// set's tree code (0x0079FAD0, 0x0079FBD0, 0x0079FEB0) and the shared lower
// bound 0x0079FA90.
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

// Offsets from the window part (object +8), as Init and Shutdown see it.
namespace win {
constexpr std::size_t kFlags = 0xD8;        // an embedded object; slot 6 sets a flag
constexpr std::size_t kStarted = 0x128;     // Init ran
constexpr std::size_t kMessages = 0x130;    // the message server, one reference
constexpr std::size_t kOverlay = 0x134;     // a 0x50-byte helper object (0x007DCC10)
constexpr std::size_t kOverlayShown = 0x139; // the helper is registered with the view
constexpr std::size_t kHeld = 0x9B0;        // an object shut down (slot 4) and released
} // namespace win

// Offsets from the object's start.
namespace map {
constexpr std::size_t kTarget = 0x4;        // the message target
constexpr std::size_t kWindow = 0x8;        // the window part
constexpr std::size_t kMessages = 0x138;
constexpr std::size_t kMarkerName = 0x988;  // the text of a string: begin, end
constexpr std::size_t kNotifications = 0x9BC; // vector of message IDs: begin, end
} // namespace map

// Offsets from the filter's start; its serialisable part is at +4.
namespace filter {
constexpr std::size_t kSerial = 0x4;
constexpr std::size_t kTypes = 0xC;         // vector of occupant types
constexpr std::size_t kTypesEnd = 0x10;
constexpr std::size_t kTypesCapacity = 0x14;
constexpr std::size_t kGroups = 0x18;       // vector of values asked of a building
constexpr std::size_t kGroupsEnd = 0x1C;
constexpr std::size_t kGroupsCapacity = 0x20;
constexpr std::size_t kFilterA = 0x24;      // two more filters, from the network manager
constexpr std::size_t kFilterB = 0x28;
constexpr std::size_t kNotListed = 0x2C;    // the answer for a type that is not in the list
constexpr std::size_t kNoGroup = 0x2D;      // the answer for a building with none of the groups
constexpr std::size_t kLoose = 0x2E;        // 1 after the constructor
} // namespace filter

constexpr uint32_t kWinIid = 0x22BA0121;
constexpr uint32_t kButtonIid = 0x8810;
constexpr uint32_t kLegendIid = 0xCA318385;
constexpr uint32_t kLegend = 0x4203;
constexpr uint32_t kFilterClass = 0xCA0E3852;

// One marker of the window's set, keyed by the occupant's address.
struct Marker {
    void* occupant;
    void* object;
    float size;
    float x;
    float y;
    float z;
};
static_assert(sizeof(Marker) == 0x18);

// What the marker's object is given before it starts: two bytes, a 3 by 3
// identity, the place and twice the size. 0x38 bytes of the original's frame.
struct MarkerPlace {
    uint8_t kind;
    uint8_t flags;
    uint8_t unset[2];
    float axes[9];
    float x;
    float y;
    float z;
    float scale;
};
static_assert(sizeof(MarkerPlace) == 0x38);

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

// The game's inlined reference holder: assign.
void Assign(void*& holder, void* object)
{
    void* const old = holder;
    if (object != old) {
        if (object)
            AddRef(object);
        holder = object;
        if (old)
            Release(old);
    }
}

// --- The window --------------------------------------------------------------

// The game's 0x0079FB20. `this` is the object's start. Takes the message
// target off the message server for every message ID in the vector at
// +0x9BC and empties the vector.
void __fastcall RemoveNotifications(char* self, void*)
{
    uint32_t* it = At<uint32_t*>(self, map::kNotifications);
    uint32_t* const end = At<uint32_t*>(self, map::kNotifications + 4);
    for (; it != end; ++it)
        Virtual<uint8_t>(At<void*>(self, map::kMessages), 0x18, static_cast<void*>(self + map::kTarget), *it);
    At<uint32_t*>(self, map::kNotifications + 4) = At<uint32_t*>(self, map::kNotifications);
}

// The game's 0x0079E400, cIGZWin slot 4 (Init). `this` is the window part.
// Sets four flags, runs the base window's Init and, the first time, asks
// the message server for three messages and makes the helper object.
uint8_t __fastcall Init(char* self, void*)
{
    void* const flags = self + win::kFlags;
    Virtual<uint32_t>(flags, 0x18, uint32_t{1}, uint32_t{1});
    Virtual<uint32_t>(flags, 0x18, uint32_t{0x10}, uint32_t{0});
    Virtual<uint32_t>(flags, 0x18, uint32_t{0x100}, uint32_t{1});
    Virtual<uint32_t>(flags, 0x18, uint32_t{4}, uint32_t{0});
    This<uint8_t>(addr::cGZWin_Init, self);
    if (At<uint8_t>(self, win::kStarted))
        return 1;
    At<uint8_t>(self, win::kStarted) = 1;
    if (!Global<uint8_t>(addr::SC4UI_MapWindowStartedOnce))
        Global<uint8_t>(addr::SC4UI_MapWindowStartedOnce) = 1;

    if (!At<void*>(self, win::kMessages)) {
        void* const messages = Global<void*>(addr::SC4_MessageServer);
        At<void*>(self, win::kMessages) = messages;
        if (!messages)
            return 0;
        AddRef(messages);
        void* const target = self - 4;
        Virtual<uint8_t>(At<void*>(self, win::kMessages), 0x14, target, uint32_t{0x26D31EC1});
        Virtual<uint8_t>(At<void*>(self, win::kMessages), 0x14, target, uint32_t{0x26D31EC2});
        Virtual<uint8_t>(At<void*>(self, win::kMessages), 0x14, target, uint32_t{0xA6B79602});
    }

    if (!At<void*>(self, win::kOverlay)) {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x50});
        void* const overlay = memory ? This<void*>(addr::SC4UI_MapOverlay_Construct, memory) : nullptr;
        At<void*>(self, win::kOverlay) = overlay;
        // Not checked for null, as in the original.
        AddRef(overlay);
        This<void>(addr::SC4UI_MapOverlay_Start, At<void*>(self, win::kOverlay));
        This<void>(addr::SC4UI_MapOverlay_SetFlagD, At<void*>(self, win::kOverlay), uint32_t{1});
        This<void>(addr::SC4UI_MapOverlay_SetFlagE, At<void*>(self, win::kOverlay), uint32_t{1});
    }
    return 1;
}

// The game's 0x0079FC90, cIGZWin slot 5 (Shutdown). `this` is the window
// part. Undoes Init: seven sibling windows are taken off the window
// manager, the helper object, the notifications and the object at +0x9B0
// are let go, then the base window's Shutdown runs.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (At<uint8_t>(self, win::kStarted)) {
        At<uint8_t>(self, win::kStarted) = 0;
        if (void* const manager = Virtual<void*>(self, 0x18)) {
            void* const parent = Virtual<void*>(self, 0x2C);
            static const uint32_t siblings[] = {
                0x8A2871B2, 0x8A2871B1, 0xAA32BCD4, 0x4200, 0xAA32BCD5, 0x8A2871C3, 0x8A2871C4,
            };
            for (const uint32_t id : siblings)
                Virtual<uint32_t>(manager, 0x5C, Virtual<void*>(parent, 0x8C, id));
        }

        if (void* const overlay = At<void*>(self, win::kOverlay)) {
            void* const view = Global<void*>(addr::SC4_View43DD0);
            if (view && At<uint8_t>(self, win::kOverlayShown)) {
                Virtual<uint32_t>(view, 0x84, overlay);
                At<uint8_t>(self, win::kOverlayShown) = 0;
            }
            This<void>(addr::SC4UI_MapOverlay_Stop, At<void*>(self, win::kOverlay));
            Release(At<void*>(self, win::kOverlay));
            At<void*>(self, win::kOverlay) = nullptr;
        }

        if (At<void*>(self, win::kMessages)) {
            RemoveNotifications(self - 8, nullptr);
            void* const target = self - 4;
            Virtual<uint8_t>(At<void*>(self, win::kMessages), 0x18, target, uint32_t{0xA6B79602});
            Virtual<uint8_t>(At<void*>(self, win::kMessages), 0x18, target, uint32_t{0x26D31EC2});
            Virtual<uint8_t>(At<void*>(self, win::kMessages), 0x18, target, uint32_t{0x26D31EC1});
            Release(At<void*>(self, win::kMessages));
            At<void*>(self, win::kMessages) = nullptr;
        }

        if (void* const held = At<void*>(self, win::kHeld)) {
            Virtual<uint32_t>(held, 0x10);
            Release(At<void*>(self, win::kHeld));
            At<void*>(self, win::kHeld) = nullptr;
        }
    }
    This<uint8_t>(addr::cGZWin_Shutdown, self);
    return 1;
}

// The game's 0x0079EA40. `this` is the object's start; the city view calls
// it once the window is loaded. Empties the legend control (child 0x4203 of
// the window's parent) and gives it nine colours.
void __fastcall SetLegendColours(char* self, void*)
{
    void* legend = nullptr;
    void* const parent = Virtual<void*>(self + map::kWindow, 0x2C);
    if (void* const held = legend) {
        legend = nullptr;
        Release(held);
    }
    if (Virtual<uint8_t>(parent, 0x94, kLegend, kLegendIid, &legend)) {
        Virtual<uint32_t>(legend, 0x20, uint32_t{0xFFFFFFFF}, uint32_t{0});
        Virtual<uint32_t>(legend, 0x18);
        static const uint32_t colours[] = {
            0x00FF00, 0x44CC22, 0x1E911E, 0x5B81D0, 0x3D6EB8, 0x3C42AD, 0xD0D030, 0xC6B733, 0xBC9F37,
        };
        uint32_t index = 1;
        for (const uint32_t colour : colours)
            Virtual<uint32_t>(legend, 0x1C, index++, colour);
    }
    if (legend)
        Release(legend);
}

// The game's 0x0079E540: the parent window in EDI, the first button's ID in
// ECX, the number of buttons in EAX and the chosen ID on the stack (the
// caller removes it). Of a row of buttons with IDs in sequence the chosen
// one is set on (slot 6) and the others off (slot 7).
void __cdecl ChooseButton(void* parent, uint32_t first, uint32_t count, uint32_t chosen)
{
    uint32_t id = first;
    for (; count != 0; --count, ++id) {
        void* button = nullptr;
        if (Virtual<uint8_t>(parent, 0x94, id, kButtonIid, &button)) {
            if (id == chosen)
                Virtual<uint32_t>(button, 0x18);
            else
                Virtual<uint32_t>(button, 0x1C);
        }
        if (button)
            Release(button);
    }
}

__declspec(naked) void ChooseButtonThunk()
{
    __asm push dword ptr [esp + 4]
    __asm push eax
    __asm push ecx
    __asm push edi
    __asm call ChooseButton
    __asm add esp, 16
    __asm ret
}

// The game's 0x0079E5A0: the parent window in EDI, the first child's ID in
// EBX, the number of children and a value on the stack (the caller removes
// them). Sets flag 2 of each child window to the value. The original keeps
// the child it holds in the first argument's stack slot; ours leaves the
// caller's slots alone (no caller reads them afterwards).
void __cdecl SetChildrenFlag(void* parent, uint32_t first, uint32_t count, uint32_t value)
{
    for (uint32_t index = 0; index < count; ++index) {
        void* child = nullptr;
        if (Virtual<uint8_t>(parent, 0x94, index + first, kWinIid, &child))
            Virtual<uint32_t>(child, 0x110, uint32_t{2}, value);
        if (child)
            Release(child);
    }
}

__declspec(naked) void SetChildrenFlagThunk()
{
    __asm push dword ptr [esp + 8]
    __asm push dword ptr [esp + 8]
    __asm push ebx
    __asm push edi
    __asm call SetChildrenFlag
    __asm add esp, 16
    __asm ret
}

// --- The markers ---------------------------------------------------------------

// The game's 0x0079F480: a marker copied into raw memory (a new node of the
// set), one reference more on its two objects. Returns `this`.
Marker* __fastcall MarkerCopy(Marker* self, void*, const Marker* from)
{
    self->occupant = from->occupant;
    if (self->occupant)
        AddRef(self->occupant);
    self->object = from->object;
    if (self->object)
        AddRef(self->object);
    self->size = from->size;
    self->x = from->x;
    self->y = from->y;
    self->z = from->z;
    return self;
}

// The game's 0x0079F760: a marker assigned. Returns `this`.
Marker* __fastcall MarkerAssign(Marker* self, void*, Marker* from)
{
    if (from != self)
        Assign(self->occupant, from->occupant);
    if (&from->object != &self->object)
        Assign(self->object, from->object);
    self->size = from->size;
    self->x = from->x;
    self->y = from->y;
    self->z = from->z;
    return self;
}

// The game's 0x0079F620. `this` is the object's start. Makes the marker's
// object by the name the window keeps at +0x988 (nothing is done while the
// name is empty), places it at the marker's place with twice its size,
// starts it and stores it in the marker. Bytes 2 and 3 of the structure
// handed over are not set by the original (stack garbage); ours has zeros.
void __fastcall MarkerStart(char* self, void*, Marker* marker)
{
    if (At<char*>(self, map::kMarkerName) == At<char*>(self, map::kMarkerName + 4))
        return;
    void* object = nullptr;
    void* const service = Global<void*>(addr::SC4_Service43D1C);
    if (Virtual<uint8_t>(service, 0x1C, At<char*>(self, map::kMarkerName), &object)) {
        MarkerPlace place = {};
        place.x = marker->x;
        place.y = marker->y;
        place.z = marker->z;
        place.axes[0] = 1.0f;
        place.axes[4] = 1.0f;
        place.axes[8] = 1.0f;
        const double scale = static_cast<double>(marker->size) + static_cast<double>(marker->size);
        place.scale = static_cast<float>(scale);
        // Compared with the constant 1.0f at 0x00A81228.
        place.flags = scale == 1.0 ? 1 : 3;
        place.kind = 2;
        Virtual<uint32_t>(object, 0x20, &place);
        Virtual<uint32_t>(object, 0xC, uint32_t{0});
        Assign(marker->object, object);
    }
    if (object)
        Release(object);
}

// --- The occupant filter ---------------------------------------------------------

// The game's 0x0079FE60: the filter's constructor. Returns `this`.
char* __fastcall FilterConstruct(char* self, void*)
{
    This<void*>(addr::SC4_OccupantFilterBase, self);
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_MapFilter_Vtable;
    At<std::uintptr_t>(self, filter::kSerial) = addr::SC4UI_MapFilter_SerialVtable;
    At<uint32_t>(self, filter::kTypes) = 0;
    At<uint32_t>(self, filter::kTypesEnd) = 0;
    At<uint32_t>(self, filter::kTypesCapacity) = 0;
    At<uint32_t>(self, filter::kGroups) = 0;
    At<uint32_t>(self, filter::kGroupsEnd) = 0;
    At<uint32_t>(self, filter::kGroupsCapacity) = 0;
    At<uint32_t>(self, filter::kFilterA) = 0;
    At<uint32_t>(self, filter::kFilterB) = 0;
    At<uint8_t>(self, filter::kNotListed) = 0;
    At<uint8_t>(self, filter::kNoGroup) = 0;
    At<uint8_t>(self, filter::kLoose) = 1;
    return self;
}

// The game's 0x0079FEA0: the filter's class ID (slot 5 of its serialisable
// part, and called by the class registration at 0x004608B0).
uint32_t __fastcall FilterClassId(void*, void*)
{
    return kFilterClass;
}

// The game's 0x0079FB90. `this` is the filter. Appends an occupant type to
// the list.
void __fastcall FilterAddType(char* self, void*, uint32_t type)
{
    uint32_t* const end = At<uint32_t*>(self, filter::kTypesEnd);
    if (end != At<uint32_t*>(self, filter::kTypesCapacity)) {
        if (end)
            *end = type;
        At<uint32_t*>(self, filter::kTypesEnd) += 1;
        return;
    }
    This<void>(addr::SC4PtrVector_InsertN, static_cast<void*>(self + filter::kTypes), end, &type, &type,
        uint32_t{1}, uint32_t{1});
}

// One of the filter's two extra filters taken from the network manager
// (its slot at `slot`) or dropped.
void UseNetworkFilter(char* self, std::size_t field, std::size_t slot, uint8_t on)
{
    if (!on) {
        if (void* const held = At<void*>(self, field)) {
            At<void*>(self, field) = nullptr;
            Release(held);
        }
        return;
    }
    void* const manager = Cdecl<void*>(addr::SC4_GetNetworkManager);
    Assign(At<void*>(self, field), Virtual<void*>(manager, slot));
}

// The game's 0x0079E980. `this` is the filter.
void __fastcall FilterUseA(char* self, void*, uint8_t on)
{
    UseNetworkFilter(self, filter::kFilterA, 0x48, on);
}

// The game's 0x0079E9E0. `this` is the filter.
void __fastcall FilterUseB(char* self, void*, uint8_t on)
{
    UseNetworkFilter(self, filter::kFilterB, 0x4C, on);
}

// The game's 0x0079F7F0, the filter's slot 3 (IsOccupantIncluded). An
// occupant of type 0x497F6D9D is first looked up on the lot manager by the
// two numbers its slot 11 gives: with no lot there, or a lot whose slot 33
// is under 2, the answer is the byte at +0x2E. Otherwise the type list
// decides (+0x2C, turned over when the type is listed); a building
// (0xC772BF98) is then judged by the second list; an occupant turned down is
// still taken when one of the two extra filters takes it, or when it is of
// type 0xA823821E with a kind of 0xAB72FBB3 or 0xCB79919B.
uint8_t __fastcall FilterIncludes(char* self, void*, void* occupant)
{
    uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (type == 0x497F6D9D) {
        // The original's frame leaves 16 bytes for what slot 11 fills.
        uint32_t cell[4];
        Virtual<uint32_t>(occupant, 0x2C, static_cast<void*>(cell));
        void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, cell[0], cell[1], uint32_t{0});
        if (!lot || Virtual<int32_t>(lot, 0x84) < 2)
            return At<uint8_t>(self, filter::kLoose);
    }

    // The original keeps the answer in its argument's stack slot and hands
    // that slot's address to the search as its fourth argument.
    uint32_t answer = At<uint8_t>(self, filter::kNotListed);
    uint32_t* const end = At<uint32_t*>(self, filter::kTypesEnd);
    if (end != Cdecl<uint32_t*>(addr::SC4_FindUint32, At<uint32_t*>(self, filter::kTypes), end, &type, &answer))
        answer = static_cast<uint8_t>(answer) == 0;
    uint8_t result = static_cast<uint8_t>(answer);

    if (result) {
        if (type == 0xC772BF98) {
            void* building = nullptr;
            if (Virtual<uint8_t>(occupant, 0x0, uint32_t{0xA821EF94}, &building)) {
                const uint32_t* it = At<uint32_t*>(self, filter::kGroups);
                const uint32_t* const last = At<uint32_t*>(self, filter::kGroupsEnd);
                result = At<uint8_t>(self, filter::kNoGroup);
                for (; it != last; ++it) {
                    if (Virtual<uint8_t>(building, 0x58, *it)) {
                        result = result == 0;
                        break;
                    }
                }
                Release(building);
            }
        }
        if (result)
            return result;
    }

    if (void* const extra = At<void*>(self, filter::kFilterA)) {
        if (Virtual<uint8_t>(extra, 0xC, occupant))
            return 1;
    }
    if (void* const extra = At<void*>(self, filter::kFilterB)) {
        if (Virtual<uint8_t>(extra, 0xC, occupant))
            return 1;
    }
    if (At<uint8_t>(self, filter::kLoose) && type == 0xA823821E) {
        void* typed = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, uint32_t{0xE9793A65}, &typed)) {
            const uint32_t kind = Virtual<uint32_t>(typed, 0x1C);
            if (kind == 0xAB72FBB3 || kind == 0xCB79919B) {
                if (typed)
                    Release(typed);
                return 1;
            }
        }
        if (typed)
            Release(typed);
    }
    return result;
}

// The game's 0x0079F9A0, slot 3 of the filter's serialisable part (Write).
// `this` is that part (filter +4). Version 2, the two lists with their
// counts, five bytes (+0x2C, +0x2D, whether each extra filter is held,
// +0x2E), then the base filter's part. For the two "is held" bytes the
// original pushes a dword whose upper bytes are left over; ours pushes 0 or
// 1.
uint8_t __fastcall FilterWrite(char* part, void*, void* stream)
{
    char* const self = part - filter::kSerial;
    Virtual<uint8_t>(stream, 0x1C, uint32_t{2});

    Virtual<uint8_t>(stream, 0x20, static_cast<int32_t>((reinterpret_cast<intptr_t>(At<uint32_t*>(self,
        filter::kTypesEnd)) - reinterpret_cast<intptr_t>(At<uint32_t*>(self, filter::kTypes))) >> 2));
    for (uint32_t index = 0; index < static_cast<uint32_t>(At<uint32_t*>(self, filter::kTypesEnd)
             - At<uint32_t*>(self, filter::kTypes)); ++index)
        Virtual<uint8_t>(stream, 0x24, At<uint32_t*>(self, filter::kTypes)[index]);

    Virtual<uint8_t>(stream, 0x20, static_cast<int32_t>((reinterpret_cast<intptr_t>(At<uint32_t*>(self,
        filter::kGroupsEnd)) - reinterpret_cast<intptr_t>(At<uint32_t*>(self, filter::kGroups))) >> 2));
    for (uint32_t index = 0; index < static_cast<uint32_t>(At<uint32_t*>(self, filter::kGroupsEnd)
             - At<uint32_t*>(self, filter::kGroups)); ++index)
        Virtual<uint8_t>(stream, 0x24, At<uint32_t*>(self, filter::kGroups)[index]);

    Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, filter::kNotListed)});
    Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, filter::kNoGroup)});
    Virtual<uint8_t>(stream, 0x14, uint32_t{At<void*>(self, filter::kFilterA) != nullptr});
    Virtual<uint8_t>(stream, 0x14, uint32_t{At<void*>(self, filter::kFilterB) != nullptr});
    Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, filter::kLoose)});
    return This<uint8_t>(addr::SC4OccupantFilterBase_Write, part, stream);
}

bool InstallMap1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F79E400, reinterpret_cast<void*>(&Init)},
        {addr::SC4UI_F79E540, reinterpret_cast<void*>(&ChooseButtonThunk)},
        {addr::SC4UI_F79E5A0, reinterpret_cast<void*>(&SetChildrenFlagThunk)},
        {addr::SC4UI_F79E980, reinterpret_cast<void*>(&FilterUseA)},
        {addr::SC4UI_F79E9E0, reinterpret_cast<void*>(&FilterUseB)},
        {addr::SC4UI_F79EA40, reinterpret_cast<void*>(&SetLegendColours)},
        {addr::SC4UI_F79F480, reinterpret_cast<void*>(&MarkerCopy)},
        {addr::SC4UI_F79F620, reinterpret_cast<void*>(&MarkerStart)},
        {addr::SC4UI_F79F760, reinterpret_cast<void*>(&MarkerAssign)},
        {addr::SC4UI_F79F7F0, reinterpret_cast<void*>(&FilterIncludes)},
        {addr::SC4UI_F79F9A0, reinterpret_cast<void*>(&FilterWrite)},
        {addr::SC4UI_F79FB20, reinterpret_cast<void*>(&RemoveNotifications)},
        {addr::SC4UI_F79FB90, reinterpret_cast<void*>(&FilterAddType)},
        {addr::SC4UI_F79FC90, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4UI_F79FE60, reinterpret_cast<void*>(&FilterConstruct)},
        {addr::SC4UI_F79FEA0, reinterpret_cast<void*>(&FilterClassId)},
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

const bool g_map1Registered = registry::Register({
    "ui.map.part1",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F79E400,
    "Our map window, part 1: Init and Shutdown, the legend's colours, the button helpers, the layers' occupant "
    "filter and the markers (docs/ui.md)",
    false,
    InstallMap1,
});

} // namespace
} // namespace opensc4::ui
