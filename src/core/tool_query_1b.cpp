// The query tool of the city view, first piece, second half: the object and
// its small classes between 0x004C37B0 and 0x004C56E5.
// Replacement, off by default:
//
//   ui.tools.query1b: 23 functions of the query tool
//     (cSC4ViewInputControlQuery: a view input control of 0xA0 bytes, vtable
//     0x00A90A88, message target 0x00A90A78 at +0x28; docs/tools.md): the
//     constructor, OnMouseUpL, six of its own methods, the tests of its five
//     occupant filters, three picks under the pointer, the two functions its
//     timers call, the sound of a query, and the methods of two small
//     classes that serve its windows.
//
// The tool is made by the city view (0x007F1C10) for the commands
// 0x6A935CF3 (kind 0) and 0x6A935CF4 (kind 1), both with 0x32 as the second
// argument; the layouts are in tool_query.h. Init, Shutdown, the other
// mouse handlers, DoMessage and the destructor are later pieces and still
// the game's.
//
// Two functions here are what calls patched by the plugin
// sc4-query-ui-hooks go to: 0x004C3810 (called at 0x004D747F and 0x004D74BF)
// and 0x004C4F40 (called at 0x004D4406). The hooks are at the functions'
// entries, so a call the plugin has turned elsewhere does not come here, and
// a call the plugin makes to the game's address does.
//
// Time: the timer at +0x50 (a cRZTimer, real time) is made by the
// constructor and stopped by OnMouseUpL (its slot 7); nothing here reads
// it. The two timer functions are called by the game's timers on real time
// and each posts a message. No random number is drawn; the constructor
// makes no generator.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_query_1b {
namespace {

namespace base = tool_query::base;
namespace field = tool_query::field;
namespace window_runner = tool_query::window_runner;

// Occupant types.
constexpr uint32_t kTypeBuilding = 0x278128A0;
constexpr uint32_t kTypePowerPole = 0x2890D4DE;
constexpr uint32_t kType6A11BD2B = 0x6A11BD2B;
constexpr uint32_t kTypeFlora = 0x74758926;
constexpr uint32_t kTypeProp = 0xA823821E;
constexpr uint32_t kTypeNetwork = 0xC772BF98;
constexpr uint32_t kTypeAutomatonFirst = 0x496E5783;
constexpr uint32_t kTypeAutomatonSecond = 0xC7CC75F1;

constexpr uint32_t kNetworkOccupant = 0xA821EF94; // the network occupant's interface
constexpr uint32_t kOccupantIid = 0x26BB767E;     // the occupant's own interface
constexpr uint32_t kAutomaton = 0xA9B40F05;
constexpr uint32_t kPropOccupant = 0xE9793A65;
constexpr uint32_t kQueryProperty = 0x8A416A99;   // the property a flora or prop occupant needs to be asked about
constexpr uint32_t kNoQueryProperty = 0xCAA9AB92; // the property that takes an occupant out of the fifth filter
constexpr uint32_t kNameProperty = 0x899AFBAD;
constexpr uint32_t kExemplarName = 0x20;
constexpr uint32_t kCursorText = 0x2A456CB1;      // the tool's text at the pointer
constexpr uint32_t kMessageTimer = 0x0A456D95;    // what the tool's timer posts to the tool
constexpr uint32_t kMessageOther = 0x0A456D96;    // what the other timer sends to all
constexpr uint32_t kRunnerProcedure = 0x22E85D8E; // IIDs of the window runner's two parts
constexpr uint32_t kRunnerTarget = 0x452294AA;

// One cell of a route as the game's iterator hands it out (0x006D6510; 10
// bytes used of the 12 the game's frames give it) and the iterator (0x16
// bytes used of 0x18).
struct RouteStep {
    int16_t x;
    int16_t z;
    uint8_t kind;
    uint8_t rest[7];
};
static_assert(sizeof(RouteStep) == 12);

struct RouteIterator {
    uint32_t words[6];
};
static_assert(sizeof(RouteIterator) == 0x18);

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

// --- The occupant filters --------------------------------------------------------------

// The game's 0x004C37B0 (slot 3, IsOccupantIncluded, of the filter with the
// vtables 0x00A909B0 and 0x00A90998, made at 0x004CBCE3; ret 4): what can
// be asked about. Buildings, power poles, network occupants and the type
// 0x6A11BD2B always; flora and props when their property holder (the
// occupant's slot 6) has the property 0x8A416A99 (the holder's slot 3);
// nothing else.
uint8_t __fastcall IsAskable(char*, void*, void* occupant)
{
    switch (Virtual<uint32_t>(occupant, 0x1C)) {
    case kTypeBuilding:
    case kTypePowerPole:
    case kType6A11BD2B:
    case kTypeNetwork:
        return 1;
    case kTypeFlora:
    case kTypeProp:
        return Virtual<uint8_t>(Virtual<void*>(occupant, 0x18), 0xC, kQueryProperty) != 0;
    default:
        return 0;
    }
}

// The game's 0x004C39A0 (slot 3 of the filter with the vtables 0x00A909E4
// and 0x00A909CC, made at 0x004CBCB8; ret 4): buildings, and network
// occupants (asked for the interface 0xA821EF94, let go after) whose slot
// 22 says yes for any of 0, 1, 2, 3, 6, 7, 8, 9, 12, 10, 11, asked in that
// order up to the first yes.
uint8_t __fastcall IsBuildingOrNetwork(char*, void*, void* occupant)
{
    static const uint32_t asked[] = {0, 1, 2, 3, 6, 7, 8, 9, 0xC, 0xA, 0xB};
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (type == kTypeBuilding)
        return 1;
    if (type != kTypeNetwork)
        return 0;
    void* network = nullptr;
    if (!Virtual<uint8_t>(occupant, 0x0, kNetworkOccupant, &network))
        return 0;
    uint8_t found = 0;
    for (const uint32_t one : asked) {
        if (Virtual<uint8_t>(network, 0x58, one)) {
            found = 1;
            break;
        }
    }
    Release(network);
    return found;
}

// The game's 0x004C3AB0 (slot 4, IsOccupantTypeIncluded, of the filter with
// the vtables 0x00A90A18 and 0x00A90A00, made by 0x004C4D10; ret 4): the two
// automaton types.
uint8_t __fastcall IsAutomatonType(char*, void*, uint32_t type)
{
    return type == kTypeAutomatonFirst || type == kTypeAutomatonSecond;
}

// The game's 0x004C3AD0 (slot 3 of the filter with the vtables 0x00A90A4C
// and 0x00A90A34, made by 0x004C4DC0; ret 4): props of the occupant group
// 0x5002 or 0x5003 (the occupant's slot 22).
uint8_t __fastcall IsGroupProp(char*, void*, void* occupant)
{
    if (Virtual<uint32_t>(occupant, 0x1C) != kTypeProp)
        return 0;
    return Virtual<uint8_t>(occupant, 0x58, uint32_t{0x5002}) || Virtual<uint8_t>(occupant, 0x58, uint32_t{0x5003});
}

// The game's 0x004C46C0 (slot 3 of the filter with the vtables 0x00A90B18
// and 0x00A90B00, made at 0x004D42A2 for the pick of 0x004D4140; ret 4):
// what can be asked about (0x004C37B0) and whose property holder does not
// have the property 0xCAA9AB92.
uint8_t __fastcall IsAskableUnmarked(char* self, void*, void* occupant)
{
    if (Virtual<uint8_t>(Virtual<void*>(occupant, 0x18), 0xC, kNoQueryProperty))
        return 0;
    return IsAskable(self, nullptr, occupant);
}

// --- The tool's own methods --------------------------------------------------------------

// The game's 0x004C3810 (ret 8; the second argument is not read): a name
// for the occupant at +0x2C into a string, from its property holder (slot
// 6): the string property 0x899AFBAD, else the text the key property
// 0x8A416A99 names, else the string property 0x20. What the one that
// answered returned, else the last one's answer. The plugin
// sc4-query-ui-hooks turns both of the game's calls of this function
// (0x004D747F, 0x004D74BF) to its own.
uint8_t __fastcall OccupantName(char* self, void*, void* text, uint32_t)
{
    void* const holder = Virtual<void*>(At<void*>(self, field::kOccupant), 0x18);
    if (const uint8_t found = Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, kNameProperty, text))
        return found;
    if (const uint8_t found = Cdecl<uint8_t>(addr::SC4_GetPropertyKeyString, text, holder, kQueryProperty, uint32_t{0}))
        return found;
    return Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, kExemplarName, text);
}

// The route of a trip record: at +0xC when the record's byte at +0x1E has
// its bit 0 clear and the caller asks for the way back, else at +0.
char* RouteOf(char* trip, uint32_t back)
{
    if (!(At<uint8_t>(trip, 0x1E) & 1) && static_cast<uint8_t>(back))
        return trip + 0xC;
    return trip;
}

// The game's 0x004C3860 (ret 8; this is not read): the kinds of the cells of
// a trip's route as bits (1 shifted by the cell's kind, the shift taken
// modulo 32).
uint32_t __fastcall RouteKinds(char*, void*, char* trip, uint32_t back)
{
    RouteIterator walk;
    RouteStep step;
    This<void>(addr::SC4RouteIterator_Init, &walk, RouteOf(trip, back));
    uint32_t kinds = 0;
    while (This<uint8_t>(addr::SC4RouteIterator_Next, &walk, &step))
        kinds |= uint32_t{1} << (step.kind & 0x1F);
    return kinds;
}

// The game's 0x004C4AE0 (ret 0xC): does a trip touch a box of cells (min x,
// min z, max x, max z, all inclusive)? Yes when the record's own cell (16
// bits each at +0x20, +0x22) is inside. Else the route's cells are walked:
// in mode 1 (+0x98) those of kind 6 are passed over, in mode 2 all but
// those, in any other mode none; the first cell inside says yes.
uint8_t __fastcall RouteTouches(char* self, void*, char* trip, const int32_t* box, uint32_t back)
{
    const auto inside = [box](int32_t x, int32_t z) {
        return x >= box[0] && z >= box[1] && x <= box[2] && z <= box[3];
    };
    if (inside(At<int16_t>(trip, 0x20), At<int16_t>(trip, 0x22)))
        return 1;
    RouteIterator walk;
    RouteStep step;
    This<void>(addr::SC4RouteIterator_Init, &walk, RouteOf(trip, back));
    const uint32_t mode = At<uint32_t>(self, field::kRouteMode);
    while (This<uint8_t>(addr::SC4RouteIterator_Next, &walk, &step)) {
        if (mode == 1 && step.kind == 6)
            continue;
        if (mode == 2 && step.kind != 6)
            continue;
        if (inside(step.x, step.z))
            return 1;
    }
    return 0;
}

// The game's 0x004C4630 (ret 4): which of the two windows at +0x48 and
// +0x4C shows. Nothing unless both are there. With the argument the first
// is hidden (slot 70), the second shown (slot 69) and moved (slot 56) to
// the first's slot 45 less its own slot 41, and the first's slot 44; without
// it the first is shown and the second hidden.
void __fastcall ShowOtherPanel(char* self, void*, uint32_t other)
{
    void* const panel = At<void*>(self, field::kPanel);
    if (!panel || !At<void*>(self, field::kPanelOther))
        return;
    if (!static_cast<uint8_t>(other)) {
        Virtual<void>(panel, 0x114);
        Virtual<void>(At<void*>(self, field::kPanelOther), 0x118);
        return;
    }
    Virtual<void>(panel, 0x118);
    Virtual<void>(At<void*>(self, field::kPanelOther), 0x114);
    void* const first = At<void*>(self, field::kPanel);
    void* const second = At<void*>(self, field::kPanelOther);
    const uint32_t along = Virtual<uint32_t>(first, 0xB0);
    const uint32_t own = Virtual<uint32_t>(second, 0xA4);
    Virtual<void>(second, 0xE0, Virtual<uint32_t>(first, 0xB4) - own, along);
}

// The game's 0x004C4700 (no arguments; this is not read): the application
// says yes (0x00B43C94, slot 21) and the three keys 0x10, 0x11 and 0x12
// (shift, control, alt) are down (the window manager's slot 38 gives the
// object asked, its slot 3).
uint8_t __fastcall DebugKeysDown(char*, void*)
{
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x54))
        return 0;
    void* const keys = Virtual<void*>(Cdecl<void*>(addr::SC4_GetWinManager), 0x98);
    return Virtual<uint8_t>(keys, 0xC, uint32_t{0x10}) && Virtual<uint8_t>(keys, 0xC, uint32_t{0x11}) &&
           Virtual<uint8_t>(keys, 0xC, uint32_t{0x12});
}

// The game's 0x004C4C50 (slot 18, OnMouseUpL, ret 0xC; the arguments are
// not read): the timer is stopped (its slot 7), the view's text at the
// pointer cleared (the view's slot 41 with 0x2A456CB1), the window of the
// object at +0x44 (its slot 3) hidden (slot 70), the byte at +0x89 cleared.
// True. Neither object is checked.
uint8_t __fastcall OnMouseUpL(char* self, void*, int32_t, int32_t, uint32_t)
{
    Virtual<void>(self + field::kTimer, 0x1C);
    Virtual<void>(At<void*>(self, base::kView), 0xA4, kCursorText);
    Virtual<void>(Virtual<void*>(At<void*>(self, field::kTip), 0xC), 0x118);
    At<uint8_t>(self, field::kPressed) = 0;
    return 1;
}

// The game's 0x004C4590 (ret 8): the constructor, returns this. The base's;
// the vtables; no occupant; +0x40 to +0x4C cleared; the timer made; +0x78
// set; the place is the dword at 0x00B21858 three times; the two arguments
// into +0x8C and +0x90; the route mode 3; no object at +0x9C. +0x30 to
// +0x3F and the padding are left as they come.
char* __fastcall Construct(char* self, void*, uint32_t kind, uint32_t second)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, field::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0) = addr::SC4QueryTool_Vtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4QueryTool_TargetVtable;
    At<void*>(self, field::kOccupant) = nullptr;
    At<uint32_t>(self, field::kField40) = 0;
    At<void*>(self, field::kTip) = nullptr;
    At<void*>(self, field::kPanel) = nullptr;
    At<void*>(self, field::kPanelOther) = nullptr;
    This<void*>(addr::cRZTimer_Construct, self + field::kTimer);
    At<uint8_t>(self, field::kFlag78) = 1;
    At<uint32_t>(self, field::kPlace) = Global<uint32_t>(addr::SC4QueryTool_DwordB21858);
    At<uint32_t>(self, field::kPlace + 4) = Global<uint32_t>(addr::SC4QueryTool_DwordB21858);
    At<uint32_t>(self, field::kPlace + 8) = Global<uint32_t>(addr::SC4QueryTool_DwordB21858);
    At<uint8_t>(self, field::kFlag88) = 0;
    At<uint8_t>(self, field::kPressed) = 0;
    At<uint32_t>(self, field::kKind) = kind;
    At<uint8_t>(self, field::kFlag94) = 0;
    At<uint32_t>(self, field::kSecond) = second;
    At<uint32_t>(self, field::kRouteMode) = 3;
    At<void*>(self, field::kObject9C) = nullptr;
    return self;
}

// --- Picks under the pointer -------------------------------------------------------------

// What the three picks share: the object at 0x00B43DD0 picks at a screen
// point with a filter (its slot 65: x, z, the filter, the picked one out);
// what the picked one's slot 3 gives is asked for an interface, which goes
// to *out with its reference; the picked one is let go. False, and *out
// null, when there is no such object, nothing is picked or the interface is
// not answered.
uint8_t Pick(void* view, int32_t x, int32_t z, void* filter, uint32_t iid, void** out)
{
    void* picked = nullptr;
    if (!Virtual<uint8_t>(view, 0x104, x, z, filter, &picked))
        return 0;
    uint8_t found = 0;
    void* as = nullptr;
    void* const occupant = Virtual<void*>(picked, 0xC);
    if (occupant && Virtual<uint8_t>(occupant, 0x0, iid, &as)) {
        *out = as;
        found = 1;
    }
    Release(picked);
    return found;
}

// The game's 0x004C4C90 (cdecl): the occupant (interface 0x26BB767E) under
// a screen point by the caller's filter.
uint8_t __cdecl PickOccupant(int32_t x, int32_t z, void** out, void* filter)
{
    *out = nullptr;
    void* const view = Global<void*>(addr::SC4_View43DD0);
    if (!view)
        return 0;
    return Pick(view, x, z, filter, kOccupantIid, out);
}

// A filter of 0xC bytes on the base (0x005EA080) with two vtables of the
// tool's, made for one pick and referenced. A failed allocation calls
// through null, as in the game.
char* MakeFilter(std::uintptr_t vtable, std::uintptr_t serialVtable)
{
    char* const filter = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0xC});
    if (filter) {
        This<void*>(addr::SC4_OccupantFilterBase, filter);
        At<std::uintptr_t>(filter, 0) = vtable;
        At<std::uintptr_t>(filter, 4) = serialVtable;
    }
    AddRef(filter);
    return filter;
}

// The game's 0x004C4D10 (cdecl): the automaton (interface 0xA9B40F05) under
// a screen point, by a filter of the two automaton types (0x004C3AB0).
uint8_t __cdecl PickAutomaton(int32_t x, int32_t z, void** out)
{
    *out = nullptr;
    void* const view = Global<void*>(addr::SC4_View43DD0);
    if (!view)
        return 0;
    char* const filter =
        MakeFilter(addr::SC4QueryTool_AutomatonFilterVtable, addr::SC4QueryTool_AutomatonFilterVtable2);
    const uint8_t found = Pick(view, x, z, filter, kAutomaton, out);
    Release(filter);
    return found;
}

// The game's 0x004C4DC0 (cdecl): the prop (interface 0xE9793A65) under a
// screen point, by a filter of the props of two groups (0x004C3AD0).
uint8_t __cdecl PickGroupProp(int32_t x, int32_t z, void** out)
{
    *out = nullptr;
    void* const view = Global<void*>(addr::SC4_View43DD0);
    if (!view)
        return 0;
    char* const filter = MakeFilter(addr::SC4QueryTool_PropFilterVtable, addr::SC4QueryTool_PropFilterVtable2);
    const uint8_t found = Pick(view, x, z, filter, kPropOccupant, out);
    Release(filter);
    return found;
}

// --- Timers and the sound ----------------------------------------------------------------

// A message of the given type, referenced. Null memory skips the reference
// and calls through null, as in the game.
void* MakeMessage(uint32_t type)
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    if (message)
        AddRef(message);
    Virtual<void>(message, 0x14, type);
    return message;
}

// The game's 0x004C4E70 (cdecl; what Init gives the tool's timer, its slot
// 9, with the tool): the message 0x0A456D95 is posted to the tool's target
// part (the message server's slot 7) and let go. Nothing without the
// message server.
void __cdecl TimerPost(char* tool)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (!server)
        return;
    void* const message = MakeMessage(kMessageTimer);
    Virtual<void>(server, 0x1C, message, static_cast<void*>(tool ? tool + field::kTarget : nullptr));
    Release(message);
}

// The game's 0x004C4EE0 (cdecl; what 0x004D3C30 gives a timer of its own):
// the message 0x0A456D96 is sent to all (the message server's slot 4 with
// 0) and let go.
void __cdecl TimerSend()
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (!server)
        return;
    void* const message = MakeMessage(kMessageOther);
    Virtual<void>(server, 0x10, message, uint32_t{0});
    Release(message);
}

// The game's 0x004C4F40 (cdecl): the sound of asking about an occupant.
// Nothing without the sound service (0x00B43CB8) or the occupant. The
// occupant's lot (the lot manager's slot 14) is held meanwhile. On a lot of
// condition 4 (the lot's slot 35) the sound is the occupant's property
// 0x0A902434, on one of condition 2 or 3 the property 0xAA905AB9; without a
// lot, in another condition or without that property it is the property
// 0xAA1DD397. The sound goes to the service's slot 11 (1, the sound, the
// occupant, 0, 0). The plugin sc4-query-ui-hooks turns the game's one call
// (0x004D4406) to its own.
void __cdecl QuerySound(void* occupant)
{
    void* const sounds = Global<void*>(addr::SC4_Sound43CB8);
    if (!sounds || !occupant)
        return;
    void* const holder = Virtual<void*>(occupant, 0x18);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant);
    uint32_t sound = 0;
    if (lot) {
        AddRef(lot);
        bool found = false;
        if (Virtual<uint32_t>(lot, 0x8C) == 4)
            found = Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0x0A902434}, &sound) != 0;
        if (!found && (Virtual<uint32_t>(lot, 0x8C) == 2 || Virtual<uint32_t>(lot, 0x8C) == 3))
            found = Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0xAA905AB9}, &sound) != 0;
        if (found) {
            Virtual<void>(sounds, 0x2C, uint32_t{1}, sound, occupant, uint32_t{0}, uint32_t{0});
            Release(lot);
            return;
        }
    }
    if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0xAA1DD397}, &sound))
        Virtual<void>(sounds, 0x2C, uint32_t{1}, sound, occupant, uint32_t{0}, uint32_t{0});
    if (lot)
        Release(lot);
}

// --- The panel's target ------------------------------------------------------------------

// The game's 0x004C4570 (slot 5 of the vtable 0x00A90980, ret 4): the
// deleting destructor of the 0xC-byte object the tool gives a window as
// target: the vtable set again, delete for bit 0; returns this.
char* __fastcall PanelTargetDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::SC4QueryPanelTarget_Vtable;
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// --- The window runner -------------------------------------------------------------------

// The game's 0x004C44E0 (slot 0 of the vtable 0x00A90B6C, ret 8; also where
// the two parts' thunks 0x004429A0 and 0x004429B0 jump): QueryInterface.
// The procedure part for 0x22E85D8E, the target part for 0x452294AA, with a
// reference; every other ID, and either of the two on a null object, goes
// to the base's (0x009457C6).
uint8_t __fastcall RunnerQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid == kRunnerProcedure || iid == kRunnerTarget) {
        char* const part =
            self ? self + (iid == kRunnerProcedure ? window_runner::kProcedure : window_runner::kTarget) : nullptr;
        *out = part;
        if (part) {
            AddRef(self);
            return 1;
        }
    }
    return This<uint8_t>(addr::cRZBaseUnknown_QueryInterface1, self, iid, out);
}

// The game's 0x004C4530 (slot 6, ret 4): the object at +0x14 is let go and
// the given one kept with a reference. False for null.
uint8_t __fastcall RunnerSetObject(char* self, void*, void* object)
{
    if (void* const held = At<void*>(self, window_runner::kObject)) {
        Release(held);
        At<void*>(self, window_runner::kObject) = nullptr;
    }
    if (!object)
        return 0;
    At<void*>(self, window_runner::kObject) = object;
    AddRef(object);
    return 1;
}

// The game's 0x004C5620 (ret 4): the constructor, returns this. The counted
// base (0x0090DA1E), the three vtables (the two parts' bases first), no
// window and no object; then the window is kept with a reference and
// referenced once more. A null window calls through null, as in the game.
char* __fastcall RunnerConstruct(char* self, void*, void* window)
{
    This<void*>(addr::cRZBaseUnknown_Construct, self);
    At<std::uintptr_t>(self, window_runner::kProcedure) = addr::SC4NewsTicker_WinProcBaseVtable;
    At<std::uintptr_t>(self, window_runner::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0) = addr::SC4QueryWindowRunner_Vtable;
    At<std::uintptr_t>(self, window_runner::kProcedure) = addr::SC4QueryWindowRunner_ProcedureVtable;
    At<std::uintptr_t>(self, window_runner::kTarget) = addr::SC4QueryWindowRunner_TargetVtable;
    At<void*>(self, window_runner::kWindow) = nullptr;
    At<void*>(self, window_runner::kObject) = nullptr;
    if (window) {
        AddRef(window);
        At<void*>(self, window_runner::kWindow) = window;
    }
    AddRef(At<void*>(self, window_runner::kWindow));
    return self;
}

// The game's 0x004C5690 (ret 4): the window's run is ended with a code: the
// window manager's slot 9 (0xF, the window by its slot 3, the code); then
// the window is let go twice and the field cleared, and the object at +0x14
// let go and cleared. The one caller (0x004D3F40) passes 0 or 1.
void __fastcall RunnerEnd(char* self, void*, uint32_t code)
{
    void* const window = At<void*>(self, window_runner::kWindow);
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    Virtual<void>(manager, 0x24, uint32_t{0xF}, Virtual<void*>(window, 0xC), code);
    Release(At<void*>(self, window_runner::kWindow));
    if (void* const held = At<void*>(self, window_runner::kWindow)) {
        At<void*>(self, window_runner::kWindow) = nullptr;
        Release(held);
    }
    if (void* const object = At<void*>(self, window_runner::kObject)) {
        Release(object);
        At<void*>(self, window_runner::kObject) = nullptr;
    }
}

// --- Registration ----------------------------------------------------------------------

bool InstallQuery1b(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_IsAskable, reinterpret_cast<void*>(&IsAskable)},
        {addr::SC4QueryTool_OccupantName, reinterpret_cast<void*>(&OccupantName)},
        {addr::SC4QueryTool_RouteKinds, reinterpret_cast<void*>(&RouteKinds)},
        {addr::SC4QueryTool_IsBuildingOrNetwork, reinterpret_cast<void*>(&IsBuildingOrNetwork)},
        {addr::SC4QueryTool_IsAutomatonType, reinterpret_cast<void*>(&IsAutomatonType)},
        {addr::SC4QueryTool_IsGroupProp, reinterpret_cast<void*>(&IsGroupProp)},
        {addr::SC4QueryWindowRunner_QueryInterface, reinterpret_cast<void*>(&RunnerQueryInterface)},
        {addr::SC4QueryWindowRunner_SetObject, reinterpret_cast<void*>(&RunnerSetObject)},
        {addr::SC4QueryPanelTarget_DeletingDestructor, reinterpret_cast<void*>(&PanelTargetDelete)},
        {addr::SC4ViewInputControls_F4C4590, reinterpret_cast<void*>(&Construct)},
        {addr::SC4QueryTool_ShowOtherPanel, reinterpret_cast<void*>(&ShowOtherPanel)},
        {addr::SC4QueryTool_IsAskableUnmarked, reinterpret_cast<void*>(&IsAskableUnmarked)},
        {addr::SC4QueryTool_DebugKeysDown, reinterpret_cast<void*>(&DebugKeysDown)},
        {addr::SC4QueryTool_RouteTouches, reinterpret_cast<void*>(&RouteTouches)},
        {addr::SC4QueryTool_OnMouseUpL, reinterpret_cast<void*>(&OnMouseUpL)},
        {addr::SC4QueryTool_PickOccupant, reinterpret_cast<void*>(&PickOccupant)},
        {addr::SC4QueryTool_PickAutomaton, reinterpret_cast<void*>(&PickAutomaton)},
        {addr::SC4QueryTool_PickGroupProp, reinterpret_cast<void*>(&PickGroupProp)},
        {addr::SC4QueryTool_TimerPost, reinterpret_cast<void*>(&TimerPost)},
        {addr::SC4QueryTool_TimerSend, reinterpret_cast<void*>(&TimerSend)},
        {addr::SC4QueryTool_QuerySound, reinterpret_cast<void*>(&QuerySound)},
        {addr::SC4QueryWindowRunner_Construct, reinterpret_cast<void*>(&RunnerConstruct)},
        {addr::SC4QueryWindowRunner_End, reinterpret_cast<void*>(&RunnerEnd)},
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
    "ui.tools.query1b",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4ViewInputControls_F4C4590,
    "Our query tool, first piece: the constructor, the filters and the small classes (docs/tools.md)",
    false,
    InstallQuery1b,
});

} // namespace
} // namespace opensc4::tool_query_1b
