// cSC4MyStreetSim (class 0x0BF606A5), the Microphone Tool's Sim: a
// cSC4MySim (object 0x2C8 bytes, the base's 0x2B8 plus a small interface at
// +0x2B8) that follows a car or a pedestrian chosen with the tool and
// publishes what is around it to its script. Replacements, off by default:
//
//   simulators.mysim_agents.street: the object (factory, constructor,
//     deleting destructor, QueryInterface), the agent, advisor and
//     serializable parts (Init, the three advisor slots, Write, Read,
//     GetGZCLSID), Stop, MakeTool, the location overrides of the cISC4MySim
//     part and UpdateGlobals, the trip facts for the followed automaton.
//
// The methods take the part their vtable gives them as `self`: the object's
// start for the agent slots and the constructor, the +8 part for Init, the
// +0xC part for Write and Read, the cISC4MySim part (+0xB8) for the
// location overrides, the +0x2B8 interface for Stop and MakeTool. Follow
// (0x0043E0E0), DoMessage and the tool belong to the neighbouring file and
// stay the game's here.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::mysim_street {
namespace {

namespace field { // from the object's start
constexpr std::size_t kState = 0xC0;         // 0, or 4 to 6 while following
constexpr std::size_t kAutomaton = 0xF8;     // the followed automaton (counted)
constexpr std::size_t kOverlayKey = 0x12C; // the key given to the overlay manager
constexpr std::size_t kMarker = 0x150;       // the overlay marker handle, -1 for none
constexpr std::size_t kScriptTable = 0x164;
constexpr std::size_t kScriptContext = 0x1F0;
constexpr std::size_t kFollowing = 0x210;    // byte
constexpr std::size_t kSquare = 0x240;       // diameter of the look-around square in cells
[[maybe_unused]] constexpr std::size_t kFrameWatch = 0x280; // the stopwatch that paces the frame message
constexpr std::size_t kTravel = 0x2BC;       // 9 none, 0 foot, 1 car, 2 bus, 3 train, 7, 8
constexpr std::size_t kLandmarkName = 0x2C0; // an empty counted string
constexpr std::size_t kStreetPart = 0x2B8;   // the interface of Follow, Stop and MakeTool
} // namespace field

constexpr std::size_t kObject = 0xB8; // the cISC4MySim part

using ThisFn = void(__thiscall*)(void* self);
using ThinkFn = void(__thiscall*)(void* mysim, uint32_t flag);
using AssignFn = void(__thiscall*)(void* holder, void* from);

struct ScriptString { // SC4String, 0x14 bytes, referenced
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

struct Place { // SC4MySimPlace, 0x28 bytes
    void* object;
    float x;
    float z;
    int32_t cellX;
    int32_t cellZ;
    uint32_t rest[5];
};
static_assert(sizeof(Place) == 0x28);

void Release(void* object)
{
    if (object)
        Virtual<uint32_t>(object, 0x8);
}

// The script's variable setters, the game's helper 0x0043D1F0 (name in
// ECX, script in ESI, the value as an int on the stack): select the
// variable, set a number, pop (-3). The script is read from the object each
// time, as the helper's callers do.
void SetNumber(char* self, std::uintptr_t name, double value)
{
    void* const script = At<void*>(self, field::kScriptContext);
    Virtual<void>(script, 0x84, reinterpret_cast<const char*>(name));
    Virtual<void>(script, 0x7C, value);
    Virtual<void>(script, 0xB8, -3);
}

// The game's 0x00425B80: select the variable, hold the object (it is
// counted) and set it with the city's type for it, pop (-3).
void SetObject(char* self, std::uintptr_t name, void* object)
{
    void* const script = At<void*>(self, field::kScriptContext);
    Virtual<void>(script, 0x84, reinterpret_cast<const char*>(name));
    Virtual<uint32_t>(object, 0x4);
    void* const city = Virtual<void*>(Global<void*>(addr::SC4_City), 0x144);
    const uint32_t kind = Virtual<uint32_t>(city, 0x58, uint32_t{1});
    Virtual<void>(script, 0x114, object, kind);
    Virtual<void>(script, 0xB8, -3);
}

int32_t Truncate(double value)
{
    return static_cast<int32_t>(value);
}

// --- Identity and the object ---------------------------------------------------------------

// The agent part. 0x0BF6069F is the object itself, 0x0BF61276 the interface
// at +0x2B8, everything else the base's.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid == 0x0BF6069F) {
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (iid == 0x0BF61276) {
        *out = self ? self + field::kStreetPart : nullptr;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cSC4MySim_QueryInterface)(self, iid,
                                                                                                            out);
}

uint32_t __fastcall GetGZCLSID(char*, void*)
{
    return 0x0BF606A5;
}

// The advisor part (`advisor` = the object + 8): the base's Init, then the
// script context if the script table reference is still 0.
uint8_t __fastcall Init(char* advisor, void*, void* argument)
{
    if (!reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::cSC4MySim_Init)(advisor, argument))
        return 0;
    if (!At<int32_t>(advisor, 0x15C))
        reinterpret_cast<ThisFn>(addr::cSC4MySim_LoadLuaContext)(advisor - 8);
    return 1;
}

// The serializable part (+0xC): the base's record, and the stream's slot
// 0x48 inverted as the result.
uint32_t __fastcall Write(char* ser, void*, void* out)
{
    reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::cSC4MySim_Write)(ser, out);
    return Virtual<uint8_t>(out, 0x48) ? 0 : 1;
}

uint32_t __fastcall Read(char* ser, void*, void* in)
{
    reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::cSC4MySim_Read)(ser, in);
    return Virtual<uint8_t>(in, 0x48) ? 0 : 1;
}

// Agent slots 6, 7 and 8 (the advisor system's turns): only while following.
void __fastcall Update(char* self, void*)
{
    if (!At<void*>(self, field::kAutomaton))
        return;
    Virtual<void>(self + kObject, 0x110); // ActivateLuaContext
    reinterpret_cast<ThinkFn>(addr::cSC4MySim_Think)(self, 0);
}

uint32_t __fastcall ProcessEvents(char* self, void*)
{
    if (!At<void*>(self, field::kAutomaton))
        return 0;
    Virtual<void>(self + kObject, 0x110); // ActivateLuaContext
    return reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::cSC4MySim_ProcessAdviceEvents)(self);
}

void __fastcall MonthPassed(char* self, void*)
{
    if (At<void*>(self, field::kAutomaton))
        reinterpret_cast<ThisFn>(addr::cSC4MySim_MonthPassed)(self);
}

// Stop (the +0x2B8 interface's slot 4): lets go of the automaton (its
// callback, the flag it was given, our count), takes the marker off the
// overlay manager and clears the state.
uint8_t __fastcall Stop(char* street, void*)
{
    char* const object = street - field::kStreetPart;
    if (At<void*>(object, field::kAutomaton)) {
        void* automatonInterface = nullptr;
        Virtual<uint8_t>(At<void*>(object, field::kAutomaton), 0x0, 0x8B99EE7Cu, &automatonInterface);
        void* const target = reinterpret_cast<std::uintptr_t>(object) ? object + 0x10 : nullptr;
        Virtual<void>(At<void*>(object, field::kAutomaton), 0xA0, target); // RemoveCallback
        if (void* const held = At<void*>(object, field::kAutomaton)) {
            At<void*>(object, field::kAutomaton) = nullptr;
            Virtual<uint32_t>(held, 0x8);
        }
        At<int32_t>(object, field::kState) = 0;
        Release(automatonInterface);
    }
    if (At<int32_t>(object, field::kMarker) != -1) {
        void* const overlays = Virtual<void*>(Global<void*>(addr::SC4_DispatchService), 0x58);
        reinterpret_cast<void(__thiscall*)(void*, int32_t, uint32_t)>(addr::SC4OverlayManager_Remove)(
            overlays, At<int32_t>(object, field::kMarker), 0);
        At<int32_t>(object, field::kMarker) = -1;
    }
    At<int32_t>(object, field::kState) = 0;
    At<uint8_t>(object, field::kFollowing) = 0;
    return 1;
}

// HideLocation of the cISC4MySim part (`self` = the object + 0xB8): stops
// following, but keeps the automaton, so that ShowLocation can follow it
// again; if the view's tool is the Microphone Tool (ID 0xCC23D564) it is
// put away.
void __fastcall HideLocation(char* self, void*)
{
    void* const automaton = At<void*>(self, 0x40);
    if (!automaton)
        return;
    char* const street = self + 0x200; // the object + 0x2B8
    Virtual<uint32_t>(automaton, 0x4);
    Virtual<void>(street, 0x10); // Stop
    void* const tool = Virtual<void*>(Global<void*>(addr::SC4_Service43DC4), 0x60);
    if (tool) {
        Virtual<uint32_t>(tool, 0x4);
        if (Virtual<uint32_t>(tool, 0x14) == 0xCC23D564)
            Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x68, uint32_t{0});
    }
    // Stop cleared the field; it is set again (the original's inlined smart
    // pointer assignment; the address compare in front of it never holds).
    void* const held = At<void*>(self, 0x40);
    if (automaton != held) {
        Virtual<uint32_t>(automaton, 0x4);
        At<void*>(self, 0x40) = automaton;
        Release(held);
    }
    Release(tool);
    Virtual<uint32_t>(automaton, 0x8);
}

// ShowLocation of the cISC4MySim part: follows the stored automaton again.
void __fastcall ShowLocation(char* self, void*)
{
    void* const automaton = At<void*>(self, 0x40);
    if (!automaton)
        return;
    Virtual<uint32_t>(automaton, 0x4);
    Virtual<void>(self + 0x200, 0xC, automaton); // Follow
    Virtual<uint32_t>(automaton, 0x8);
}

// MakeTool (the interface's slot 5): a new Microphone Tool (0x30 bytes)
// given the street sim and made the view's tool (kind 2).
uint8_t __fastcall MakeTool(char* street, void*)
{
    char* const object = street - field::kStreetPart;
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x30);
    void* const tool = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4MicrophoneTool_SC4MicrophoneTool)(memory)
                              : nullptr;
    if (tool)
        Virtual<uint32_t>(tool, 0x4);
    if (!reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::SC4MicrophoneTool_SetStreetSim)(tool, object)) {
        Release(tool);
        return 0;
    }
    Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x64, tool, uint32_t{2});
    Release(tool);
    return 1;
}

char* __fastcall DeletingDestructor(char* self, void*, uint8_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::cSC4MyStreetSim_Vtable;
    At<std::uintptr_t>(self, 0x8) = addr::cSC4MyStreetSim_AdvisorVtable;
    At<std::uintptr_t>(self, 0xC) = addr::cSC4MyStreetSim_SerializableVtable;
    At<std::uintptr_t>(self, 0x10) = addr::cSC4MyStreetSim_MessageVtable;
    At<std::uintptr_t>(self, 0xB8) = addr::cSC4MyStreetSim_MySimVtable;
    At<std::uintptr_t>(self, field::kStreetPart) = addr::cSC4MyStreetSim_StreetVtable;
    Release(At<void*>(self, field::kLandmarkName));
    reinterpret_cast<ThisFn>(addr::cSC4MySim_Destruct)(self);
    if (flags & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(self);
    return self;
}

// The base constructor (its random generator is made there, under its
// own site), the vtables, the overlay key and an empty counted string for
// the landmark name. The original stores a temporary vtable at +0x2B8
// between the base constructor and the final stores; nothing can see it.
char* __fastcall Construct(char* self, void*)
{
    reinterpret_cast<char*(__thiscall*)(void*)>(addr::cSC4MySim_cSC4MySim)(self);
    At<std::uintptr_t>(self, 0x0) = addr::cSC4MyStreetSim_Vtable;
    At<std::uintptr_t>(self, 0x8) = addr::cSC4MyStreetSim_AdvisorVtable;
    At<std::uintptr_t>(self, 0xC) = addr::cSC4MyStreetSim_SerializableVtable;
    At<std::uintptr_t>(self, 0x10) = addr::cSC4MyStreetSim_MessageVtable;
    At<std::uintptr_t>(self, 0xB8) = addr::cSC4MyStreetSim_MySimVtable;
    At<std::uintptr_t>(self, field::kStreetPart) = addr::cSC4MyStreetSim_StreetVtable;
    At<void*>(self, field::kLandmarkName) = nullptr;
    At<uint32_t>(self, field::kOverlayKey) = 0x0BFA331E;
    At<int32_t>(self, field::kMarker) = -1;
    auto* const text = static_cast<ScriptString*>(
        reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(sizeof(ScriptString)));
    if (text) {
        text->begin = nullptr;
        text->end = nullptr;
        text->capacity = nullptr;
        char* const buffer = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8));
        text->begin = buffer;
        text->end = buffer;
        text->capacity = buffer + 8;
        *buffer = 0;
        text->vtable = addr::SC4String_Vtable;
        text->references = 0;
    }
    void* const old = At<void*>(self, field::kLandmarkName);
    if (text != old) {
        if (text)
            Virtual<uint32_t>(text, 0x4);
        At<void*>(self, field::kLandmarkName) = text;
        Release(old);
    }
    return self;
}

// The class factory: the object's interface at +0x2B8.
void* __cdecl Create()
{
    char* const memory = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x2C8));
    if (!memory)
        return nullptr;
    char* const street = reinterpret_cast<char*(__thiscall*)(void*)>(addr::cSC4MyStreetSim_cSC4MyStreetSim)(memory);
    return street ? street + field::kStreetPart : nullptr;
}

// --- UpdateGlobals ---------------------------------------------------------------------------
//
// From the object's start. Publishes to the script what is around the
// followed automaton: the traffic, pollution, crime, mayor rating, EQ and
// HQ in its cell, the buildings in the square around it (distress and
// wealth), a landmark, and the disasters nearby. The same set as the trip
// part of cSC4MySim's OnCommand, but always all of it: no state or change
// tests, the means of travel is the one at +0x2BC, the landmark name is the
// empty string at +0x2C0 when there is no landmark.
void __fastcall UpdateGlobals(char* self, void*)
{
    if (!At<void*>(self, field::kScriptContext))
        return;
    Virtual<void>(At<void*>(self, field::kScriptContext), 0xAC, At<int32_t>(self, field::kScriptTable));

    Place place;
    reinterpret_cast<ThisFn>(addr::SC4MySimPlace_Construct)(&place);
    reinterpret_cast<void(__thiscall*)(Place*, void*)>(addr::SC4MySimPlace_SetOccupant)(
        &place, Virtual<void*>(At<void*>(self, field::kAutomaton), 0x14));

    void* const grades = Global<void*>(addr::SC4_Service43DA4);
    void* const mayor = Global<void*>(addr::SC4_Service43D60);
    void* const crime = Global<void*>(addr::SC4_Service43D70);
    void* const traffic = Global<void*>(addr::SC4_Service43DA8);
    void* const pollution = Global<void*>(addr::SC4_PollutionSimulator);
    const int32_t half = At<int32_t>(self, field::kSquare) / 2;
    const int32_t rect[4] = {place.cellX - half, place.cellZ - half, half + place.cellX, half + place.cellZ};
    if (!traffic || !pollution || !crime) {
        Release(place.object);
        return;
    }

    // A grid's value at the place: its slot 19 maps the position to a cell,
    // the reader (slot 22) reads it.
    const auto cell = [&](void* grid, auto read) {
        int32_t gx = 0;
        int32_t gz = 0;
        Virtual<void>(grid, 0x4C, place.x, place.z, &gx, &gz);
        return read(grid, gx, gz);
    };
    using FloatAtFn = double(__thiscall*)(void*, int32_t, int32_t);

    SetNumber(self, 0x00A84F00, cell(Virtual<void*>(traffic, 0x28), [](void* g, int32_t gx, int32_t gz) {
                  return static_cast<int32_t>(Virtual<uint8_t>(g, 0x58, gx, gz));
              })); // trip_traffic_congestion
    using RatioFn = double(__thiscall*)(void*, int32_t, int32_t, int32_t);
    const double ratio = reinterpret_cast<RatioFn>(At<void**>(traffic, 0)[0x60 / 4])(
        traffic, place.cellX, place.cellZ, At<int32_t>(self, field::kTravel));
    SetNumber(self, 0x00A84EF0, Truncate(ratio)); // trip_time_ratio
    SetNumber(self, 0x00A84EDC, 0);               // trip_wait_length
    const std::uintptr_t pollutionNames[] = {0x00A84EC8, 0x00A84EB0, 0x00A84E98};
    for (uint32_t kind = 0; kind < 3; ++kind)
        SetNumber(self, pollutionNames[kind], cell(Virtual<void*>(pollution, 0x14, kind), [](void* g, int32_t gx, int32_t gz) {
                      return static_cast<int32_t>(Virtual<int16_t>(g, 0x58, gx, gz));
                  })); // trip_pollution_air, _water, _garbage
    SetNumber(self, 0x00A84E8C, cell(Virtual<void*>(crime, 0x14), [](void* g, int32_t gx, int32_t gz) {
                  return static_cast<int32_t>(Virtual<uint8_t>(g, 0x58, gx, gz));
              })); // trip_crime
    SetNumber(self, 0x00A84E78, cell(Virtual<void*>(mayor, 0x30), [](void* g, int32_t gx, int32_t gz) {
                  return static_cast<int32_t>(Virtual<int8_t>(g, 0x58, gx, gz));
              })); // trip_mayor_rating
    void* eq = nullptr;
    Virtual<void>(grades, 0x54, &eq, uint32_t{0}, uint32_t{0});
    SetNumber(self, 0x00A84E70, Truncate(cell(eq, [](void* g, int32_t gx, int32_t gz) {
                  return reinterpret_cast<FloatAtFn>(At<void**>(g, 0)[0x58 / 4])(g, gx, gz);
              }))); // trip_eq
    void* hq = nullptr;
    Virtual<void>(grades, 0x58, &hq, uint32_t{0}, uint32_t{0});
    SetNumber(self, 0x00A84E68, Truncate(cell(hq, [](void* g, int32_t gx, int32_t gz) {
                  return reinterpret_cast<FloatAtFn>(At<void**>(g, 0)[0x58 / 4])(g, gx, gz);
              }))); // trip_hq

    // The buildings in the square: a list (a sentinel node of 12 bytes made
    // here) the finder fills, emptied from its last node.
    void* finder = nullptr;
    Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DB0), 0x0, 0x681905BDu, &finder);
    char* const head = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(0xC));
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    void* list = head;
    Virtual<void>(finder, 0x1C, &list, rect[0], rect[1], rect[2], rect[3], uint32_t{0});
    int32_t count = 0, distress1 = 0, distress2 = 0, distress3 = 0, low = 0, medium = 0, high = 0;
    void* landmark = nullptr;
    while (At<char*>(head, 0) != head) {
        char* const last = At<char*>(head, 4);
        void* const occupant = At<void*>(last, 8);
        if (void* const building = Virtual<void*>(occupant, 0xC0)) {
            ++count;
            const int32_t distress = Virtual<int32_t>(occupant, 0x8C);
            if (distress >= 4)
                ++distress3;
            else if (distress >= 3)
                ++distress2;
            else if (distress >= 2)
                ++distress1;
            const int32_t wealth = Virtual<int32_t>(occupant, 0xA4);
            if (wealth >= 3)
                ++high;
            else if (wealth >= 2)
                ++medium;
            else if (wealth >= 1)
                ++low;
            if (Virtual<uint8_t>(Virtual<void*>(building, 0xC), 0x58, uint32_t{0x150A}) && !landmark)
                reinterpret_cast<AssignFn>(addr::SC4RefPtr_Assign)(&landmark, building);
        }
        char* const node = At<char*>(head, 4);
        char* const previous = At<char*>(node, 4);
        char* const next = At<char*>(node, 0);
        At<char*>(previous, 0) = next;
        At<char*>(next, 4) = previous;
        Release(At<void*>(node, 8));
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(node);
    }
    const double epsilon = Global<double>(addr::SC4MySim_TripShareEpsilon);
    const float share = static_cast<float>(1.0 / (static_cast<double>(count) + epsilon));
    const auto percent = [&](int32_t n) { return Truncate(static_cast<double>(n) * share * 100.0f); };
    SetNumber(self, 0x00A84E48, percent(distress1)); // trip_distress1_p
    SetNumber(self, 0x00A84E34, percent(distress2)); // trip_distress2_p
    SetNumber(self, 0x00A84E20, percent(distress3)); // trip_distress3_p
    SetNumber(self, 0x00A84E0C, percent(low));       // trip_wealth_low_p
    SetNumber(self, 0x00A84DF8, percent(medium));    // trip_wealth_med_p
    SetNumber(self, 0x00A84DE4, percent(high));      // trip_wealth_high_p
    SetNumber(self, 0x00A84DCC, landmark ? 1 : 0);   // trip_landmark_nearby
    int32_t funding = 0;
    if (landmark) {
        void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x14);
        void* const name = memory ? reinterpret_cast<void*(__thiscall*)(void*, void*)>(addr::SC4String_FromIGZString)(
                                        memory, Virtual<void*>(landmark, 0x30))
                                  : nullptr;
        SetObject(self, 0x00A84DB8, name); // trip_landmark_name
        // The property record the game fills on the stack: 0x20 bytes in the
        // original's frame (the float at +0x1C is the last dword; only that
        // one is cleared there, here all of it).
        struct {
            uint32_t words[7];
            float funding;
        } item{};
        static_assert(sizeof(item) == 0x20);
        const uint32_t key = Virtual<uint32_t>(Virtual<void*>(landmark, 0xC), 0x18, 0xAA59670Cu, &item);
        Virtual<void>(Global<void*>(addr::SC4_BudgetSimulator), 0xCC, key);
        funding = Truncate(static_cast<double>(item.funding) * 100.0f);
    } else {
        SetObject(self, 0x00A84DB8, At<void*>(self, field::kLandmarkName)); // trip_landmark_name
    }
    SetNumber(self, 0x00A84DA0, funding); // trip_landmark_funding_p

    void* const disasters = Global<void*>(addr::SC4_Service43D3C);
    const int32_t total = Virtual<int32_t>(disasters, 0x40);
    bool riot = false;
    bool fire = false;
    bool any = false;
    for (int32_t i = 0; i < total; ++i) {
        void* disaster = nullptr;
        int32_t dx = 0;
        int32_t dz = 0;
        Virtual<void>(disasters, 0x44, i, &disaster, &dx, &dz);
        const bool inside = dx >= rect[0] && dz >= rect[1] && dx <= rect[2] && dz <= rect[3];
        if (inside) {
            any = true;
            if (!riot)
                riot = Virtual<uint32_t>(disaster, 0x14) == 0xCA380B06;
            if (!fire)
                fire = Virtual<uint32_t>(disaster, 0x14) == 0x621CDA33;
        }
        const bool done = (inside || any) && fire && riot;
        Release(disaster);
        if (done)
            break;
    }
    SetNumber(self, 0x00A84D8C, fire ? 1 : 0); // trip_fire_nearby
    SetNumber(self, 0x00A84D78, riot ? 1 : 0); // trip_riot_nearby
    SetNumber(self, 0x00A84D5C, any ? 1 : 0);  // trip_any_disaster_nearby
    Virtual<void>(At<void*>(self, field::kScriptContext), 0x2C, uint32_t{1});

    Release(landmark);
    reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4RefList_Clear)(&list);
    if (list)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(list);
    Release(finder);
    Release(hq);
    Release(eq);
    Release(place.object);
}

// --- Registration ----------------------------------------------------------------------------

bool InstallStreet(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MyStreetSim_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4MyStreetSim_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4MyStreetSim_Write, reinterpret_cast<void*>(&Write)},
        {addr::cSC4MyStreetSim_Read, reinterpret_cast<void*>(&Read)},
        {addr::cSC4MyStreetSim_GetGZCLSID, reinterpret_cast<void*>(&GetGZCLSID)},
        {addr::cSC4MyStreetSim_Update, reinterpret_cast<void*>(&Update)},
        {addr::cSC4MyStreetSim_ProcessEvents, reinterpret_cast<void*>(&ProcessEvents)},
        {addr::cSC4MyStreetSim_MonthPassed, reinterpret_cast<void*>(&MonthPassed)},
        {addr::cSC4MyStreetSim_Stop, reinterpret_cast<void*>(&Stop)},
        {addr::cSC4MyStreetSim_HideLocation, reinterpret_cast<void*>(&HideLocation)},
        {addr::cSC4MyStreetSim_ShowLocation, reinterpret_cast<void*>(&ShowLocation)},
        {addr::cSC4MyStreetSim_MakeTool, reinterpret_cast<void*>(&MakeTool)},
        {addr::cSC4MyStreetSim_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
        {addr::cSC4MyStreetSim_cSC4MyStreetSim, reinterpret_cast<void*>(&Construct)},
        {addr::cSC4MyStreetSim_UpdateGlobals, reinterpret_cast<void*>(&UpdateGlobals)},
        {addr::cSC4MyStreetSim_Create, reinterpret_cast<void*>(&Create)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_streetRegistered = registry::Register({
    "simulators.mysim_agents.street",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MyStreetSim_UpdateGlobals,
    "Our cSC4MyStreetSim object, agent slots, Stop, MakeTool, location overrides and UpdateGlobals "
    "(docs/mysim-agents.md)",
    false,
    InstallStreet,
});

} // namespace
} // namespace opensc4::mysim_street
