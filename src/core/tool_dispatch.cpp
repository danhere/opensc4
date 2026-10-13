// The dispatch tool of the city view (0x004B9D60 to 0x004BABC0).
// Replacement, off by default:
//
//   ui.tools.dispatch: the view input control of 0x90 bytes (vtable
//     0x00A90268, message target 0x00A90258 at +0x28, ID 0x68E1900A) the view
//     makes for its dispatch commands with a dispatch type (0 police, 1 fire,
//     5 fire plane, 6 police helicopter), and the occupant filter of its
//     picks (0xC bytes, vtables 0x00A9023C and 0x00A90224).
//
// The tool sends the emergency services out: a click on the ground asks the
// dispatch manager (cISC4DispatchManager, the city's slot 39) for a dispatch
// of the tool's type there, which puts a marker (a pylon, cISC4PylonOccupant)
// on the ground. A click on a marker picks it up (it becomes the manager's
// active pylon and follows the pointer, shown as allowed or not); the next
// click puts it down: back on the building it came from the dispatch is
// called off, anywhere else a dispatch is asked for at the new place. A
// click with the modifier bit 1, or a second click within 500 ms, selects
// the marker's vehicle (the vehicle manager's slot 34) and tells the game
// (message 0x49F10111). The cursor text shows how many units are free.
// Markers of a MySim are not the tool's (the MySim dispatch tool, type 4,
// has those: ui_mysim_tools_2.cpp).
//
// Fields: +0x2C the dispatch type, +0x30 whether a dispatch is allowed under
// the pointer, +0x34 and +0x38 the pointer's offset from the marker when it
// was picked up, +0x40 a stopwatch between clicks (mode 4, on real time as
// the MySim dispatch tool's is: the range is not one of sim_timers'), +0x58
// the cursor to go back to, +0x5C the marker under the pointer (lit,
// referenced), +0x60 the cursor text, +0x74 whether the tool is the view's
// current one, +0x78 the filter (referenced), +0x7C the marker's position
// and +0x88 its dispatch state when it was picked up, put back when the tool
// goes away while it holds one (+0x88 is not set by the constructor).
//
// Shared code is the base's (tool_base.cpp): QueryInterface 0x004B9DE0,
// SetGrabOffset 0x004BA070, the target's thunk 0x004BA870.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_dispatch {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kInited = 0x04; // byte
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kCursor = 0x10; // cursor ID
constexpr std::size_t kView = 0x1C;   // the city view
} // namespace base

// The tool's fields.
namespace field {
constexpr std::size_t kTarget = 0x28;        // the message target part (vtable 0x00A90258)
constexpr std::size_t kType = 0x2C;          // the dispatch type
constexpr std::size_t kAllowed = 0x30;       // byte: a dispatch is allowed under the pointer
constexpr std::size_t kGrabX = 0x34;         // the pointer's offset from the held marker
constexpr std::size_t kGrabZ = 0x38;
constexpr std::size_t kClickWatch = 0x40;    // a stopwatch of 0x18 bytes, mode 4
constexpr std::size_t kIdleCursor = 0x58;    // the cursor away from markers
constexpr std::size_t kHighlighted = 0x5C;   // the marker under the pointer (referenced)
constexpr std::size_t kText = 0x60;          // the cursor text (a string of 0x14 bytes)
constexpr std::size_t kCurrent = 0x74;       // byte: the tool is the view's current one
constexpr std::size_t kFilter = 0x78;        // the occupant filter of the picks (referenced)
constexpr std::size_t kStartPosition = 0x7C; // three floats: where the held marker was
constexpr std::size_t kStartState = 0x88;    // its dispatch state then
} // namespace field

constexpr uint32_t kToolId = 0x68E1900A;
constexpr uint32_t kScrollToolId = 0x48E2A1F8; // the drag-to-scroll control
constexpr uint32_t kCursorPolice = 0xC2E4353D;
constexpr uint32_t kCursorFire = 0x02E43537;
constexpr uint32_t kCursorMarker = 0x13B138D0; // over a marker, and while one is held
constexpr uint32_t kCursorText = 0x49D02C4D;
constexpr uint32_t kTypePylon = 0xA9B6D7DE;
constexpr uint32_t kIidPylon = 0xC9B6D7D6;
constexpr uint32_t kIidMySim = 0x0A1DA42F;
constexpr uint32_t kMessageToolChanged = 0x09C8B8EA;
constexpr uint32_t kMessageOccupantRemoved = 0x99EF1143;
constexpr uint32_t kMessageDispatchFirst = 0xE94671EB; // to 0xE94671ED
constexpr uint32_t kMessageDispatchLast = 0xE94671ED;
constexpr uint32_t kMessageDispatchBack = 0x894671F6;
constexpr uint32_t kMessageSelected = 0x49F10111;
constexpr uint32_t kMessagePickedUp = 0xC9F1011A;
constexpr uint32_t kDoubleClick = 500; // milliseconds

// A string from the text resources: 0x20 bytes, a string first (the frame
// of 0x004BA8B0 keeps it in its 0x20 bytes of locals).
struct LocalizedString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
    uint32_t rest[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

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

// The four bytes of a float as the game pushes them.
uint32_t Bits(float& value)
{
    return At<uint32_t>(&value, 0);
}

// The marker under the pointer goes dark and is forgotten.
void ClearHighlighted(char* self)
{
    if (void* const marker = At<void*>(self, field::kHighlighted)) {
        Virtual<void>(marker, 0x18, uint32_t{1}); // SetUIState
        Release(At<void*>(self, field::kHighlighted));
        At<void*>(self, field::kHighlighted) = nullptr;
    }
}

// The game's 0x004B9D60 (slot 14, OnKeyDown, ret 8): true for key 9 (Tab);
// for any other the opposite of the shared key stub's answer (0x00505BE0,
// true always), so false.
uint8_t __fastcall OnKeyDown(char* self, void*, int32_t key, uint32_t modifiers)
{
    if (key == 9)
        return 1;
    return This<uint8_t>(addr::SC4ViewInputControl_OnKeyDefault, self, key, modifiers) ? 0 : 1;
}

// The game's 0x004B9D90 (ret 4): the marker under the pointer changes. The
// old one's state goes back to 1 and it is released; the new one is
// referenced and gets 3.
void __fastcall SetHighlighted(char* self, void*, void* marker)
{
    if (marker == At<void*>(self, field::kHighlighted))
        return;
    ClearHighlighted(self);
    At<void*>(self, field::kHighlighted) = marker;
    if (marker) {
        AddRef(marker);
        Virtual<void>(At<void*>(self, field::kHighlighted), 0x18, uint32_t{3});
    }
}

// The game's 0x004B9E40 (slot 3 of the filter, ret 4): whether the picks
// take an occupant. A pylon occupant (type 0xA9B6D7DE) whose pylon interface
// has a UI state (slot 7 not 0), is not being dragged (slot 16) and whose
// owner (slot 20) is not a MySim (does not answer 0x0A1DA42F), or has no
// owner. `this` is not read.
uint8_t __fastcall FilterIncludes(char*, void*, void* occupant)
{
    uint8_t included = 0;
    if (Virtual<uint32_t>(occupant, 0x1C) != kTypePylon)
        return 0;
    void* marker; // not set before the call, as in the game
    if (!Virtual<uint8_t>(occupant, 0x0, kIidPylon, &marker))
        return 0;
    if (Virtual<uint32_t>(marker, 0x1C) != 0 && !Virtual<uint8_t>(marker, 0x40)) {
        void* mysim = nullptr;
        void* const owner = Virtual<void*>(marker, 0x50);
        if (!owner || !Virtual<uint8_t>(owner, 0x0, kIidMySim, &mysim))
            included = 1;
        if (mysim)
            Release(mysim);
    }
    Release(marker);
    return included;
}

// The held marker (the manager's active pylon), when there is one, goes
// back to where it was picked up with the state it had, and the manager has
// no active pylon after.
void PutBack(char* self, void* manager)
{
    void* const marker = Virtual<void*>(manager, 0x48); // GetActivePylon
    if (!marker)
        return;
    Virtual<void>(Virtual<void*>(marker, 0xC), 0x24, static_cast<void*>(self + field::kStartPosition));
    Virtual<void>(marker, 0x2C, At<uint32_t>(self, field::kStartState)); // SetDispatchState
    Virtual<void>(manager, 0x4C, static_cast<void*>(nullptr));         // SetActivePylon
}

// The game's 0x004B9EF0 (slot 4): Shutdown. The cursor text cleared, the
// five messages given back, the marker under the pointer released (its
// state is left), a held marker put back, the manager's EndDispatchTool
// (slot 17) with the type, the filter released, then the base.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    if (void* const view = At<void*>(self, base::kView))
        Virtual<void>(view, 0xA4, kCursorText); // ClearCursorText
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        void* const target = self + field::kTarget;
        Virtual<uint8_t>(server, 0x18, target, kMessageToolChanged);
        Virtual<uint8_t>(server, 0x18, target, uint32_t{0xE94671EB});
        Virtual<uint8_t>(server, 0x18, target, uint32_t{0xE94671EC});
        Virtual<uint8_t>(server, 0x18, target, uint32_t{0xE94671ED});
        Virtual<uint8_t>(server, 0x18, target, kMessageDispatchBack);
    }
    if (void* const marker = At<void*>(self, field::kHighlighted)) {
        Release(marker);
        At<void*>(self, field::kHighlighted) = nullptr;
    }
    void* const manager = Virtual<void*>(Global<void*>(addr::SC4_City), 0x9C);
    PutBack(self, manager);
    Virtual<void>(manager, 0x44, At<uint32_t>(self, field::kType));
    if (void* const filter = At<void*>(self, field::kFilter)) {
        Release(filter);
        At<void*>(self, field::kFilter) = nullptr;
    }
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x004B9FF0 (ret 0xC): the marker at a screen point, referenced,
// into `out`; null and false when there is none. The renderer's pick of a
// model with the tool's filter (slot 0x104), the model's owner (slot 3)
// asked for the pylon interface.
uint8_t __fastcall PickMarker(char* self, void*, int32_t x, int32_t z, void** out)
{
    void* const renderer = Global<void*>(addr::SC4_View43DD0);
    uint8_t found = 0;
    *out = nullptr;
    void* model = nullptr;
    if (Virtual<uint8_t>(renderer, 0x104, x, z, At<void*>(self, field::kFilter), &model)) {
        void* marker = nullptr;
        if (void* const owner = Virtual<void*>(model, 0xC)) {
            if (Virtual<uint8_t>(owner, 0x0, kIidPylon, &marker)) {
                *out = marker;
                found = 1;
            }
        }
        Release(model);
    }
    return found;
}

// The game's 0x004BA0D0 (ret 0xC): asks for a dispatch of a type at a place
// (the manager's Dispatch, slot 6, with x and z). False for type 8, without
// a manager, without `really`, and when the manager answers 0. `this` is not
// read.
uint8_t __fastcall Dispatch(char*, void*, uint32_t type, float* position, uint8_t really)
{
    if (type == 8)
        return 0;
    void* const manager = Global<void*>(addr::SC4_DispatchService);
    if (manager && really) {
        if (Virtual<uint32_t>(manager, 0x18, type, Bits(position[0]), Bits(position[2])) != 0)
            return 1;
    }
    return 0;
}

// The game's 0x004BA120 (ret 0x18): posts a message of the framework's
// (0x2C bytes) with a type, an object and four numbers through the message
// server (slot 4, with 0). The message is not checked for null. `this` is
// not read.
void __fastcall Post(char*, void*, uint32_t type, void* object, uint32_t first, uint32_t second, uint32_t third,
                     uint32_t fourth)
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, std::size_t{0x2C});
    void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    AddRef(message);
    Virtual<void>(message, 0x14, type);
    Virtual<void>(message, 0x84, object);
    Virtual<void>(message, 0x2C, first);
    Virtual<void>(message, 0x40, second);
    Virtual<void>(message, 0x54, third);
    Virtual<void>(message, 0x68, fourth);
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x10, message, uint32_t{0});
    Release(message);
}

// The game's 0x004BA1B0: puts the held marker (the manager's active pylon)
// down where it is. False without one. When it has a source occupant (slot
// 18) and stands inside that one's cells (the corners of the cell rectangle
// from the city's slot 96; both edges count), the dispatch is called off
// (the manager's RevokeDispatchByPylon, slot 8). Otherwise, when the manager
// allows a dispatch of the tool's type at the marker's place (CanDispatch,
// slot 5) and the type is not 8, one is asked for (Dispatch, slot 6): true
// when that gave an ID. The manager has no active pylon after, whichever
// way. The manager is read from its global each time and, as in the game,
// used unchecked where the game jumps to its last call with a null one.
uint8_t __fastcall PutDown(char* self, void*)
{
    void* const marker = Virtual<void*>(Global<void*>(addr::SC4_DispatchService), 0x48);
    if (!marker)
        return 0;
    float position[3];
    Virtual<void>(Virtual<void*>(marker, 0xC), 0x20, &position[0]); // GetPosition
    bool revoke = false;
    if (void* const source = Virtual<void*>(marker, 0x48)) {
        // A cell rectangle of four dwords (the frame leaves 0x10 bytes for
        // it): x and z of the first and the last cell.
        int32_t cells[4];
        Virtual<void>(source, 0x2C, &cells[0]); // GetBoundingCityCells
        float minX;
        float minZ;
        float maxX;
        float maxZ;
        Virtual<void>(Global<void*>(addr::SC4_City), 0x180, cells[0], cells[1], &minX, &minZ);
        Virtual<void>(Global<void*>(addr::SC4_City), 0x180, cells[2] + 1, cells[3] + 1, &maxX, &maxZ);
        // FCOMP by the flags as written: outside when below the first
        // corner or above the second, and when unordered.
        const double px = position[0];
        const double pz = position[2];
        revoke = px >= static_cast<double>(minX) && px <= static_cast<double>(maxX) &&
                 pz >= static_cast<double>(minZ) && pz <= static_cast<double>(maxZ);
    }
    if (revoke) {
        Virtual<void>(Global<void*>(addr::SC4_DispatchService), 0x20, marker);
        Virtual<void>(Global<void*>(addr::SC4_DispatchService), 0x4C, static_cast<void*>(nullptr));
        return 0;
    }
    uint8_t dispatched = 0;
    if (void* const manager = Global<void*>(addr::SC4_DispatchService)) {
        if (Virtual<uint8_t>(manager, 0x14, At<uint32_t>(self, field::kType), Bits(position[0]), Bits(position[2]))) {
            const uint32_t type = At<uint32_t>(self, field::kType);
            if (type != 8) {
                if (Virtual<uint32_t>(Global<void*>(addr::SC4_DispatchService), 0x18, type, Bits(position[0]),
                                      Bits(position[2])) != 0)
                    dispatched = 1;
            }
        }
    }
    Virtual<void>(Global<void*>(addr::SC4_DispatchService), 0x4C, static_cast<void*>(nullptr));
    return dispatched;
}

// The terrain point under a screen point (the view's PickTerrain, slot 36,
// with 0), with the terrain's height there (the terrain's slot 13) as its y.
bool PickGround(char* self, int32_t x, int32_t z, float* position)
{
    if (!Virtual<uint8_t>(At<void*>(self, base::kView), 0x90, x, z, position, uint32_t{0}))
        return false;
    void* const terrain = Virtual<void*>(Global<void*>(addr::SC4_City), 0x150);
    position[1] = static_cast<float>(Virtual<double>(terrain, 0x34, Bits(position[0]), Bits(position[2])));
    return true;
}

// The game's 0x004BA320 (slot 20, OnMouseMove, ret 0xC). With a held
// marker: the marker cursor, nothing more within 500 ms of a click, else
// the point is the pointer plus the grab offset. Without one: a marker under
// the pointer with a UI state is lit (marker cursor); otherwise the lit one
// goes dark (idle cursor). Then, with a terrain point there: a dispatch is
// allowed when the modifier bit 1 is clear and the manager says so
// (CanDispatch); a held marker moves there with state 3 when allowed and
// gets state 1 when not. +0x30 takes the answer. True always.
uint8_t __fastcall OnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    uint8_t allowed = 0;
    if (At<void*>(self, base::kView)) {
        void* const cityManager = Virtual<void*>(Global<void*>(addr::SC4_City), 0x9C);
        void* const held = Virtual<void*>(cityManager, 0x48);
        void* picked = nullptr;
        int32_t pointX = x;
        int32_t pointZ = z;
        if (held) {
            Virtual<void>(self, 0x20, kCursorMarker); // SetCursor by ID
            char* const watch = self + field::kClickWatch;
            if (This<uint8_t>(addr::GZStopwatch_IsRunning, watch)) {
                if (This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, watch) < kDoubleClick)
                    return 1;
                This<void>(addr::SC4Stopwatch_Reset, watch);
            }
            pointX = x + At<int32_t>(self, field::kGrabX);
            pointZ = z + At<int32_t>(self, field::kGrabZ);
        } else if (PickMarker(self, nullptr, x, z, &picked)) {
            if (Virtual<uint32_t>(picked, 0x1C) != 0) {
                SetHighlighted(self, nullptr, picked);
                Virtual<void>(self, 0x20, kCursorMarker);
            } else {
                ClearHighlighted(self);
                Virtual<void>(self, 0x20, At<uint32_t>(self, field::kIdleCursor));
            }
            Release(picked);
        } else {
            Virtual<void>(self, 0x20, At<uint32_t>(self, field::kIdleCursor));
            ClearHighlighted(self);
        }
        float position[3];
        if (PickGround(self, pointX, pointZ, position)) {
            void* const manager = Global<void*>(addr::SC4_DispatchService);
            const uint32_t type = At<uint32_t>(self, field::kType);
            if (manager && !(modifiers & 2) &&
                Virtual<uint8_t>(manager, 0x14, type, Bits(position[0]), Bits(position[2]))) {
                allowed = 1;
                if (held) {
                    Virtual<void>(Virtual<void*>(held, 0xC), 0x24, &position[0]); // SetPosition
                    Virtual<void>(held, 0x2C, uint32_t{3});
                }
            } else if (held) {
                Virtual<void>(held, 0x2C, uint32_t{1});
            }
        }
    }
    if (allowed != At<uint8_t>(self, field::kAllowed))
        At<uint8_t>(self, field::kAllowed) = allowed;
    return 1;
}

// A marker's vehicle (the manager's GetAutomatonFromPylon, slot 20), when it
// has one, becomes the vehicle manager's selected one (slot 34) and the game
// is told with message 0x49F10111 and the marker.
void SelectVehicle(char* self, void* manager, void* marker)
{
    void* const vehicle = Virtual<void*>(manager, 0x50, marker);
    if (!vehicle)
        return;
    Virtual<void>(Virtual<void*>(Global<void*>(addr::SC4_City), 0xAC), 0x88, vehicle);
    Post(self, nullptr, kMessageSelected, marker, 0, 0, 0, 0);
}

// The body of OnMouseDownL with a view; its answer is the handler's.
uint8_t MouseDown(char* self, int32_t x, int32_t z, uint32_t modifiers)
{
    void* const manager = Global<void*>(addr::SC4_DispatchService);
    char* const watch = self + field::kClickWatch;
    void* picked = nullptr;
    if (This<uint8_t>(addr::GZStopwatch_IsRunning, watch) &&
        This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, watch) < kDoubleClick) {
        // The second click of two: the held marker is put down and its
        // vehicle selected.
        This<void>(addr::SC4Stopwatch_Reset, watch);
        void* const held = Virtual<void*>(manager, 0x48);
        PutDown(self, nullptr);
        SelectVehicle(self, manager, held);
        return 1;
    }
    This<void>(addr::GZStopwatch_Start, watch);

    if (PickMarker(self, nullptr, x, z, &picked)) {
        const bool other = picked != Virtual<void*>(manager, 0x48);
        if (other) {
            if (modifiers & 1) {
                PutDown(self, nullptr);
                SelectVehicle(self, manager, picked);
            } else {
                // The marker is picked up.
                PutDown(self, nullptr);
                Virtual<void>(manager, 0x4C, picked);
                Virtual<void>(Virtual<void*>(picked, 0xC), 0x20, static_cast<void*>(self + field::kStartPosition));
                At<uint32_t>(self, field::kStartState) = Virtual<uint32_t>(picked, 0x30); // GetDispatchState
                This<void>(addr::SC4ViewInputControl_SetGrabOffset, self, picked, x, z);
                void* const vehicles = Virtual<void*>(Global<void*>(addr::SC4_City), 0xAC);
                Virtual<void>(vehicles, 0x88, static_cast<void*>(nullptr));
                Post(self, nullptr, kMessagePickedUp, picked, 0, 0, 0, 0);
            }
        }
        Release(picked);
        if (other)
            return 1;
    }

    void* const held = Virtual<void*>(manager, 0x48);
    int32_t pointX = x;
    int32_t pointZ = z;
    if (held) {
        pointX += At<int32_t>(self, field::kGrabX);
        pointZ += At<int32_t>(self, field::kGrabZ);
    }
    float position[3];
    if (!PickGround(self, pointX, pointZ, position))
        return 0;
    if (held) {
        // The held marker goes to the point and is put down there.
        Virtual<void>(Virtual<void*>(held, 0xC), 0x24, &position[0]);
        PutDown(self, nullptr);
        return 0;
    }
    void* const again = Global<void*>(addr::SC4_DispatchService);
    if (!again)
        return 0;
    if (!Virtual<uint8_t>(again, 0x14, At<uint32_t>(self, field::kType), Bits(position[0]), Bits(position[2])))
        return 0;
    return Dispatch(self, nullptr, At<uint32_t>(self, field::kType), &position[0], 1);
}

// The game's 0x004BA510 (slot 16, OnMouseDownL, ret 0xC). A second click
// within 500 ms of the first puts the held marker down and selects its
// vehicle. Otherwise the stopwatch starts; a marker under the pointer that
// is not the held one is picked up (the held one put down first; its place
// and state kept, message 0xC9F1011A) or, with the modifier bit 0, has its
// vehicle selected; else a held marker is moved to the point and put down,
// and with none a dispatch is asked for there when the manager allows it.
// PlaySound (slot 28) with 0 last. True when a marker was taken or a
// dispatch made.
uint8_t __fastcall OnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    uint8_t handled = 0;
    if (At<void*>(self, base::kView))
        handled = MouseDown(self, x, z, modifiers);
    Virtual<void>(self, 0x70, uint32_t{0}); // PlaySound
    return handled;
}

// The game's 0x004BA7D0 (ret 4): the constructor with the dispatch type;
// the caller allocated 0x90 bytes. +0x88 is left as it comes.
char* __fastcall Construct(char* self, void*, uint32_t type)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, field::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0) = addr::SC4DispatchTool_Vtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4DispatchTool_TargetVtable;
    At<uint32_t>(self, field::kType) = type;
    At<uint8_t>(self, field::kAllowed) = 0;
    At<int32_t>(self, field::kGrabX) = 0;
    At<int32_t>(self, field::kGrabZ) = 0;
    This<void*>(addr::SC4Stopwatch_Construct, self + field::kClickWatch, uint32_t{4});
    At<void*>(self, field::kHighlighted) = nullptr;
    At<char*>(self, field::kText + 0x4) = nullptr;
    At<char*>(self, field::kText + 0x8) = nullptr;
    At<char*>(self, field::kText + 0xC) = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, std::size_t{8});
    At<char*>(self, field::kText + 0x4) = buffer;
    At<char*>(self, field::kText + 0x8) = buffer;
    At<char*>(self, field::kText + 0xC) = buffer + 8;
    *buffer = 0;
    At<uint32_t>(self, field::kText + 0x10) = 0;
    At<std::uintptr_t>(self, field::kText) = addr::SC4String_Vtable;
    At<uint8_t>(self, field::kCurrent) = 0;
    At<void*>(self, field::kFilter) = nullptr;
    At<uint32_t>(self, field::kStartPosition) = Global<uint32_t>(addr::SC4DispatchTool_Zero);
    At<uint32_t>(self, field::kStartPosition + 0x4) = Global<uint32_t>(addr::SC4DispatchTool_Zero);
    At<uint32_t>(self, field::kStartPosition + 0x8) = Global<uint32_t>(addr::SC4DispatchTool_Zero);
    return self;
}

// The game's 0x004BA880 (slot 26, ret 4): the deleting destructor. The
// string, the stopwatch, the base; the marker at +0x5C and the filter are
// Shutdown's to let go.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    This<void>(addr::SC4String_Destroy, self + field::kText);
    This<void>(addr::SC4Stopwatch_Destruct, self + field::kClickWatch);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x004BA8B0: the cursor text. The text 0x4A54ED13 of group
// 0x6A231EAA is the format for the count of free units of the tool's type
// (the manager's GetAvailablePylonCount, slot 21), printed into the string
// at +0x60 (the string's slot 27) and shown by the view (SetCursorText, slot
// 40) under 0x49D02C4D.
void __fastcall UpdateCursorText(char* self, void*)
{
    void* const manager = Virtual<void*>(Global<void*>(addr::SC4_City), 0x9C);
    LocalizedString format;
    This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &format, uint32_t{0x6A231EAA}, uint32_t{0x4A54ED13},
                uint32_t{1});
    char* const text = self + field::kText;
    using PrintFn = int(__cdecl*)(void*, const char*, ...);
    const PrintFn print = At<PrintFn>(At<char*>(text, 0), 0x6C);
    const uint32_t units = Virtual<uint32_t>(manager, 0x54, At<uint32_t>(self, field::kType));
    print(text, This<const char*>(addr::cRZBaseString_Data, &format), units);
    if (void* const view = At<void*>(self, base::kView))
        Virtual<void>(view, 0xA0, kCursorText, uint32_t{0}, uint32_t{0}, static_cast<void*>(text), uint32_t{0});
    // The string part's destructor, as the game has it inline.
    format.vtable = addr::SC4String_Vtable;
    if (format.begin)
        Cdecl<void>(addr::SC4_Free, format.begin);
}

// The game's 0x004BA940 (slot 3 of the message target at +0x28, ret 4).
// `this` is the target part.
//   0x09C8B8EA, the view's tool changed (the old one and the new one in the
//     message): when this tool goes and the new one is neither itself nor
//     the drag-to-scroll control, +0x74 is cleared, the cursor text too, the
//     lit marker goes dark and a held marker is put back; when it comes,
//     +0x74 is set and the cursor text shown.
//   0xE94671EB to 0xE94671ED and 0x894671F6, dispatches made, moved, called
//     off or back: the cursor text again while +0x74 is set.
//   0x99EF1143, an occupant removed (not asked for by Init): the lit marker
//     is let go when it is that occupant's; +0x5C is not checked for null.
// True for these, false for anything else.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const self = target - field::kTarget;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == kMessageOccupantRemoved) {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (occupant == Virtual<void*>(At<void*>(self, field::kHighlighted), 0xC)) {
            Release(At<void*>(self, field::kHighlighted));
            At<void*>(self, field::kHighlighted) = nullptr;
        }
        return 1;
    }
    if (type == kMessageToolChanged) {
        void* const oldTool = Virtual<void*>(message, 0x28);
        void* const newTool = Virtual<void*>(message, 0x3C);
        void* const manager = Global<void*>(addr::SC4_DispatchService);
        if (oldTool == self) {
            if (newTool == self)
                return 1;
            if (newTool && Virtual<uint32_t>(newTool, 0x14) == kScrollToolId)
                return 1;
            At<uint8_t>(self, field::kCurrent) = 0;
            if (void* const view = At<void*>(self, base::kView))
                Virtual<void>(view, 0xA4, kCursorText);
            ClearHighlighted(self);
            PutBack(self, manager);
            return 1;
        }
        if (newTool == self) {
            At<uint8_t>(self, field::kCurrent) = 1;
            UpdateCursorText(self, nullptr);
        }
        return 1;
    }
    if (type == kMessageDispatchBack || (type >= kMessageDispatchFirst && type <= kMessageDispatchLast)) {
        if (At<uint8_t>(self, field::kCurrent))
            UpdateCursorText(self, nullptr);
        return 1;
    }
    return 0;
}

// The game's 0x004BAAB0 (slot 3): Init. The ID; the cursor by the type
// (0xC2E4353D for 0 and 6, 0x02E43537 for 1 and 5, else 1), kept at +0x58
// too; the manager's BeginDispatchTool (slot 16) with the type; the
// stopwatch made again (mode 4); the base Init; five messages asked of the
// message server for the target; +0x74 set and the cursor text shown; and a
// new filter at +0x78, referenced (the old value is not released, and a
// failed allocation is not checked).
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    At<uint32_t>(self, base::kId) = kToolId;
    At<uint32_t>(self, base::kCursor) = 1;
    switch (At<uint32_t>(self, field::kType)) {
    case 0:
    case 6:
        At<uint32_t>(self, base::kCursor) = kCursorPolice;
        break;
    case 1:
    case 5:
        At<uint32_t>(self, base::kCursor) = kCursorFire;
        break;
    default:
        break;
    }
    At<uint32_t>(self, field::kIdleCursor) = At<uint32_t>(self, base::kCursor);
    void* const manager = Virtual<void*>(Global<void*>(addr::SC4_City), 0x9C);
    Virtual<void>(manager, 0x40, At<uint32_t>(self, field::kType));
    This<void>(addr::SC4Stopwatch_ConstructMode, self + field::kClickWatch, uint32_t{4});
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        void* const target = self + field::kTarget;
        Virtual<uint8_t>(server, 0x14, target, kMessageToolChanged);
        Virtual<uint8_t>(server, 0x14, target, uint32_t{0xE94671EB});
        Virtual<uint8_t>(server, 0x14, target, uint32_t{0xE94671EC});
        Virtual<uint8_t>(server, 0x14, target, uint32_t{0xE94671ED});
        Virtual<uint8_t>(server, 0x14, target, kMessageDispatchBack);
    }
    At<uint8_t>(self, field::kCurrent) = 1;
    UpdateCursorText(self, nullptr);
    char* const filter = Cdecl<char*>(addr::SC4_operator_new, std::size_t{0xC});
    if (filter) {
        This<void*>(addr::SC4_OccupantFilterBase, filter);
        At<std::uintptr_t>(filter, 0x0) = addr::SC4DispatchTool_FilterVtable;
        At<std::uintptr_t>(filter, 0x4) = addr::SC4DispatchTool_FilterVtable4;
    }
    At<void*>(self, field::kFilter) = filter;
    AddRef(filter);
    return 1;
}

// --- Registration ----------------------------------------------------------------------

bool InstallDispatchTool(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4DispatchTool_OnKeyDown, reinterpret_cast<void*>(&OnKeyDown)},
        {addr::SC4DispatchTool_SetHighlighted, reinterpret_cast<void*>(&SetHighlighted)},
        {addr::SC4DispatchTool_FilterIncludes, reinterpret_cast<void*>(&FilterIncludes)},
        {addr::SC4DispatchTool_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4DispatchTool_PickMarker, reinterpret_cast<void*>(&PickMarker)},
        {addr::SC4DispatchTool_Dispatch, reinterpret_cast<void*>(&Dispatch)},
        {addr::SC4DispatchTool_Post, reinterpret_cast<void*>(&Post)},
        {addr::SC4DispatchTool_PutDown, reinterpret_cast<void*>(&PutDown)},
        {addr::SC4DispatchTool_OnMouseMove, reinterpret_cast<void*>(&OnMouseMove)},
        {addr::SC4DispatchTool_OnMouseDownL, reinterpret_cast<void*>(&OnMouseDownL)},
        {addr::SC4DispatchTool_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4DispatchTool_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4DispatchTool_UpdateCursorText, reinterpret_cast<void*>(&UpdateCursorText)},
        {addr::SC4DispatchTool_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::SC4DispatchTool_Init, reinterpret_cast<void*>(&Init)},
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
    "ui.tools.dispatch",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4DispatchTool_OnMouseDownL,
    "Our police and fire dispatch tool of the city view (docs/tools.md)",
    false,
    InstallDispatchTool,
});

} // namespace
} // namespace opensc4::tool_dispatch
