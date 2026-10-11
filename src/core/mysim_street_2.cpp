// cSC4MyStreetSim (class 0x0BF606A5), the Microphone Tool's MySim: the
// messages, Follow, and the tool the player clicks a car or a pedestrian
// with. Replacement, off by default:
//
//   simulators.mysim_agents.street_tool: DoMessage (the message target at
//     object + 0x10), Follow (the street interface at object + 0x2B8) and
//     the tool (0x30 bytes, vtable 0x00A85F40, on the view input control
//     base class, ID 0xCC23D564): Init, constructor, the street sim setter,
//     Shutdown, OnMouseDownL, the deleting destructor and OnMouseMove.
//
// The object is the cSC4MySim (0x2B8 bytes) plus 0x10. Follow takes the
// followed car or walker, publishes the trip globals in the MySim's script
// table and puts a marker on it; the code at 0x0043D0F0 to 0x0043DFD0
// (UpdateGlobals 0x0043D6C0, Stop 0x0043D3A0, MakeTool 0x0043D520) is called
// by address.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/sim_timers.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::mysim_agents {
namespace {

// Street sim fields, from the object's start.
constexpr std::size_t kState = 0xC0;         // the MySim state (4, 5, 6 set by Follow)
constexpr std::size_t kAutomaton = 0xF8;     // the followed automaton (counted)
constexpr std::size_t kOverlayKey = 0x12C;   // 0x0BFA331E, the key of the overlay marker
constexpr std::size_t kMarker = 0x150;       // the overlay marker, -1 for none
constexpr std::size_t kScriptTable = 0x164;  // script table reference
constexpr std::size_t kScriptContext = 0x1F0;
constexpr std::size_t kFollowing = 0x210;    // byte
constexpr std::size_t kFrameWatch = 0x280;   // GZStopwatch pacing the frame message
constexpr std::size_t kTravel = 0x2BC;       // 9 none, 0 foot, 1 car, 2 bus, 3 train, 7 elevated train, 8 monorail
constexpr std::size_t kStreetPart = 0x2B8;   // the street interface (Follow, Stop, MakeTool)
constexpr std::size_t kMessagePart = 0x10;

// The tool's fields, from its start (up to +0x24 the base class).
constexpr std::size_t kToolView = 0x1C;      // the city view (set by the base Init)
constexpr std::size_t kToolStreet = 0x28;    // the street interface
constexpr std::size_t kToolHovered = 0x2C;   // the automaton under the cursor (as its 0x8B99EE7C interface)

constexpr std::uintptr_t kUpdateGlobals = 0x0043D6C0; // thiscall on the object, not translated here
constexpr uint32_t kWaitingIid = 0x8B99EE7C;          // the automaton itself (base class QueryInterface)
constexpr uint32_t kToolId = 0xCC23D564;
constexpr uint32_t kCursorIdle = 0xEBF9C962;
constexpr uint32_t kCursorHit = 0xEBF9C971;

using ThinkFn = void(__thiscall*)(void* mysim, uint32_t flag);
using MessageFn = uint8_t(__thiscall*)(void* self, void* message);

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

// Sets a number in the MySim's script, as the game's inlined sequence does
// (and its register-convention helper 0x0043D1F0 for an integer).
void SetNumber(void* script, std::uintptr_t name, double value)
{
    Virtual<void>(script, 0x84, reinterpret_cast<const char*>(name));
    Virtual<void>(script, 0x7C, value);
    Virtual<void>(script, 0xB8, -3);
}

// --- Messages ---------------------------------------------------------------------------

// The game's 0x0043DFD0: DoMessage of the message target (this = object +
// 0x10). Every path ends in the cSC4MySim handler.
//   0x09D92FC3 (an automaton event) with the message's value 2, 3 or 5: Stop
//     (the followed car or walker arrived or went);
//   0x86AD10EE (every frame): when the MySim has a state, the simulator is
//     paused (slot 0x48 is IsAnyPaused: one of the three pause counters
//     above zero) and the frame stopwatch shows more than 2 ms: the globals
//     again, Think(1), the stopwatch restarted;
//   0x4BC42C48 with the message's object being the street interface: the
//     globals again and Think(1).
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const object = target - kMessagePart;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 0x09D92FC3: {
        const uint32_t kind = Virtual<uint32_t>(message, 0x38);
        if (kind >= 2 && (kind <= 3 || kind == 5))
            Virtual<uint8_t>(object + kStreetPart, 0x10); // Stop
        break;
    }
    case 0x4BC42C48:
        if (Virtual<void*>(message, 0x80) == object + kStreetPart) {
            This<void>(kUpdateGlobals, object);
            reinterpret_cast<ThinkFn>(addr::cSC4MySim_Think)(object, 1);
        }
        break;
    case 0x86AD10EE: {
        if (!At<int32_t>(object, kState))
            break;
        void* const simulator = Global<void*>(addr::SC4_SimulatorService);
        if (!simulator || !Virtual<uint8_t>(simulator, 0x48))
            break;
        void* const watch = object + kFrameWatch;
        sim_timers::Claim(watch);
        if (This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, watch) <= 2)
            break;
        This<void>(kUpdateGlobals, object);
        reinterpret_cast<ThinkFn>(addr::cSC4MySim_Think)(object, 1);
        This<void>(addr::GZStopwatch_Start, watch);
        break;
    }
    default:
        break;
    }
    return reinterpret_cast<MessageFn>(addr::cSC4MySim_DoMessage)(target, message);
}

// --- Follow -----------------------------------------------------------------------------

// The game's 0x0043E0E0: Follow (this = the street interface, object +
// 0x2B8; ret 4). Stops the last follow, keeps the automaton, publishes the
// trip globals for it (wealth 2, in transit, the means of travel, the
// state) and puts the marker on it. The state of a vehicle is 4 or 5 by one
// draw of the advisor system's generator (service 0x00B43D34 slot 9 is its
// +0x30); a walker's is 6.
uint8_t __fastcall Follow(char* street, void*, void* automaton)
{
    char* const object = street - kStreetPart;
    Virtual<uint8_t>(street, 0x10); // Stop
    if (!automaton || !At<void*>(object, kScriptContext))
        return 1;
    void* const script = At<void*>(object, kScriptContext);

    void* const old = At<void*>(object, kAutomaton);
    if (automaton != old) {
        AddRef(automaton);
        At<void*>(object, kAutomaton) = automaton;
        if (old)
            Release(old);
    }

    // The automaton's own interface: the byte it is told (slot 0x268, the
    // base class stores it at +0x1E8) and the flag 0x2000000.
    void* waiting = nullptr;
    if (Virtual<uint8_t>(At<void*>(object, kAutomaton), 0x0, kWaitingIid, &waiting)) {
        Virtual<void>(waiting, 0x268, uint32_t{0xFF});
        Virtual<void>(waiting, 0x78, uint32_t{0x2000000}, uint32_t{1});
    }
    Virtual<void>(At<void*>(object, kAutomaton), 0x9C, object + kMessagePart); // AddCallback

    Virtual<void>(script, 0xAC, At<int32_t>(object, kScriptTable));
    SetNumber(script, 0x00A85060, 2.0);  // wealth
    SetNumber(script, 0x00A84FE8, 1.0);  // trip_in_transit
    SetNumber(script, 0x00A84FD4, 0.0);  // trip_at_location
    SetNumber(script, 0x00A84FA8, 0.0);  // trip_at_transit_switch
    SetNumber(script, 0x00A84F9C, 0.0);  // trip_idle
    SetNumber(script, 0x00A85F14, 0.0);  // trip_approaching_location
    void* const occupant = Virtual<void*>(automaton, 0x14);
    SetNumber(script, 0x00A84F90, 0.0);  // trip_on_bus
    SetNumber(script, 0x00A84F80, 0.0);  // trip_on_foot
    SetNumber(script, 0x00A84F70, 0.0);  // trip_on_subway
    SetNumber(script, 0x00A84F64, 0.0);  // trip_in_car
    SetNumber(script, 0x00A84F54, 0.0);  // trip_on_train
    SetNumber(script, 0x00A84F3C, 0.0);  // trip_on_elevated_train
    SetNumber(script, 0x00A84F28, 0.0);  // trip_on_monorail
    At<int32_t>(object, kTravel) = 9;

    const auto group = [&](uint32_t id) { return Virtual<uint8_t>(occupant, 0x58, id) != 0; };
    // The vehicle's state: 5 when the draw is below 0x7FFFFFFF, else 4.
    const auto draw = [&]() -> uint32_t {
        void* const generator = Virtual<void*>(Global<void*>(addr::SC4_Service43D34), 0x24);
        return This<uint32_t>(addr::cRZRandom_Advance, generator);
    };
    const auto vehicleState = [&](uint32_t value) {
        At<int32_t>(object, kState) = (value < 0x7FFFFFFFu ? 1 : 0) + 4;
    };

    if (group(0x4001)) {
        SetNumber(script, 0x00A84F80, 1.0); // trip_on_foot
        At<int32_t>(object, kState) = 6;
        At<int32_t>(object, kTravel) = 0;
        if (Virtual<uint32_t>(At<void*>(object, kAutomaton), 0xEC) == 7)
            SetNumber(script, 0x00A84F9C, 1.0); // trip_idle
    } else if (group(0x420B)) {
        SetNumber(script, 0x00A84F64, 1.0); // trip_in_car
        const uint32_t value = draw();
        vehicleState(value);
        At<int32_t>(object, kTravel) = 1;
    } else if (group(0x4200)) {
        SetNumber(script, 0x00A84F90, 1.0); // trip_on_bus
        const uint32_t value = draw();
        vehicleState(value);
        At<int32_t>(object, kTravel) = 2;
    } else if (group(0x4201) || group(0x4215)) {
        SetNumber(script, 0x00A84F54, 1.0); // trip_on_train
        const uint32_t value = draw();
        At<int32_t>(object, kTravel) = 3;
        vehicleState(value);
    } else if (group(0x422C) || group(0x422D)) {
        SetNumber(script, 0x00A84F3C, 1.0); // trip_on_elevated_train
        const uint32_t value = draw();
        At<int32_t>(object, kTravel) = 7;
        vehicleState(value);
    } else if (group(0x422E) || group(0x422F)) {
        SetNumber(script, 0x00A84F28, 1.0); // trip_on_monorail
        const uint32_t value = draw();
        At<int32_t>(object, kTravel) = 8;
        vehicleState(value);
    }

    SetNumber(script, 0x00A8506C, static_cast<double>(At<int32_t>(object, kState))); // state
    Virtual<void>(script, 0x2C, uint32_t{1});
    This<void>(kUpdateGlobals, object);

    void* const overlays = Virtual<void*>(Global<void*>(addr::SC4_DispatchService), 0x58);
    void* const marked = Virtual<void*>(At<void*>(object, kAutomaton), 0x14);
    At<int32_t>(object, kMarker) = This<int32_t>(addr::SC4OverlayManager_Add, overlays, marked, uint32_t{3},
                                                 At<uint32_t>(object, kOverlayKey), uint32_t{0});
    At<uint8_t>(object, kFollowing) = 1;
    if (waiting)
        Release(waiting);
    return 1;
}

// --- The tool ---------------------------------------------------------------------------

// The game's 0x0043E740: Init (slot 3): the idle cursor, then the base.
uint8_t __fastcall ToolInit(char* self, void*)
{
    Virtual<void>(self, 0x20, kCursorIdle);
    return This<uint8_t>(addr::SC4ViewInputControl_Init, self);
}

// The game's 0x0043E760: the constructor (the caller allocated 0x30 bytes).
char* __fastcall ToolConstruct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::SC4MicrophoneTool_Vtable;
    At<void*>(self, kToolStreet) = nullptr;
    At<void*>(self, kToolHovered) = nullptr;
    At<uint32_t>(self, 0xC) = kToolId;
    return self;
}

// The game's 0x0043E790: keeps the street interface of the street sim object
// (its +0x2B8), after Init; false when Init fails.
uint8_t __fastcall ToolSetStreet(char* self, void*, char* object)
{
    if (!Virtual<uint8_t>(self, 0xC))
        return 0;
    void* const part = object ? object + kStreetPart : nullptr;
    void* const old = At<void*>(self, kToolStreet);
    if (part != old) {
        if (part)
            AddRef(part);
        At<void*>(self, kToolStreet) = part;
        if (old)
            Release(old);
    }
    return 1;
}

// The game's 0x0043E7E0: Shutdown (slot 4).
uint8_t __fastcall ToolShutdown(char* self, void*)
{
    if (void* const street = At<void*>(self, kToolStreet)) {
        At<void*>(self, kToolStreet) = nullptr;
        Release(street);
    }
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x0043E810: OnMouseDownL (slot 16). Only for the view's current
// tool: Follow the hovered automaton unless its exemplar carries the bool
// property 0x1BC42C49 (the dispatch sets it on the vehicles it makes for a
// MySim). The property's presence is tested; the value read is not.
uint8_t __fastcall ToolMouseDown(char* self, void*, uint32_t, uint32_t, uint32_t)
{
    if (Virtual<void*>(At<void*>(self, kToolView), 0x60) != self)
        return 0;
    if (void* const hovered = At<void*>(self, kToolHovered)) {
        uint8_t value = 0;
        void* const occupant = Virtual<void*>(hovered, 0x14);
        void* const holder = Virtual<void*>(occupant, 0x18);
        if (!Cdecl<uint8_t>(addr::SC4_GetPropertyBool, holder, uint32_t{0x1BC42C49}, &value))
            Virtual<uint8_t>(At<void*>(self, kToolStreet), 0xC, hovered);
    }
    return 1;
}

// The game's 0x0043E870: the deleting destructor (slot 26).
char* __fastcall ToolDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::SC4MicrophoneTool_Vtable;
    if (void* const hovered = At<void*>(self, kToolHovered))
        Release(hovered);
    if (void* const street = At<void*>(self, kToolStreet))
        Release(street);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// A list of the occupant manager's: one pointer to a sentinel node; a node is
// {next, previous, occupant}.
struct OccupantList {
    char* head;
};

// The game's 0x0043E8B0: OnMouseMove (slot 20; x, y, flags). Drops the old
// hover, then picks the occupant under the cursor through the view (slot
// 0x94): a car, bus, train or pedestrian whose automaton flag 1 is clear. If
// that gives no automaton, the 3D point under the cursor (slot 0x90) is
// searched in a box of 6 by 14 by 6 around it for the first pedestrian
// (group 0x4001) with the flag clear (the first list found is released
// through the occupant manager). The cursor shows a hit or not.
uint8_t __fastcall ToolMouseMove(char* self, void*, uint32_t x, uint32_t y, uint32_t)
{
    void* waiting = nullptr;
    void* picked = nullptr;
    if (void* const hovered = At<void*>(self, kToolHovered)) {
        At<void*>(self, kToolHovered) = nullptr;
        Release(hovered);
        // The game then looks at `picked` again, still empty: nothing follows.
    }
    void* const view = At<void*>(self, kToolView);
    if (Virtual<uint8_t>(view, 0x94, x, y, &picked)) {
        if (waiting) {
            void* const old = waiting;
            waiting = nullptr;
            Release(old);
        }
        if (Virtual<uint8_t>(picked, 0x0, kWaitingIid, &waiting) && !Virtual<uint8_t>(waiting, 0x7C, uint32_t{1})) {
            const auto group = [&](uint32_t id) { return Virtual<uint8_t>(picked, 0x58, id) != 0; };
            if (group(0x420B) || group(0x4201) || group(0x4215) || group(0x4001) || group(0x4200))
                This<void>(addr::SC4RefPtr_Assign, self + kToolHovered, waiting);
        }
    }
    if (!waiting) {
        uint32_t unset;
        std::memcpy(&unset, &Global<float>(addr::SC4MicrophoneTool_UnsetPoint), sizeof(unset));
        float point[3];
        std::memcpy(&point[0], &unset, sizeof(float));
        std::memcpy(&point[1], &unset, sizeof(float));
        std::memcpy(&point[2], &unset, sizeof(float));
        if (Virtual<uint8_t>(view, 0x90, x, y, point, uint32_t{1})) {
            const double three = Global<float>(addr::Budget_Three);
            const double seven = Global<float>(addr::SC4MicrophoneTool_BoxHeight);
            float low[3];
            float high[3];
            low[0] = static_cast<float>(static_cast<double>(point[0]) - three);
            low[1] = static_cast<float>(static_cast<double>(point[1]) - seven);
            low[2] = static_cast<float>(static_cast<double>(point[2]) - three);
            high[0] = static_cast<float>(static_cast<double>(point[0]) + three);
            high[1] = static_cast<float>(static_cast<double>(point[1]) + seven);
            high[2] = static_cast<float>(static_cast<double>(point[2]) + three);
            void* const allocator = ListNodeAllocator();
            void* const manager = Global<void*>(addr::SC4_NetworkService);
            OccupantList list;
            list.head = Virtual<char*>(allocator, 0xC, uint32_t{0xC});
            At<char*>(list.head, 0) = list.head;
            At<char*>(list.head, 4) = list.head;
            Virtual<void>(manager, 0x94, &list, low, high, uint32_t{0}, int32_t{-1}); // GetOccupantsByBBox
            char* const sentinel = list.head;
            char* node = At<char*>(sentinel, 0);
            if (node != sentinel) {
                do {
                    char* const current = node;
                    node = At<char*>(current, 0);
                    void* const occupant = At<void*>(current, 8);
                    if (waiting) {
                        void* const old = waiting;
                        waiting = nullptr;
                        Release(old);
                    }
                    if (Virtual<uint8_t>(occupant, 0x0, kWaitingIid, &waiting) &&
                        !Virtual<uint8_t>(waiting, 0x7C, uint32_t{1}) && Virtual<uint8_t>(occupant, 0x58, uint32_t{0x4001})) {
                        This<void>(addr::SC4RefPtr_Assign, self + kToolHovered, waiting);
                        break;
                    }
                } while (node != sentinel);
            }
            Virtual<void>(manager, 0xC0, &list); // ReleaseOccupantList
            This<void>(addr::SC4List_Destroy2, &list);
        }
    }
    Virtual<void>(self, 0x20, At<void*>(self, kToolHovered) ? kCursorHit : kCursorIdle);
    if (waiting)
        Release(waiting);
    if (picked)
        Release(picked);
    return 1;
}

// --- Registration -----------------------------------------------------------------------

bool InstallStreetTool(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MyStreetSim_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::cSC4MyStreetSim_Follow, reinterpret_cast<void*>(&Follow)},
        {addr::SC4MicrophoneTool_Init, reinterpret_cast<void*>(&ToolInit)},
        {addr::SC4MicrophoneTool_SC4MicrophoneTool, reinterpret_cast<void*>(&ToolConstruct)},
        {addr::SC4MicrophoneTool_SetStreetSim, reinterpret_cast<void*>(&ToolSetStreet)},
        {addr::SC4MicrophoneTool_Shutdown, reinterpret_cast<void*>(&ToolShutdown)},
        {addr::SC4MicrophoneTool_OnMouseDownL, reinterpret_cast<void*>(&ToolMouseDown)},
        {addr::SC4MicrophoneTool_DeletingDestructor, reinterpret_cast<void*>(&ToolDelete)},
        {addr::SC4MicrophoneTool_OnMouseMove, reinterpret_cast<void*>(&ToolMouseMove)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_streetToolRegistered = registry::Register({
    "simulators.mysim_agents.street_tool",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MyStreetSim_DoMessage,
    "Our cSC4MyStreetSim messages, Follow and the Microphone Tool (docs/mysim-agents.md)",
    false,
    InstallStreetTool,
});

} // namespace
} // namespace opensc4::mysim_agents
