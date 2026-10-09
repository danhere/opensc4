// cSC4MySim (class 0x4A1DBBBF; docs/mysim-agents.md): the player's own Sim
// in a city. Replacements, each off by default:
//
//   simulators.mysim_agents.interface: the cISC4MySim methods (the interface
//     at +0xB8) that are long enough to hook;
//   simulators.mysim_agents.save: Write and Read (the cIGZSerializable part
//     at +0xC);
//   simulators.mysim_agents.messages: DoMessage (the message target at
//     +0x10);
//   simulators.mysim_agents.update: the update (agent slot 6) and Think;
//   simulators.mysim_agents.events: the cISC4MySim methods for changes in
//     the city (home, job, buildings, fires, disasters) and the others of
//     moderate size;
//   simulators.mysim_agents.residence: MoveIn, FindNewResidence and
//     SetResidence.
//
// The methods take the cISC4MySim part as `this` (here `self`); offsets
// below are from it. The object starts at self - 0xB8.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/sim_timers.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstdint>

namespace opensc4::mysim_agents {
namespace {

namespace field { // from the cISC4MySim part
constexpr std::size_t kState = 0x8;          // 0 not moved in; 1-5 at home, at work, travelling
constexpr std::size_t kResidence = 0xC;
constexpr std::size_t kJob = 0x28;
constexpr std::size_t kJobPending = 0x2C;
constexpr std::size_t kVehicleKey = 0x48;    // type, group, instance
constexpr std::size_t kTitleSet = 0x54;      // 1-3: which job titles
constexpr std::size_t kMale = 0x5B;
constexpr std::size_t kResidenceView = 0x8C; // shown with HideResidence
constexpr std::size_t kResidenceShown = 0x90;
constexpr std::size_t kSchoolView = 0xA0;    // byte: the job is school
constexpr std::size_t kNeighborLost = 0xA8;
constexpr std::size_t kLuaArgument = 0xAC;
constexpr std::size_t kHomeName = 0xB0;
constexpr std::size_t kJobName = 0xB4;
constexpr std::size_t kJobTitle = 0xB8;
constexpr std::size_t kEducation = 0xC4;     // float
constexpr std::size_t kHealth = 0xC8;        // float
constexpr std::size_t kDistances = 0x130;    // map: building type -> distance
constexpr std::size_t kLua = 0x138;
constexpr std::size_t kNoName = 0x13C;
constexpr std::size_t kNoJobName = 0x140;
constexpr std::size_t kSchoolName = 0x144;
constexpr std::size_t kCarButton = 0x14C;
constexpr std::size_t kPedestrianButton = 0x150;
constexpr std::size_t kDispatch = 0x154;
constexpr std::size_t kLocationShown = 0x158;
constexpr std::size_t kBalloonTexts = 0x15C; // 12 bytes each, the ID first
constexpr std::size_t kBalloons = 0x174;     // list of indices
constexpr std::size_t kEventBalloons = 0x178;
constexpr std::size_t kPylon = 0x190;
constexpr std::size_t kSoundTime = 0x1E4;
constexpr std::size_t kSoundWatch = 0x1E8;   // GZStopwatch
} // namespace field

constexpr std::size_t kObject = 0xB8; // the object's start, below the cISC4MySim part

// Object fields the methods use (from the object's start).
constexpr std::size_t kTripTimeObject = 0x150;
constexpr std::size_t kMaleObject = 0x113;
constexpr std::size_t kDispatchObject = 0x20C;

using FindFn = void(__thiscall*)(void* map, void** node, const uint32_t* key);
using ThisFn = void(__thiscall*)(void* self);
using ThisBoolFn = uint8_t(__thiscall*)(void* self);
using ThisIdFn = void(__thiscall*)(void* self, uint32_t id);
using RectFn = int32_t*(__thiscall*)(void* service, int32_t index);
using LoadTextFn = void(__cdecl*)(void* text, uint32_t group, uint32_t instance, uint32_t zero);
using FormatFn = void(__cdecl*)(void* text, const char* format, ...);
using ElapsedFn = uint32_t(__thiscall*)(void* stopwatch);

const auto LoadText = reinterpret_cast<LoadTextFn>(addr::SC4_LoadLocalizedText);

char* Object(char* self)
{
    return self - kObject;
}

// --- cISC4MySim -------------------------------------------------------------------------

void* __fastcall AsAdvisor(char* self, void*)
{
    return reinterpret_cast<std::uintptr_t>(self) != kObject ? self - 0xB0 : nullptr;
}

void __fastcall GetVehicleKey(char* self, void*, uint32_t* key)
{
    key[0] = At<uint32_t>(self, field::kVehicleKey);
    key[1] = At<uint32_t>(self, field::kVehicleKey + 4);
    key[2] = At<uint32_t>(self, field::kVehicleKey + 8);
}

void* __fastcall GetHomeName(char* self, void*)
{
    void* const name = At<void*>(self, field::kHomeName);
    return name ? name : At<void*>(self, field::kNoName);
}

// The job's name or title (`at`): the school's while at school, "no job"
// without one.
void* JobText(char* self, std::size_t at)
{
    if (At<uint8_t>(self, field::kSchoolView))
        return At<void*>(self, field::kSchoolName);
    if (!At<void*>(self, field::kJob) && !At<void*>(self, field::kJobPending))
        return At<void*>(self, field::kNoJobName);
    void* const text = At<void*>(self, at);
    return text ? text : At<void*>(self, field::kNoName);
}

void* __fastcall GetJobName(char* self, void*)
{
    return JobText(self, field::kJobName);
}

void* __fastcall GetJobTitle(char* self, void*)
{
    return JobText(self, field::kJobTitle);
}

// The title for a job at `occupant`, by the wealth level its slot 11 gives
// (5-8 one title, 2 another, else a third) and the MySim's title set.
void __fastcall GetJobTitleOf(char* self, void*, void* occupant, void* text)
{
    if (!occupant)
        return;
    const int32_t level = *Virtual<int32_t*>(occupant, 0x2C);
    const bool high = level >= 5 && level <= 8;
    const uint32_t notTwo = level != 2 ? 1 : 0;
    uint32_t id;
    switch (At<int32_t>(self, field::kTitleSet)) {
    case 1:
        id = high ? 0x8A538A5Eu + (level == 5 ? 1 : 0) : 0x8A538A60u + notTwo;
        break;
    case 2:
        id = high ? 0x8A538A62u : 0x8A538A63u + notTwo;
        break;
    case 3:
        id = high ? 0x8A538A65u : 0x8A538A66u + notTwo;
        break;
    default:
        return;
    }
    LoadText(text, 0xEA231E96, id, 0);
}

uint8_t __fastcall GetEQAsText(char* self, void*, void* text)
{
    const auto format = reinterpret_cast<FormatFn>(At<void**>(text, 0)[0x6C / 4]);
    format(text, "%d", static_cast<int32_t>(At<float>(self, field::kEducation)));
    return 1;
}

uint8_t __fastcall GetHQAsText(char* self, void*, void* text)
{
    if (!At<int32_t>(self, field::kState))
        return 0;
    const float health = At<float>(self, field::kHealth);
    if (health < 50.0f)
        LoadText(text, 0xEA5524EB, 0x0A5FE2DF, 0);
    else if (health < 100.0f)
        LoadText(text, 0x6A231EAA, 0xAA317340, 0);
    else
        LoadText(text, 0xEA5524EB, 0x6A5FE294, 0);
    return 1;
}

void __fastcall HideResidence(char* self, void*)
{
    if (void* const view = At<void*>(self, field::kResidenceView)) {
        void* const target = Virtual<void*>(view, 0xC);
        Virtual<void>(target, 0x118);
    }
    At<uint8_t>(self, field::kResidenceShown) = 0;
}

void __fastcall HideLocation(char* self, void*)
{
    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_HideLocation)(At<void*>(self, field::kDispatch));
    reinterpret_cast<ThisFn>(addr::cSC4MySim_ClearBalloons)(Object(self));
    At<uint8_t>(self, field::kLocationShown) = 0;
}

void __fastcall SetPylonVisibility(char* self, void*, uint32_t visible)
{
    if (void* const pylon = At<void*>(self, field::kPylon)) {
        void* const target = Virtual<void*>(pylon, 0xC);
        Virtual<void>(target, 0x48, visible, uint32_t{1});
    }
}

// A trip to work (state 4) or home (5) starts if CanTravel allows; its
// time comes from the dispatch object.
void StartTrip(char* self, int32_t state)
{
    char* const object = Object(self);
    At<int32_t>(self, field::kState) = state;
    At<int32_t>(object, kTripTimeObject) = At<int32_t>(At<void*>(object, kDispatchObject), 0x3C);
}

void __fastcall LeaveForWork(char* self, void*)
{
    const int32_t state = At<int32_t>(self, field::kState);
    if (state != 2 && state != 1 && state != 5)
        return;
    char* const object = Object(self);
    if (reinterpret_cast<ThisBoolFn>(addr::cSC4MySim_CanTravel)(object)) {
        StartTrip(self, 4);
        return;
    }
    reinterpret_cast<ThisIdFn>(addr::cSC4MySim_ShowAdvice)(object,
                                                           At<uint8_t>(object, kMaleObject) ? 0x2A5F9CDCu : 0x6A5FB141u);
}

void __fastcall ReturnHome(char* self, void*)
{
    const int32_t state = At<int32_t>(self, field::kState);
    if (state != 3 && state != 4)
        return;
    if (reinterpret_cast<ThisBoolFn>(addr::cSC4MySim_CanTravel)(Object(self)))
        StartTrip(self, 5);
}

void __fastcall DoBuildingChanged(char* self, void*, void* occupant)
{
    if (At<int32_t>(self, field::kState) &&
        (occupant == At<void*>(self, field::kResidence) || occupant == At<void*>(self, field::kJob)))
        reinterpret_cast<ThisFn>(addr::cSC4MySim_UpdateHomeAndJob)(Object(self));
}

void __fastcall DoNeighborConnectionRemoved(char* self, void*, int32_t, int32_t)
{
    if (At<int32_t>(self, field::kState) && At<void*>(self, field::kJobPending))
        At<uint8_t>(self, field::kNeighborLost) = 1;
}

int32_t __fastcall DistanceToClosest(char* self, void*, uint32_t type)
{
    void* const map = At<void*>(self, field::kDistances);
    if (!At<int32_t>(self, field::kState) || !map)
        return -1;
    void* node = nullptr;
    reinterpret_cast<FindFn>(addr::SC4Uint32Map_Find)(map, &node, &type);
    if (node == *static_cast<void**>(map))
        return -1;
    return At<int32_t>(node, 0x14);
}

void __fastcall PlayNightSound(char* self, void*)
{
    if (At<int32_t>(self, field::kState) != 2)
        return;
    reinterpret_cast<ThisIdFn>(addr::cSC4MySim_ShowAdvice)(Object(self),
                                                           At<uint8_t>(self, field::kMale) ? 0x6A5FC4F2u : 0x4A5FC52Au);
}

uint8_t __fastcall GetCurrentScreenLocationXY(char* self, void*, int32_t* x, int32_t* y)
{
    const int32_t index = Virtual<int32_t>(Object(self), 0x40);
    if (index == -1)
        return 0;
    void* const map = Virtual<void*>(Global<void*>(addr::SC4_DispatchService), 0x58);
    if (!map)
        return 0;
    const int32_t* const rect = reinterpret_cast<RectFn>(addr::SC4_RectByIndex)(map, index);
    if (!rect)
        return 0;
    *x = rect[0] + (rect[2] - rect[0]) / 2;
    *y = rect[1] + 0x19;
    return 1;
}

// A sound at most every 5 ms of the stopwatch (simulation time under
// determinism.sim_timers).
uint8_t __fastcall RequestToPlaySound(char* self, void*)
{
    void* const watch = self + field::kSoundWatch;
    sim_timers::Claim(watch);
    const uint32_t now = reinterpret_cast<ElapsedFn>(addr::GZStopwatch_ElapsedMilliseconds)(watch);
    if (!At<int32_t>(self, field::kState) || now - At<uint32_t>(self, field::kSoundTime) <= 4)
        return 0;
    At<uint32_t>(self, field::kSoundTime) = now;
    return 1;
}

uint32_t __fastcall GetModelButtonBitmapID(char* self, void*, uint8_t pedestrian)
{
    return At<uint32_t>(self, pedestrian ? field::kCarButton : field::kPedestrianButton);
}

void __fastcall GetNextBalloonAdvice(char* self, void*)
{
    reinterpret_cast<ThisIdFn>(addr::cSC4MySim_NextBalloon)(Object(self), 2);
}

void __fastcall GetNextBalloonEventAdvice(char* self, void*)
{
    reinterpret_cast<ThisIdFn>(addr::cSC4MySim_NextBalloon)(Object(self), 1);
}

// The text ID of the first balloon in a list, or 0.
uint32_t FirstBalloon(char* self, std::size_t list)
{
    char* const head = At<char*>(self, list);
    char* const first = At<char*>(head, 0);
    if (first == head)
        return 0;
    const int32_t index = At<int32_t>(first, 8);
    return At<uint32_t>(At<char*>(self, field::kBalloonTexts), static_cast<std::size_t>(index) * 12);
}

uint32_t __fastcall GetCurrentBalloonAdvice(char* self, void*)
{
    return FirstBalloon(self, field::kBalloons);
}

uint32_t __fastcall GetCurrentBalloonEventAdvice(char* self, void*)
{
    return FirstBalloon(self, field::kEventBalloons);
}

uint8_t __fastcall CanUDriveIt(char* self, void*)
{
    if (!At<int32_t>(self, field::kState))
        return 0;
    return Virtual<uint8_t>(At<void*>(self, field::kDispatch), 0x18);
}

uint8_t __fastcall EnterUDriveIt(char* self, void*)
{
    if (!Virtual<uint8_t>(self, 0x100)) // CanUDriveIt
        return 0;
    return Virtual<uint8_t>(At<void*>(self, field::kDispatch), 0x1C);
}

uint8_t __fastcall ExitUDriveIt(char* self, void*)
{
    if (!At<int32_t>(self, field::kState))
        return 0;
    return Virtual<uint8_t>(At<void*>(self, field::kDispatch), 0x20);
}

uint8_t __fastcall ActivateLuaContext(char* self, void*)
{
    void* const lua = At<void*>(self, field::kLua);
    if (!lua)
        return 0;
    if (!At<int32_t>(self, field::kLuaArgument))
        reinterpret_cast<ThisFn>(addr::cSC4MySim_LoadLuaContext)(Object(self));
    const int32_t argument = At<int32_t>(self, field::kLuaArgument);
    if (argument <= 0)
        return 0;
    Virtual<void>(At<void*>(self, field::kLua), 0xAC, argument);
    Virtual<void>(At<void*>(self, field::kLua), 0xB4, reinterpret_cast<const void*>(0x00A85078));
    return 1;
}

// --- Write and Read ----------------------------------------------------------------------
//
// From the cIGZSerializable part (`ser`, the object's +0xC): +0xB4 is the
// state (the cISC4MySim part's +0x8); nothing but the state is saved for a
// MySim that has not moved in. Version 0x11 is written; Read takes 0xA and
// later (older ones lack the keys, the dispatch object and what follows).

using WriteReferenceFn = void(__cdecl*)(void* stream, void* object);
using ReadReferenceFn = void(__cdecl*)(void* stream, uint32_t iid, void* slot, uint32_t zero);
using KeyFn = void(__cdecl*)(void* stream, void* key);
using WriteBoolFn = void(__cdecl*)(void* stream, uint32_t value);
using ReadBoolFn = void(__cdecl*)(void* stream, void* value);
using StreamFn = uint8_t(__thiscall*)(void* self, void* stream);

void Drop(char* ser, std::size_t at)
{
    if (void* const object = At<void*>(ser, at)) {
        At<void*>(ser, at) = nullptr;
        Virtual<uint32_t>(object, 0x8);
    }
}

uint8_t __fastcall Write(char* ser, void*, void* out)
{
    const auto writeReference = reinterpret_cast<WriteReferenceFn>(addr::SC4Stream_WriteReference);
    const auto writeKey = reinterpret_cast<KeyFn>(addr::SC4Stream_WriteResourceKey);
    const auto writeBool = reinterpret_cast<WriteBoolFn>(addr::SC4Stream_WriteBool);
    const auto u8 = [&](std::size_t at) { Virtual<uint8_t>(out, 0x14, uint32_t{At<uint8_t>(ser, at)}); };
    const auto u32 = [&](std::size_t at) { Virtual<uint8_t>(out, 0x24, At<uint32_t>(ser, at)); };
    const auto s32 = [&](std::size_t at) { Virtual<uint8_t>(out, 0x20, At<int32_t>(ser, at)); };
    const auto f32 = [&](std::size_t at) { Virtual<uint8_t>(out, 0x30, At<float>(ser, at)); };
    const auto text = [&](std::size_t at) { Virtual<uint8_t>(out, 0x3C, static_cast<void*>(ser + at)); };
    const auto boolean = [&](std::size_t at) { writeBool(out, At<uint8_t>(ser, at)); };

    Virtual<uint8_t>(out, 0x1C, uint32_t{0x11});
    const uint32_t state = At<uint32_t>(ser, 0xB4);
    Virtual<uint8_t>(out, 0x14, state);
    if (state) {
        u8(0xB0);
        writeReference(out, At<void*>(ser, 0xB8));
        u32(0xBC);
        u32(0xC0);
        writeReference(out, At<void*>(ser, 0xD4));
        s32(0xDC);
        s32(0xE0);
        s32(0xE4);
        s32(0xE8);
        u32(0xF0);
        writeKey(out, ser + 0xF4);
        u8(0x100);
        Virtual<uint8_t>(out, 0x1C, uint32_t{At<uint16_t>(ser, 0x104)});
        u8(0x106);
        boolean(0x107);
        text(0x108);
        u8(0x11C);
        u8(0x11D);
        u32(0x120);
        text(0x124);
        u32(0x148);
        boolean(0x14C);
        u32(0x150);
        for (std::size_t at = 0xC4; at < 0xD0; at += 4)
            f32(at);
        u8(0x1CC);
        f32(0x1D0);
        for (std::size_t at = 0x19C; at < 0x1B4; at += 4)
            f32(at);
        u8(0x1CD);
        f32(0x1D4);
        for (std::size_t at = 0x1B4; at < 0x1CC; at += 4)
            f32(at);
        writeKey(out, ser + 0x240);
        writeKey(out, ser + 0x24C);
        s32(0x1F8);
        s32(0x1FC);
        writeReference(out, At<void*>(ser, 0x200));
        reinterpret_cast<KeyFn>(addr::SC4Stream_WriteStopwatch)(out, ser + 0x25C);
        void* const target = At<void*>(ser, 0x23C);
        const bool hasTarget = target && !Virtual<uint8_t>(target, 0x40);
        writeBool(out, hasTarget ? 1 : 0);
        if (hasTarget)
            writeReference(out, At<void*>(ser, 0x23C));
        boolean(0x239);
        s32(0x28C);
    }
    return reinterpret_cast<StreamFn>(addr::SC4AgentBase_Write)(ser, out);
}

uint8_t __fastcall Read(char* ser, void*, void* in)
{
    const auto readReference = reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference);
    const auto readKey = reinterpret_cast<KeyFn>(addr::SC4Stream_ReadResourceKey);
    const auto readBool = reinterpret_cast<ReadBoolFn>(addr::SC4Stream_ReadBool);
    const auto get = [&](std::size_t slot, std::size_t at) { Virtual<uint8_t>(in, slot, static_cast<void*>(ser + at)); };

    uint16_t version = 0;
    Virtual<uint8_t>(in, 0x1C, &version);
    uint8_t state = 0;
    Virtual<uint8_t>(in, 0x14, &state);
    At<uint32_t>(ser, 0xB4) = state;
    if (state) {
        get(0x14, 0xB0);
        Drop(ser, 0xB8);
        readReference(in, 0x87DDA3A9, ser + 0xB8, 0);
        get(0x24, 0xBC);
        get(0x24, 0xC0);
        Drop(ser, 0xD4);
        readReference(in, 0x87DDA3A9, ser + 0xD4, 0);
        for (std::size_t at = 0xDC; at <= 0xE8; at += 4)
            get(0x20, at);
        get(0x24, 0xF0);
        readKey(in, ser + 0xF4);
        uint8_t value = 0;
        Virtual<uint8_t>(in, 0x14, &value);
        At<uint32_t>(ser, 0x100) = value;
        get(0x1C, 0x104);
        get(0x14, 0x106);
        readBool(in, ser + 0x107);
        get(0x3C, 0x108);
        get(0x14, 0x11C);
        get(0x14, 0x11D);
        get(0x24, 0x120);
        get(0x3C, 0x124);
        get(0x24, 0x148);
        readBool(in, ser + 0x14C);
        get(0x24, 0x150);
        for (std::size_t at = 0xC4; at < 0xD0; at += 4)
            get(0x30, at);
        get(0x14, 0x1CC);
        get(0x30, 0x1D0);
        for (std::size_t at = 0x19C; at < 0x1B4; at += 4)
            get(0x30, at);
        get(0x14, 0x1CD);
        get(0x30, 0x1D4);
        for (std::size_t at = 0x1B4; at < 0x1CC; at += 4)
            get(0x30, at);
        if (version >= 0xB) {
            if (version < 0xF) {
                int32_t unused = 0;
                for (int i = 0; i < 5; ++i)
                    Virtual<uint8_t>(in, 0x20, &unused);
            } else {
                readKey(in, ser + 0x240);
                readKey(in, ser + 0x24C);
            }
        }
        if (version >= 0x11) {
            get(0x20, 0x1F8);
            get(0x20, 0x1FC);
        }
        if (version >= 0xC) {
            if (version >= 0xD) {
                if (void* const dispatch = At<void*>(ser, 0x200)) {
                    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Shutdown)(dispatch);
                    Drop(ser, 0x200);
                }
                readReference(in, 0xEB8B6F9A, ser + 0x200, 0);
            } else {
                Virtual<uint8_t>(At<char*>(ser, 0x200) + 0xC, 0x10, in); // its own Read
            }
            reinterpret_cast<KeyFn>(addr::SC4Stream_ReadStopwatch)(in, ser + 0x25C);
            uint8_t hasTarget = 0;
            readBool(in, &hasTarget);
            if (hasTarget) {
                Drop(ser, 0x23C);
                readReference(in, 0xC9B6D7D6, ser + 0x23C, 0);
            }
        } else {
            Drop(ser, 0x200);
        }
        if (version >= 0xE)
            readBool(in, ser + 0x239);
        if (version >= 0x10)
            get(0x20, 0x28C);
    }
    return reinterpret_cast<StreamFn>(addr::SC4AgentBase_Read)(ser, in);
}

// --- Messages ---------------------------------------------------------------------------
//
// From the message target part (`target`, the object's +0x10): the
// cISC4MySim part is at +0xA8, the advisor part at -0x8, the state at +0xB0
// and the dispatch object at +0x1FC. A place (SC4MySimPlace, 0x28 bytes)
// starts with a referenced object; the dispatch object holds the current
// one at +0xAC, home at +0x84 and work at +0xD4.

struct Place {
    void* object;
    uint32_t words[9];
};
static_assert(sizeof(Place) == 0x28);

using PlaceCopyFn = void(__thiscall*)(Place* self, const void* from);
using SameFn = uint8_t(__thiscall*)(const void* self, const void* other);
using AtPlaceFn = uint8_t(__thiscall*)(void* mysim, const void* place);
using CommandFn = void(__thiscall*)(void* mysim, int32_t a, int32_t b, int32_t c, int32_t d, int32_t e);
using ThinkFn = void(__thiscall*)(void* mysim, uint32_t flag);
using PostFn = void(__cdecl*)(uint32_t type, uint32_t a, uint32_t b, uint32_t c, uint32_t d, void* data);
using AssignFn = void(__thiscall*)(void* holder, void* from);
using HasFlagFn = uint8_t(__thiscall*)(void* dispatch, uint32_t flag);
using MessageFn = uint8_t(__thiscall*)(void* self, void* message);
using ConstructFn = void*(__thiscall*)(void* memory, void* mysim);
using CancelFn = void(__cdecl*)();

constexpr uint32_t kBalloonMessage = 0x4BC42C48;

void* Iface(char* target)
{
    return reinterpret_cast<std::uintptr_t>(target) != 0x10 ? target + 0xA8 : nullptr;
}

void Post(char* target)
{
    reinterpret_cast<PostFn>(addr::SC4_PostMessage)(kBalloonMessage, 0, 0, 0, 0, Iface(target));
}

void Restart(char* target, std::size_t at)
{
    sim_timers::Claim(target + at);
    reinterpret_cast<ThisFn>(addr::GZStopwatch_Start)(target + at);
}

// The balloon target (+0x238), when the MySim is at its place: restart the
// balloon stopwatch, then (if +0x235 and the current place matches) tell
// the target `what` (its slot 11); `post` sends the balloon message first.
void TellBalloonTarget(char* target, uint32_t what, bool post)
{
    if (!At<void*>(target, 0x238))
        return;
    Restart(target, 0x258);
    if (!At<uint8_t>(target, 0x235))
        return;
    char* const dispatch = At<char*>(target, 0x1FC);
    if (!reinterpret_cast<AtPlaceFn>(addr::cSC4MySim_AtBalloonPlace)(target - 0x10, dispatch + 0xAC))
        return;
    if (post)
        Post(target);
    Virtual<void>(At<void*>(target, 0x238), 0x2C, what);
}

void Release(void* object)
{
    if (object)
        Virtual<uint32_t>(object, 0x8);
}

// 0x2BA06B99 with 0xEBA06BF4: the work place changed. No occupant there:
// the occupant at its cell (service 0x00B43C94); else the lot.
void OnWorkPlace(char* target)
{
    char* const mysim = target - 0x10;
    Place work;
    reinterpret_cast<PlaceCopyFn>(addr::SC4MySimPlace_Copy)(&work, At<char*>(target, 0x1FC) + 0xD4);
    if (!work.object) {
        void* const cells = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x8C);
        const uint32_t x = work.words[4]; // +0x14
        const uint32_t z = work.words[5]; // +0x18
        void* const occupant = Virtual<void*>(cells, 0x2C, x, z);
        if (occupant)
            reinterpret_cast<AssignFn>(addr::SC4RefPtr_AssignFrom)(target + 0xD4, occupant);
        if (!At<void*>(target, 0xD4))
            return;
        At<uint32_t>(target, 0xE4) = work.words[3]; // +0x10
        At<uint32_t>(target, 0xD8) = x;
        At<uint32_t>(target, 0xDC) = z;
        At<uint32_t>(target, 0xE0) = work.words[2]; // +0xC
        At<uint32_t>(target, 0xEC) = 0;
        reinterpret_cast<ThisFn>(addr::cSC4MySim_UpdateHomeAndJob)(mysim);
        Virtual<void>(target - 0x8, 0x48, 0xAA3EDBB1u, uint32_t{0});
        if (void* const old = At<void*>(target, 0xD0)) {
            At<void*>(target, 0xD0) = nullptr;
            Release(old);
        }
        return;
    }
    const uint32_t x = work.words[2]; // +0xC
    const uint32_t z = work.words[3]; // +0x10
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, uint32_t{0});
    reinterpret_cast<AssignFn>(addr::SC4RefPtr_Assign)(target + 0xD0, Virtual<void*>(lot, 0xC0));
    if (At<void*>(target, 0xD0)) {
        At<uint32_t>(target, 0xE0) = x;
        At<uint32_t>(target, 0xE4) = z;
        At<uint32_t>(target, 0xEC) = 0;
        if (void* const old = At<void*>(target, 0xD4)) {
            At<void*>(target, 0xD4) = nullptr;
            Release(old);
        }
        reinterpret_cast<ThisFn>(addr::cSC4MySim_UpdateHomeAndJob)(mysim);
        Virtual<void>(target - 0x8, 0x48, 0xAA3EDBB1u, uint32_t{0});
    }
    Release(work.object);
}

// 0x2BA06B99 with 0x4BB71A7E: where the MySim is now decides its state
// (home: 5 unless 1; work: 4; else 6).
void OnPlaceChanged(char* target)
{
    char* const dispatch = At<char*>(target, 0x1FC);
    Place here;
    reinterpret_cast<PlaceCopyFn>(addr::SC4MySimPlace_Copy)(&here, dispatch + 0xAC);
    const auto same = reinterpret_cast<SameFn>(addr::SC4MySimPlace_Same);
    if (same(&here, dispatch + 0x84)) {
        if (At<int32_t>(target, 0xB0) != 1)
            At<int32_t>(target, 0xB0) = 5;
    } else {
        At<int32_t>(target, 0xB0) = same(&here, dispatch + 0xD4) ? 4 : 6;
    }
    Post(target);
    Release(here.object);
}

// 0x2BA06B99 with 0xEBA06BD4: arrived (home: 2, announcing the move-in if
// it was 1; work: 3).
void OnArrived(char* target)
{
    char* const dispatch = At<char*>(target, 0x1FC);
    const auto same = reinterpret_cast<SameFn>(addr::SC4MySimPlace_Same);
    if (same(dispatch + 0x5C, dispatch + 0x84)) {
        if (At<int32_t>(target, 0xB0) == 1) {
            At<int32_t>(target, 0xB0) = 2;
            Virtual<void>(target + 0xA8, 0xA0); // AnnounceMoveIn
        }
        At<int32_t>(target, 0xB0) = 2;
    } else if (same(dispatch + 0x5C, dispatch + 0xD4)) {
        At<int32_t>(target, 0xB0) = 3;
    }
    TellBalloonTarget(target, 4, true);
}

// 0x2BA06B99 with 0xEBA06C12.
void OnStopped(char* target)
{
    TellBalloonTarget(target, 1, false);
    char* const dispatch = At<char*>(target, 0x1FC);
    if (At<uint8_t>(dispatch, 0xAC + 0x1D)) {
        reinterpret_cast<ThisFn>(addr::cSC4MySim_OnStranded)(target - 0x10);
        return;
    }
    if (reinterpret_cast<HasFlagFn>(addr::cSC4MySimDispatch_HasFlag)(dispatch, 0x80) && At<int32_t>(target, 0xB0) == 1) {
        At<int32_t>(target, 0xB0) = 2;
        Virtual<void>(target + 0xA8, 0xA0); // AnnounceMoveIn
    }
}

void OnDispatch(char* target, void* message)
{
    char* const mysim = target - 0x10;
    const auto command = reinterpret_cast<CommandFn>(addr::cSC4MySim_OnCommand);
    switch (Virtual<uint32_t>(message, 0x24)) {
    case 0x2BAC398F:
        TellBalloonTarget(target, 1, true);
        break;
    case 0x4BB2CFC0: {
        const int32_t data2 = Virtual<int32_t>(message, 0x38);
        const int32_t data3 = Virtual<int32_t>(message, 0x4C);
        command(mysim, data3, data2, -1, -1, 0);
        break;
    }
    case 0x4BB2D020: {
        const int32_t data2 = Virtual<int32_t>(message, 0x38);
        const int32_t data3 = Virtual<int32_t>(message, 0x4C);
        command(mysim, -1, -1, data3, data2, 0);
        break;
    }
    case 0x4BB71A7E:
        OnPlaceChanged(target);
        break;
    case 0xEBA06BD0:
        reinterpret_cast<ThisFn>(addr::cSC4MySim_OnScriptEvent)(mysim);
        break;
    case 0xEBA06BD4:
        OnArrived(target);
        break;
    case 0xEBA06BF4:
        OnWorkPlace(target);
        break;
    case 0xEBA06C12:
        OnStopped(target);
        break;
    default:
        break;
    }
}

uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const mysim = target - 0x10;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 0x0BB04126: {
        if (!Virtual<uint8_t>(target + 0xA8, 0x50)) // IsActive
            break;
        void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x90);
        void* const helper =
            memory ? reinterpret_cast<ConstructFn>(addr::SC4MySimHelper90_Construct)(memory, mysim) : nullptr;
        if (Virtual<uint8_t>(helper, 0xC))
            Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x64, helper, uint32_t{2});
        break;
    }
    case 0x0B6F3E27:
        Virtual<uint32_t>(message, 0x24);
        break;
    case 0x26D31EC1:
        Virtual<void>(target + 0xA8, 0x84); // HideResidence
        Virtual<void>(target + 0xA8, 0x8C); // HideLocation
        Virtual<void>(target + 0xA8, 0x90, uint32_t{0}); // SetPylonVisibility
        break;
    case 0x2BA06B99:
        OnDispatch(target, message);
        break;
    case 0x4B6F3EBF:
    case 0xAB6F3E2D: {
        const uint32_t slot = Virtual<uint32_t>(message, 0x24);
        if (At<uint32_t>(target, 0x50) == slot || slot == 0)
            reinterpret_cast<CancelFn>(addr::cSC4MySim_CancelMoveIn)();
        break;
    }
    case 0x86AD10EE: { // every frame
        if (!At<int32_t>(target, 0xB0))
            break;
        void* const simulator = Global<void*>(addr::SC4_SimulatorService);
        if (!simulator || !(Virtual<uint8_t>(simulator, 0x3C) || Virtual<uint8_t>(simulator, 0x40) ||
                            Virtual<uint8_t>(simulator, 0x44)))
            break;
        void* const watch = target + 0x270;
        sim_timers::Claim(watch);
        if (reinterpret_cast<ElapsedFn>(addr::GZStopwatch_ElapsedMilliseconds)(watch) <= 2)
            break;
        reinterpret_cast<ThinkFn>(addr::cSC4MySim_Think)(mysim, 1);
        reinterpret_cast<ThisFn>(addr::GZStopwatch_Start)(watch);
        break;
    }
    case kBalloonMessage:
        if (Virtual<void*>(message, 0x80) == Iface(target)) {
            reinterpret_cast<ThisFn>(addr::cSC4MySim_UpdateHomeAndJob)(mysim);
            reinterpret_cast<ThinkFn>(addr::cSC4MySim_Think)(mysim, 1);
        }
        break;
    default:
        return reinterpret_cast<MessageFn>(addr::SC4AgentBase_DoMessage)(target, message);
    }
    return 1;
}

// --- Update and Think -------------------------------------------------------------------
//
// From the object's start. The update runs when the advisor system gives
// the MySim its turn; Think steps through the balloon advice entries
// (+0x214, 12 bytes each: the advice, the time it may show next) from a
// cursor (+0x234).

using ServiceFn = void(__thiscall*)(void* framework, uint32_t clsid, uint32_t iid, void** out);

void __fastcall Think(char* mysim, void*, uint8_t all)
{
    if (!At<uint8_t>(mysim, 0x210))
        return;
    const int32_t count =
        static_cast<int32_t>((At<char*>(mysim, 0x218) - At<char*>(mysim, 0x214)) / 12);
    if (!count)
        return;
    int32_t steps = static_cast<int32_t>(static_cast<double>(count) * static_cast<double>(0.142857149f) + 1.0);
    if (all)
        steps = count;
    Virtual<void>(mysim + 0xB8, 0x110); // ActivateLuaContext
    for (; steps; --steps) {
        int32_t& cursor = At<int32_t>(mysim, 0x234);
        if (cursor >= count)
            cursor = 0;
        char* const entry = At<char*>(mysim, 0x214) + cursor * 12;
        void* const advice = At<void*>(entry, 0);
        void* const data = Virtual<void*>(advice, 0x10);
        if (!Virtual<uint8_t>(advice, 0x38, uint32_t{1}) && !Virtual<uint8_t>(data, 0x3C, uint32_t{0x40}) &&
            (At<int32_t>(entry, 4) <= At<int32_t>(mysim, 0x5C) || all)) {
            if (Virtual<uint8_t>(advice, 0x30))
                reinterpret_cast<void(__thiscall*)(void*, uint32_t, int32_t)>(addr::cSC4MySim_QueueBalloon)(
                    mysim, 2, cursor);
            else
                At<int32_t>(entry, 4) = Virtual<int32_t>(Virtual<void*>(advice, 0x10), 0x40) + At<int32_t>(mysim, 0x5C);
        }
        ++cursor;
    }
}

void __fastcall Update(char* mysim, void*)
{
    if (!At<int32_t>(mysim, 0xC0))
        return;
    void* service = nullptr;
    void* const framework = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetFramework)();
    if (framework)
        Virtual<void>(framework, 0x14, 0xA417445Eu, 0x5A4u, &service);
    const bool paused = Virtual<uint8_t>(service, 0xA8);
    Release(service);
    if (paused)
        return;
    if (!At<int32_t>(mysim, 0xE0) && !At<int32_t>(mysim, 0xE4) && !At<uint8_t>(mysim, 0x158) &&
        At<int32_t>(mysim, 0xC0) == 2 && !reinterpret_cast<ThisBoolFn>(addr::cSC4MySim_IsBusy)(mysim))
        ++At<int32_t>(mysim, 0xFC);
    if (At<uint8_t>(mysim, 0x160)) {
        reinterpret_cast<ThisFn>(addr::cSC4MySim_RefreshLocation)(mysim);
        At<uint8_t>(mysim, 0x160) = 0;
    }
    Virtual<void>(mysim + 0xB8, 0x110); // ActivateLuaContext
    reinterpret_cast<ThinkFn>(addr::cSC4MySim_Think)(mysim, 0);
    reinterpret_cast<ThisFn>(addr::SC4AgentBase_StepItems)(mysim);
    const uint32_t now = At<uint32_t>(mysim, 0x5C);
    if (static_cast<int32_t>(now - At<uint32_t>(mysim, 0x264)) > 0) {
        At<uint32_t>(mysim, 0x264) = now;
        reinterpret_cast<CommandFn>(addr::cSC4MySim_OnCommand)(mysim, -1, -1, -1, -1, 0);
    }
    if (!At<void*>(mysim, 0x248))
        return;
    void* const watch = mysim + 0x268;
    if (!reinterpret_cast<ThisBoolFn>(addr::GZStopwatch_IsRunning)(watch))
        return;
    sim_timers::Claim(watch);
    if (!Virtual<uint8_t>(At<void*>(mysim, 0x248), 0x40)) {
        if (reinterpret_cast<ElapsedFn>(addr::GZStopwatch_ElapsedMilliseconds)(watch) <= 5)
            return;
        reinterpret_cast<ThisFn>(addr::cSC4MySim_OnBalloonTimeout)(mysim);
    }
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Stop)(watch);
}

// --- Events -----------------------------------------------------------------------------
//
// cISC4MySim methods (`self` is the +0xB8 part). Advice goes to the advisor
// part (self - 0xB0, its slot 18: advice ID, data); its data is an object
// the COM makes (class 0x6A26D049, IID 0x6A26D050).

using CreateFn = uint8_t(__thiscall*)(void* com, uint32_t clsid, uint32_t iid, void** out);

void Advise(char* self, uint32_t id, void* data)
{
    Virtual<void>(self - 0xB0, 0x48, id, data);
}

// The advice data object, or null.
void* NewAdviceData()
{
    void* data = nullptr;
    void* const com = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
    if (!Virtual<uint8_t>(com, 0xC, 0x6A26D049u, 0x6A26D050u, &data))
        return nullptr;
    return data;
}

int32_t Truncate(double value)
{
    return static_cast<int32_t>(value);
}

uint8_t __fastcall ChangeResidence(char* self, void*, void* occupant)
{
    void* const old = At<void*>(self, field::kResidence);
    if (old)
        Virtual<uint32_t>(old, 0x4);
    if (!Virtual<uint8_t>(self, 0x1C, occupant, uint32_t{0}, uint32_t{0})) { // TestForResidence
        Release(old);
        return 0;
    }
    char* const object = Object(self);
    reinterpret_cast<ThisFn>(addr::cSC4MySim_ResetTrip)(object);
    Virtual<void>(self, 0x34, occupant); // SetResidence
    if (Virtual<uint8_t>(self - 0xB0, 0x4C, 0x6A47E79Bu)) {
        reinterpret_cast<ThisFn>(addr::cSC4MySim_ResetTrip)(object);
        Virtual<void>(self, 0x34, old);
    }
    if (At<uint8_t>(self, field::kResidenceShown))
        Virtual<void>(self, 0x80); // ShowResidence
    At<int32_t>(object, kTripTimeObject) = At<int32_t>(At<void*>(object, kDispatchObject), 0x3C);
    if (At<void*>(self, field::kResidence) == old) {
        Release(old);
        return 0;
    }
    if (void* const residence = At<void*>(self, 0x24))
        At<int32_t>(self, field::kTitleSet) = Virtual<int32_t>(residence, 0xA4);
    At<int32_t>(self, field::kState) = 1;
    Release(old);
    return 1;
}

uint8_t __fastcall TestForResidence(char* self, void*, void* occupant, uint8_t test, uint8_t sameWealth)
{
    void* const mysims = Virtual<void*>(Global<void*>(addr::SC4_City), 0x11C);
    if (occupant == At<void*>(self, field::kResidence))
        return 0;
    void* const lots = Global<void*>(addr::SC4_LotManager);
    const uint32_t id = Virtual<uint32_t>(occupant, 0xC);
    void* const lot = Virtual<void*>(lots, 0x38, id);
    if (sameWealth && lot && Virtual<int32_t>(lot, 0xA4) != At<int32_t>(self, field::kTitleSet))
        return 0;
    if (Virtual<int32_t>(mysims, 0x24, occupant)) // MySimLivesHere
        return 0;
    if (!test)
        return 1;
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x10);
    void* const tester = memory ? reinterpret_cast<ConstructFn>(addr::SC4MySimHelper10_Construct)(memory,
                                                                                                    reinterpret_cast<void*>(1))
                                : nullptr;
    if (tester)
        Virtual<uint32_t>(tester, 0x4);
    const uint32_t occupantId = Virtual<uint32_t>(occupant, 0xC);
    const uint8_t result = Virtual<uint8_t>(tester, 0xC, occupantId);
    Release(tester);
    return result;
}

uint8_t __fastcall ChangeJob(char* self, void*, void* occupant)
{
    Place place;
    reinterpret_cast<ThisFn>(addr::SC4MySimPlace_Construct)(&place);
    reinterpret_cast<void(__thiscall*)(Place*, uint32_t)>(addr::SC4MySimPlace_SetOccupant)(
        &place, Virtual<uint32_t>(occupant, 0xC));
    reinterpret_cast<uint8_t*>(&place)[0x1D] = 1;
    const uint8_t result = reinterpret_cast<uint8_t(__thiscall*)(void*, Place*, uint32_t)>(
        addr::cSC4MySimDispatch_SetDestination)(At<void*>(self, field::kDispatch), &place, 1);
    Release(place.object);
    return result;
}

// A message of `type` to the message server.
void Send(uint32_t type, void (*fill)(void* message, char* self), char* self)
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory)
                                 : nullptr;
    if (message)
        Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, type);
    if (fill)
        fill(message, self);
    Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0xC, message);
    Virtual<uint32_t>(message, 0x8);
}

uint8_t __fastcall DeleteFromGame(char* self, void*)
{
    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Shutdown)(At<void*>(self, field::kDispatch));
    char* const object = Object(self);
    if (void* const pylon = At<void*>(self, field::kPylon)) {
        Virtual<void>(pylon, 0x14);
        if (void* const balloon = At<void*>(object, 0x248)) {
            At<void*>(object, 0x248) = nullptr;
            Release(balloon);
        }
        At<uint8_t>(object, 0x245) = 0;
    }
    for (std::size_t at = 0x194; at <= 0x1A8; at += 4)
        At<uint32_t>(self, at) = 0;
    reinterpret_cast<ThisFn>(addr::cSC4MySim_ResetTrip)(object);
    At<int32_t>(self, field::kState) = 0;
    Virtual<void>(self, 0x8C); // HideLocation
    reinterpret_cast<ThisFn>(addr::cSC4MySim_Unregister)(object);
    Send(0xAA25827C, nullptr, self);
    return 1;
}

uint8_t __fastcall GetLocation(char* self, void*, float* out)
{
    char* const dispatch = At<char*>(self, field::kDispatch);
    if (!dispatch)
        return 0;
    struct {
        void* object;
        float x;
        float z;
        int32_t cellX;
        int32_t cellZ;
        uint32_t rest[5];
    } place;
    static_assert(sizeof(place) == 0x28);
    reinterpret_cast<void(__thiscall*)(void*, const void*)>(addr::SC4MySimPlace_Copy)(&place, dispatch + 0x5C);
    if (place.cellX < 0 || place.cellZ < 0) {
        Release(place.object);
        return 0;
    }
    out[0] = place.x;
    reinterpret_cast<uint32_t*>(out)[1] = 0;
    out[2] = place.z;
    if (void* const terrain = Global<void*>(addr::SC4_Terrain))
        out[1] = static_cast<float>(
            reinterpret_cast<double(__thiscall*)(void*, float, float)>(At<void**>(terrain, 0)[0x34 / 4])(terrain, place.x,
                                                                                                        place.z));
    Release(place.object);
    return 1;
}

void FillMoveIn(void* message, char* self)
{
    void* const residence = At<void*>(self, field::kResidence);
    Virtual<void>(message, 0x84, Virtual<void*>(residence, 0xC));
    Virtual<void>(message, 0x2C, uint32_t{At<uint8_t>(self, field::kMale)});
    Virtual<void>(message, 0x40, At<uint32_t>(self, field::kTitleSet));
}

void __fastcall AnnounceMoveIn(char* self, void*)
{
    if (Virtual<uint8_t>(self, 0xE0)) // RequestToPlaySound
        Send(0xEA3ADA33, &FillMoveIn, self);
    reinterpret_cast<ThisFn>(addr::cSC4MySim_UpdateHomeAndJob)(Object(self));
    Advise(self, 0xCA3E87A8, nullptr);
}

void __fastcall DoScriptCommand(char* self, void*, uint32_t command)
{
    if (!At<int32_t>(self, field::kState))
        return;
    char* const object = Object(self);
    switch (command) {
    case 0x0A57DE1F:
        reinterpret_cast<ThisFn>(addr::cSC4MySim_Script0A57DE1F)(object);
        break;
    case 0xEA4D269C:
        reinterpret_cast<ThisFn>(addr::cSC4MySim_ScriptEA4D269C)(object);
        break;
    case 0xAA4934B4:
        reinterpret_cast<ThisFn>(addr::cSC4MySim_ScriptAA4934B4)(object);
        break;
    case 0x6A47E79B:
        Virtual<uint8_t>(self, 0x14); // FindNewResidence
        break;
    case 0x6A495F7F:
        reinterpret_cast<ThisFn>(addr::cSC4MySim_Script6A495F7F)(object);
        break;
    case 0x6A4A5E9E:
        reinterpret_cast<ThisFn>(addr::cSC4MySim_Script6A4A5E9E)(object);
        break;
    case 0x6A5E6860:
        reinterpret_cast<ThisFn>(addr::cSC4MySim_FindNewJob)(object);
        break;
    default:
        break;
    }
}

void __fastcall DoBuildingAdded(char* self, void*, void* occupant)
{
    if (!At<int32_t>(self, field::kState) || !At<void*>(self, field::kResidence))
        return;
    void* const data = NewAdviceData();
    if (!data)
        return;
    const uint32_t value = reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t)>(addr::cSC4MySim_DescribeOccupant)(
        Object(self), Virtual<uint32_t>(occupant, 0xC));
    Virtual<void>(data, 0x1C, occupant);
    Virtual<void>(data, 0x28, value);
    Advise(self, 0xCA4E8986, data);
    Release(data);
}

void __fastcall DoBuildingRemoved(char* self, void*, void* occupant, uint32_t replacement)
{
    if (!At<int32_t>(self, field::kState))
        return;
    char* const object = Object(self);
    void* const residence = At<void*>(self, field::kResidence);
    if (residence && residence == occupant) {
        if (replacement) {
            At<uint32_t>(self, 0xA4) = replacement;
            reinterpret_cast<ThisFn>(addr::cSC4MySim_UpdateHomeAndJob)(object);
            Advise(self, 0xAA4D2D64, nullptr);
            reinterpret_cast<ThisFn>(addr::cSC4MySim_ResetTrip)(object);
        } else {
            Advise(self, 0xAA48F5C1, nullptr);
            Virtual<uint8_t>(self, 0x14); // FindNewResidence
        }
        return;
    }
    void* const job = At<void*>(self, field::kJob);
    if (!job || job != occupant)
        return;
    if (replacement) {
        At<uint32_t>(self, 0xA4) = replacement;
        reinterpret_cast<ThisFn>(addr::cSC4MySim_UpdateHomeAndJob)(object);
        Advise(self, 0xAA4D2D65, nullptr);
    } else {
        Advise(self, 0xCA48F879, nullptr);
    }
    reinterpret_cast<ThisFn>(addr::cSC4MySim_FindNewJob)(object);
}

// Fire, pipe burst, tornado: advice with the distance from the MySim.
void AdviseNear(char* self, float x, float z, uint32_t id)
{
    if (!At<int32_t>(self, field::kState))
        return;
    void* const data = NewAdviceData();
    if (!data)
        return;
    const double dx = static_cast<double>(x) - At<float>(self, 0x18);
    const double dz = static_cast<double>(z) - At<float>(self, 0x20);
    Virtual<void>(data, 0x28, Truncate(std::sqrt(dx * dx + dz * dz)));
    Advise(self, id, data);
    Release(data);
}

void __fastcall DoFireAdded(char* self, void*, float x, float z)
{
    AdviseNear(self, x, z, 0xEA5E4E89);
}

void __fastcall DoPipeBurst(char* self, void*, float x, float z)
{
    AdviseNear(self, x, z, 0x6A5E598C);
}

void __fastcall DoTornado(char* self, void*, float x, float z)
{
    AdviseNear(self, x, z, 0x0A5E8243);
}

void __fastcall DoDisaster(char* self, void*, uint32_t type, int32_t cellX, int32_t cellZ)
{
    if (!At<int32_t>(self, field::kState) || (type != 0xCA380B06 && type != 0xC4C90997))
        return;
    void* const data = NewAdviceData();
    if (!data)
        return;
    Virtual<void>(data, 0xC, static_cast<float>(cellX), static_cast<float>(cellZ));
    float x = 0.0f;
    float z = 0.0f;
    Virtual<void>(Global<void*>(addr::SC4_City), 0x184, cellX, cellZ, &x, &z);
    const float dx = static_cast<float>(static_cast<double>(x) - At<float>(self, 0x18));
    const float dz = static_cast<float>(static_cast<double>(z) - At<float>(self, 0x20));
    Virtual<void>(data, 0x28, Truncate(std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dz) * dz)));
    Advise(self, type == 0xC4C90997 ? 0x2AC14492u : 0x8AC11A9Bu, data);
    Release(data);
}

uint32_t __fastcall DistanceToClosestSpecial(char* self, void*, uint32_t type)
{
    uint32_t best = 0xFFFFFFFF;
    void* const buildings = Virtual<void*>(Virtual<void*>(Global<void*>(addr::SC4_City), 0xD4), 0xB0);
    char* const head = *static_cast<char**>(buildings);
    for (char* node = At<char*>(head, 0); node != *static_cast<char**>(buildings); node = At<char*>(node, 0)) {
        void* const occupant = At<void*>(node, 8);
        void* building = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, 0x87DDA3A9u, &building) && Virtual<uint32_t>(building, 0x10) == type) {
            float position[3] = {};
            Virtual<void>(occupant, 0x20, position);
            const double dx = static_cast<double>(At<float>(self, 0x18)) - position[0];
            const double dz = static_cast<double>(At<float>(self, 0x20)) - position[2];
            const uint32_t distance = static_cast<uint32_t>(Truncate(std::sqrt(dz * dz + dx * dx)));
            if (distance < best)
                best = distance;
        }
        Release(building);
    }
    return best;
}

uint8_t __fastcall CreateDispatchTool(char* self, void*)
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x90);
    void* const tool =
        memory ? reinterpret_cast<ConstructFn>(addr::SC4MySimHelper90_Construct)(memory, Object(self)) : nullptr;
    if (tool)
        Virtual<uint32_t>(tool, 0x4);
    if (!Virtual<uint8_t>(tool, 0xC)) {
        Release(tool);
        return 0;
    }
    Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x64, tool, uint32_t{2});
    void*& current = Global<void*>(addr::SC4_DispatchTool);
    void* const old = current;
    if (tool != old) {
        Virtual<uint32_t>(tool, 0x4);
        current = tool;
        Release(old);
    }
    Release(tool);
    return 1;
}

// --- Moving in --------------------------------------------------------------------------

using AssignRangeFn = void(__thiscall*)(void* string, const char* begin, const char* end, void* allocator);

void __fastcall MoveIn(char* self, void*, uint8_t slot, void* name, uint8_t male, uint8_t sign, void* bio,
                       uint32_t bitmap, void* bitmapPath)
{
    At<uint8_t>(self, 0x4) = slot;
    Virtual<void>(self - 0x80, 0x38, name);
    At<uint8_t>(self, field::kMale) = male;
    At<uint8_t>(self, 0x5A) = sign;
    At<uint32_t>(self, 0x74) = bitmap;
    const auto assign = reinterpret_cast<AssignRangeFn>(addr::SC4String_AssignRange);
    uint32_t allocator = 0;
    if (!bitmap) {
        char* const path = bitmapPath ? static_cast<char*>(bitmapPath) + 4 : nullptr;
        if (path != self + 0x7C)
            assign(self + 0x7C, At<char*>(path, 0), At<char*>(path, 4), &allocator);
    }
    char* const text = bio ? static_cast<char*>(bio) + 4 : nullptr;
    if (text != self + 0x60)
        assign(self + 0x60, At<char*>(text, 0), At<char*>(text, 4), &allocator);
    At<uint16_t>(self, 0x58) = 1;
    At<int8_t>(self, 0x71) = static_cast<int8_t>(At<int32_t>(self - 0x60, 0) % 12);

    // The move-in tool becomes the dispatch tool, replacing one in use.
    void*& current = Global<void*>(addr::SC4_DispatchTool);
    bool drop = true;
    if (void* const tools = Global<void*>(addr::SC4_Service43DC4)) {
        if (!current)
            drop = false;
        else if (Virtual<void*>(tools, 0x60) == current)
            Virtual<void>(tools, 0x68, uint32_t{0});
    }
    if (drop && current) {
        void* const old = current;
        current = nullptr;
        Release(old);
    }
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x4C);
    void* const tool = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4MySimMoveInTool_Construct)(memory)
                              : nullptr;
    if (reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::SC4MySimMoveInTool_Init)(tool, Object(self))) {
        void* const old = current;
        if (tool != old) {
            if (tool)
                Virtual<uint32_t>(tool, 0x4);
            current = tool;
            Release(old);
        }
        Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x64, current, uint32_t{2});
    }
    At<uint32_t>(self, 0x1E0) = 0;
    Virtual<void>(self, 0x88); // ShowLocation
}

// A random house of the occupant manager's (a filter the game defines),
// tried from there round the list until one passes TestForResidence and
// ChangeResidence. Without a home after that, or with the same one, advice
// 0x6A57DD94; with the same one, the home is dropped. Always false.
uint8_t __fastcall FindNewResidence(char* self, void*)
{
    using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t n);
    using ListFn = void(__thiscall*)(void* list);
    void** const head = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    head[0] = head;
    head[1] = head;
    struct {
        void* head;
        uint32_t size;
    } list{head, 0};
    void* const occupants = Virtual<void*>(Global<void*>(addr::SC4_City), 0x88);
    void* const before = At<void*>(self, field::kResidence);
    auto* const filter = static_cast<std::uintptr_t*>(
        reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x10));
    if (filter) {
        reinterpret_cast<ThisFn>(addr::SC4_OccupantFilterBase)(filter);
        filter[0] = addr::SC4MySim_ResidenceFilterVtable;
        filter[1] = addr::SC4MySim_ResidenceFilterVtable4;
        reinterpret_cast<uint8_t*>(filter)[0xC] = 1;
        Virtual<uint32_t>(filter, 0x4);
    }
    if (Virtual<uint8_t>(occupants, 0x98, &list, uint32_t{0}, uint32_t{0}, static_cast<void*>(filter), -1)) {
        uint32_t count = 0;
        for (void** node = static_cast<void**>(head[0]); node != head; node = static_cast<void**>(node[0]))
            ++count;
        uint32_t skip = reinterpret_cast<BelowFn>(addr::SC4Random_BelowLcg)(self + 0xD8, count) - 1;
        void** node = static_cast<void**>(head[0]);
        while (node != head && skip > 0) {
            node = static_cast<void**>(node[0]);
            --skip;
        }
        for (; count; --count, node = static_cast<void**>(node[0])) {
            if (node == head)
                node = static_cast<void**>(head[0]);
            void* building = nullptr;
            if (Virtual<uint8_t>(node[2], 0x0, 0x87DDA3A9u, &building) &&
                Virtual<uint8_t>(self, 0x1C, building, uint32_t{0}, uint32_t{0}) && // TestForResidence
                Virtual<uint8_t>(self, 0x18, building)) {                            // ChangeResidence
                Release(building);
                break;
            }
            Release(building);
        }
        Virtual<void>(occupants, 0xC0, &list);
    }
    void* const residence = At<void*>(self, field::kResidence);
    if (!residence) {
        Advise(self, 0x6A57DD94, nullptr);
    } else if (residence == before) {
        if (void* const old = At<void*>(self, field::kResidence)) {
            At<void*>(self, field::kResidence) = nullptr;
            Release(old);
        }
        if (void* const view = At<void*>(self, field::kResidenceView))
            Virtual<void>(Virtual<void*>(view, 0xC), 0x118);
        if (void* const marker = At<void*>(self, 0x134)) {
            if (Virtual<uint8_t>(Virtual<void*>(marker, 0xC), 0x80, uint32_t{1}))
                if (void* const service = Global<void*>(addr::SC4_Service43D10))
                    Virtual<void>(service, 0x74, Virtual<void*>(At<void*>(self, 0x134), 0xC));
            if (void* const again = At<void*>(self, 0x134)) {
                At<void*>(self, 0x134) = nullptr;
                Release(again);
            }
        }
        if (void* const lot = At<void*>(self, 0x24)) {
            At<void*>(self, 0x24) = nullptr;
            Release(lot);
        }
        At<uint8_t>(self, field::kLocationShown) = 0;
        Advise(self, 0x6A57DD94, nullptr);
    }
    Release(filter);
    reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&list);
    Virtual<void>(ListNodeAllocator(), 0x10, list.head);
    return 0;
}

uint8_t __fastcall SetResidence(char* self, void*, void* occupant)
{
    void* const old = At<void*>(self, field::kResidence);
    if (occupant != old) {
        if (occupant)
            Virtual<uint32_t>(occupant, 0x4);
        At<void*>(self, field::kResidence) = occupant;
        Release(old);
    }
    if (!occupant)
        return 0;
    void* const residence = At<void*>(self, field::kResidence);
    Virtual<void>(Virtual<void*>(residence, 0xC), 0x20, self + 0x18); // its position
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, Virtual<void*>(residence, 0xC));
    void* const oldLot = At<void*>(self, 0x24);
    if (lot != oldLot) {
        if (lot)
            Virtual<uint32_t>(lot, 0x4);
        At<void*>(self, 0x24) = lot;
        Release(oldLot);
    }
    if (!At<void*>(self, 0x24))
        return 0;
    Virtual<void>(At<void*>(self, 0x24), 0x54, self + 0x10, self + 0x14); // its cell
    char* const object = Object(self);
    void* const dispatch = At<void*>(self, field::kDispatch);
    if (!At<int32_t>(self, field::kState)) {
        reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Reset)(dispatch);
        Virtual<void>(self, 0xE4, uint32_t{0}); // SetCar
        Virtual<void>(self, 0xE8, uint32_t{0}); // SetPedestrian
    }
    Place home;
    reinterpret_cast<ThisFn>(addr::SC4MySimPlace_Construct)(&home);
    reinterpret_cast<void(__thiscall*)(Place*, void*)>(addr::SC4MySimPlace_SetOccupant)(&home,
                                                                                         Virtual<void*>(occupant, 0xC));
    reinterpret_cast<void(__thiscall*)(void*, Place*)>(addr::cSC4MySimDispatch_SetHome)(At<void*>(self, field::kDispatch),
                                                                                        &home);
    if (At<int32_t>(self, field::kState)) {
        if (At<void*>(self, field::kResidence))
            At<uint8_t>(self, field::kLocationShown) = reinterpret_cast<HasFlagFn>(addr::cSC4MySimDispatch_HasFlag)(
                At<void*>(self, field::kDispatch), 0x100);
    } else {
        reinterpret_cast<ThisFn>(addr::cSC4MySim_Unregister)(object);
        At<int32_t>(self, field::kState) = 2;
        void* const city = Global<void*>(addr::SC4_Service43DA4);
        const int32_t x = At<int32_t>(self, 0x10);
        const int32_t z = At<int32_t>(self, 0x14);
        const uint8_t age = Virtual<uint8_t>(Virtual<void*>(city, 0xA4), 0x1C, x, z);
        At<uint8_t>(self, 0x70) = age < 0x12 ? 0x12 : age;
        At<int32_t>(self, field::kState) = 1;
        const int32_t wealth = Virtual<int32_t>(At<void*>(self, 0x24), 0xA4);
        At<int32_t>(self, field::kTitleSet) = wealth;
        using GradeFn = double(__thiscall*)(void* service, int32_t x, int32_t z, int32_t wealth);
        At<float>(self, field::kEducation) = static_cast<float>(reinterpret_cast<GradeFn>(At<void**>(city, 0)[0x94 / 4])(
            city, At<int32_t>(self, 0x10), At<int32_t>(self, 0x14), wealth));
        At<float>(self, field::kHealth) = static_cast<float>(
            reinterpret_cast<GradeFn>(At<void**>(city, 0)[0x98 / 4])(city, At<int32_t>(self, 0x10),
                                                                     At<int32_t>(self, 0x14), At<int32_t>(self, 0x54)));
        for (std::size_t at = 0xF0; at <= 0x104; at += 4)
            At<uint32_t>(self, at) = At<uint32_t>(self, field::kEducation);
        for (std::size_t at = 0x108; at <= 0x11C; at += 4)
            At<uint32_t>(self, at) = At<uint32_t>(self, field::kHealth);
        Send(0xAA25827C, nullptr, self);
        Place work;
        reinterpret_cast<ThisFn>(addr::SC4MySimPlace_Construct)(&work);
        reinterpret_cast<ThisFn>(addr::SC4MySimPlace_SetWork)(&work);
        reinterpret_cast<uint8_t(__thiscall*)(void*, Place*, uint32_t)>(addr::cSC4MySimDispatch_SetDestination)(
            At<void*>(self, field::kDispatch), &work, 1);
        Release(work.object);
        reinterpret_cast<ThisBoolFn>(addr::cSC4MySim_CanTravel)(object);
        At<uint8_t>(At<char*>(self, field::kDispatch), 0x10) = 1;
    }
    reinterpret_cast<ThisFn>(addr::cSC4MySim_UpdateHomeAndJob)(object);
    Release(home.object);
    return 1;
}

// --- Registration ----------------------------------------------------------------------

bool InstallInterface(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySim_AsAdvisor, reinterpret_cast<void*>(&AsAdvisor)},
        {addr::cSC4MySim_GetVehicleKey, reinterpret_cast<void*>(&GetVehicleKey)},
        {addr::cSC4MySim_GetHomeName, reinterpret_cast<void*>(&GetHomeName)},
        {addr::cSC4MySim_GetJobName, reinterpret_cast<void*>(&GetJobName)},
        {addr::cSC4MySim_GetJobTitleOf, reinterpret_cast<void*>(&GetJobTitleOf)},
        {addr::cSC4MySim_GetJobTitle, reinterpret_cast<void*>(&GetJobTitle)},
        {addr::cSC4MySim_GetEQAsText, reinterpret_cast<void*>(&GetEQAsText)},
        {addr::cSC4MySim_GetHQAsText, reinterpret_cast<void*>(&GetHQAsText)},
        {addr::cSC4MySim_HideResidence, reinterpret_cast<void*>(&HideResidence)},
        {addr::cSC4MySim_HideLocation, reinterpret_cast<void*>(&HideLocation)},
        {addr::cSC4MySim_SetPylonVisibility, reinterpret_cast<void*>(&SetPylonVisibility)},
        {addr::cSC4MySim_LeaveForWork, reinterpret_cast<void*>(&LeaveForWork)},
        {addr::cSC4MySim_ReturnHome, reinterpret_cast<void*>(&ReturnHome)},
        {addr::cSC4MySim_DoBuildingChanged, reinterpret_cast<void*>(&DoBuildingChanged)},
        {addr::cSC4MySim_DoNeighborConnectionRemoved, reinterpret_cast<void*>(&DoNeighborConnectionRemoved)},
        {addr::cSC4MySim_DistanceToClosest, reinterpret_cast<void*>(&DistanceToClosest)},
        {addr::cSC4MySim_PlayNightSound, reinterpret_cast<void*>(&PlayNightSound)},
        {addr::cSC4MySim_GetCurrentScreenLocationXY, reinterpret_cast<void*>(&GetCurrentScreenLocationXY)},
        {addr::cSC4MySim_RequestToPlaySound, reinterpret_cast<void*>(&RequestToPlaySound)},
        {addr::cSC4MySim_GetModelButtonBitmapID, reinterpret_cast<void*>(&GetModelButtonBitmapID)},
        {addr::cSC4MySim_GetNextBalloonAdvice, reinterpret_cast<void*>(&GetNextBalloonAdvice)},
        {addr::cSC4MySim_GetNextBalloonEventAdvice, reinterpret_cast<void*>(&GetNextBalloonEventAdvice)},
        {addr::cSC4MySim_GetCurrentBalloonAdvice, reinterpret_cast<void*>(&GetCurrentBalloonAdvice)},
        {addr::cSC4MySim_GetCurrentBalloonEventAdvice, reinterpret_cast<void*>(&GetCurrentBalloonEventAdvice)},
        {addr::cSC4MySim_CanUDriveIt, reinterpret_cast<void*>(&CanUDriveIt)},
        {addr::cSC4MySim_EnterUDriveIt, reinterpret_cast<void*>(&EnterUDriveIt)},
        {addr::cSC4MySim_ExitUDriveIt, reinterpret_cast<void*>(&ExitUDriveIt)},
        {addr::cSC4MySim_ActivateLuaContext, reinterpret_cast<void*>(&ActivateLuaContext)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

bool InstallSave(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[2] = {};
    const addr::Entry* write = addr_check::Find(addr::cSC4MySim_Write);
    const addr::Entry* read = addr_check::Find(addr::cSC4MySim_Read);
    return write && read && hook::Install(*write, reinterpret_cast<void*>(&Write), &originals[0]) &&
           hook::Install(*read, reinterpret_cast<void*>(&Read), &originals[1]);
}

const bool g_saveRegistered = registry::Register({
    "simulators.mysim_agents.save",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_Write,
    "Our cSC4MySim Write and Read (docs/mysim-agents.md)",
    false,
    InstallSave,
});

bool InstallMessages(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* original = nullptr;
    const addr::Entry* found = addr_check::Find(addr::cSC4MySim_DoMessage);
    return found && hook::Install(*found, reinterpret_cast<void*>(&DoMessage), &original);
}

const bool g_messagesRegistered = registry::Register({
    "simulators.mysim_agents.messages",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_DoMessage,
    "Our cSC4MySim DoMessage: trips, arrivals, balloons, the frame message (docs/mysim-agents.md)",
    false,
    InstallMessages,
});

bool InstallUpdate(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[2] = {};
    const addr::Entry* update = addr_check::Find(addr::cSC4MySim_Update);
    const addr::Entry* think = addr_check::Find(addr::cSC4MySim_Think);
    return update && think && hook::Install(*update, reinterpret_cast<void*>(&Update), &originals[0]) &&
           hook::Install(*think, reinterpret_cast<void*>(&Think), &originals[1]);
}

const bool g_updateRegistered = registry::Register({
    "simulators.mysim_agents.update",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_Update,
    "Our cSC4MySim update and Think: counting days, balloon advice, script commands (docs/mysim-agents.md)",
    false,
    InstallUpdate,
});

bool InstallEvents(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySim_ChangeResidence, reinterpret_cast<void*>(&ChangeResidence)},
        {addr::cSC4MySim_TestForResidence, reinterpret_cast<void*>(&TestForResidence)},
        {addr::cSC4MySim_ChangeJob, reinterpret_cast<void*>(&ChangeJob)},
        {addr::cSC4MySim_DeleteFromGame, reinterpret_cast<void*>(&DeleteFromGame)},
        {addr::cSC4MySim_GetLocation, reinterpret_cast<void*>(&GetLocation)},
        {addr::cSC4MySim_AnnounceMoveIn, reinterpret_cast<void*>(&AnnounceMoveIn)},
        {addr::cSC4MySim_DoScriptCommand, reinterpret_cast<void*>(&DoScriptCommand)},
        {addr::cSC4MySim_DoBuildingAdded, reinterpret_cast<void*>(&DoBuildingAdded)},
        {addr::cSC4MySim_DoBuildingRemoved, reinterpret_cast<void*>(&DoBuildingRemoved)},
        {addr::cSC4MySim_DoFireAdded, reinterpret_cast<void*>(&DoFireAdded)},
        {addr::cSC4MySim_DoPipeBurst, reinterpret_cast<void*>(&DoPipeBurst)},
        {addr::cSC4MySim_DoTornado, reinterpret_cast<void*>(&DoTornado)},
        {addr::cSC4MySim_DoDisaster, reinterpret_cast<void*>(&DoDisaster)},
        {addr::cSC4MySim_DistanceToClosestSpecial, reinterpret_cast<void*>(&DistanceToClosestSpecial)},
        {addr::cSC4MySim_CreateDispatchTool, reinterpret_cast<void*>(&CreateDispatchTool)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_eventsRegistered = registry::Register({
    "simulators.mysim_agents.events",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_ChangeResidence,
    "Our cSC4MySim methods for city changes: home, job, buildings, fires, disasters, deletion, location "
    "(docs/mysim-agents.md)",
    false,
    InstallEvents,
});

bool InstallResidence(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[3] = {};
    const addr::Entry* moveIn = addr_check::Find(addr::cSC4MySim_MoveIn);
    const addr::Entry* find = addr_check::Find(addr::cSC4MySim_FindNewResidence);
    const addr::Entry* setResidence = addr_check::Find(addr::cSC4MySim_SetResidence);
    return moveIn && find && setResidence &&
           hook::Install(*moveIn, reinterpret_cast<void*>(&MoveIn), &originals[0]) &&
           hook::Install(*find, reinterpret_cast<void*>(&FindNewResidence), &originals[1]) &&
           hook::Install(*setResidence, reinterpret_cast<void*>(&SetResidence), &originals[2]);
}

const bool g_residenceRegistered = registry::Register({
    "simulators.mysim_agents.residence",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_SetResidence,
    "Our cSC4MySim MoveIn, FindNewResidence and SetResidence (docs/mysim-agents.md)",
    false,
    InstallResidence,
});

const bool g_interfaceRegistered = registry::Register({
    "simulators.mysim_agents.interface",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_AsAdvisor,
    "Our cSC4MySim interface methods: names, titles, balloons, trips, U-Drive-It (docs/mysim-agents.md)",
    false,
    InstallInterface,
});

} // namespace
} // namespace opensc4::mysim_agents
