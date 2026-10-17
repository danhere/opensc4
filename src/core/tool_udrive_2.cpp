// The U-Drive-It driving control, second piece, translator A (0x00562C90 to
// 0x00566D55). Replacement, off by default:
//
//   ui.tools.udrive2: Init (slot 3), Shutdown (slot 4), the message
//     handler (slot 3 of the target part, this = control + 0x28) with the
//     panel's update on every tick, the panel's button commands (slot 3 of
//     the third interface, this = control + 0x2C), letting go of the driven
//     object (0x00562FD0), stopping and pausing the city (0x00564450), the
//     list of held objects (0x00565FA0), closing the panel windows
//     (0x00563330), the check when an automaton goes away (0x00563310) and
//     the test of the control's own occupant filter (vtable 0x00A9D894).
//
// The control (cSC4UDriveControl, 0xC0 bytes, vtable 0x00A9D9B0) is made by
// the park automaton 0x00506850, which then calls 0x00565160 (a vehicle to
// drive, ui.tools.udrive3) or 0x00564450 (none), and 0x00565FA0 with its
// second argument. Init takes the services and the city's automata managers,
// registers the message target for ticks (0x86AD10EC, priority 0xFF) and for
// four messages, makes the marker at +0x9C (0x005649D0, drawing: left the
// game's), loads the driving panel's window, registers the three panel
// commands with the framework's command service; Shutdown undoes that in
// its own order, resumes the simulator and the automata managers 0x00564450
// paused, and lets the park automaton drop its control (0x00506910).
//
// Left the game's: 0x005658D0 (ret 8; this is the overlay manager the
// control holds at +0x34: sets 0x400 in the marker of a byte key, a method
// of SC4OverlayManager compiled here), 0x00565430 (a byte-keyed map's find,
// outside caller 0x00674140), the marker's functions (0x005649D0,
// 0x005633C0, 0x00563730: drawing), the thunks 0x00565570 to 0x005655C0.
//
// Clocks: the stopwatch at +0x48 (mode 4) is on real time (not under
// determinism.sim_timers): DoMessage restarts it for the automaton message
// 0x09D92FC3 kind 0xB, the panel's update reads it and puts the control away
// (slot 27) once 9000 have gone by. The panel's update runs on every tick
// (0x86AD10EE) and reads the driven automaton's speed and state (frame
// driven); it sets the sound system's two engine volumes (0x00602640 slots
// 0x1C, 0x20). No random number is drawn.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace opensc4::tool_udrive_2 {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kInited = 0x04; // byte
constexpr std::size_t kCursorId = 0x10;
constexpr std::size_t kView = 0x1C;   // the city view (referenced)
} // namespace base

// The control's fields.
namespace field {
constexpr std::size_t kTarget = 0x28;      // the message target part (vtable 0x00A9D9A0)
constexpr std::size_t kDriven = 0x30;      // the driven automaton (referenced)
constexpr std::size_t kOverlays = 0x34;    // the overlay manager (dispatch service slot 0x58)
constexpr std::size_t kMarkerId = 0x38;    // T7a's kWord38: the driven automaton's marker on the overlay manager
constexpr std::size_t kSpeedScale = 0x40;  // T7a's kFloat40 (0.042f): the speed dial's top, the volume's scale
constexpr std::size_t kByte44 = 0x44;      // the button 0x0C16C9B9's state
constexpr std::size_t kStopwatch = 0x48;   // 0x18 bytes, mode 4, real time
constexpr std::size_t kRef60 = 0x60;       // the automaton under the pointer (referenced)
constexpr std::size_t kList = 0x64;        // list head (12-byte nodes: next, prev, object), objects referenced
constexpr std::size_t kTerrain = 0x68;     // the terrain (referenced) once Init ran
constexpr std::size_t kTerrainSize = 0x6C; // T7a's kFloat6C: terrain slot 6
constexpr std::size_t kTerrainScale = 0x70; // T7a's kFloat70: 1 over it
constexpr std::size_t kString74 = 0x74;    // four empty strings (0x14 bytes, referenced) Init makes
constexpr std::size_t kString78 = 0x78;
constexpr std::size_t kString7C = 0x7C;
constexpr std::size_t kString80 = 0x80;
constexpr std::size_t kAutomatonFilter = 0x84; // the automaton pick's filter (vtables 0x00A9D894/0x00A9D87C)
constexpr std::size_t kNetworkFilter = 0x88;   // the network pick's filter (vtables 0x00A8FE14/0x00A8FDFC)
constexpr std::size_t kVehicles = 0x8C;    // city slots 43 to 46, the automata managers (referenced)
constexpr std::size_t kPedestrians = 0x90;
constexpr std::size_t kAircraft = 0x94;
constexpr std::size_t kWatercraft = 0x98;
constexpr std::size_t kMarker = 0x9C;      // the marker (0x4C bytes, 0x005649D0)
constexpr std::size_t kSimulator = 0xA0;   // global 0x00B43D5C (referenced)
constexpr std::size_t kPanel = 0xA4;       // the panel windows (referenced)
constexpr std::size_t kPanelSmall = 0xA8;
constexpr std::size_t kNetworks = 0xAC;    // the network manager, global 0x00B43D54 (referenced)
constexpr std::size_t kScripts = 0xB0;     // city slot 48, the automata script system (referenced)
constexpr std::size_t kOldWindow = 0xB4;   // what the view's slot 0x7C gave (referenced)
constexpr std::size_t kPanelWindow = 0xB8; // the COM object 0x42E967BE given to the view's slot 0x80
} // namespace field

// The automaton message and its kinds (GetData2).
constexpr uint32_t kMsgAutomaton = 0x09D92FC3;
constexpr uint32_t kMsgTick = 0x86AD10EE;
constexpr uint32_t kMsgView = 0xA6B79602;
constexpr uint32_t kMsgMinimize = 0xAC1F0449;
constexpr uint32_t kMsgMaximize = 0xCC1F0457;
constexpr uint32_t kMsgToggle = 0xAC1F045D;

constexpr uint32_t kCursor = 0xC16C4E1B;
constexpr uint32_t kPointerText = 0xAB73510C;
constexpr uint32_t kFlagStopped = 0x8000000;
constexpr uint32_t kFlagOne = 1;
constexpr uint32_t kBankProperty = 0xCBBBA52F;
constexpr uint32_t kDialIid = 0x0BCBF1DF;
constexpr uint32_t kTextIid = 0x212CDC1F;
constexpr uint32_t kButtonIid = 0x8810;
constexpr std::size_t kCommandCount = 3; // entries of 12 bytes at 0x00B0A110

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

uint32_t Bits(float value)
{
    return std::bit_cast<uint32_t>(value);
}

// The game's inline reference holder assignment: the new object referenced,
// kept, the old one released (nothing when they are the same).
void Assign(char* self, std::size_t offset, void* object)
{
    void* const old = At<void*>(self, offset);
    if (object == old)
        return;
    if (object)
        AddRef(object);
    At<void*>(self, offset) = object;
    if (old)
        Release(old);
}

// "if held: field = 0, release" as the game writes it.
void Drop(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        At<void*>(self, offset) = nullptr;
        Release(object);
    }
}

void* Sound()
{
    return Cdecl<void*>(addr::SC4UDrive_GetService602640);
}

uint32_t CommandId(std::size_t index)
{
    return Global<uint32_t>(addr::cSC4UDriveControl_PanelCommands + index * 12);
}

// The game's string as it sits on the stack or the heap: vtable 0x00A80810,
// STL body (begin, end, end of the buffer) and a reference count.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t count;
};
static_assert(sizeof(GameString) == 0x14);

// The game's inline destructor of a stack string: the vtable set again, the
// buffer freed.
void DestroyInline(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text.begin));
}

// Init's inline construction of a referenced empty string on the heap; the
// caller keeps it and references it.
GameString* NewEmptyString()
{
    auto* const text = static_cast<GameString*>(Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x14}));
    if (!text)
        return nullptr;
    text->begin = nullptr;
    text->end = nullptr;
    text->capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, std::size_t{8});
    text->capacity = buffer + 8;
    text->begin = buffer;
    text->end = buffer;
    *buffer = 0;
    text->vtable = addr::SC4String_Vtable;
    text->count = 0;
    return text;
}

// Init's inline construction of a 0xC-byte occupant filter with the two
// vtables given.
void* NewFilter(std::uintptr_t vtable, std::uintptr_t vtable4)
{
    void* const filter = Cdecl<void*>(addr::SC4_operator_new, std::size_t{0xC});
    if (!filter)
        return nullptr;
    This<void*>(addr::SC4_OccupantFilterBase, filter);
    At<std::uintptr_t>(filter, 0) = vtable;
    At<std::uintptr_t>(filter, 4) = vtable4;
    return filter;
}

// --- Small functions --------------------------------------------------------------------

// The game's 0x00562C90 (slot 3 of the control's filter, vtable 0x00A9D894;
// ret 4): true for the occupant types 0xC7CC75F1, 0x496E5783, 0x480E6151 and
// 0x897F7EC2, the type asked again for each (slot 0x1C).
uint8_t __fastcall FilterTest(void*, void*, void* occupant)
{
    if (Virtual<uint32_t>(occupant, 0x1C) == 0xC7CC75F1)
        return 1;
    if (Virtual<uint32_t>(occupant, 0x1C) == 0x496E5783)
        return 1;
    if (Virtual<uint32_t>(occupant, 0x1C) == 0x480E6151)
        return 1;
    if (Virtual<uint32_t>(occupant, 0x1C) == 0x897F7EC2)
        return 1;
    return 0;
}

// The game's 0x00562E20 (slot 3 of the third interface, this = control +
// 0x2C, ret 8): a button click (kind 3, 0x287259F6) on the panel's buttons
// posts a command (the command service, global 0x00B43CB0, slot 0x50 with
// the ID and two zeros); true for those four buttons, false else.
uint8_t __fastcall OnPanelCommand(void*, void*, void*, const uint32_t* message)
{
    if (message[0] != 3 || message[1] != 0x287259F6)
        return 0;
    uint32_t command;
    switch (message[2]) {
    case 0x4992046A:
        command = 0x6A935CB7;
        break;
    case 0x2C16C9CB:
        command = kMsgMinimize;
        break;
    case 0x4C1A5CC9:
        command = kMsgMaximize;
        break;
    case 0xE9920494:
        command = 0x6A935CBA;
        break;
    default:
        return 0;
    }
    Virtual<void>(Global<void*>(addr::SC4_Commands), 0x50, command, uint32_t{0}, uint32_t{0});
    return 1;
}

// The game's 0x00565FA0 (ret 4, caller the park automaton): an object not in
// the list yet is referenced and put at its end.
void __fastcall AddListed(char* self, void*, void* object)
{
    if (!object)
        return;
    void** const head = At<void**>(self, field::kList);
    void** node = static_cast<void**>(head[0]);
    while (node != head && node[2] != object)
        node = static_cast<void**>(node[0]);
    if (node != At<void**>(self, field::kList))
        return;
    AddRef(object);
    void** const list = At<void**>(self, field::kList);
    void** const added = static_cast<void**>(Cdecl<void*>(addr::SC4_stl_allocate, std::size_t{0xC}));
    added[2] = object;
    void** const last = static_cast<void**>(list[1]);
    added[0] = list;
    added[1] = last;
    last[0] = added;
    list[1] = added;
}

// The game's 0x00562FD0: lets go of the driven automaton. Our callback off
// it (slot 0xA0), its marker off the overlay manager, the vehicle manager's
// driven automaton cleared (slot 0x88 with 0), the view's slot 0x2C with (0,
// 5), the sound system's event 0 stopped and the automaton's sound bank
// (property 0xCBBBA52F) unloaded; an automaton in behaviour 0x10 is removed
// (slot 0x34 with false) unless it is stopped or its occupant is in group
// 0x4004, then it pops its state (slot 0xD4). Then released.
void __fastcall F562FD0(char* self, void*)
{
    void* const driven = At<void*>(self, field::kDriven);
    if (!driven)
        return;
    Virtual<void>(driven, 0xA0, self + field::kTarget);
    if (void* const overlays = At<void*>(self, field::kOverlays))
        This<void>(addr::SC4OverlayManager_Remove, overlays, At<uint32_t>(self, field::kMarkerId), uint32_t{0});
    Virtual<void>(At<void*>(self, field::kVehicles), 0x88, static_cast<void*>(nullptr));
    Virtual<void>(At<void*>(self, base::kView), 0x2C, uint32_t{0}, uint32_t{5});
    Virtual<uint8_t>(Sound(), 0x18, uint32_t{0});
    void* const occupant = Virtual<void*>(At<void*>(self, field::kDriven), 0x14);
    void* const sound = Sound();
    void* const holder = Virtual<void*>(occupant, 0x18);
    Virtual<uint8_t>(sound, 0x10, holder, kBankProperty);
    if (Virtual<uint32_t>(At<void*>(self, field::kDriven), 0xEC) == 0x10) {
        if (!Virtual<uint8_t>(At<void*>(self, field::kDriven), 0x7C, kFlagStopped) &&
            !Virtual<uint8_t>(Virtual<void*>(At<void*>(self, field::kDriven), 0x14), 0x58, uint32_t{0x4003}))
            Virtual<void>(At<void*>(self, field::kDriven), 0x34, uint32_t{0});
        else
            Virtual<uint8_t>(At<void*>(self, field::kDriven), 0xD4);
    }
    Drop(self, field::kDriven);
}

// The game's 0x00563310 (ret 4, caller DoMessage): when the automaton that
// went away is the driven one, lets go of it and puts the control away
// (slot 27).
void __fastcall OnAutomatonGone(char* self, void*, void* automaton)
{
    if (automaton != At<void*>(self, field::kDriven))
        return;
    F562FD0(self, nullptr);
    Virtual<uint32_t>(self, 0x6C);
}

// The game's 0x00563330 (caller Shutdown): the two panel windows taken off
// the view's window (slot 3, its slot 0x3C) and released, then the view's
// slot 0xE4.
void __fastcall CloseWindows(char* self, void*)
{
    for (const std::size_t offset : {field::kPanel, field::kPanelSmall}) {
        if (!At<void*>(self, offset))
            continue;
        void* const window = Virtual<void*>(At<void*>(self, base::kView), 0xC);
        Virtual<uint8_t>(window, 0x3C, At<void*>(self, offset));
        Drop(self, offset);
    }
    Virtual<uint32_t>(At<void*>(self, base::kView), 0xE4);
}

// The game's 0x00564450 (caller the park automaton, when it has no vehicle
// to give): lets go of the driven automaton and of +0x60 (and the view's
// pointer text 0xAB73510C, view slot 0xA4), pauses the simulator (slot 0x24 when
// slot 0x3C says it is not paused) and the four automata managers (slot
// 0x24).
void __fastcall StopAndPause(char* self, void*)
{
    F562FD0(self, nullptr);
    if (At<void*>(self, field::kRef60)) {
        Drop(self, field::kRef60);
        Virtual<uint32_t>(At<void*>(self, base::kView), 0xA4, kPointerText);
    }
    if (void* const simulator = At<void*>(self, field::kSimulator)) {
        if (!Virtual<uint8_t>(simulator, 0x3C))
            Virtual<uint8_t>(At<void*>(self, field::kSimulator), 0x24);
    }
    for (const std::size_t offset : {field::kVehicles, field::kPedestrians, field::kAircraft, field::kWatercraft}) {
        if (void* const manager = At<void*>(self, offset))
            Virtual<void>(manager, 0x24);
    }
}

// --- The panel ------------------------------------------------------------------------

// One of the panel's dials: a child of the interface 0x0BCBF1DF gets its
// bottom, top and value (slots 0x20, 0x28, 0x30).
void SetDial(void* dial, uint32_t bottom, uint32_t top, uint32_t value)
{
    Virtual<void>(dial, 0x20, bottom);
    Virtual<void>(dial, 0x28, top);
    Virtual<void>(dial, 0x30, value);
}

// The game's way of letting go of a child it asks for again: the holder
// cleared, the child released.
void Let(void*& child)
{
    if (void* const held = child) {
        child = nullptr;
        Release(held);
    }
}

// The game's 0x00566000 (caller DoMessage on every tick): with a driven
// automaton that is neither stopped nor flagged 1: the cursor from its state
// 0x10 (+0x114 when +0x9C is not -1, else 0xC16C4E1B; slot 8), the panel's
// dials and texts (speed, the state's figures, height over the ground, the
// city's money, a count), the view's slot 0xE8, the two engine volumes.
// Then, with the stopwatch running and 9000 gone by, the control put away
// (slot 27).
void __fastcall UpdatePanel(char* self, void*)
{
    void* const drivenAtStart = At<void*>(self, field::kDriven);
    if (drivenAtStart && !Virtual<uint8_t>(drivenAtStart, 0x7C, kFlagStopped) &&
        !Virtual<uint8_t>(At<void*>(self, field::kDriven), 0x7C, kFlagOne)) {
        char* const scratch = Cdecl<char*>(addr::SC4_stl_allocate, std::size_t{8});
        *scratch = 0;
        const uint32_t fill = Global<uint32_t>(addr::SC4UDrive_VectorFill);
        uint32_t position[3] = {fill, fill, fill};
        uint32_t destination[3] = {fill, fill, fill};
        Virtual<void>(At<void*>(self, field::kDriven), 0x1C, position);
        Virtual<void>(At<void*>(self, field::kDriven), 0x2C, destination);
        char* const state = Virtual<char*>(At<void*>(self, field::kDriven), 0xE4, uint32_t{0x10});
        if (state) {
            const uint32_t cursor = At<uint32_t>(state, 0x114);
            if (!(At<float>(state, 0x9C) == -1.0f) && cursor) {
                At<uint32_t>(self, base::kCursorId) = cursor;
                Virtual<void>(self, 0x20, cursor);
            } else {
                At<uint32_t>(self, base::kCursorId) = kCursor;
                Virtual<void>(self, 0x20, kCursor);
            }
        }
        if (void* const panel = At<void*>(self, field::kPanel)) {
            void* dial = nullptr;   // the game's [esp+0x8]
            void* text = nullptr;   // [esp+0xC]
            void* button = nullptr; // [esp+0x10]
            if (state) {
                if (Virtual<uint8_t>(panel, 0x90, uint32_t{0xEBCB9403}, kDialIid, &dial)) {
                    const float speed = static_cast<float>(Virtual<double>(At<void*>(self, field::kDriven), 0x88));
                    SetDial(dial, 0, At<uint32_t>(self, field::kSpeedScale), Bits(speed));
                }
                Let(dial);
                if (Virtual<uint8_t>(At<void*>(self, field::kPanel), 0x90, uint32_t{0x2BCB940B}, kDialIid, &dial)) {
                    const float low = At<float>(state, 0x30);
                    const float high = At<float>(state, 0x34);
                    const float value = At<float>(state, 0x2C);
                    float shown;
                    if (value < low)
                        shown = low;
                    else if (value > high)
                        shown = high;
                    else
                        shown = value;
                    SetDial(dial, At<uint32_t>(state, 0x30), At<uint32_t>(state, 0x34), Bits(shown));
                }
                Let(dial);
                if (Virtual<uint8_t>(At<void*>(self, field::kPanel), 0x90, uint32_t{0x2BCBCE68}, kDialIid, &dial)) {
                    float place[3];
                    Virtual<void>(At<void*>(self, field::kDriven), 0x1C, place);
                    void* const terrain = Global<void*>(addr::SC4_Terrain);
                    const uint8_t inside = Virtual<uint8_t>(terrain, 0x2C, place[0], place[2]);
                    double ground = inside ? Virtual<double>(terrain, 0x34, place[0], place[2])
                                           : Virtual<double>(terrain, 0x74);
                    void* const occupant = Virtual<void*>(At<void*>(self, field::kDriven), 0x14);
                    if (Virtual<uint8_t>(occupant, 0x58, uint32_t{0x4004})) {
                        ground = inside ? Virtual<double>(terrain, 0x34, place[0], place[2])
                                        : Virtual<double>(terrain, 0x74);
                    } else {
                        int32_t cellX, cellZ;
                        Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x17C, place[0], place[2], &cellX, &cellZ);
                        ground = inside ? Virtual<double>(Global<void*>(addr::SC4_RegionTerrain), 0x48, cellX, cellZ)
                                        : Virtual<double>(terrain, 0x74);
                    }
                    const float height = static_cast<float>(static_cast<double>(place[1]) - ground);
                    Let(text);
                    if (Virtual<uint8_t>(At<void*>(self, field::kPanel), 0x90, uint32_t{0xEC0EA996}, kTextIid, &text)) {
                        GameString caption;
                        This<void*>(addr::SC4String_SC4String, &caption);
                        Cdecl<void>(addr::cRZBaseString_Sprintf, &caption, "%.0f", static_cast<double>(height));
                        Virtual<uint8_t>(Virtual<void*>(text, 0xC), 0x128, &caption);
                        This<void>(addr::SC4String_Destroy, &caption);
                    }
                    Virtual<void>(dial, 0x20, uint32_t{0});
                    Virtual<void>(dial, 0x28, uint32_t{0x42C80000}); // 100
                    float shown = height;
                    double reduced = height;
                    if (reduced > 100.0) {
                        do
                            reduced -= 100.0;
                        while (reduced > 100.0);
                        shown = static_cast<float>(reduced);
                    }
                    Virtual<void>(dial, 0x30, Bits(shown));
                }
                Let(dial);
                if (Virtual<uint8_t>(At<void*>(self, field::kPanel), 0x90, uint32_t{0x6BCBCE73}, kDialIid, &dial))
                    SetDial(dial, 0xBF060A92, 0x3F060A92, At<uint32_t>(state, 0x238)); // -30 to 30 degrees
                Let(dial);
                if (Virtual<uint8_t>(At<void*>(self, field::kPanel), 0x90, uint32_t{0x2BF98D69}, kDialIid, &dial))
                    SetDial(dial, At<uint32_t>(state, 0x248), At<uint32_t>(state, 0x24C), At<uint32_t>(state, 0x250));
                Let(dial);
                if (Virtual<uint8_t>(At<void*>(self, field::kPanel), 0x90, uint32_t{0xEBF98D37}, kDialIid, &dial))
                    SetDial(dial, At<uint32_t>(state, 0x254), At<uint32_t>(state, 0x258), At<uint32_t>(state, 0x25C));
                Let(dial);
                if (Virtual<uint8_t>(At<void*>(self, field::kPanel), 0x90, uint32_t{0x6C0AE7B1}, kDialIid, &dial))
                    SetDial(dial, At<uint32_t>(state, 0x23C), At<uint32_t>(state, 0x240), At<uint32_t>(state, 0x244));
                Let(dial);
                if (Virtual<uint8_t>(At<void*>(self, field::kPanel), 0x90, uint32_t{0x2C0C1C8C}, kDialIid, &dial))
                    SetDial(dial, At<uint32_t>(state, 0x260), At<uint32_t>(state, 0x264), At<uint32_t>(state, 0x268));
                Let(button);
                if (Virtual<uint8_t>(At<void*>(self, field::kPanel), 0x90, uint32_t{0x0C16C9B9}, kButtonIid, &button))
                    Virtual<void>(button, 0x14, uint32_t{1}, static_cast<uint32_t>(At<uint8_t>(self, field::kByte44)));
                Let(text);
                if (Virtual<uint8_t>(At<void*>(self, field::kPanel), 0x90, uint32_t{0xAC0EA9DB}, kTextIid, &text)) {
                    GameString caption;
                    This<void*>(addr::SC4String_SC4String, &caption);
                    void* const budget = Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0);
                    const int64_t funds = Virtual<int64_t>(budget, 0x18);
                    Cdecl<void>(addr::SC4_FormatMoney, funds, &caption);
                    Virtual<uint8_t>(Virtual<void*>(text, 0xC), 0x128, &caption);
                    DestroyInline(caption);
                }
                Let(text);
                if (Virtual<uint8_t>(At<void*>(self, field::kPanel), 0x94, uint32_t{0x8C3923AE}, kTextIid, &text)) {
                    GameString caption;
                    This<void*>(addr::SC4String_SC4String, &caption);
                    Cdecl<void>(addr::cRZBaseString_Sprintf, &caption, "%d",
                                At<int32_t>(state, 0x144) - At<int32_t>(state, 0x178));
                    Virtual<uint8_t>(Virtual<void*>(text, 0xC), 0x128, &caption);
                    Let(button);
                    if (Virtual<uint8_t>(At<void*>(self, field::kPanel), 0x94, uint32_t{0x6C3A0A6F}, kButtonIid, &button))
                        Virtual<void>(button, 0x24, static_cast<uint32_t>(At<uint8_t>(state, 0x1FF)));
                    DestroyInline(caption);
                }
            }
            Virtual<uint32_t>(At<void*>(self, base::kView), 0xE8);
            if (text)
                Release(text);
            if (button)
                Release(button);
            if (dial)
                Release(dial);
        }
        if (state) {
            float volume;
            if (At<float>(self, field::kSpeedScale) > 0.0f) {
                const double speed = Virtual<double>(At<void*>(self, field::kDriven), 0x88);
                volume = static_cast<float>(std::fabs(speed) / static_cast<double>(At<float>(self, field::kSpeedScale)));
            } else {
                volume = 0.0f;
            }
            Virtual<void>(Sound(), 0x1C, Bits(volume));
            const float low = At<float>(state, 0x30);
            const float high = At<float>(state, 0x34);
            const double value = std::fabs(static_cast<double>(At<float>(state, 0x2C)));
            uint32_t second;
            if (high > 0.0f) {
                if (!(high <= low))
                    second = Bits(static_cast<float>((value - low) / (static_cast<double>(high) - low)));
                else
                    second = Bits(static_cast<float>(value / high));
            } else {
                second = 0;
            }
            Virtual<void>(Sound(), 0x20, second);
        }
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(scratch));
    }
    char* const stopwatch = self + field::kStopwatch;
    if (This<uint8_t>(addr::GZStopwatch_IsRunning, stopwatch)) {
        float elapsed;
        This<void>(addr::SC4Stopwatch_GetElapsed, stopwatch, &elapsed);
        if (elapsed >= 9000.0f)
            Virtual<uint32_t>(self, 0x6C);
    }
}

// --- The message handler --------------------------------------------------------------

// The game's 0x00566890 (slot 3 of the target part, this = control + 0x28,
// ret 4). True always. 0xA6B79602: the panel's two zoom buttons enabled by
// the view (slots 0x30, 0x34); 0x86AD10EE (tick): the panel's update;
// 0x09D92FC3 (an automaton's news, GetVoid1 the automaton, GetData2 the
// kind): 3 it went away, 0xB the sound event 0 stopped, its marker flagged
// (0x005658D0 on the overlay manager) and the stopwatch restarted, 0xD with
// GetData3 0x10 and no state 0x10 any more the sound event 0 stopped;
// 0xAC1F0449 / 0xCC1F0457 / 0xAC1F045D: the panel made small, big, or
// switched.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const self = target - field::kTarget;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case kMsgView:
        if (void* const panel = At<void*>(self, field::kPanel)) {
            const uint32_t smaller = Virtual<uint32_t>(At<void*>(self, base::kView), 0x30);
            Cdecl<uint8_t>(addr::SC4Win_EnableChild, panel, uint32_t{0x4992046A}, smaller);
            void* const again = At<void*>(self, field::kPanel);
            const uint32_t larger = Virtual<uint32_t>(At<void*>(self, base::kView), 0x34);
            Cdecl<uint8_t>(addr::SC4Win_EnableChild, again, uint32_t{0xE9920494}, larger);
        }
        break;
    case kMsgTick:
        UpdatePanel(self, nullptr);
        break;
    case kMsgAutomaton: {
        void* const automaton = Virtual<void*>(message, 0x28);
        const uint32_t kind = Virtual<uint32_t>(message, 0x38);
        if (kind == 3) {
            OnAutomatonGone(self, nullptr, automaton);
        } else if (kind == 0xB) {
            Virtual<uint8_t>(Sound(), 0x18, uint32_t{0});
            This<uint8_t>(addr::SC4OverlayManager_F5658D0, At<void*>(self, field::kOverlays),
                          At<uint32_t>(self, field::kMarkerId), uint32_t{1});
            char* const stopwatch = self + field::kStopwatch;
            This<void*>(addr::SC4Stopwatch_ConstructMode, stopwatch, uint32_t{4});
            This<void>(addr::GZStopwatch_Start, stopwatch);
        } else if (kind == 0xD) {
            if (Virtual<uint32_t>(message, 0x4C) == 0x10 && !Virtual<void*>(automaton, 0xE4, uint32_t{0x10}))
                Virtual<uint8_t>(Sound(), 0x18, uint32_t{0});
        }
        break;
    }
    case kMsgMinimize:
        if (void* const panel = At<void*>(self, field::kPanel)) {
            Virtual<void>(panel, 0x118);
            Virtual<void>(At<void*>(self, field::kPanelSmall), 0x114);
        }
        break;
    case kMsgMaximize:
        if (void* const panel = At<void*>(self, field::kPanel)) {
            Virtual<void>(panel, 0x114);
            Virtual<void>(At<void*>(self, field::kPanelSmall), 0x118);
        }
        break;
    case kMsgToggle:
        if (void* const panel = At<void*>(self, field::kPanel)) {
            if (Virtual<uint8_t>(panel, 0x11C)) {
                Virtual<void>(At<void*>(self, field::kPanel), 0x118);
                Virtual<void>(At<void*>(self, field::kPanelSmall), 0x114);
            } else {
                Virtual<void>(At<void*>(self, field::kPanel), 0x114);
                Virtual<void>(At<void*>(self, field::kPanelSmall), 0x118);
            }
        }
        break;
    default:
        break;
    }
    return 1;
}

// --- Init and Shutdown ----------------------------------------------------------------

// The game's 0x00565A10 (slot 3): Init. Once (the base's inited byte). True
// always.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    Virtual<uint8_t>(Cdecl<void*>(addr::SC4_GetService86AD10EC), 0x14, self + field::kTarget, uint32_t{0xFF});
    if (!At<void*>(self, field::kTerrain)) {
        void* const terrain = Global<void*>(addr::SC4_Terrain);
        At<void*>(self, field::kTerrain) = terrain;
        AddRef(terrain);
        const double size = Virtual<double>(At<void*>(self, field::kTerrain), 0x18);
        At<float>(self, field::kTerrainSize) = static_cast<float>(size);
        At<float>(self, field::kTerrainScale) = static_cast<float>(1.0 / size);
    }
    if (!At<void*>(self, field::kAutomatonFilter)) {
        void* const filter = NewFilter(addr::cSC4UDriveControl_FilterVtable, addr::cSC4UDriveControl_FilterVtable4);
        At<void*>(self, field::kAutomatonFilter) = filter;
        AddRef(filter);
    }
    if (!At<void*>(self, field::kNetworkFilter)) {
        void* const filter = NewFilter(0x00A8FE14u, 0x00A8FDFCu);
        At<void*>(self, field::kNetworkFilter) = filter;
        AddRef(filter);
    }
    for (const std::size_t offset : {field::kString74, field::kString78, field::kString7C, field::kString80}) {
        GameString* const text = NewEmptyString();
        At<GameString*>(self, offset) = text;
        AddRef(text);
    }
    if (void* const strings = Cdecl<void*>(addr::SC4_GetService913BEE)) {
        for (const std::size_t offset : {field::kString80, field::kString74, field::kString7C, field::kString78})
            Virtual<uint8_t>(strings, 0x18, At<void*>(self, offset));
    }
    void* const city = Global<void*>(addr::SC4_City);
    Assign(self, field::kVehicles, Virtual<void*>(city, 0xAC));
    Assign(self, field::kPedestrians, Virtual<void*>(Global<void*>(addr::SC4_City), 0xB0));
    Assign(self, field::kAircraft, Virtual<void*>(Global<void*>(addr::SC4_City), 0xB4));
    Assign(self, field::kWatercraft, Virtual<void*>(Global<void*>(addr::SC4_City), 0xB8));
    Drop(self, field::kDriven);
    void* const renderer = Global<void*>(addr::SC4_Lighting43DD0);
    void* marker = Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x4C});
    if (marker)
        marker = This<void*>(addr::SC4UDriveMarker_Construct, marker);
    Assign(self, field::kMarker, marker);
    This<void>(addr::SC4UDriveMarker_Init, At<void*>(self, field::kMarker));
    Virtual<void>(renderer, 0x80, At<void*>(self, field::kMarker), uint32_t{5}, uint32_t{0x3E8});
    if (void* const dispatch = Global<void*>(addr::SC4_DispatchService))
        Assign(self, field::kOverlays, Virtual<void*>(dispatch, 0x58));
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        Virtual<uint8_t>(server, 0x14, self + field::kTarget, kMsgView);
        for (std::size_t i = 0; i < kCommandCount; ++i)
            Virtual<uint8_t>(server, 0x14, self + field::kTarget, CommandId(i));
    }
    if (void* const overlays = At<void*>(self, field::kOverlays))
        This<void>(addr::SC4OverlayManager_F46D3A0, overlays, uint32_t{4}, uint8_t{0});
    Assign(self, field::kNetworks, Global<void*>(addr::SC4_Service43D54));
    Assign(self, field::kSimulator, Global<void*>(addr::SC4_SimulatorService));
    Assign(self, field::kScripts, Virtual<void*>(Global<void*>(addr::SC4_City), 0xC0));
    if (At<void*>(self, base::kView)) {
        uint32_t key[3] = {0xA2E3D533, 0x6A231EAA, 0x0C0EAAD0};
        void* resource = nullptr;
        if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, uint32_t{0x42E3EA4B}, &resource,
                             uint32_t{0}, uint32_t{0})) {
            void* const old = Virtual<void*>(Global<void*>(addr::SC4_Service43DC4), 0x7C);
            At<void*>(self, field::kOldWindow) = old;
            if (old)
                AddRef(old);
            void** const window = &At<void*>(self, field::kPanelWindow);
            if (Virtual<uint8_t>(Cdecl<void*>(addr::RZGetCOM), 0xC, uint32_t{0x42E967BE}, uint32_t{0xE2C1B3C4}, window))
                Virtual<uint8_t>(resource, 0xC, *window);
            Virtual<uint32_t>(At<void*>(self, base::kView), 0x80, *window);
        }
        if (resource)
            Release(resource);
    }
    void* commands = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0xEB903A32}, uint32_t{0xCB90253A}, &commands);
    Virtual<uint8_t>(commands, 0x1C, addr::cSC4UDriveControl_PanelCommands, uint32_t{kCommandCount});
    This<void>(addr::SC4RefPtr_Destruct562DD0, &commands);
    return 1;
}

// The game's 0x00563970 (slot 4): Shutdown. When inited: the inited byte
// cleared first (so the base's Shutdown at the end finds nothing to let go
// of: the view, the window manager and the cursor stay referenced, as in the
// original), then everything Init took, in this order. True always.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    At<uint8_t>(self, base::kInited) = 0;
    void* commands = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0xEB903A32}, uint32_t{0xCB90253A}, &commands);
    Virtual<uint8_t>(commands, 0x28, addr::cSC4UDriveControl_PanelCommands, uint32_t{kCommandCount});
    if (void* const view = At<void*>(self, base::kView)) {
        Virtual<uint32_t>(view, 0x80, At<void*>(self, field::kOldWindow));
        if (void* const window = At<void*>(self, field::kPanelWindow)) {
            Release(window);
            At<void*>(self, field::kPanelWindow) = nullptr;
        }
        if (void* const old = At<void*>(self, field::kOldWindow)) {
            Release(old);
            At<void*>(self, field::kOldWindow) = nullptr;
        }
    }
    if (void* const overlays = At<void*>(self, field::kOverlays)) {
        uint8_t shown = 1;
        if (void* const city = Global<void*>(addr::SC4_City)) {
            if (void* const situations = Virtual<void*>(city, 0xC4))
                shown = Virtual<uint8_t>(situations, 0x78);
        }
        This<void>(addr::SC4OverlayManager_F46D3A0, overlays, uint32_t{4}, shown);
    }
    void** const head = At<void**>(self, field::kList);
    for (void** node = static_cast<void**>(head[0]); node != At<void**>(self, field::kList);
         node = static_cast<void**>(node[0])) {
        if (node[2])
            Release(node[2]);
    }
    for (void** node = static_cast<void**>(At<void**>(self, field::kList)[0]); node != At<void**>(self, field::kList);) {
        void** const next = static_cast<void**>(node[0]);
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
        node = next;
    }
    At<void**>(self, field::kList)[0] = At<void*>(self, field::kList);
    At<void**>(self, field::kList)[1] = At<void*>(self, field::kList);
    F562FD0(self, nullptr);
    if (At<void*>(self, field::kRef60)) {
        Drop(self, field::kRef60);
        Virtual<uint32_t>(At<void*>(self, base::kView), 0xA4, kPointerText);
    }
    if (void* const simulator = At<void*>(self, field::kSimulator)) {
        if (Virtual<uint8_t>(simulator, 0x3C))
            Virtual<uint8_t>(At<void*>(self, field::kSimulator), 0x30);
    }
    for (const std::size_t offset : {field::kVehicles, field::kPedestrians, field::kAircraft, field::kWatercraft}) {
        if (void* const manager = At<void*>(self, offset))
            Virtual<void>(manager, 0x28);
    }
    CloseWindows(self, nullptr);
    Virtual<uint32_t>(At<void*>(self, base::kView), 0xDC, uint32_t{0});
    Drop(self, field::kSimulator);
    Drop(self, field::kNetworks);
    Virtual<uint8_t>(Sound(), 0x18, uint32_t{0});
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        Virtual<uint8_t>(server, 0x18, self + field::kTarget, kMsgView);
        for (std::size_t i = 0; i < kCommandCount; ++i)
            Virtual<uint8_t>(server, 0x18, self + field::kTarget, CommandId(i));
    }
    if (void* const marker = At<void*>(self, field::kMarker)) {
        Virtual<uint32_t>(Global<void*>(addr::SC4_Lighting43DD0), 0x84, marker);
        This<void>(addr::SC4UDriveMarker_Shutdown, At<void*>(self, field::kMarker));
        Drop(self, field::kMarker);
    }
    if (void* const strings = Cdecl<void*>(addr::SC4_GetService913BEE)) {
        for (const std::size_t offset : {field::kString80, field::kString74, field::kString7C, field::kString78})
            Virtual<uint8_t>(strings, 0x1C, At<void*>(self, offset));
    }
    for (const std::size_t offset : {field::kString74, field::kString80, field::kString78, field::kString7C}) {
        Release(At<void*>(self, offset));
        At<void*>(self, offset) = nullptr;
    }
    Virtual<uint32_t>(At<void*>(self, base::kView), 0xA4, kPointerText);
    for (const std::size_t offset : {field::kAutomatonFilter, field::kNetworkFilter, field::kTerrain}) {
        if (void* const held = At<void*>(self, offset)) {
            Release(held);
            At<void*>(self, offset) = nullptr;
        }
    }
    Virtual<uint8_t>(Cdecl<void*>(addr::SC4_GetService86AD10EC), 0x18, self + field::kTarget, uint32_t{0xFF});
    for (const std::size_t offset : {field::kVehicles, field::kPedestrians, field::kAircraft, field::kWatercraft,
                                     field::kDriven, field::kOverlays, field::kScripts})
        Drop(self, offset);
    Cdecl<void>(addr::SC4_ReleaseUDriveControl);
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    This<void>(addr::SC4RefPtr_Destruct562DD0, &commands);
    return 1;
}

// --- Registration ----------------------------------------------------------------------

bool InstallUDrive2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[12] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4UDriveControl_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4UDriveControl_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4UDriveControl_CloseWindows, reinterpret_cast<void*>(&CloseWindows)},
        {addr::cSC4UDriveControl_StopAndPause, reinterpret_cast<void*>(&StopAndPause)},
        {addr::cSC4UDriveControl_AddListed, reinterpret_cast<void*>(&AddListed)},
        {addr::cSC4UDriveControl_F562FD0, reinterpret_cast<void*>(&F562FD0)},
        {addr::cSC4UDriveControl_OnAutomatonGone, reinterpret_cast<void*>(&OnAutomatonGone)},
        {addr::cSC4UDriveControl_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::cSC4UDriveControl_UpdatePanel, reinterpret_cast<void*>(&UpdatePanel)},
        {addr::cSC4UDriveControl_OnPanelCommand, reinterpret_cast<void*>(&OnPanelCommand)},
        {addr::cSC4UDriveControl_FilterTest, reinterpret_cast<void*>(&FilterTest)},
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
    "ui.tools.udrive2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4UDriveControl_Init,
    "Our U-Drive-It control's Init, Shutdown, messages and panel (docs/tools.md)",
    false,
    InstallUDrive2,
});

} // namespace
} // namespace opensc4::tool_udrive_2
