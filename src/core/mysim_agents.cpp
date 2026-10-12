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
//     SetResidence;
//   simulators.mysim_agents.script: UpdateHomeAndJob, the home and job
//     facts the MySim's script reads;
//   simulators.mysim_agents.trip: OnCommand, the trip facts;
//   simulators.mysim_agents.agent: the agent (+0) and advisor (+8)
//     interfaces' methods;
//   simulators.mysim_agents.ui: the cISC4MySim methods the panel and the
//     residence view use;
//   simulators.mysim_agents.object: the factory, constructor and
//     destructors;
//   simulators.mysim_agents.helpers: the object's own helpers (trips,
//     balloons, advice, distances);
//   simulators.mysim_agents.loading: LoadAdvice and LoadTuning;
//   simulators.mysim_agents.misc: the rest of the object's own methods
//     (jobs, stranding, the script's commands, the ghost, the location).
//
// The methods take the cISC4MySim part as `this` (here `self`); offsets
// below are from it. The object starts at self - 0xB8.
#include "core/address_check.h"
#include "core/game.h"
#include "core/fixed_seed.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
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

void* __fastcall GetNextBalloonAdvice(char* self, void*)
{
    return reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(addr::cSC4MySim_NextBalloon)(Object(self), 2);
}

void* __fastcall GetNextBalloonEventAdvice(char* self, void*)
{
    return reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(addr::cSC4MySim_NextBalloon)(Object(self), 1);
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
        At<int32_t>(mysim, 0xC0) == 2 && !reinterpret_cast<ThisBoolFn>(addr::cSC4MySim_TryGoToWork)(mysim))
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
    reinterpret_cast<ThisFn>(addr::cSC4MySim_ClearResidence)(object);
    Virtual<void>(self, 0x34, occupant); // SetResidence
    if (Virtual<uint8_t>(self - 0xB0, 0x4C, 0x6A47E79Bu)) {
        reinterpret_cast<ThisFn>(addr::cSC4MySim_ClearResidence)(object);
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
    reinterpret_cast<ThisFn>(addr::cSC4MySim_ClearResidence)(object);
    At<int32_t>(self, field::kState) = 0;
    Virtual<void>(self, 0x8C); // HideLocation
    reinterpret_cast<ThisFn>(addr::cSC4MySim_ResetAdvisor)(object);
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
    const uint32_t value = reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t)>(addr::cSC4MySim_DistanceTo)(
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
            reinterpret_cast<ThisFn>(addr::cSC4MySim_ClearResidence)(object);
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
        reinterpret_cast<ThisFn>(addr::cSC4MySim_ResetAdvisor)(object);
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

// --- The script's facts -----------------------------------------------------------------
//
// UpdateHomeAndJob (from the object's start) sets ~40 globals in the
// MySim's script table (the script context at +0x1F0, the table's
// reference at +0x164): education and health, the home's condition,
// services, coverage, pollution, funding and traffic, the job's. The names
// are the game's strings.

struct ScriptString { // SC4String, 0x14 bytes, referenced
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

void* Script(char* mysim)
{
    return At<void*>(mysim, 0x1F0);
}

void SetNumber(char* mysim, std::uintptr_t name, double value)
{
    void* const script = Script(mysim);
    Virtual<void>(script, 0x84, reinterpret_cast<const char*>(name));
    Virtual<void>(script, 0x7C, value);
    Virtual<void>(script, 0xB8, -3);
}

void SetObject(char* mysim, std::uintptr_t name, void* object)
{
    void* const script = Script(mysim);
    Virtual<void>(script, 0x84, reinterpret_cast<const char*>(name));
    Virtual<uint32_t>(object, 0x4);
    void* const city = Virtual<void*>(Global<void*>(addr::SC4_City), 0x144);
    const uint32_t kind = Virtual<uint32_t>(city, 0x58, uint32_t{1});
    Virtual<void>(script, 0x114, object, kind);
    Virtual<void>(script, 0xB8, -3);
}

// An empty SC4String made as UpdateHomeAndJob makes it, held at `at`.
void NewString(char* mysim, std::size_t at)
{
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
    void* const old = At<void*>(mysim, at);
    if (text != old) {
        if (text)
            Virtual<uint32_t>(text, 0x4);
        At<void*>(mysim, at) = text;
        Release(old);
    }
}

bool Flag(uint8_t value)
{
    return value != 0;
}

// A grid value at the home's cell through a service's slot 5 (with an
// argument for pollution's grids).
template <typename... Args>
void* ServiceGrid(std::uintptr_t service, Args... args)
{
    return Virtual<void*>(Global<void*>(service), 0x14, args...);
}

// The nearest station's funding (police: slot 21, its slot 7; fire: slot
// 22, its slot 8, by distance from the MySim), starting from the default.
float Funding(char* mysim, void* service, bool fire, void** head)
{
    using ListFn = void(__thiscall*)(void* list);
    float funding = Global<float>(addr::SC4_DefaultFunding);
    uint32_t best = 0xFFFFFFFF;
    reinterpret_cast<ListFn>(addr::SC4List_Clear2)(head);
    if (Virtual<uint32_t>(service, fire ? 0x58 : 0x54, head)) {
        char* const list = static_cast<char*>(*head);
        for (char* node = At<char*>(list, 0); node != *head; node = At<char*>(node, 0)) {
            void* const station = At<void*>(node, 8);
            uint32_t distance;
            if (fire) {
                float position[3];
                Virtual<void>(station, 0x20, position);
                const double dx = static_cast<double>(At<float>(mysim, 0xD0)) - position[0];
                const double dz = static_cast<double>(At<float>(mysim, 0xD8)) - position[2];
                distance = static_cast<uint32_t>(Truncate(std::sqrt(dz * dz + dx * dx)));
            } else {
                distance = reinterpret_cast<uint32_t(__thiscall*)(void*, void*)>(addr::cSC4MySim_DistanceTo)(
                    mysim, station);
            }
            if (distance < best &&
                Virtual<uint8_t>(service, fire ? 0x20 : 0x1C, station, uint32_t{0}, &funding))
                best = distance;
            Virtual<uint32_t>(station, 0x8);
        }
        reinterpret_cast<ListFn>(addr::SC4List_Clear2)(head);
    }
    return funding;
}

// The most congested network cell on the border of the home's lot, grown
// by one cell and kept inside the city.
uint8_t Congestion(int32_t* rect)
{
    void* const city = Global<void*>(addr::SC4_City);
    --rect[0];
    --rect[1];
    ++rect[2];
    ++rect[3];
    const int32_t last = Virtual<int32_t>(city, 0x174) - 1;
    if (rect[0] < 0)
        rect[0] = 0;
    if (rect[1] < 0)
        rect[1] = 0;
    if (rect[2] > last)
        rect[2] = last;
    if (rect[3] > last)
        rect[3] = last;
    void* const network = Virtual<void*>(Global<void*>(addr::SC4_City), 0x114);
    uint8_t most = 0;
    const auto check = [&](int32_t x, int32_t z) {
        using AtFn = double(__thiscall*)(void* self, uint32_t kind, int32_t x, int32_t z);
        const double value = reinterpret_cast<AtFn>(At<void**>(network, 0)[0x5C / 4])(network, 1, x, z);
        if (value == 0.0)
            return;
        const uint8_t congestion = Virtual<uint8_t>(Virtual<void*>(network, 0x28), 0x1C, x, z);
        if (congestion > most)
            most = congestion;
    };
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        check(x, rect[3]);
        check(x, rect[1]);
    }
    for (int32_t z = rect[1] + 1; z < rect[3]; ++z) {
        check(rect[0], z);
        check(rect[2], z);
    }
    return most;
}

// An occupant's location in the script: advice data holding it, or nil.
void SetLocation(char* mysim, std::uintptr_t name, void* occupant)
{
    void* const script = Script(mysim);
    Virtual<void>(script, 0x84, reinterpret_cast<const char*>(name));
    if (!occupant) {
        Virtual<void>(script, 0x114, nullptr, uint32_t{0});
        Virtual<void>(script, 0xB8, -3);
        return;
    }
    void* data = nullptr;
    void* const com = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
    if (Virtual<uint8_t>(com, 0xC, 0x6A26D049u, 0x6A26D050u, &data)) {
        Virtual<void>(data, 0x1C, occupant);
        Virtual<uint32_t>(data, 0x4);
        void* const city = Virtual<void*>(Global<void*>(addr::SC4_City), 0x144);
        const uint32_t kind = Virtual<uint32_t>(city, 0x58, uint32_t{0});
        Virtual<void>(script, 0x114, data, kind);
    }
    Release(data);
    Virtual<void>(script, 0xB8, -3);
}

// The distance map at +0x1E8 (DistanceToClosest's): for each occupant
// within 20 cells of home that is a building, the uint32 list of its type's
// resource (service 0x00B43D68's slot 14 gives the key) names keys, and
// each key keeps the shortest distance from the MySim.
void __fastcall UpdateDistances(char* mysim, void*)
{
    using ListFn = void(__thiscall*)(void* list);
    void** const head = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    head[0] = head;
    head[1] = head;
    struct {
        void* head;
        uint32_t size;
    } list{head, 0};
    if (At<void*>(mysim, 0xC4)) {
        void* const types = Global<void*>(addr::SC4_Service43D68);
        struct {
            uint32_t* begin;
            uint32_t* end;
            uint32_t* capacity;
        } keys{};
        uint32_t key[3] = {};
        auto* const filter = static_cast<std::uintptr_t*>(
            reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x10));
        if (filter) {
            reinterpret_cast<ThisFn>(addr::SC4_OccupantFilterBase)(filter);
            filter[0] = addr::SC4MySim_ResidenceFilterVtable;
            filter[1] = addr::SC4MySim_ResidenceFilterVtable4;
            reinterpret_cast<uint8_t*>(filter)[0xC] = 0;
            Virtual<uint32_t>(filter, 0x4);
        }
        char* map = At<char*>(mysim, 0x1E8);
        if (!map) {
            void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0xC);
            map = memory ? static_cast<char*>(
                               reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4TrafficZone_Construct)(memory)) // a uint32 map's constructor
                         : nullptr;
            At<char*>(mysim, 0x1E8) = map;
        } else if (At<uint32_t>(map, 4)) {
            char* const root = At<char*>(map, 0);
            reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4Uint32Map_EraseSubtree)(map, At<void*>(root, 4));
            At<char*>(At<char*>(map, 0), 8) = At<char*>(map, 0);
            At<void*>(At<char*>(map, 0), 4) = nullptr;
            At<char*>(At<char*>(map, 0), 0xC) = At<char*>(map, 0);
            At<uint32_t>(map, 4) = 0;
        }
        const int32_t x = At<int32_t>(mysim, 0xC8);
        const int32_t z = At<int32_t>(mysim, 0xCC);
        int32_t low[2] = {x - 20, z - 20};
        int32_t high[2] = {x + 20, z + 20};
        void* const occupants = Virtual<void*>(Global<void*>(addr::SC4_City), 0x88);
        if (Virtual<uint8_t>(occupants, 0xA0, &list, low, high, static_cast<void*>(filter), -1)) {
            for (char* node = static_cast<char*>(head[0]); node != list.head; node = At<char*>(node, 0)) {
                void* const occupant = At<void*>(node, 8);
                void* building = nullptr;
                if (Virtual<uint8_t>(occupant, 0x0, 0x87DDA3A9u, &building)) {
                    float position[3];
                    Virtual<void>(occupant, 0x20, position);
                    const double dx = static_cast<double>(At<float>(mysim, 0xD0)) - position[0];
                    const double dz = static_cast<double>(At<float>(mysim, 0xD8)) - position[2];
                    const uint32_t distance = static_cast<uint32_t>(Truncate(std::sqrt(dz * dz + dx * dx)));
                    const uint32_t type = Virtual<uint32_t>(building, 0x10);
                    Virtual<void>(types, 0x38, type, key);
                    reinterpret_cast<void(__cdecl*)(void*, void*)>(addr::SC4_LoadUint32List)(key, &keys);
                    const auto count = static_cast<int32_t>(keys.end - keys.begin);
                    using AtFn = uint32_t*(__thiscall*)(void* map, const uint32_t* key);
                    const auto at = reinterpret_cast<AtFn>(addr::SC4Uint32Map_At);
                    for (int32_t i = 0; i < count; ++i) {
                        const uint32_t id = keys.begin[i];
                        const uint32_t nearest = *at(At<void*>(mysim, 0x1E8), &id);
                        if (!nearest || distance < nearest)
                            *at(At<void*>(mysim, 0x1E8), &id) = distance;
                    }
                }
                Virtual<uint32_t>(occupant, 0x8);
                Release(building);
            }
            reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&list);
        }
        Release(filter);
        if (keys.begin)
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(keys.begin);
    }
    reinterpret_cast<ListFn>(addr::SC4List_Destroy2)(&list);
}

// The MySim's own facts: state, age, wealth, generation, retired, zodiac,
// sex, the last local disaster, home and job locations, name.
void __fastcall PublishBasics(char* mysim, void*)
{
    if (!At<int32_t>(mysim, 0x164) && Script(mysim)) {
        Virtual<void>(Script(mysim), 0xB0);
        At<int32_t>(mysim, 0x164) = Virtual<int32_t>(Script(mysim), 0xC8, uint32_t{1});
        reinterpret_cast<CommandFn>(addr::cSC4MySim_OnCommand)(mysim, -1, -1, -1, -1, 1);
    }
    if (!Script(mysim))
        return;
    Virtual<void>(Script(mysim), 0xAC, At<int32_t>(mysim, 0x164));
    SetNumber(mysim, 0x00A8506C, At<int32_t>(mysim, 0xC0));   // state
    SetNumber(mysim, 0x00A85068, At<uint8_t>(mysim, 0x128));  // age
    SetNumber(mysim, 0x00A85060, At<int32_t>(mysim, 0x10C));  // wealth
    SetNumber(mysim, 0x00A85054, At<uint16_t>(mysim, 0x110)); // generation
    SetNumber(mysim, 0x00A8504C, At<uint8_t>(mysim, 0x158));  // retired
    SetNumber(mysim, 0x00A85044, At<uint8_t>(mysim, 0x112));  // zodiac
    SetNumber(mysim, 0x00A8503C, At<uint8_t>(mysim, 0x113));  // is_male
    SetNumber(mysim, 0x00A85028, At<uint32_t>(mysim, 0x15C)); // last_local_disaster
    SetLocation(mysim, 0x00A85018, At<void*>(mysim, 0xC4));   // home_location
    SetLocation(mysim, 0x00A85008, At<void*>(mysim, 0xE0));   // job_location
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x14);
    void* name = nullptr;
    if (memory) {
        void* const advisorName = Virtual<void*>(mysim + 8, 0x20);
        name = reinterpret_cast<void*(__thiscall*)(void*, void*)>(addr::SC4String_FromIGZString)(memory, advisorName);
    }
    if (name)
        Virtual<uint32_t>(name, 0x4);
    SetObject(mysim, 0x00A81380, name); // name
    Virtual<void>(Script(mysim), 0x2C, uint32_t{1});
    Virtual<uint32_t>(name, 0x8);
}

void __fastcall UpdateHomeAndJob(char* mysim, void*)
{
    reinterpret_cast<ThisFn>(addr::cSC4MySim_PublishBasics)(mysim);
    void* const power = Global<void*>(addr::SC4_Service43DA0);
    void* const water = Global<void*>(addr::SC4_Service43D94);
    if (!At<int32_t>(mysim, 0x164) && Script(mysim)) {
        Virtual<void>(Script(mysim), 0xB0);
        At<int32_t>(mysim, 0x164) = Virtual<int32_t>(Script(mysim), 0xC8, uint32_t{1});
        reinterpret_cast<CommandFn>(addr::cSC4MySim_OnCommand)(mysim, -1, -1, -1, -1, 1);
    }
    if (!Script(mysim) || At<int32_t>(mysim, 0x164) <= 0)
        return;
    void* const fire = Global<void*>(addr::SC4_Service43D78);
    void* const city = Global<void*>(addr::SC4_Service43DA4);
    void* const police = Global<void*>(addr::SC4_PoliceSimulator);
    Virtual<void>(Script(mysim), 0xAC, At<int32_t>(mysim, 0x164));
    const auto x = [&] { return At<int32_t>(mysim, 0xC8); };
    const auto z = [&] { return At<int32_t>(mysim, 0xCC); };

    if (void* const residence = At<void*>(mysim, 0xC4)) {
        int32_t rect[4];
        Virtual<void>(At<void*>(mysim, 0xDC), 0x38, rect);
        using GradeFn = double(__thiscall*)(void* service, int32_t x, int32_t z, int32_t wealth);
        const double eq = reinterpret_cast<GradeFn>(At<void**>(city, 0)[0x94 / 4])(city, x(), z(), At<int32_t>(mysim, 0x10C));
        if (eq > 0.0)
            At<float>(mysim, 0x17C) = static_cast<float>(eq);
        SetNumber(mysim, 0x00A85470, Truncate(At<float>(mysim, 0x17C))); // eq
        const double hq = reinterpret_cast<GradeFn>(At<void**>(city, 0)[0x98 / 4])(city, x(), z(), At<int32_t>(mysim, 0x10C));
        if (hq > 0.0)
            At<float>(mysim, 0x180) = static_cast<float>(hq);
        SetNumber(mysim, 0x00A8546C, Truncate(At<float>(mysim, 0x180))); // hq
        SetNumber(mysim, 0x00A85460, Truncate(At<float>(mysim, 0x1DC))); // delta_eq
        SetNumber(mysim, 0x00A85454, Truncate(At<float>(mysim, 0x1E0))); // delta_hq
        SetNumber(mysim, 0x00A85450, Virtual<uint8_t>(city, 0x74, x(), z())); // le
        SetNumber(mysim, 0x00A85440,
                  reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::SC4_OccupantCondition)(residence)); // home_condition
        if (!At<void*>(mysim, 0x168)) {
            void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x14);
            void* const text = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4String_SC4String)(memory)
                                      : nullptr;
            reinterpret_cast<AssignFn>(addr::SC4RefPtr_Assign)(mysim + 0x168, text);
            SetObject(mysim, 0x00A85430, At<void*>(mysim, 0x168)); // home_building
        }
        Virtual<void>(residence, 0x4C, At<void*>(mysim, 0x168));
        SetNumber(mysim, 0x00A85420, Flag(Virtual<uint8_t>(At<void*>(mysim, 0xDC), 0x70)) ? 1 : 0); // home_has_road
        SetNumber(mysim, 0x00A85410, Flag(Virtual<uint8_t>(power, 0x7C, Virtual<void*>(residence, 0xC))) ? 1 : 0);
        SetNumber(mysim, 0x00A85400, Flag(Virtual<uint8_t>(water, 0x24, Virtual<void*>(residence, 0xC))) ? 1 : 0);
        SetNumber(mysim, 0x00A853F4, Virtual<uint8_t>(ServiceGrid(addr::SC4_Service43D70), 0x1C, x(), z())); // local_crime
        const int32_t width = rect[2] - rect[0] + 1;
        const int32_t height = rect[3] - rect[1] + 1;
        SetNumber(mysim, 0x00A853DC, Virtual<uint8_t>(Virtual<void*>(police, 0x14), 0x20, rect[0], rect[1], width, height));
        SetNumber(mysim, 0x00A853C8, Virtual<uint8_t>(Virtual<void*>(fire, 0x14), 0x20, rect[0], rect[1], width, height));
        const std::uintptr_t pollutionNames[] = {0x00A853B4, 0x00A8539C, 0x00A8538C, 0x00A8537C};
        for (uint32_t kind = 0; kind < 4; ++kind)
            SetNumber(mysim, pollutionNames[kind],
                      Virtual<int16_t>(ServiceGrid(addr::SC4_PollutionSimulator, kind), 0x1C, x(), z()));
        SetNumber(mysim, 0x00A85364, At<int32_t>(mysim, 0x298)); // last_player_trip_day
        uint32_t desirability = 0;
        switch (At<int32_t>(mysim, 0x10C)) {
        case 1:
            desirability = 0x1010;
            break;
        case 2:
            desirability = 0x1020;
            break;
        case 3:
            desirability = 0x1030;
            break;
        default:
            break;
        }
        if (desirability)
            if (void* const grids = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x80, desirability))
                SetNumber(mysim, 0x00A85354, Virtual<int16_t>(Virtual<void*>(grids, 0x34), 0x1C, x(), z()));
        SetNumber(mysim, 0x00A8533C, Virtual<int32_t>(At<void*>(mysim, 0xDC), 0xA4)); // home_occupant_wealth
        reinterpret_cast<ThisFn>(addr::cSC4MySim_UpdateDistances)(mysim);
        void** const list = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
        list[0] = list;
        list[1] = list;
        struct {
            void* head;
            uint32_t size;
        } stations{list, 0};
        const float policeFunding = Funding(mysim, police, false, &stations.head);
        SetNumber(mysim, 0x00A85324, Truncate(static_cast<double>(policeFunding) * 100.0f)); // local_police_funding
        const float fireFunding = Funding(mysim, fire, true, &stations.head);
        SetNumber(mysim, 0x00A85310, Truncate(static_cast<double>(fireFunding) * 100.0f)); // local_fire_funding
        SetNumber(mysim, 0x00A852F4, Congestion(rect)); // local_traffic_congestion
        reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4List_Clear2)(&stations.head);
        Virtual<void>(ListNodeAllocator(), 0x10, stations.head);
    }

    if (!At<void*>(mysim, 0x170)) {
        NewString(mysim, 0x170);
        SetObject(mysim, 0x00A852E8, At<void*>(mysim, 0x170)); // job_title
    }
    if (!At<void*>(mysim, 0x16C)) {
        NewString(mysim, 0x16C);
        SetObject(mysim, 0x00A852D8, At<void*>(mysim, 0x16C)); // job_building
    }
    if (!At<void*>(mysim, 0x174)) {
        NewString(mysim, 0x174);
        SetObject(mysim, 0x00A852CC, At<void*>(mysim, 0x174)); // job_city
    }
    SetNumber(mysim, 0x00A852B8, At<int32_t>(mysim, 0x154)); // last_commute_time
    void* const job = At<void*>(mysim, 0xE0);
    void* const pending = At<void*>(mysim, 0xE4);
    if (job || pending) {
        SetNumber(mysim, 0x00A852AC, 1.0); // employed
        SetNumber(mysim, 0x00A8529C, 0.0); // unemployed_days
        if (void* const title = At<void*>(mysim, 0x170)) {
            if (pending) {
                uint32_t id = 0;
                switch (At<int32_t>(mysim, 0x10C)) {
                case 1:
                    id = 0x8A538A68;
                    break;
                case 2:
                    id = 0x8A538A69;
                    break;
                case 3:
                    id = 0x8A538A70;
                    break;
                default:
                    break;
                }
                if (id)
                    LoadText(title, 0xEA231E96, id, 0);
            } else if (job) {
                Virtual<void>(mysim + 0xB8, 0x6C, job, title); // GetJobTitle(job, title)
            }
        }
    }
    if (void* const work = At<void*>(mysim, 0xE0)) {
        Virtual<void*>(work, 0x2C);
        SetNumber(mysim, 0x00A8528C, 0.0); // job_is_neighbor
        void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, Virtual<void*>(work, 0xC));
        SetNumber(mysim, 0x00A8527C, lot ? Virtual<uint8_t>(lot, 0x8C) : 0); // job_condition
        SetNumber(mysim, 0x00A8526C, Flag(Virtual<uint8_t>(power, 0x7C, Virtual<void*>(At<void*>(mysim, 0xE0), 0xC))) ? 1 : 0);
        if (At<uint32_t>(fire, 0) == 5)
            SetNumber(mysim, 0x00A8525C, 1.0); // job_has_water
        else
            SetNumber(mysim, 0x00A8525C,
                      Flag(Virtual<uint8_t>(water, 0x24, Virtual<void*>(At<void*>(mysim, 0xE0), 0xC))) ? 1 : 0);
        Virtual<void>(At<void*>(mysim, 0xE0), 0x4C, At<void*>(mysim, 0x16C));
    } else if (At<void*>(mysim, 0xE4)) {
        SetNumber(mysim, 0x00A8528C, 1.0); // job_is_neighbor
        SetNumber(mysim, 0x00A8527C, 0.0); // job_condition
        SetNumber(mysim, 0x00A8526C, 1.0); // job_has_power
        SetNumber(mysim, 0x00A8525C, 1.0); // job_has_water
        struct {
            std::uintptr_t vtable;
            char* begin;
            char* end;
            char* capacity;
            uint32_t rest[4];
        } text{};
        reinterpret_cast<void*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(addr::SC4LocalizedString_SC4LocalizedString)(
            &text, 0xEA231E96, 0x6A662504, 1);
        Virtual<void>(At<void*>(mysim, 0x16C), 0x38, &text);
        Virtual<void>(At<void*>(mysim, 0xE4), 0x84, At<void*>(mysim, 0x174));
        text.vtable = addr::SC4String_Vtable;
        if (text.begin)
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(text.begin);
    } else {
        SetNumber(mysim, 0x00A852AC, 0.0); // employed
        SetNumber(mysim, 0x00A8529C, At<int32_t>(mysim, 0xFC)); // unemployed_days
    }

    // The job building's text through the text service.
    using SingletonFn = void*(__cdecl*)();
    const auto singleton = reinterpret_cast<SingletonFn>(addr::SC4_Singleton913C72);
    void* const formatter = Virtual<void*>(singleton(), 0x14, 0x68963C4Cu);
    struct {
        uint32_t a;
        uint32_t b;
        uint32_t size;
        uint32_t font;
    } style{0, 0, 0xC8, Virtual<uint32_t>(formatter, 0x8C)};
    ScriptString text{};
    char* const buffer = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8));
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.references = 0;
    const uint32_t face = Virtual<uint32_t>(singleton(), 0x8C, 0x5377BE31u);
    reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4String_AssignFromIGZString)(&text,
                                                                                           At<void*>(mysim, 0x16C));
    Virtual<void>(formatter, 0xC8, &text, face, &style, uint32_t{0});
    Virtual<void>(At<void*>(mysim, 0x16C), 0x38, &text);
    Virtual<void>(Script(mysim), 0x2C, uint32_t{1});
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(text.begin);
}

// --- Trips ------------------------------------------------------------------------------
//
// OnCommand (from the object's start; arguments: the trip state now and
// before, the trip flags now and before, forced) sets the trip globals in
// the MySim's script: in transit or arrived, the destination, the means,
// congestion, time and waiting, pollution, crime, mayor rating, EQ and HQ
// along the way, the buildings around (distress and wealth), a landmark
// and its funding, disasters nearby. The dispatch object is at +0x20C.

// The game's script-subject setter: name in eax, script in esi.
void SetSubject(char* mysim, std::uintptr_t name, uint32_t subject)
{
    void* const script = Script(mysim);
    const std::uintptr_t fn = addr::cSC4MySim_SetSubject;
    __asm {
        push esi
        mov esi, script
        mov eax, name
        push subject
        call dword ptr [fn]
        add esp, 4
        pop esi
    }
}

void SetNumberInt(char* mysim, std::uintptr_t name, int32_t value)
{
    SetNumber(mysim, name, value);
}

void __fastcall OnCommand(char* mysim, void*, int32_t trip, int32_t tripBefore, int32_t flags, int32_t flagsBefore,
                          uint8_t forced)
{
    if (!Script(mysim) || At<int32_t>(mysim, 0x164) <= 0)
        return;
    char* const iface = mysim + 0xB8;
    if (!Virtual<uint8_t>(iface, 0x50)) // IsActive
        return;
    const int32_t top = Virtual<int32_t>(Script(mysim), 0x18);
    Virtual<void>(Script(mysim), 0xAC, At<int32_t>(mysim, 0x164));
    const auto dispatch = [&] { return At<char*>(mysim, 0x20C); };
    const auto same = reinterpret_cast<SameFn>(addr::SC4MySimPlace_Same);
    bool changed = trip != tripBefore;

    if (trip != tripBefore || forced) {
        SetNumberInt(mysim, 0x00A84FE8, trip == 2 ? 1 : 0); // trip_in_transit
        SetNumberInt(mysim, 0x00A84FD4, trip == 3 ? 1 : 0); // trip_at_location
        bool destination = false;
        if (trip == 3) {
            if (same(dispatch() + 0x5C, dispatch() + 0x84)) {
                const int32_t before = At<int32_t>(mysim, 0xC0);
                At<int32_t>(mysim, 0xC0) = 2;
                if (before == 1)
                    Virtual<void>(iface, 0xA0); // AnnounceMoveIn
                else
                    reinterpret_cast<ThisFn>(addr::cSC4MySim_ArriveHome)(mysim);
            } else if (same(dispatch() + 0x5C, dispatch() + 0xD4)) {
                At<int32_t>(mysim, 0xC0) = 3;
            } else if (At<void*>(mysim, 0x248) && At<uint8_t>(mysim, 0x245) &&
                       reinterpret_cast<AtPlaceFn>(addr::cSC4MySim_AtBalloonPlace)(mysim, dispatch() + 0xAC)) {
                reinterpret_cast<PostFn>(addr::SC4_PostMessage)(kBalloonMessage, 0, 0, 0, 0, iface);
                Virtual<void>(At<void*>(mysim, 0x248), 0x2C, uint32_t{4});
                sim_timers::Claim(mysim + 0x268);
                reinterpret_cast<ThisFn>(addr::GZStopwatch_Start)(mysim + 0x268);
            }
            reinterpret_cast<ThisFn>(addr::cSC4MySim_PublishBasics)(mysim);
            destination = true;
        } else if (trip == 2) {
            SetSubject(mysim, 0x00A84FC0, At<uint32_t>(dispatch() + 0xAC, 0)); // destination_subject
        }
        if (destination) {
            Place here;
            reinterpret_cast<PlaceCopyFn>(addr::SC4MySimPlace_Copy)(&here, dispatch() + 0x5C);
            uint32_t subject = 0;
            if (!same(&here, dispatch() + 0xD4) && !same(&here, dispatch() + 0x84))
                subject = At<uint32_t>(dispatch() + 0x5C, 0);
            SetSubject(mysim, 0x00A84FC0, subject);
            Release(here.object);
        }
    }

    if ((flags != flagsBefore && (flags ^ flagsBefore) != 0x100) || forced) {
        const auto hasFlag = reinterpret_cast<HasFlagFn>(addr::cSC4MySimDispatch_HasFlag);
        SetNumberInt(mysim, 0x00A84FA8, hasFlag(dispatch(), 0x10) ? 1 : 0); // trip_at_transit_switch
        SetNumberInt(mysim, 0x00A84F9C, hasFlag(dispatch(), 0x1) ? 1 : 0);  // trip_idle
        if (!(flagsBefore & 0x8000) && (flags & 0x8000))
            Virtual<void>(mysim + 8, 0x48, 0xEC107DF9u, uint32_t{0});
        const auto means = [&] { return reinterpret_cast<int32_t(__thiscall*)(void*)>(addr::cSC4MySimDispatch_TripType)(dispatch()); };
        int32_t last = 0xFF;
        const struct {
            std::uintptr_t name;
            int32_t means;
        } kinds[] = {
            {0x00A84F90, 2}, // trip_on_bus
            {0x00A84F80, 0}, // trip_on_foot
            {0x00A84F70, 6}, // trip_on_subway
            {0x00A84F64, 1}, // trip_in_car
            {0x00A84F54, 3}, // trip_on_train
            {0x00A84F3C, 7}, // trip_on_elevated_train
            {0x00A84F28, 8}, // trip_on_monorail
        };
        for (const auto& kind : kinds) {
            const bool now = means() == kind.means;
            if (now)
                last = kind.means;
            SetNumberInt(mysim, kind.name, now ? 1 : 0);
        }
        SetNumberInt(mysim, 0x00A84F18, last); // last_trip_type
        changed = false;
    }

    void* const traffic = Global<void*>(addr::SC4_Service43DA8);
    struct {
        void* object;
        float x;
        float z;
        int32_t cellX;
        int32_t cellZ;
        uint32_t rest[5];
    } place;
    static_assert(sizeof(place) == 0x28);
    reinterpret_cast<void(__thiscall*)(void*, const void*)>(addr::SC4MySimPlace_Copy)(&place, dispatch() + 0x5C);
    if (forced)
        reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t)>(addr::SC4MySimPlace_Clear)(&place, 0, 0);
    void* const crime = Global<void*>(addr::SC4_Service43D70);
    void* const pollution = Global<void*>(addr::SC4_PollutionSimulator);
    void* const grades = Global<void*>(addr::SC4_Service43DA4);
    const int32_t half = At<int32_t>(mysim, 0x240) / 2;
    int32_t rect[4] = {place.cellX - half, place.cellZ - half, half + place.cellX, half + place.cellZ};
    if (!traffic || !pollution || !crime) {
        Release(place.object);
        return;
    }
    const auto state = [&] { return reinterpret_cast<int32_t(__thiscall*)(void*)>(addr::cSC4MySimDispatch_State)(dispatch()); };
    const auto hasFlag = reinterpret_cast<HasFlagFn>(addr::cSC4MySimDispatch_HasFlag);
    // A grid's value at the place: its slot 19 maps the position to a cell,
    // slot 22 reads it.
    const auto cell = [&](void* grid, auto read) {
        int32_t gx = 0;
        int32_t gz = 0;
        Virtual<void>(grid, 0x4C, place.x, place.z, &gx, &gz);
        return read(grid, gx, gz);
    };
    if (state() == 2 || forced) {
        if (!hasFlag(dispatch(), 1) || forced) {
            SetNumberInt(mysim, 0x00A84F00, cell(Virtual<void*>(traffic, 0x28), [](void* g, int32_t gx, int32_t gz) {
                             return static_cast<int32_t>(Virtual<uint8_t>(g, 0x58, gx, gz));
                         })); // trip_traffic_congestion
            const float one = 1.0f;
            const int32_t means = reinterpret_cast<int32_t(__thiscall*)(void*)>(addr::cSC4MySimDispatch_TripType)(dispatch());
            double ratio = one;
            if (means != 0xFE && means != 0xFF) {
                using RatioFn = double(__thiscall*)(void*, int32_t, int32_t, int32_t);
                ratio = reinterpret_cast<RatioFn>(At<void**>(traffic, 0)[0x60 / 4])(traffic, place.cellX, place.cellZ, means);
            }
            SetNumberInt(mysim, 0x00A84EF0, Truncate(ratio)); // trip_time_ratio
            SetNumberInt(mysim, 0x00A84EDC,
                         reinterpret_cast<int32_t(__thiscall*)(void*)>(addr::cSC4MySimDispatch_WaitLength)(dispatch()));
        }
        const std::uintptr_t pollutionNames[] = {0x00A84EC8, 0x00A84EB0, 0x00A84E98};
        for (uint32_t kind = 0; kind < 3; ++kind)
            SetNumberInt(mysim, pollutionNames[kind],
                         cell(Virtual<void*>(pollution, 0x14, kind), [](void* g, int32_t gx, int32_t gz) {
                             return static_cast<int32_t>(Virtual<int16_t>(g, 0x58, gx, gz));
                         }));
        SetNumberInt(mysim, 0x00A84E8C, cell(Virtual<void*>(crime, 0x14), [](void* g, int32_t gx, int32_t gz) {
                         return static_cast<int32_t>(Virtual<uint8_t>(g, 0x58, gx, gz));
                     })); // trip_crime
        SetNumberInt(mysim, 0x00A84E78, cell(Virtual<void*>(Global<void*>(addr::SC4_Service43D60), 0x30),
                                             [](void* g, int32_t gx, int32_t gz) {
                         return static_cast<int32_t>(Virtual<int8_t>(g, 0x58, gx, gz));
                     })); // trip_mayor_rating
        const auto reset = reinterpret_cast<void**(__thiscall*)(void**)>(addr::SC4RefPtr_Reset);
        using FloatAtFn = double(__thiscall*)(void*, int32_t, int32_t);
        void* eq = nullptr;
        Virtual<void>(grades, 0x54, reset(&eq), uint32_t{0}, uint32_t{0});
        SetNumberInt(mysim, 0x00A84E70, Truncate(cell(eq, [](void* g, int32_t gx, int32_t gz) {
                         return reinterpret_cast<FloatAtFn>(At<void**>(g, 0)[0x58 / 4])(g, gx, gz);
                     }))); // trip_eq
        void* hq = nullptr;
        Virtual<void>(grades, 0x58, reset(&hq), uint32_t{0}, uint32_t{0});
        SetNumberInt(mysim, 0x00A84E68, Truncate(cell(hq, [](void* g, int32_t gx, int32_t gz) {
                         return reinterpret_cast<FloatAtFn>(At<void**>(g, 0)[0x58 / 4])(g, gx, gz);
                     }))); // trip_hq
        Release(hq);
        Release(eq);
    }

    void* finder = nullptr;
    {
        void* const service = Global<void*>(addr::SC4_Service43DB0);
        const auto reset = reinterpret_cast<void**(__thiscall*)(void**)>(addr::SC4RefPtr_Reset);
        Virtual<uint8_t>(service, 0x0, 0x681905BDu, reset(&finder));
    }
    int32_t means = 0;
    if (forced || state() == 3 ||
        (means = reinterpret_cast<int32_t(__thiscall*)(void*)>(addr::cSC4MySimDispatch_TripType)(dispatch())) == 0 ||
        reinterpret_cast<int32_t(__thiscall*)(void*)>(addr::cSC4MySimDispatch_TripType)(dispatch()) == 1 ||
        hasFlag(dispatch(), 1)) {
        void* list = nullptr;
        uint8_t allocator = changed ? 1 : 0;
        reinterpret_cast<void*(__thiscall*)(void**, void*)>(addr::SC4RefList_Construct)(&list, &allocator);
        Virtual<void>(finder, 0x1C, &list, rect[0], rect[1], rect[2], rect[3], uint32_t{0});
        int32_t count = 0, distress1 = 0, distress2 = 0, distress3 = 0, low = 0, medium = 0, high = 0;
        void* landmark = nullptr;
        char* const head = static_cast<char*>(list);
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
                if (Virtual<uint8_t>(Virtual<void*>(building, 0xC), 0x58, uint32_t{0x150A}) && !landmark) {
                    reinterpret_cast<AssignFn>(addr::SC4RefPtr_Assign)(&landmark, building);
                }
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
        SetNumberInt(mysim, 0x00A84E48, percent(distress1)); // trip_distress1_p
        SetNumberInt(mysim, 0x00A84E34, percent(distress2)); // trip_distress2_p
        SetNumberInt(mysim, 0x00A84E20, percent(distress3)); // trip_distress3_p
        SetNumberInt(mysim, 0x00A84E0C, percent(low));       // trip_wealth_low_p
        SetNumberInt(mysim, 0x00A84DF8, percent(medium));    // trip_wealth_med_p
        SetNumberInt(mysim, 0x00A84DE4, percent(high));      // trip_wealth_high_p
        SetNumberInt(mysim, 0x00A84DCC, landmark ? 1 : 0);   // trip_landmark_nearby
        void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x14);
        int32_t funding = 0;
        if (landmark) {
            void* name = nullptr;
            if (memory)
                name = reinterpret_cast<void*(__thiscall*)(void*, void*)>(addr::SC4String_FromIGZString)(
                    memory, Virtual<void*>(landmark, 0x30));
            SetObject(mysim, 0x00A84DB8, name); // trip_landmark_name
            struct {
                uint32_t words[7];
                float funding; // +0x1C
                uint32_t rest[2];
            } item{};
            const uint32_t key = Virtual<uint32_t>(Virtual<void*>(landmark, 0xC), 0x18, 0xAA59670Cu, &item);
            Virtual<void>(Global<void*>(addr::SC4_BudgetSimulator), 0xCC, key);
            funding = Truncate(static_cast<double>(item.funding) * 100.0f);
        } else {
            void* const name = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4String_SC4String)(memory)
                                      : nullptr;
            SetObject(mysim, 0x00A84DB8, name); // trip_landmark_name
        }
        SetNumberInt(mysim, 0x00A84DA0, funding); // trip_landmark_funding_p
        void* const disasters = Global<void*>(addr::SC4_Service43D3C);
        const int32_t total = Virtual<int32_t>(disasters, 0x40);
        bool riot = false;
        bool fire = false;
        bool any = false;
        for (int32_t i = 0; i < total; ++i) {
            void* disaster = nullptr;
            int32_t dx = 0;
            int32_t dz = 0;
            const auto reset = reinterpret_cast<void**(__thiscall*)(void**)>(addr::SC4RefPtr_Reset);
            Virtual<void>(disasters, 0x44, i, reset(&disaster), &dx, &dz);
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
        SetNumberInt(mysim, 0x00A84D8C, fire ? 1 : 0);   // trip_fire_nearby
        SetNumberInt(mysim, 0x00A84D78, riot ? 1 : 0);   // trip_riot_nearby
        SetNumberInt(mysim, 0x00A84D5C, any ? 1 : 0);    // trip_any_disaster_nearby
        Release(landmark);
        reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4RefList_Clear)(&list);
        if (list)
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(list);
    }
    (void)means;
    Virtual<void>(Script(mysim), 0x1C, top);
    if (changed)
        reinterpret_cast<PostFn>(addr::SC4_PostMessage)(kBalloonMessage, 0, 0, 0, 0, iface);
    Release(finder);
    Release(place.object);
}

// --- The agent and advisor interfaces ------------------------------------------------------
//
// From the object's start (+0 interface) or its +8 part (advisor).

uint8_t __fastcall QueryInterface(char* mysim, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid == 0x0A1DA42F) {
        *out = mysim ? mysim + 0xB8 : nullptr;
        Virtual<uint32_t>(mysim, 0x4);
        return 1;
    }
    if (iid == 0x8BC01FFE) {
        *out = mysim;
        Virtual<uint32_t>(mysim, 0x4);
        return 1;
    }
    return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::SC4AgentBase_QueryInterface)(mysim,
                                                                                                             iid, out);
}

uint8_t __fastcall CheckBalloonTarget(char* mysim, void*)
{
    const auto timeout = reinterpret_cast<ThisFn>(addr::cSC4MySim_OnBalloonTimeout);
    char* const dispatch = At<char*>(mysim, 0x20C);
    if (At<uint8_t>(mysim, 0x244) && !At<uint8_t>(mysim, 0x245) && dispatch) {
        if (At<void*>(mysim, 0x248) &&
            reinterpret_cast<int32_t(__thiscall*)(void*)>(addr::cSC4MySimDispatch_State)(dispatch) == 2) {
            Place here;
            reinterpret_cast<PlaceCopyFn>(addr::SC4MySimPlace_Copy)(&here, At<char*>(mysim, 0x20C) + 0xAC);
            if (!reinterpret_cast<AtPlaceFn>(addr::cSC4MySim_AtBalloonPlace)(mysim, &here) ||
                !reinterpret_cast<uint8_t*>(&here)[0x1C])
                timeout(mysim);
            Release(here.object);
            At<uint8_t>(mysim, 0x244) = 0;
            return 1;
        }
        timeout(mysim);
    }
    At<uint8_t>(mysim, 0x244) = 0;
    return 1;
}

void* __fastcall GetBalloonTarget(char* mysim, void*)
{
    return At<void*>(mysim, 0x248);
}

void __fastcall SetDispatchFlag(char* mysim, void*, uint8_t value)
{
    At<uint8_t>(At<char*>(mysim, 0x20C), 0x10) = value;
}

uint32_t __fastcall IsAboveWater(char*, void*, float x, float z)
{
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    if (!terrain)
        return 0;
    using HeightFn = double(__thiscall*)(void*, float, float);
    using LevelFn = double(__thiscall*)(void*);
    const auto height = static_cast<float>(reinterpret_cast<HeightFn>(At<void**>(terrain, 0)[0x34 / 4])(terrain, x, z));
    const double water = reinterpret_cast<LevelFn>(At<void**>(terrain, 0)[0x74 / 4])(terrain);
    return water < height ? 1 : 0;
}

int32_t __fastcall GetTripTime(char* mysim, void*)
{
    if (char* const dispatch = At<char*>(mysim, 0x20C))
        return At<int32_t>(dispatch, 0x3C);
    return At<int32_t>(mysim, 0x150);
}

// Birthdays, the facts, and the last six months of education and health
// (delta_eq and delta_hq: now against six months ago).
void __fastcall MonthPassed(char* mysim, void*)
{
    bool skip = true;
    if (At<int32_t>(mysim, 0xC0)) {
        void* service = nullptr;
        if (void* const framework = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetFramework)())
            Virtual<void>(framework, 0x14, 0xA417445Eu, 0x5A4u, &service);
        skip = Virtual<uint8_t>(service, 0xA8) != 0;
        Release(service);
    }
    if (skip)
        return;
    if (At<int32_t>(mysim, 0x58) % 12 == At<uint8_t>(mysim, 0x129))
        ++At<uint8_t>(mysim, 0x128);
    if (At<void*>(mysim, 0xE0) || At<void*>(mysim, 0xE4))
        reinterpret_cast<ThisFn>(addr::cSC4MySim_RefreshLocation)(mysim);
    reinterpret_cast<ThisFn>(addr::cSC4MySim_UpdateHomeAndJob)(mysim);
    const auto history = [&](std::size_t index, std::size_t now, std::size_t ring, std::size_t delta) {
        uint8_t& i = At<uint8_t>(mysim, index);
        const std::size_t at = ring + std::size_t{i} * 4;
        At<float>(mysim, delta) =
            static_cast<float>(static_cast<double>(At<float>(mysim, now)) - static_cast<double>(At<float>(mysim, at)));
        At<uint32_t>(mysim, at) = At<uint32_t>(mysim, now);
        i = static_cast<uint8_t>(i + 1);
        if (i == 6)
            i = 0;
    };
    history(0x1D8, 0x17C, 0x1A8, 0x1DC); // education
    history(0x1D9, 0x180, 0x1C0, 0x1E0); // health
    Virtual<void>(mysim + 0xB8, 0x110); // ActivateLuaContext
    reinterpret_cast<ThisFn>(addr::SC4AgentBase_MonthPassed)(mysim);
}

// The name, from the second generation with a suffix (localized for
// generations up to 10, else "name n"); made once and kept at +0x170.
void* __fastcall GetName(char* advisor, void*)
{
    if (At<uint16_t>(advisor, 0x108) <= 1)
        return advisor + 0x30;
    if (At<void*>(advisor, 0x170))
        return At<void*>(advisor, 0x170);
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x14);
    void* const text = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4String_SC4String)(memory) : nullptr;
    void* const old = At<void*>(advisor, 0x170);
    if (text != old) {
        if (text)
            Virtual<uint32_t>(text, 0x4);
        At<void*>(advisor, 0x170) = text;
        Release(old);
    }
    const uint16_t generation = At<uint16_t>(advisor, 0x108);
    if (generation <= 10) {
        struct {
            std::uintptr_t vtable;
            char* begin;
            char* end;
            char* capacity;
            uint32_t rest[4];
        } suffix{};
        reinterpret_cast<void*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(addr::SC4LocalizedString_SC4LocalizedString)(
            &suffix, 0xEA231E96, 0x6A56248Eu + generation, 1);
        const bool found = suffix.end != suffix.begin;
        if (found) {
            char* const name = At<char*>(advisor, 0x170);
            reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::cRZBaseString_Assign)(name, advisor + 0x30);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4String_PushBack)(At<char*>(advisor, 0x170) + 4,
                                                                                          ' ');
            uint8_t allocator = 0;
            reinterpret_cast<void(__thiscall*)(void*, char*, char*, void*)>(addr::SC4String_AppendRange)(
                At<char*>(advisor, 0x170) + 4, suffix.begin, suffix.end, &allocator);
        }
        suffix.vtable = addr::SC4String_Vtable;
        if (suffix.begin)
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(suffix.begin);
        if (found)
            return At<void*>(advisor, 0x170);
    }
    void* const name = At<void*>(advisor, 0x170);
    const auto format = reinterpret_cast<void(__cdecl*)(void*, const char*, const char*, uint32_t)>(
        At<void**>(name, 0)[0x6C / 4]);
    format(name, reinterpret_cast<const char*>(addr::SC4MySim_GenerationFormat), At<const char*>(advisor, 0x34),
           uint32_t{generation});
    return At<void*>(advisor, 0x170);
}

// The balloon target (+0x248): made once and set up (its name from the
// path service plus the MySim's text at +0x134, the key from its slot 3's
// slot 9, the MySim itself); then the active tool becomes the dispatch tool.
void* __fastcall MakeBalloonTarget(char* mysim, void*)
{
    if (!At<void*>(mysim, 0x248)) {
        void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x1CC);
        void* const made = memory ? reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(
                                        addr::SC4MySimBalloonTarget_Construct)(memory, 1)
                                  : nullptr;
        void* const old = At<void*>(mysim, 0x248);
        if (made != old) {
            if (made)
                Virtual<uint32_t>(made, 0x4);
            At<void*>(mysim, 0x248) = made;
            Release(old);
        }
        Virtual<void>(At<void*>(mysim, 0x248), 0x10);
        Virtual<void>(At<void*>(mysim, 0x248), 0x2C, uint32_t{0});
        Virtual<void>(At<void*>(mysim, 0x248), 0x24, uint32_t{4});
        Virtual<void>(At<void*>(mysim, 0x248), 0x18, uint32_t{1});
        uint32_t key[3] = {};
        Virtual<void>(Virtual<void*>(At<void*>(mysim, 0x248), 0xC), 0x24, key);
        ScriptString text{};
        char* const buffer = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8));
        text.begin = buffer;
        text.end = buffer;
        text.capacity = buffer + 8;
        *buffer = 0;
        text.vtable = addr::SC4String_Vtable;
        text.references = 0;
        Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0xF4, &text);
        uint8_t allocator = 0;
        reinterpret_cast<void(__thiscall*)(void*, char*, char*, void*)>(addr::SC4String_AppendRange)(
            &text.begin, At<char*>(mysim, 0x134), At<char*>(mysim, 0x138), &allocator);
        Virtual<void>(At<void*>(mysim, 0x248), 0x20, At<uint32_t>(mysim, 0x12C), &text);
        void* self = nullptr;
        Virtual<uint8_t>(mysim, 0x0, uint32_t{1}, &self);
        Virtual<void>(At<void*>(mysim, 0x248), 0x4C, self);
        At<uint8_t>(mysim, 0x245) = 0;
        Release(self);
        text.vtable = addr::SC4String_Vtable;
        if (text.begin)
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(text.begin);
    }
    At<uint8_t>(mysim, 0x244) = 1;
    void* const tool = Virtual<void*>(Global<void*>(addr::SC4_Service43DC4), 0x60);
    void*& current = Global<void*>(addr::SC4_DispatchTool);
    void* const old = current;
    if (tool != old) {
        if (tool)
            Virtual<uint32_t>(tool, 0x4);
        current = tool;
        Release(old);
    }
    return At<void*>(mysim, 0x248);
}

// With `follow`, the MySim heads for the balloon target's place (its
// building's lot when there is one); arriving there is state 6. Without,
// the target lets go of it.
uint8_t __fastcall FollowBalloonTarget(char* mysim, void*, uint8_t follow)
{
    if (At<void*>(mysim, 0x20C) && follow) {
        void* target = Virtual<void*>(At<void*>(mysim, 0x248), 0xC);
        if (target)
            Virtual<uint32_t>(target, 0x4);
        Place there;
        reinterpret_cast<ThisFn>(addr::SC4MySimPlace_Construct)(&there);
        const auto setOccupant = reinterpret_cast<void(__thiscall*)(Place*, void*)>(addr::SC4MySimPlace_SetOccupant);
        setOccupant(&there, target);
        if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, there.words[2], there.words[3],
                                             uint32_t{0}))
            if (void* const building = Virtual<void*>(lot, 0xC0)) {
                reinterpret_cast<AssignFn>(addr::SC4RefPtr_Assign)(&target, Virtual<void*>(building, 0xC));
                Virtual<void>(At<void*>(mysim, 0x248), 0x44, target);
                setOccupant(&there, target);
            }
        Place here;
        reinterpret_cast<PlaceCopyFn>(addr::SC4MySimPlace_Copy)(&here, At<char*>(mysim, 0x20C) + 0xAC);
        const auto* const hereBytes = reinterpret_cast<const uint8_t*>(&here);
        const auto* const thereBytes = reinterpret_cast<const uint8_t*>(&there);
        bool go = false;
        if (hereBytes[0x1C] == thereBytes[0x1C]) {
            using DifferFn = uint8_t(__cdecl*)(const void*, const void*);
            if (reinterpret_cast<DifferFn>(addr::SC4_CellPairsDiffer)(&here.words[2], &there.words[2]))
                go = true;
            else if (there.object != here.object && here.object)
                go = there.object != nullptr;
        }
        if (go) {
            void* const balloon = At<void*>(mysim, 0x248);
            if (reinterpret_cast<uint8_t(__thiscall*)(void*, Place*)>(addr::cSC4MySimDispatch_GoTo)(At<void*>(mysim, 0x20C),
                                                                                                   &there)) {
                Virtual<void>(balloon, 0x2C, uint32_t{3});
                reinterpret_cast<PostFn>(addr::SC4_PostMessage)(kBalloonMessage, 0, 0, 0, 0, mysim + 0xB8);
                At<int32_t>(mysim, 0xC0) = 6;
                At<int32_t>(mysim, 0x298) = At<int32_t>(mysim, 0x5C);
            } else {
                Virtual<void>(balloon, 0x2C, uint32_t{1});
            }
        }
        Release(here.object);
        Release(there.object);
        Release(target);
    }
    At<uint8_t>(mysim, 0x245) = follow;
    if (!follow)
        Virtual<void>(At<void*>(mysim, 0x248), 0x44, nullptr);
    return 1;
}

// The +8 part's Init: the message stopwatch, the base Init, the script
// context (from the city), three strings, and for a MySim that has moved in
// the dispatch object (made if missing), the home's lot, the job's
// occupant, the first trip; then the advice and tuning.
uint8_t __fastcall Init(char* advisor, void*, void* argument)
{
    char* const mysim = advisor - 8;
    sim_timers::Claim(advisor + 0x278);
    reinterpret_cast<ThisFn>(addr::GZStopwatch_Start)(advisor + 0x278);
    if (!reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::SC4AdvisorBase_Init)(advisor, argument))
        return 0;
    const auto hold = [&](std::size_t at, void* object) {
        void* const old = At<void*>(advisor, at);
        if (object == old)
            return;
        if (object)
            Virtual<uint32_t>(object, 0x4);
        At<void*>(advisor, at) = object;
        Release(old);
    };
    hold(0x1E8, Virtual<void*>(Virtual<void*>(Global<void*>(addr::SC4_City), 0x144), 0x28));
    const auto newString = [&] {
        void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x14);
        return memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4String_SC4String)(memory) : nullptr;
    };
    if (!At<void*>(advisor, 0x1EC))
        hold(0x1EC, newString());
    if (!At<void*>(advisor, 0x1F0)) {
        hold(0x1F0, newString());
        LoadText(At<void*>(advisor, 0x1F0), 0xEA231E96, 0xAA665D3E, 0);
    }
    if (!At<void*>(advisor, 0x1F4)) {
        hold(0x1F4, newString());
        LoadText(At<void*>(advisor, 0x1F4), 0xEA231E96, 0x0A665D4C, 0);
    }
    if (At<int32_t>(advisor, 0xB8)) {
        const bool made = !At<void*>(advisor, 0x204);
        if (made) {
            void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x18C);
            void* const dispatch = memory ? reinterpret_cast<void*(__thiscall*)(void*, void*)>(
                                                addr::cSC4MySimDispatch_cSC4MySimDispatch)(memory, mysim)
                                          : nullptr;
            reinterpret_cast<AssignFn>(addr::SC4RefPtr_Assign)(advisor + 0x204, dispatch);
            reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Reset)(At<void*>(advisor, 0x204));
        }
        if (void* const residence = At<void*>(advisor, 0xBC))
            reinterpret_cast<AssignFn>(addr::SC4RefPtr_Assign)(
                advisor + 0xD4,
                Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, Virtual<void*>(residence, 0xC)));
        if (!At<void*>(advisor, 0xD8) && (At<int32_t>(advisor, 0xE0) || At<int32_t>(advisor, 0xE4))) {
            void* const cells = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x8C);
            if (void* const occupant =
                    Virtual<void*>(cells, 0x2C, At<int32_t>(advisor, 0xE0), At<int32_t>(advisor, 0xE4)))
                reinterpret_cast<AssignFn>(addr::SC4RefPtr_AssignFrom)(advisor + 0xDC, occupant);
            At<uint8_t>(advisor, 0x158) = 1;
        } else if (void* const pending = At<void*>(advisor, 0xDC)) {
            At<void*>(advisor, 0xDC) = nullptr;
            Release(pending);
        }
        if (made) {
            Place home;
            reinterpret_cast<ThisFn>(addr::SC4MySimPlace_Construct)(&home);
            if (void* const residence = At<void*>(advisor, 0xBC)) {
                reinterpret_cast<void(__thiscall*)(Place*, void*)>(addr::SC4MySimPlace_SetOccupant)(
                    &home, Virtual<void*>(residence, 0xC));
                reinterpret_cast<void(__thiscall*)(void*, Place*)>(addr::cSC4MySimDispatch_SetHome)(
                    At<void*>(advisor, 0x204), &home);
            }
            if (void* const job = At<void*>(advisor, 0xD8))
                Virtual<void>(advisor + 0xB0, 0x20, job); // ChangeJob
            Release(home.object);
        }
        reinterpret_cast<ThisFn>(addr::cSC4MySim_UpdateHomeAndJob)(mysim);
        Virtual<void>(advisor + 0xB0, 0x110); // ActivateLuaContext
    }
    reinterpret_cast<ThisFn>(addr::cSC4MySim_LoadAdvice)(mysim);
    reinterpret_cast<ThisFn>(addr::cSC4MySim_LoadTuning)(mysim);
    return 1;
}

uint32_t __fastcall Shutdown(char* advisor, void*)
{
    char* const mysim = advisor - 8;
    const auto drop = [&](std::size_t at) {
        if (void* const object = At<void*>(advisor, at)) {
            At<void*>(advisor, at) = nullptr;
            Release(object);
        }
    };
    if (At<uint8_t>(advisor, 0x1DC)) {
        void* table = nullptr;
        if (void* const framework = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetFramework)())
            Virtual<void>(framework, 0x14, 0xEBE0E860u, 0xE898ED03u, &table);
        reinterpret_cast<void(__cdecl*)(void*, void*)>(addr::SC4Script_UnregisterTable)(mysim, table);
        reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4_ReleaseRefHolder)(&table);
        void* const target = reinterpret_cast<std::uintptr_t>(mysim) ? advisor + 8 : nullptr;
        if (void* const frames = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
            Virtual<void>(frames, 0x18, target, uint32_t{0xFF});
        reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Shutdown)(At<void*>(advisor, 0x204));
        drop(0x204);
        reinterpret_cast<ThisFn>(addr::cSC4MySim_DropTripObject)(At<void*>(advisor, 0x230));
        drop(0x230);
        void* const server = Global<void*>(addr::SC4_MessageServer);
        for (const uint32_t type : {0x0B6F3E27u, 0xAB6F3E2Du, 0x4B6F3EBFu, 0x0BB04126u, 0x4BC42C48u, 0x26D31EC1u})
            Virtual<void>(server, 0x18, target, type);
        At<uint8_t>(advisor, 0x1DC) = 0;
    }
    for (const std::size_t at : {0x1ECu, 0x1F0u, 0x1F4u, 0xBCu, 0xD4u, 0xD8u, 0xDCu})
        drop(at);
    const bool view = At<void*>(advisor, 0x13C) != nullptr;
    At<uint32_t>(advisor, 0xE0) = 0;
    At<uint32_t>(advisor, 0xE4) = 0;
    if (view) {
        Virtual<void>(advisor + 0xB0, 0x84); // HideResidence
        drop(0x13C);
    }
    if (void* const pylon = At<void*>(advisor, 0xF0)) {
        Virtual<void>(pylon, 0x34, uint32_t{0});
        drop(0xF0);
    }
    for (const std::size_t at : {0x160u, 0x164u, 0x168u, 0x16Cu, 0x170u})
        drop(at);
    reinterpret_cast<ThisFn>(addr::cSC4MySim_DropAdvice)(mysim);
    if (const uint32_t table = At<uint32_t>(advisor, 0x15C)) {
        Virtual<void>(At<void*>(advisor, 0x1E8), 0xF8, table);
        At<uint32_t>(advisor, 0x15C) = 0;
    }
    if (char* const map = At<char*>(advisor, 0x1E0)) {
        if (At<uint32_t>(map, 4)) {
            reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4Uint32Map_EraseSubtree)(
                map, At<void*>(At<char*>(map, 0), 4));
            At<char*>(At<char*>(map, 0), 8) = At<char*>(map, 0);
            At<void*>(At<char*>(map, 0), 4) = nullptr;
            At<char*>(At<char*>(map, 0), 0xC) = At<char*>(map, 0);
            At<uint32_t>(map, 4) = 0;
        }
        if (void* const head = At<void*>(map, 0))
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(head);
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(map);
        At<void*>(advisor, 0x1E0) = nullptr;
    }
    for (const std::size_t at : {0x17Cu, 0x180u, 0x1ECu, 0x1F4u, 0x1F0u, 0x1E8u, 0x184u})
        drop(at);
    if (void* const tool = Global<void*>(addr::SC4_DispatchTool)) {
        Global<void*>(addr::SC4_DispatchTool) = nullptr;
        Release(tool);
    }
    At<int32_t>(advisor, 0xB8) = 0;
    if (void* const balloon = At<void*>(mysim, 0x248)) {
        Virtual<void>(balloon, 0x14);
        if (void* const again = At<void*>(mysim, 0x248)) {
            At<void*>(mysim, 0x248) = nullptr;
            Release(again);
        }
        At<uint8_t>(mysim, 0x245) = 0;
    }
    At<uint8_t>(advisor, 0x23D) = 0;
    At<uint8_t>(advisor, 0x23C) = 0;
    return reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::SC4AdvisorBase_Shutdown)(advisor);
}

// For each queued event (the list at +0xB0; its count taken first, the
// first node read each time), the advice entries the map at +0x220 keys by
// the event's ID (an ordered map: key +0x10, entry index +0x14) are told of
// it; those that become due are reset and queued (the list at +0x230).
uint32_t __fastcall ProcessAdviceEvents(char* mysim, void*)
{
    char* const events = At<char*>(mysim, 0xB0);
    uint32_t count = 0;
    for (char* node = At<char*>(events, 0); node != events; node = At<char*>(node, 0))
        ++count;
    for (; count; --count) {
        char* const event = At<char*>(At<char*>(mysim, 0xB0), 0) + 8;
        const int32_t id = At<int32_t>(event, 0);
        char* const head = At<char*>(mysim, 0x220);
        char* low = head;
        for (char* x = At<char*>(head, 4); x;) {
            if (At<int32_t>(x, 0x10) < id) {
                x = At<char*>(x, 0xC);
            } else {
                low = x;
                x = At<char*>(x, 8);
            }
        }
        char* high = head;
        for (char* x = At<char*>(head, 4); x;) {
            if (id >= At<int32_t>(x, 0x10)) {
                x = At<char*>(x, 0xC);
            } else {
                high = x;
                x = At<char*>(x, 8);
            }
        }
        for (char* node = low; node != high;) {
            const int32_t index = At<int32_t>(node, 0x14);
            char* const entry = At<char*>(mysim, 0x214) + index * 12;
            void* const advice = At<void*>(entry, 0);
            void* const data = Virtual<void*>(advice, 0x10);
            if (Virtual<uint8_t>(At<void*>(entry, 0), 0x38, uint32_t{1})) {
                if (Virtual<uint8_t>(At<void*>(entry, 0), 0x30)) {
                    Virtual<void>(data, 0x60, At<uint32_t>(event, 4));
                    Virtual<void>(data, 0x38, uint32_t{0x10}, uint32_t{1});
                }
            } else if (Virtual<uint8_t>(At<void*>(entry, 0), 0x30)) {
                Virtual<void>(data, 0x60, At<uint32_t>(event, 4));
                Virtual<void>(data, 0x38, uint32_t{0x10}, uint32_t{1});
                const auto entries =
                    static_cast<int32_t>((At<char*>(mysim, 0x218) - At<char*>(mysim, 0x214)) / 12);
                if (At<uint8_t>(mysim, 0x210) && index >= 0 && static_cast<uint32_t>(index) < static_cast<uint32_t>(entries)) {
                    char* const due = At<char*>(mysim, 0x214) + index * 12;
                    At<uint32_t>(due, 8) = At<uint32_t>(mysim, 0x200);
                    At<uint32_t>(due, 4) = 0;
                    Virtual<void>(At<void*>(due, 0), 0x34, uint32_t{1}, uint32_t{1});
                    char* const queue = At<char*>(mysim, 0x230);
                    char* const added =
                        static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(0xC));
                    At<int32_t>(added, 8) = index;
                    char* const last = At<char*>(queue, 4);
                    At<char*>(added, 0) = queue;
                    At<char*>(added, 4) = last;
                    At<char*>(last, 0) = added;
                    At<char*>(queue, 4) = added;
                }
            }
            // The in-order successor.
            if (char* right = At<char*>(node, 0xC)) {
                while (At<char*>(right, 8))
                    right = At<char*>(right, 8);
                node = right;
            } else {
                char* parent = At<char*>(node, 4);
                while (node == At<char*>(parent, 0xC)) {
                    node = parent;
                    parent = At<char*>(parent, 4);
                }
                if (At<char*>(node, 0xC) != parent)
                    node = parent;
            }
        }
    }
    return reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::SC4AgentBase_ProcessEvents)(mysim);
}

uint8_t __fastcall AdvisorRun(char* advisor, void*, uint32_t argument)
{
    if (!At<int32_t>(advisor, 0xB8))
        return 0;
    Virtual<void>(advisor + 0xB0, 0x110); // ActivateLuaContext
    return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t)>(addr::SC4AdvisorBase_Run)(advisor, argument);
}

// --- The panel and the residence view ---------------------------------------------------

void __fastcall UpdateResidence(char* self, void*)
{
    if (!At<int32_t>(self, field::kState) || !At<void*>(self, field::kResidence) || !At<void*>(self, field::kResidenceView))
        return;
    int32_t point[3];
    void* const residence = At<void*>(self, field::kResidence);
    int32_t* const second = &point[2];
    int32_t* const first = &point[0];
    const std::uintptr_t fn = addr::cSC4MySim_ResidenceScreenPoint;
    __asm {
        push edi
        mov ecx, residence
        mov edi, second
        push first
        call dword ptr [fn]
        add esp, 4
        pop edi
    }
    point[1] = point[2];
    Virtual<void>(At<void*>(self, field::kResidenceView), 0x3C, point);
}

// The nearest building (from the city service's list slot) that is of
// `kind`, and its grade (the service's grade slot); the default funding
// value when there is none.
void* Grade(char* self, void* out, uint32_t kind, std::size_t listSlot, std::size_t gradeSlot)
{
    using ListFn = void(__thiscall*)(void* list);
    void* const grades = Global<void*>(addr::SC4_Service43DA4);
    void** const head = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    head[0] = head;
    head[1] = head;
    struct {
        void* head;
        uint32_t size;
    } list{head, 0};
    uint32_t result = Global<uint32_t>(addr::SC4_DefaultFunding);
    uint32_t best = 0xFFFFFFFF;
    Virtual<void>(grades, listSlot, &list, uint32_t{0});
    for (char* node = static_cast<char*>(head[0]); node != list.head; node = At<char*>(node, 0)) {
        void* const building = At<void*>(node, 8);
        if (Virtual<uint8_t>(building, 0x58, kind)) {
            float position[3];
            Virtual<void>(building, 0x20, position);
            const double dx = static_cast<double>(At<float>(self, 0x18)) - position[0];
            const double dz = static_cast<double>(At<float>(self, 0x20)) - position[2];
            const auto distance = static_cast<uint32_t>(Truncate(std::sqrt(dz * dz + dx * dx)));
            if (distance < best) {
                struct {
                    std::uintptr_t vtable;
                    char* begin;
                    char* end;
                    char* capacity;
                    uint32_t references;
                    uint32_t words[5];
                    uint32_t grade; // +0x28
                    uint32_t more[5];
                } info{};
                char* const buffer =
                    static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8));
                info.begin = buffer;
                info.end = buffer;
                info.capacity = buffer + 8;
                *buffer = 0;
                info.vtable = addr::SC4String_Vtable;
                Virtual<void>(grades, gradeSlot, building, &info);
                result = info.grade;
                best = distance;
                info.vtable = addr::SC4String_Vtable;
                if (info.begin)
                    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(info.begin);
            }
        }
        Virtual<uint32_t>(building, 0x8);
    }
    reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&list);
    *static_cast<uint32_t*>(out) = result;
    reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&list);
    Virtual<void>(ListNodeAllocator(), 0x10, list.head);
    return out;
}

// A model from an exemplar (or the default one, from the resource
// manager): its key (property 0x09C80416) into `keyAt`, its button
// (0xEBFC5E5E) into `buttonAt`; returns the exemplar holder to release.
void* LoadModel(char* self, void* exemplar, std::uintptr_t defaultKey, bool pedestrian, std::size_t keyAt,
                std::size_t buttonAt, uint32_t* key)
{
    key[0] = Global<uint32_t>(defaultKey);
    key[1] = Global<uint32_t>(defaultKey + 4);
    key[2] = Global<uint32_t>(defaultKey + 8);
    void* holder = nullptr;
    if (!exemplar) {
        const void* const at = pedestrian ? static_cast<const void*>(key) : reinterpret_cast<const void*>(defaultKey);
        Virtual<void>(Global<void*>(addr::SC4_ResourceManager), 0xC, at, 0xA52160F5u, &holder, uint32_t{0},
                      uint32_t{0});
        exemplar = holder;
    }
    At<uint32_t>(self, buttonAt) = 0;
    At<uint32_t>(self, keyAt) = 0;
    At<uint32_t>(self, keyAt + 4) = 0;
    At<uint32_t>(self, keyAt + 8) = 0;
    if (exemplar) {
        using KeyFn = uint8_t(__cdecl*)(void*, uint32_t, uint32_t*);
        if (reinterpret_cast<KeyFn>(addr::SC4_GetPropertyResourceKey)(Virtual<void*>(exemplar, 0x18), 0x09C80416, key)) {
            At<uint32_t>(self, keyAt) = key[0];
            At<uint32_t>(self, keyAt + 4) = key[1];
            At<uint32_t>(self, keyAt + 8) = key[2];
        }
        int32_t button = 0;
        using IntFn = uint8_t(__cdecl*)(void*, uint32_t, int32_t*);
        if (reinterpret_cast<IntFn>(addr::SC4_GetPropertySint32)(Virtual<void*>(exemplar, 0x18), 0xEBFC5E5E, &button))
            At<int32_t>(self, buttonAt) = button;
    }
    return holder;
}

uint8_t __fastcall SetCar(char* self, void*, void* exemplar)
{
    uint32_t key[3];
    void* const holder = LoadModel(self, exemplar, addr::SC4MySim_DefaultCarKey, false, 0x194, 0x150, key);
    reinterpret_cast<void(__thiscall*)(void*, uint32_t, void*, uint32_t)>(addr::cSC4MySimDispatch_SetModel)(
        At<void*>(self, field::kDispatch), 0, self + 0x194, 0);
    Release(holder);
    return 1;
}

uint8_t __fastcall SetPedestrian(char* self, void*, void* exemplar)
{
    uint32_t key[3];
    void* const holder = LoadModel(self, exemplar, addr::SC4MySim_DefaultPedestrianKeyData, true, 0x1A0, 0x14C, key);
    uint32_t made[3];
    const auto* const other = reinterpret_cast<const uint32_t*(__thiscall*)(void*, uint32_t*)>(
        addr::cSC4MySim_DefaultPedestrianKey)(Object(self), made);
    key[0] = other[0];
    key[1] = other[1];
    key[2] = other[2];
    if (!At<uint32_t>(self, 0x1A8)) {
        At<uint32_t>(self, 0x1A0) = key[0];
        At<uint32_t>(self, 0x1A4) = key[1];
        At<uint32_t>(self, 0x1A8) = key[2];
    }
    const auto setModel = reinterpret_cast<void(__thiscall*)(void*, uint32_t, void*, uint32_t)>(
        addr::cSC4MySimDispatch_SetModel);
    setModel(At<void*>(self, field::kDispatch), 1, self + 0x1A0, 0);
    setModel(At<void*>(self, field::kDispatch), 2, key, 0);
    Release(holder);
    return 1;
}

void* __fastcall LocalSchoolGrade(char* self, void*, void* out, uint32_t kind)
{
    return Grade(self, out, kind, 0xBC, 0x68);
}

void* __fastcall LocalHospitalGrade(char* self, void*, void* out, uint32_t kind)
{
    return Grade(self, out, kind, 0xC4, 0x6C);
}

// --- The object ---------------------------------------------------------------------------

void* NewList()
{
    void** const head =
        static_cast<void**>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(0xC));
    head[0] = head;
    head[1] = head;
    return head;
}

void EmptyString(char* object, std::size_t at)
{
    At<char*>(object, at + 4) = nullptr;
    At<char*>(object, at + 8) = nullptr;
    At<char*>(object, at + 0xC) = nullptr;
    char* const buffer = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8));
    At<char*>(object, at + 4) = buffer;
    At<char*>(object, at + 8) = buffer;
    At<char*>(object, at + 0xC) = buffer + 8;
    *buffer = 0;
}

char* __fastcall Construct(char* mysim, void*)
{
    reinterpret_cast<ThisFn>(addr::SC4AgentBase_Construct)(mysim);
    At<std::uintptr_t>(mysim, 0xB8) = 0x00A84C10;
    At<std::uintptr_t>(mysim, 0x0) = 0x00A85218;
    At<std::uintptr_t>(mysim, 0x8) = 0x00A851C0;
    At<std::uintptr_t>(mysim, 0xC) = 0x00A851A4;
    At<std::uintptr_t>(mysim, 0x10) = 0x00A85194;
    At<std::uintptr_t>(mysim, 0xB8) = 0x00A85080;
    At<uint8_t>(mysim, 0xBC) = 0;
    for (std::size_t at = 0xC0; at <= 0xCC; at += 4)
        At<uint32_t>(mysim, at) = 0;
    for (std::size_t at = 0xDC; at <= 0x10C; at += 4)
        At<uint32_t>(mysim, at) = 0;
    At<uint16_t>(mysim, 0x110) = 1;
    At<uint8_t>(mysim, 0x112) = 0;
    At<uint8_t>(mysim, 0x113) = 1;
    EmptyString(mysim, 0x114);
    At<uint32_t>(mysim, 0x124) = 0;
    At<std::uintptr_t>(mysim, 0x114) = addr::SC4String_Vtable;
    At<uint8_t>(mysim, 0x128) = 0;
    At<uint8_t>(mysim, 0x129) = 0;
    At<uint32_t>(mysim, 0x12C) = 0;
    EmptyString(mysim, 0x130);
    At<std::uintptr_t>(mysim, 0x130) = addr::SC4String_Vtable;
    At<uint32_t>(mysim, 0x140) = 0;
    At<uint32_t>(mysim, 0x144) = 0;
    At<uint8_t>(mysim, 0x148) = 0;
    At<uint8_t>(mysim, 0x149) = 0;
    At<uint32_t>(mysim, 0x14C) = 0;
    At<uint32_t>(mysim, 0x150) = 0;
    At<uint32_t>(mysim, 0x154) = 0;
    At<uint8_t>(mysim, 0x158) = 0;
    At<uint32_t>(mysim, 0x15C) = 0;
    At<uint8_t>(mysim, 0x160) = 0;
    for (std::size_t at = 0x164; at <= 0x18C; at += 4)
        At<uint32_t>(mysim, at) = 0;
    {
        // A clock seed, counted under the original constructor's site.
        const fixed_seed::SiteOverride site(0x00429A68);
        reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(addr::SC4Random_Construct)(mysim + 0x190, 0xFFFFFFFF);
    }
    At<uint8_t>(mysim, 0x1D8) = 0;
    At<uint8_t>(mysim, 0x1D9) = 0;
    At<uint32_t>(mysim, 0x1DC) = 0;
    At<uint32_t>(mysim, 0x1E0) = 0;
    At<uint8_t>(mysim, 0x1E4) = 0;
    for (std::size_t at = 0x1E8; at <= 0x20C; at += 4)
        At<uint32_t>(mysim, at) = 0;
    At<uint8_t>(mysim, 0x210) = 0;
    At<uint32_t>(mysim, 0x214) = 0;
    At<uint32_t>(mysim, 0x218) = 0;
    At<uint32_t>(mysim, 0x21C) = 0;
    At<uint32_t>(mysim, 0x220) = 0;
    char* const node = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(0x18));
    At<char*>(mysim, 0x220) = node;
    At<uint32_t>(mysim, 0x224) = 0;
    At<uint8_t>(node, 0) = 0;
    At<void*>(node, 4) = nullptr;
    At<char*>(node, 8) = node;
    At<char*>(node, 0xC) = node;
    At<uint32_t>(mysim, 0x22C) = 0;
    At<void*>(mysim, 0x22C) = NewList();
    At<uint32_t>(mysim, 0x230) = 0;
    At<void*>(mysim, 0x230) = NewList();
    At<uint32_t>(mysim, 0x234) = 0;
    At<uint32_t>(mysim, 0x238) = 0;
    At<uint32_t>(mysim, 0x23C) = 0;
    At<int32_t>(mysim, 0x240) = 7;
    At<uint8_t>(mysim, 0x244) = 0;
    At<uint8_t>(mysim, 0x245) = 0;
    for (std::size_t at = 0x248; at <= 0x260; at += 4)
        At<uint32_t>(mysim, at) = 0;
    At<int32_t>(mysim, 0x264) = -1;
    const auto stopwatch = reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4Stopwatch_Construct);
    stopwatch(mysim + 0x268, 5);
    stopwatch(mysim + 0x280, 5);
    At<uint32_t>(mysim, 0x298) = 0;
    At<uint32_t>(mysim, 0x29C) = 0;
    stopwatch(mysim + 0x2A0, 5);
    At<uint32_t>(mysim, 0xD0) = 0;
    At<uint32_t>(mysim, 0xD4) = 0;
    At<uint32_t>(mysim, 0xD8) = 0;
    for (std::size_t at = 0x1A8; at <= 0x1D4; at += 4)
        At<uint32_t>(mysim, at) = 0;
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x18C);
    void* const dispatch =
        memory ? reinterpret_cast<void*(__thiscall*)(void*, void*)>(addr::cSC4MySimDispatch_cSC4MySimDispatch)(memory, mysim)
               : nullptr;
    void* const old = At<void*>(mysim, 0x20C);
    if (dispatch != old) {
        if (dispatch)
            Virtual<uint32_t>(dispatch, 0x4);
        At<void*>(mysim, 0x20C) = dispatch;
        Release(old);
    }
    sim_timers::Claim(mysim + 0x2A0);
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Start)(mysim + 0x2A0);
    return mysim;
}

void __fastcall Destruct(char* mysim, void*)
{
    At<std::uintptr_t>(mysim, 0x0) = 0x00A85218;
    At<std::uintptr_t>(mysim, 0x8) = 0x00A851C0;
    At<std::uintptr_t>(mysim, 0xC) = 0x00A851A4;
    At<std::uintptr_t>(mysim, 0x10) = 0x00A85194;
    At<std::uintptr_t>(mysim, 0xB8) = 0x00A85080;
    const auto stopwatch = reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Destruct);
    stopwatch(mysim + 0x2A0);
    stopwatch(mysim + 0x280);
    stopwatch(mysim + 0x268);
    Release(At<void*>(mysim, 0x248));
    Release(At<void*>(mysim, 0x238));
    reinterpret_cast<ThisFn>(addr::SC4PairList_Destroy)(mysim + 0x230);
    reinterpret_cast<ThisFn>(addr::SC4PairList_Destroy)(mysim + 0x22C);
    char*& head = At<char*>(mysim, 0x220);
    if (At<uint32_t>(mysim, 0x224)) {
        reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4Uint32Map_EraseSubtree2)(mysim + 0x220,
                                                                                            At<void*>(head, 4));
        At<char*>(head, 8) = head;
        At<void*>(head, 4) = nullptr;
        At<char*>(head, 0xC) = head;
        At<uint32_t>(mysim, 0x224) = 0;
    }
    if (head)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(head);
    reinterpret_cast<ThisFn>(addr::SC4Vector12_Destroy)(mysim + 0x214);
    for (const std::size_t at : {0x20Cu, 0x1FCu, 0x1F8u, 0x1F4u, 0x1F0u, 0x1ECu, 0x18Cu, 0x188u, 0x184u, 0x178u,
                                 0x174u, 0x170u, 0x16Cu, 0x168u, 0x14Cu, 0x144u})
        Release(At<void*>(mysim, at));
    for (const std::size_t at : {0x130u, 0x114u}) {
        At<std::uintptr_t>(mysim, at) = addr::SC4String_Vtable;
        if (void* const buffer = At<void*>(mysim, at + 4))
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(buffer);
    }
    for (const std::size_t at : {0xF8u, 0xE4u, 0xE0u, 0xDCu, 0xC4u})
        Release(At<void*>(mysim, at));
    reinterpret_cast<ThisFn>(addr::SC4AgentBase_Destruct)(mysim);
}

char* __fastcall DeletingDestructor(char* mysim, void*, uint8_t flags)
{
    reinterpret_cast<ThisFn>(addr::cSC4MySim_Destruct)(mysim);
    if (flags & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(mysim);
    return mysim;
}

void* __cdecl Create()
{
    char* const memory = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x2B8));
    if (!memory)
        return nullptr;
    char* const mysim = reinterpret_cast<char*(__thiscall*)(void*)>(addr::cSC4MySim_cSC4MySim)(memory);
    return mysim ? mysim + 0xB8 : nullptr;
}

// --- Helpers ------------------------------------------------------------------------------
//
// From the object's start. The balloons: the advice entries (+0x214, 12
// bytes: the advice, its next time, a stamp) and two queues of entry
// indices (+0x230 events, +0x22C ordinary).

int32_t AdviceCount(char* mysim)
{
    return static_cast<int32_t>((At<char*>(mysim, 0x218) - At<char*>(mysim, 0x214)) / 12);
}

bool AdviceIndex(char* mysim, int32_t index)
{
    return index >= 0 && static_cast<uint32_t>(index) < static_cast<uint32_t>(AdviceCount(mysim));
}

// A trip: at home (state 2), to work (the work place, or a default when
// it is not valid) within the working hours left; from work (3) or before
// arriving (1), home.
uint8_t __fastcall CanTravel(char* mysim, void*)
{
    if (!At<void*>(mysim, 0xC4))
        return 0;
    char* const dispatch = At<char*>(mysim, 0x20C);
    const int32_t state = At<int32_t>(mysim, 0xC0);
    if (state == 2) {
        Place work;
        reinterpret_cast<PlaceCopyFn>(addr::SC4MySimPlace_Copy)(&work, dispatch + 0xD4);
        if (!reinterpret_cast<uint8_t*>(&work)[0x1D] ||
            !reinterpret_cast<uint8_t(__thiscall*)(Place*)>(addr::SC4MySimPlace_IsValid)(&work)) {
            Place empty;
            reinterpret_cast<ThisFn>(addr::SC4MySimPlace_Construct)(&empty);
            reinterpret_cast<void(__thiscall*)(Place*, const Place*)>(addr::SC4MySimPlace_Assign)(&work, &empty);
            Release(empty.object);
            reinterpret_cast<ThisFn>(addr::SC4MySimPlace_SetWork)(&work);
        }
        const int32_t hour = Virtual<int32_t>(Global<void*>(addr::SC4_Clock43DBC), 0x28);
        int32_t window = 5;
        if (hour >= 8) {
            window = (0x11 - hour) * 0x3C;
            if (window < 0)
                window = 5;
        }
        work.words[7] = static_cast<uint32_t>(window); // +0x20
        reinterpret_cast<void(__thiscall*)(void*, Place*, uint32_t)>(addr::cSC4MySimDispatch_StartTrip)(
            At<void*>(mysim, 0x20C), &work, 4);
        Release(work.object);
        return 1;
    }
    if (state == 3 || state == 1) {
        if (state == 1)
            reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Stop)(At<void*>(mysim, 0x20C));
        char* const now = At<char*>(mysim, 0x20C);
        reinterpret_cast<void(__thiscall*)(void*, void*, uint32_t)>(addr::cSC4MySimDispatch_StartTrip)(now, now + 0x84, 4);
    }
    return 1;
}

// Advice text `id` shown at the MySim: a new advice object (service
// 0x00B43D10, slot 16) at its position, added to the occupants.
void __fastcall ShowAdvice(char* mysim, void*, uint32_t id)
{
    if (!Virtual<uint8_t>(mysim + 0xB8, 0xE0)) // RequestToPlaySound
        return;
    reinterpret_cast<ThisFn>(addr::cSC4MySim_DropAdvice)(mysim);
    void* const service = Global<void*>(addr::SC4_Service43D10);
    const uint32_t key[3] = {0x6534284A, 0xC977C536, id};
    if (void* const old = At<void*>(mysim, 0x1EC)) {
        At<void*>(mysim, 0x1EC) = nullptr;
        Release(old);
    }
    if (!Virtual<uint8_t>(service, 0x40, key, mysim + 0x1EC))
        return;
    Virtual<void>(Virtual<void*>(At<void*>(mysim, 0x1EC), 0xC), 0x24, mysim + 0xD0);
    void* const occupants = Virtual<void*>(Global<void*>(addr::SC4_City), 0x88);
    void* const advice = At<void*>(mysim, 0x1EC);
    void* const occupant = Virtual<void*>(advice, 0xC);
    Virtual<void>(occupants, 0x50, occupant, uint32_t{0});
}

uint8_t __fastcall PopBalloon(char* mysim, void*, uint32_t kind, uint32_t mask)
{
    char* const holder = mysim + (kind == 1 ? 0x230 : 0x22C);
    char* const head = At<char*>(holder, 0);
    char* const first = At<char*>(head, 0);
    if (first == head)
        return 0;
    const int32_t index = At<int32_t>(first, 8);
    if (!AdviceIndex(mysim, index))
        return 0;
    char* const entry = At<char*>(mysim, 0x214) + index * 12;
    if (!Virtual<uint8_t>(At<void*>(entry, 0), 0x38, mask))
        return 0;
    At<int32_t>(entry, 4) = Virtual<int32_t>(Virtual<void*>(At<void*>(entry, 0), 0x10), 0x40) + At<int32_t>(mysim, 0x5C);
    Virtual<void>(At<void*>(entry, 0), 0x34, uint32_t{7}, uint32_t{0});
    char* const node = At<char*>(At<char*>(holder, 0), 0);
    char* const next = At<char*>(node, 0);
    char* const previous = At<char*>(node, 4);
    At<char*>(previous, 0) = next;
    At<char*>(next, 4) = previous;
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(node);
    return 1;
}

void* __fastcall NextBalloon(char* mysim, void*, uint32_t kind)
{
    const auto pop = reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, uint32_t)>(addr::cSC4MySim_PopBalloon);
    pop(mysim, kind, 4);
    char* const holder = mysim + (kind == 1 ? 0x230 : 0x22C);
    for (;;) {
        char* const head = At<char*>(holder, 0);
        char* const first = At<char*>(head, 0);
        if (first == head)
            return nullptr;
        const int32_t index = At<int32_t>(first, 8);
        if (AdviceIndex(mysim, index)) {
            void* const advice = At<void*>(At<char*>(mysim, 0x214), static_cast<std::size_t>(index) * 12);
            if (!Virtual<uint8_t>(advice, 0x38, uint32_t{4}) && Virtual<uint8_t>(advice, 0x30))
                return advice;
        }
        pop(mysim, kind, 0xFFFF);
    }
}

void __fastcall QueueBalloon(char* mysim, void*, uint32_t kind, int32_t index)
{
    char* const holder = mysim + (kind == 1 ? 0x230 : 0x22C);
    if (!At<uint8_t>(mysim, 0x210) || !AdviceIndex(mysim, index))
        return;
    char* const entry = At<char*>(mysim, 0x214) + index * 12;
    At<uint32_t>(entry, 8) = At<uint32_t>(mysim, 0x200);
    At<uint32_t>(entry, 4) = 0;
    Virtual<void>(At<void*>(entry, 0), 0x34, uint32_t{1}, uint32_t{1});
    reinterpret_cast<void(__thiscall*)(void*, const int32_t*)>(addr::SC4StlList_PushBack)(holder, &index);
}

void __fastcall ClearBalloons(char* mysim, void*)
{
    const auto pop = reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, uint32_t)>(addr::cSC4MySim_PopBalloon);
    for (int i = 1000; i && pop(mysim, 1, 0xFFFF); --i) {
    }
    for (int i = 1000; i && pop(mysim, 2, 0xFFFF); --i) {
    }
}

uint8_t __fastcall ClearResidence(char* mysim, void*)
{
    if (void* const residence = At<void*>(mysim, 0xC4)) {
        At<void*>(mysim, 0xC4) = nullptr;
        Release(residence);
        if (void* const view = At<void*>(mysim, 0x144))
            Virtual<void>(Virtual<void*>(view, 0xC), 0x118);
        reinterpret_cast<ThisFn>(addr::cSC4MySim_DropAdvice)(mysim);
    }
    if (void* const lot = At<void*>(mysim, 0xDC)) {
        At<void*>(mysim, 0xDC) = nullptr;
        Release(lot);
    }
    At<uint8_t>(mysim, 0x210) = 0;
    return 1;
}

void __fastcall DropAdvice(char* mysim, void*)
{
    void* const advice = At<void*>(mysim, 0x1EC);
    if (!advice)
        return;
    if (Virtual<uint8_t>(Virtual<void*>(advice, 0xC), 0x80, uint32_t{1}))
        if (void* const service = Global<void*>(addr::SC4_Service43D10))
            Virtual<void>(service, 0x74, Virtual<void*>(At<void*>(mysim, 0x1EC), 0xC));
    if (void* const again = At<void*>(mysim, 0x1EC)) {
        At<void*>(mysim, 0x1EC) = nullptr;
        Release(again);
    }
}

uint8_t __fastcall OnBalloonTimeout(char* mysim, void*)
{
    if (void* const balloon = At<void*>(mysim, 0x248)) {
        Virtual<void>(balloon, 0x14);
        if (void* const again = At<void*>(mysim, 0x248)) {
            At<void*>(mysim, 0x248) = nullptr;
            Release(again);
        }
        At<uint8_t>(mysim, 0x245) = 0;
    }
    return 1;
}

uint8_t __fastcall TryGoToWork(char* mysim, void*)
{
    Place work;
    reinterpret_cast<ThisFn>(addr::SC4MySimPlace_Construct)(&work);
    reinterpret_cast<ThisFn>(addr::SC4MySimPlace_SetWork)(&work);
    const uint8_t result = reinterpret_cast<uint8_t(__thiscall*)(void*, Place*, uint32_t)>(
        addr::cSC4MySimDispatch_SetDestination)(At<void*>(mysim, 0x20C), &work, 1);
    Release(work.object);
    return result;
}

void __fastcall LoadLuaContext(char* mysim, void*)
{
    if (!Script(mysim))
        return;
    Virtual<void>(Script(mysim), 0xB0);
    At<int32_t>(mysim, 0x164) = Virtual<int32_t>(Script(mysim), 0xC8, uint32_t{1});
    reinterpret_cast<CommandFn>(addr::cSC4MySim_OnCommand)(mysim, -1, -1, -1, -1, 1);
}

uint8_t __fastcall AtBalloonPlace(char* mysim, void*, const Place* place)
{
    void* const balloon = At<void*>(mysim, 0x248);
    if (!balloon || !At<uint8_t>(mysim, 0x245))
        return 0;
    if (place->object == Virtual<void*>(balloon, 0xC))
        return 1;
    return place->object == Virtual<void*>(At<void*>(mysim, 0x248), 0x48) ? 1 : 0;
}

int32_t __fastcall DistanceTo(char* mysim, void*, void* occupant)
{
    float position[3];
    Virtual<void>(occupant, 0x20, position);
    const double dx = static_cast<double>(At<float>(mysim, 0xD0)) - position[0];
    const double dz = static_cast<double>(At<float>(mysim, 0xD8)) - position[2];
    return Truncate(std::sqrt(dz * dz + dx * dx));
}

uint8_t __fastcall OccupantCondition(void* occupant, void*)
{
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, Virtual<void*>(occupant, 0xC));
    return lot ? Virtual<uint8_t>(lot, 0x8C) : 0;
}

// The advisor base shut down and set up again, the advice reloaded; the
// name text (an SC4LocalizedString at +0x38) kept across.
void __fastcall ResetAdvisor(char* mysim, void*)
{
    char* const name = mysim + 0x38;
    struct {
        std::uintptr_t vtable;
        char* begin;
        char* end;
        char* capacity;
        uint32_t references;
        uint32_t key[3];
    } saved{};
    uint8_t allocator = 0;
    reinterpret_cast<void(__thiscall*)(void*, const void*, uint32_t, uint32_t, void*)>(addr::SC4String_CopyBuffer)(
        &saved.begin, name + 4, 0, 0xFFFFFFFF, &allocator);
    saved.key[0] = At<uint32_t>(name, 0x14);
    saved.key[1] = At<uint32_t>(name, 0x18);
    saved.key[2] = At<uint32_t>(name, 0x1C);
    saved.references = 0;
    saved.vtable = addr::SC4LocalizedString_Vtable;
    const uint32_t argument = At<uint32_t>(mysim, 0x14);
    reinterpret_cast<ThisFn>(addr::SC4AdvisorBase_Shutdown)(mysim + 8);
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4AdvisorBase_Init)(mysim + 8, argument);
    reinterpret_cast<ThisFn>(addr::cSC4MySim_LoadAdvice)(mysim);
    reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4LocalizedString_Assign)(name, &saved);
    saved.vtable = addr::SC4String_Vtable;
    if (saved.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(saved.begin);
}

// --- Loading ----------------------------------------------------------------------------
//
// From the object's start. LoadAdvice rebuilds the balloon entries from
// the advisor's advice (vector +0x84, 16 bytes each) and the map of
// advice still waiting (+0x6C), and the first time also makes the
// dispatch object, the balloon and the script tables. LoadTuning reads
// tuning_constants from the MySim's script.

constexpr uint32_t kIidAdvice = 0x6B70E6D9;

struct TreeNode { // std::map<int, int> node
    uint8_t color;
    uint8_t pad[3];
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    int32_t key;
    int32_t value;
};

struct Tree { // header pointer and size
    TreeNode* header;
    uint32_t size;
};

void ClearTree(Tree* tree)
{
    if (!tree->size)
        return;
    reinterpret_cast<void(__thiscall*)(Tree*, TreeNode*)>(addr::SC4Uint32Map_EraseSubtree2)(tree, tree->header->parent);
    tree->header->left = tree->header;
    tree->header->parent = nullptr;
    tree->header->right = tree->header;
    tree->size = 0;
}

TreeNode* Next(TreeNode* node)
{
    if (TreeNode* right = node->right) {
        while (right->left)
            right = right->left;
        return right;
    }
    TreeNode* parent = node->parent;
    while (node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return node->right == parent ? node : parent;
}

void ClearList(char* mysim, std::size_t at)
{
    char* const head = At<char*>(mysim, at);
    for (char* node = At<char*>(head, 0); node != At<char*>(mysim, at);) {
        char* const next = At<char*>(node, 0);
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(node);
        node = next;
    }
    At<char*>(At<char*>(mysim, at), 0) = At<char*>(mysim, at);
    At<char*>(At<char*>(mysim, at), 4) = At<char*>(mysim, at);
}

void GetScriptService(void** holder)
{
    *holder = nullptr;
    if (void* const framework = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetFramework)())
        Virtual<uint8_t>(framework, 0x14, 0xEBE0E860u, 0xE898ED03u, holder);
}

void ReleaseHolder(void** holder)
{
    reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4_ReleaseRefHolder)(holder);
}

void AssignHeld(char* mysim, std::size_t at, void* object)
{
    void* const old = At<void*>(mysim, at);
    if (object == old)
        return;
    if (object)
        Virtual<uint32_t>(object, 0x4);
    At<void*>(mysim, at) = object;
    Release(old);
}

void __fastcall LoadAdvice(char* mysim, void*)
{
    char* const entries = mysim + 0x214;
    reinterpret_cast<void(__thiscall*)(void*, char*, char*)>(addr::SC4MySimAdviceVector_EraseRange)(
        entries, At<char*>(mysim, 0x214), At<char*>(mysim, 0x218));
    ClearList(mysim, 0x22C);
    ClearList(mysim, 0x230);
    ClearTree(reinterpret_cast<Tree*>(mysim + 0x220));

    // The new entry of each advice, by its place in the advisor's vector.
    Tree indices;
    indices.header = static_cast<TreeNode*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(0x18));
    indices.header->color = 0;
    indices.header->parent = nullptr;
    indices.header->left = indices.header;
    indices.header->right = indices.header;
    indices.size = 0;
    using AtFn = int32_t*(__thiscall*)(Tree*, const int32_t*);
    int32_t number = 0;
    for (char* item = At<char*>(mysim, 0x84); item != At<char*>(mysim, 0x88); item += 0x10, ++number) {
        void* advice = nullptr;
        if (!Virtual<uint8_t>(At<void*>(item, 0), 0x0, kIidAdvice, &advice))
            continue;
        *reinterpret_cast<AtFn>(addr::SC4Sint32Map_At)(&indices, &number) = AdviceCount(mysim);
        Virtual<void>(advice, 0x34, uint32_t{7}, uint32_t{0});
        struct {
            void* advice;
            int32_t time;
            uint32_t stamp;
        } entry{};
        if (advice)
            Virtual<uint32_t>(advice, 0x4);
        entry.advice = advice;
        entry.time = At<int32_t>(mysim, 0x5C);
        entry.stamp = 0;
        char* const end = At<char*>(mysim, 0x218);
        if (end != At<char*>(mysim, 0x21C)) {
            if (end) {
                At<void*>(end, 0) = entry.advice;
                if (entry.advice)
                    Virtual<uint32_t>(entry.advice, 0x4);
                At<int32_t>(end, 4) = entry.time;
                At<uint32_t>(end, 8) = 0;
            }
            At<char*>(mysim, 0x218) = end + 12;
        } else {
            uint8_t allocator;
            reinterpret_cast<void(__thiscall*)(void*, char*, const void*, uint8_t*, uint32_t, uint32_t)>(
                addr::SC4MySimAdviceVector_Insert)(entries, end, &entry, &allocator, 1, 1);
        }
        Release(entry.advice);
        Release(advice);
    }

    // Advice waiting in the advisor's map whose advice still exists: its
    // entry goes into +0x220 under the map's key, and it leaves the map.
    struct {
        TreeNode** begin;
        TreeNode** end;
        TreeNode** capacity;
    } done{};
    Tree* const waiting = reinterpret_cast<Tree*>(mysim + 0x6C);
    for (TreeNode* node = waiting->header->left; node != waiting->header;) {
        const int32_t key = node->key;
        const int32_t value = node->value;
        void* advice = nullptr;
        if (Virtual<uint8_t>(At<void*>(At<char*>(mysim, 0x84), static_cast<std::size_t>(value) * 0x10), 0x0, kIidAdvice,
                             &advice)) {
            if (done.end != done.capacity) {
                if (done.end)
                    *done.end = node;
                ++done.end;
            } else {
                const uint32_t size = static_cast<uint32_t>(done.end - done.begin);
                const uint32_t capacity = size + (size < 1 ? 1 : size);
                auto* const grown = capacity ? static_cast<TreeNode**>(
                    reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(capacity * 4)) : nullptr;
                TreeNode** out = grown;
                for (TreeNode** in = done.begin; in != done.end; ++in, ++out)
                    if (out)
                        *out = *in;
                if (out)
                    *out = node;
                if (done.begin)
                    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(done.begin);
                done.begin = grown;
                done.end = out + 1;
                done.capacity = grown + capacity;
            }
            TreeNode* found = indices.header;
            for (TreeNode* at = indices.header->parent; at;) {
                if (at->key >= value) {
                    found = at;
                    at = at->left;
                } else {
                    at = at->right;
                }
            }
            if (found != indices.header && value >= found->key) {
                int32_t pair[2] = {key, found->value};
                Tree* const target = reinterpret_cast<Tree*>(mysim + 0x220);
                TreeNode* parent = target->header;
                TreeNode* at = target->header->parent;
                while (at) {
                    parent = at;
                    at = key < at->key ? at->left : at->right;
                }
                void* result;
                reinterpret_cast<void(__thiscall*)(Tree*, void**, TreeNode*, TreeNode*, const int32_t*, uint32_t)>(
                    addr::SC4Sint32Multimap_Insert)(target, &result, at, parent, pair, 0);
            }
        }
        node = Next(node);
        Release(advice);
    }
    for (TreeNode** at = done.end; at != done.begin;) {
        --at;
        TreeNode* const header = waiting->header;
        using RebalanceFn = void*(__cdecl*)(TreeNode*, TreeNode**, TreeNode**, TreeNode**);
        if (void* const erased = reinterpret_cast<RebalanceFn>(addr::SC4Tree_RebalanceForErase)(
                *at, &header->parent, &header->left, &header->right))
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(erased);
        --waiting->size;
    }

    if (!At<uint8_t>(mysim, 0x1E4)) {
        using NewFn = void*(__cdecl*)(std::size_t);
        if (!At<void*>(mysim, 0x20C)) {
            void* const memory = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x18C);
            void* const dispatch = memory ? reinterpret_cast<void*(__thiscall*)(void*, void*)>(
                addr::cSC4MySimDispatch_cSC4MySimDispatch)(memory, mysim) : nullptr;
            AssignHeld(mysim, 0x20C, dispatch);
        }
        void* const memory = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x58);
        void* const balloon = memory ? reinterpret_cast<void*(__thiscall*)(void*, void*)>(
            addr::SC4MySimBalloon_Construct)(memory, mysim + 0xB8) : nullptr;
        AssignHeld(mysim, 0x238, balloon);
        reinterpret_cast<ThisFn>(addr::SC4MySimBalloon_Create)(At<void*>(mysim, 0x238));
        void* const messages = Global<void*>(addr::SC4_MessageServer);
        for (const uint32_t type : {0x0B6F3E27u, 0xAB6F3E2Du, 0x4B6F3EBFu, 0x0BB04126u, 0x4BC42C48u, 0x26D31EC1u})
            Virtual<void>(messages, 0x14, mysim + 0x10, type);
        if (void* const frames = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
            Virtual<void>(frames, 0x14, mysim + 0x10, uint32_t{0xFF});
        At<uint8_t>(mysim, 0x1E4) = 1;

        using RegisterTableFn = uint32_t(__cdecl*)(void*, const char*, void*, uint32_t, uint32_t, void*);
        using RegisterVariableFn = uint32_t(__cdecl*)(void*, const char*, std::uintptr_t, void*, uint32_t, void*, void*);
        void* const table = Virtual<void*>(Global<void*>(addr::SC4_City), 0x11C);
        void* script;
        GetScriptService(&script);
        void* const service = script;
        ReleaseHolder(&script);
        if (!Virtual<uint32_t>(service, 0x24, table)) {
            GetScriptService(&script);
            reinterpret_cast<RegisterTableFn>(addr::SC4Script_RegisterTable)(
                table, reinterpret_cast<const char*>(addr::MySim_ScriptMysims), nullptr, 0, 0, script);
            ReleaseHolder(&script);
        }
        GetScriptService(&script);
        reinterpret_cast<RegisterTableFn>(addr::SC4Script_RegisterTable)(
            mysim, reinterpret_cast<const char*>(addr::MySim_ScriptEmpty), table, 0, 0, script);
        ReleaseHolder(&script);
        GetScriptService(&script);
        reinterpret_cast<RegisterVariableFn>(addr::SC4Script_RegisterVariable)(
            mysim + 0x128, reinterpret_cast<const char*>(addr::MySim_ScriptAge), addr::SC4Script_MySimAge, mysim, 0,
            nullptr, script);
        ReleaseHolder(&script);
        GetScriptService(&script);
        reinterpret_cast<RegisterVariableFn>(addr::SC4Script_RegisterVariable)(
            mysim + 0x18, reinterpret_cast<const char*>(addr::MySim_ScriptName), addr::SC4Script_MySimName, mysim, 2,
            mysim, script);
        ReleaseHolder(&script);
    }

    if (done.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(done.begin);
    ClearTree(&indices);
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(indices.header);
}

struct ScriptTable { // SC4ScriptTable, 0x24 bytes
    int32_t top;
    uint32_t name;
    void* script;
    uint8_t open;
    uint8_t pad[3];
    std::uintptr_t textVtable;
    char* textBegin;
    char* textEnd;
    char* textCapacity;
    uint32_t textReferences;
};
static_assert(sizeof(ScriptTable) == 0x24);

uint8_t __fastcall LoadTuning(char* mysim, void*)
{
    ScriptTable table;
    reinterpret_cast<void(__thiscall*)(ScriptTable*, void*, const char*, uint32_t)>(addr::SC4ScriptTable_Open)(
        &table, Script(mysim), reinterpret_cast<const char*>(addr::MySim_TuningTable), 0);
    const auto* const keys = reinterpret_cast<const char* const*>(addr::MySim_TuningKeys);
    // Each value: the script's when it has one, the default otherwise.
    const auto value = [&](int key, int32_t fallback) {
        if (!reinterpret_cast<uint8_t(__thiscall*)(ScriptTable*, const char*)>(addr::SC4ScriptTable_Has)(&table, keys[key]))
            return fallback;
        return reinterpret_cast<int32_t(__thiscall*)(ScriptTable*, const char*)>(addr::SC4ScriptTable_GetInt)(
            &table, keys[key]);
    };
    Virtual<void>(At<void*>(mysim, 0x238), 0x1C, value(0, 300));  // fade
    Virtual<void>(At<void*>(mysim, 0x238), 0x18, value(1, 1500)); // shown
    Virtual<void>(At<void*>(mysim, 0x238), 0x14, value(2, 300));  // hidden
    At<int32_t>(At<char*>(mysim, 0x20C), 0x20) = value(3, 1);
    At<int32_t>(At<char*>(mysim, 0x20C), 0x24) = value(4, 1);
    At<int32_t>(At<char*>(mysim, 0x20C), 0x1C) = value(5, 1);
    At<int32_t>(At<char*>(mysim, 0x20C), 0x18) = value(7, 10);
    At<int32_t>(mysim, 0x240) = value(6, 10);
    At<int32_t>(At<char*>(mysim, 0x20C), 0x14) = value(8, 10);
    At<int32_t>(mysim, 0x23C) = value(9, 0);
    if (table.open)
        Virtual<void>(table.script, 0x1C, table.top);
    if (table.textBegin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(table.textBegin);
    return 1;
}

// --- Misc -------------------------------------------------------------------------------
//
// From the object's start.

void __cdecl PostChanged()
{
    Send(0xAA25827C, nullptr, nullptr);
}

// The move-in tool, if it is the current one, is dropped.
void __cdecl CancelMoveIn()
{
    void* const tools = Global<void*>(addr::SC4_Service43DC4);
    if (tools) {
        void* const tool = Global<void*>(addr::SC4_DispatchTool);
        if (!tool)
            return;
        if (Virtual<void*>(tools, 0x60) == tool)
            Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x68, uint32_t{0});
    }
    if (void* const tool = Global<void*>(addr::SC4_DispatchTool)) {
        Global<void*>(addr::SC4_DispatchTool) = nullptr;
        Release(tool);
    }
}

// The script's game.mysim_get_idle_animation_offset(context), handed to
// the dispatch object (-1 without one).
int32_t __fastcall OnScriptEvent(char* mysim, void*)
{
    void* const script = Script(mysim);
    const int32_t top = Virtual<int32_t>(script, 0x18);
    Virtual<void>(mysim + 0xB8, 0x110); // ActivateLuaContext
    int32_t result = -1;
    Virtual<void>(Script(mysim), 0x94, reinterpret_cast<const char*>(addr::MySim_ScriptGame));
    if (Virtual<int32_t>(Script(mysim), 0x30, -1) == 5) {
        Virtual<void>(Script(mysim), 0x84, reinterpret_cast<const char*>(addr::MySim_ScriptIdleAnimation));
        Virtual<void>(Script(mysim), 0x9C, -2);
        if (Virtual<int32_t>(Script(mysim), 0x30, -1) == 6) {
            Virtual<void>(Script(mysim), 0xAC, At<int32_t>(mysim, 0x164));
            if (!Virtual<int32_t>(Script(mysim), 0xCC, uint32_t{1}, uint32_t{1}))
                result = Truncate(Virtual<double>(Script(mysim), 0x5C, -1));
        }
    }
    reinterpret_cast<void(__thiscall*)(void*, uint32_t, int32_t, uint32_t)>(addr::cSC4MySimDispatch_ScriptEvent)(
        At<void*>(mysim, 0x20C), 2, result, 0);
    Virtual<void>(Script(mysim), 0x1C, top);
    return result;
}

void __fastcall ArriveHome(char* mysim, void*)
{
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::cSC4MySim_ShowAdvice)(
        mysim, At<uint8_t>(mysim, 0x113) ? 0x2A5F9CDCu : 0x6A5FB141u);
}

// The job (+0xE0, or the neighbour job at +0xE4) given up, then the
// script's facts again.
void __fastcall FindNewJob(char* mysim, void*)
{
    if (!At<int32_t>(mysim, 0xC0))
        return;
    if (At<void*>(mysim, 0xE0)) {
        Virtual<void>(mysim + 0xB8, 0x9C);
        if (void* const job = At<void*>(mysim, 0xE0)) {
            At<void*>(mysim, 0xE0) = nullptr;
            Release(job);
        }
    } else if (void* const job = At<void*>(mysim, 0xE4)) {
        At<void*>(mysim, 0xE4) = nullptr;
        Release(job);
        At<uint32_t>(mysim, 0xE8) = 0;
        At<uint32_t>(mysim, 0xEC) = 0;
    }
    if (At<int32_t>(mysim, 0xC0) != 5)
        At<int32_t>(mysim, 0xC0) = 2;
    reinterpret_cast<ThisFn>(addr::cSC4MySim_UpdateHomeAndJob)(mysim);
}

void __fastcall OnStranded(char* mysim, void*)
{
    reinterpret_cast<ThisFn>(addr::cSC4MySim_FindNewJob)(mysim);
    Virtual<void>(mysim + 8, 0x48, uint32_t{0xCA4553B7}, uint32_t{0});
    Place work;
    reinterpret_cast<ThisFn>(addr::SC4MySimPlace_Construct)(&work);
    reinterpret_cast<ThisFn>(addr::SC4MySimPlace_SetWork)(&work);
    reinterpret_cast<uint8_t(__thiscall*)(void*, Place*, uint32_t)>(addr::cSC4MySimDispatch_SetDestination)(
        At<void*>(mysim, 0x20C), &work, 1);
    Release(work.object);
}

// A route from home to work (or the neighbour connection at +0xF0) asked
// of the path finder; its time to +0x154, or stranded without one.
uint8_t __fastcall RefreshLocation(char* mysim, void*)
{
    void* const finder = Virtual<void*>(Global<void*>(addr::SC4_City), 0x114);
    uint8_t result = 0;
    void* path = nullptr;
    if ((!At<void*>(mysim, 0xE0) && !At<void*>(mysim, 0xE4)) || !At<void*>(mysim, 0xC4))
        return 0;
    Virtual<void>(finder, 0x18, &path);
    if (!path)
        return 0;
    struct Rect {
        int32_t v[4];
    } home, work;
    Virtual<void>(Virtual<void*>(At<void*>(mysim, 0xC4), 0xC), 0x2C, &home);
    if (void* const job = At<void*>(mysim, 0xE0)) {
        Virtual<void>(Virtual<void*>(job, 0xC), 0x2C, &work);
    } else {
        work.v[0] = At<int32_t>(mysim, 0xF0);
        work.v[1] = At<int32_t>(mysim, 0xF4);
        work.v[2] = At<int32_t>(mysim, 0xF0);
        work.v[3] = At<int32_t>(mysim, 0xF4);
    }
    Virtual<void>(path, 0x14, uint32_t{1});
    Virtual<void>(path, 0x24, uint32_t{3});
    Virtual<void>(path, 0x48, uint32_t{1});
    Virtual<void>(path, 0x4C, home);
    Virtual<void>(path, 0x50, work);
    const int32_t found = Virtual<int32_t>(path, 0x5C);
    if (found == 1) {
        result = static_cast<uint8_t>(found);
        At<int32_t>(mysim, 0x154) = Truncate(Virtual<float>(path, 0x68));
    } else {
        reinterpret_cast<ThisFn>(addr::cSC4MySim_OnStranded)(mysim);
    }
    Release(path);
    return result;
}

// The ghost effect at the MySim's position.
void __fastcall PlaceGhost(char* mysim, void*)
{
    void* const effects = Global<void*>(addr::SC4_Service43D1C);
    for (int i = 0; i < 2; ++i)
        if (void* const old = At<void*>(mysim, 0x18C)) {
            At<void*>(mysim, 0x18C) = nullptr;
            Release(old);
        }
    if (!Virtual<uint8_t>(effects, 0x1C, reinterpret_cast<const char*>(addr::MySim_GhostName), mysim + 0x18C))
        return;
    struct {
        uint8_t flags[4];
        float rotation[9];
        float position[3];
        float scale;
    } place{};
    place.flags[0] = 1;
    place.flags[1] = 1;
    place.rotation[0] = 1.0f;
    place.rotation[4] = 1.0f;
    place.rotation[8] = 1.0f;
    place.position[0] = At<float>(mysim, 0xD0);
    place.position[1] = At<float>(mysim, 0xD4);
    place.position[2] = At<float>(mysim, 0xD8);
    place.scale = 1.0f;
    Virtual<void>(At<void*>(mysim, 0x18C), 0x1C, &place);
    Virtual<void>(At<void*>(mysim, 0x18C), 0xC, uint32_t{0});
}

// Leaving the game: home given up, the trip stopped, the balloon dropped.
void __fastcall Script0A57DE1F(char* mysim, void*)
{
    if (!At<int32_t>(mysim, 0xC0))
        return;
    reinterpret_cast<ThisFn>(addr::cSC4MySim_ClearResidence)(mysim);
    Virtual<void>(mysim + 0xB8, 0x8C);
    At<int32_t>(mysim, 0xC0) = 0;
    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Shutdown)(At<void*>(mysim, 0x20C));
    for (std::size_t at = 0x24C; at <= 0x260; at += 4)
        At<uint32_t>(mysim, at) = 0;
    if (void* const balloon = At<void*>(mysim, 0x248)) {
        Virtual<void>(balloon, 0x14);
        if (void* const again = At<void*>(mysim, 0x248)) {
            At<void*>(mysim, 0x248) = nullptr;
            Release(again);
        }
        At<uint8_t>(mysim, 0x245) = 0;
    }
    reinterpret_cast<void(__cdecl*)()>(addr::cSC4MySim_PostChanged)();
}

// Died: the news at its position, the ghost, then leaving.
void __fastcall ScriptEA4D269C(char* mysim, void*)
{
    if (!At<int32_t>(mysim, 0xC0))
        return;
    ScriptString text;
    reinterpret_cast<void(__thiscall*)(void*, const char*)>(addr::SC4String_FromText)(
        &text, reinterpret_cast<const char*>(addr::MySim_DiedText));
    void* const news = Virtual<void*>(Global<void*>(addr::SC4_City), 0xBC);
    Virtual<void>(news, 0x28, &text, uint32_t{0}, mysim + 0xD0);
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(text.begin);
    reinterpret_cast<ThisFn>(addr::cSC4MySim_PlaceGhost)(mysim);
    reinterpret_cast<ThisFn>(addr::cSC4MySim_Script0A57DE1F)(mysim);
}

// Fired: a new job looked for; the trip time kept.
void __fastcall ScriptAA4934B4(char* mysim, void*)
{
    if (!At<int32_t>(mysim, 0xC0))
        return;
    reinterpret_cast<ThisFn>(addr::cSC4MySim_FindNewJob)(mysim);
    At<uint8_t>(mysim, 0x158) = 1;
    At<int32_t>(mysim, 0x150) = At<int32_t>(At<char*>(mysim, 0x20C), 0x3C);
}

// A new generation: the child's age and job.
void __fastcall Script6A495F7F(char* mysim, void*)
{
    if (!At<int32_t>(mysim, 0xC0))
        return;
    ++At<uint16_t>(mysim, 0x110);
    At<uint8_t>(mysim, 0x158) = 0;
    if (void* const old = At<void*>(mysim, 0x178)) {
        At<void*>(mysim, 0x178) = nullptr;
        Release(old);
    }
    At<uint8_t>(mysim, 0x128) = Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA4), 0xA8, At<int32_t>(mysim, 0x10C));
    reinterpret_cast<ThisFn>(addr::cSC4MySim_FindNewJob)(mysim);
    reinterpret_cast<ThisFn>(addr::cSC4MySim_PlaceGhost)(mysim);
    reinterpret_cast<void(__cdecl*)()>(addr::cSC4MySim_PostChanged)();
}

// Richer, up to wealth 3.
void __fastcall Script6A4A5E9E(char* mysim, void*)
{
    if (!At<int32_t>(mysim, 0xC0) || At<int32_t>(mysim, 0x10C) >= 3)
        return;
    ++At<int32_t>(mysim, 0x10C);
    reinterpret_cast<ThisFn>(addr::cSC4MySim_FindNewJob)(mysim);
    Virtual<void>(mysim + 0xB8, 0x14);
}

// The pedestrian model's key: a walker made at (0, -100, 0) and asked.
// Returns `out`.
uint32_t* __fastcall DefaultPedestrianKey(char*, void*, uint32_t* out)
{
    uint32_t key[3] = {0, 0, 0};
    if (void* const automata = Virtual<void*>(Global<void*>(addr::SC4_City), 0xB0)) {
        const float where[3] = {0.0f, -100.0f, 0.0f};
        void* const walker = Virtual<void*>(automata, 0x3C, uint32_t{0x4149}, where, uint32_t{2}, key, uint32_t{0},
                                            uint32_t{0xFF}, uint32_t{0});
        if (walker) {
            Virtual<uint32_t>(walker, 0x4);
            uint32_t scratch[3];
            const uint32_t* const got = Virtual<const uint32_t*>(walker, 0x180, scratch, uint32_t{0});
            key[0] = got[0];
            key[1] = got[1];
            key[2] = got[2];
            Virtual<void>(walker, 0x34, uint32_t{1});
            Release(walker);
        }
    }
    out[0] = key[0];
    out[1] = key[1];
    out[2] = key[2];
    return out;
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

bool InstallScript(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[3] = {};
    const addr::Entry* update = addr_check::Find(addr::cSC4MySim_UpdateHomeAndJob);
    const addr::Entry* basics = addr_check::Find(addr::cSC4MySim_PublishBasics);
    const addr::Entry* distances = addr_check::Find(addr::cSC4MySim_UpdateDistances);
    return update && basics && distances &&
           hook::Install(*update, reinterpret_cast<void*>(&UpdateHomeAndJob), &originals[0]) &&
           hook::Install(*basics, reinterpret_cast<void*>(&PublishBasics), &originals[1]) &&
           hook::Install(*distances, reinterpret_cast<void*>(&UpdateDistances), &originals[2]);
}

const bool g_scriptRegistered = registry::Register({
    "simulators.mysim_agents.script",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_UpdateHomeAndJob,
    "Our cSC4MySim UpdateHomeAndJob, PublishBasics and UpdateDistances: the facts in the MySim's script "
    "(docs/mysim-agents.md)",
    false,
    InstallScript,
});

bool InstallTrip(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* original = nullptr;
    const addr::Entry* found = addr_check::Find(addr::cSC4MySim_OnCommand);
    return found && hook::Install(*found, reinterpret_cast<void*>(&OnCommand), &original);
}

const bool g_tripRegistered = registry::Register({
    "simulators.mysim_agents.trip",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_OnCommand,
    "Our cSC4MySim OnCommand: the trip facts in the MySim's script (docs/mysim-agents.md)",
    false,
    InstallTrip,
});

bool InstallAgent(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySim_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4MySim_CheckBalloonTarget, reinterpret_cast<void*>(&CheckBalloonTarget)},
        {addr::cSC4MySim_GetBalloonTarget, reinterpret_cast<void*>(&GetBalloonTarget)},
        {addr::cSC4MySim_SetDispatchFlag, reinterpret_cast<void*>(&SetDispatchFlag)},
        {addr::cSC4MySim_IsAboveWater, reinterpret_cast<void*>(&IsAboveWater)},
        {addr::cSC4MySim_GetTripTime, reinterpret_cast<void*>(&GetTripTime)},
        {addr::cSC4MySim_AdvisorRun, reinterpret_cast<void*>(&AdvisorRun)},
        {addr::cSC4MySim_MonthPassed, reinterpret_cast<void*>(&MonthPassed)},
        {addr::cSC4MySim_GetName, reinterpret_cast<void*>(&GetName)},
        {addr::cSC4MySim_MakeBalloonTarget, reinterpret_cast<void*>(&MakeBalloonTarget)},
        {addr::cSC4MySim_FollowBalloonTarget, reinterpret_cast<void*>(&FollowBalloonTarget)},
        {addr::cSC4MySim_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4MySim_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4MySim_ProcessAdviceEvents, reinterpret_cast<void*>(&ProcessAdviceEvents)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_agentRegistered = registry::Register({
    "simulators.mysim_agents.agent",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_QueryInterface,
    "Our cSC4MySim agent and advisor methods: QueryInterface, balloon target, trip time (docs/mysim-agents.md)",
    false,
    InstallAgent,
});

bool InstallUi(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySim_UpdateResidence, reinterpret_cast<void*>(&UpdateResidence)},
        {addr::cSC4MySim_LocalSchoolGrade, reinterpret_cast<void*>(&LocalSchoolGrade)},
        {addr::cSC4MySim_LocalHospitalGrade, reinterpret_cast<void*>(&LocalHospitalGrade)},
        {addr::cSC4MySim_SetCar, reinterpret_cast<void*>(&SetCar)},
        {addr::cSC4MySim_SetPedestrian, reinterpret_cast<void*>(&SetPedestrian)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_uiRegistered = registry::Register({
    "simulators.mysim_agents.ui",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_UpdateResidence,
    "Our cSC4MySim panel and residence-view methods (docs/mysim-agents.md)",
    false,
    InstallUi,
});

bool InstallObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySim_Create, reinterpret_cast<void*>(&Create)},
        {addr::cSC4MySim_cSC4MySim, reinterpret_cast<void*>(&Construct)},
        {addr::cSC4MySim_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::cSC4MySim_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
    };
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_objectRegistered = registry::Register({
    "simulators.mysim_agents.object",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_cSC4MySim,
    "Our cSC4MySim factory, constructor and destructors (docs/mysim-agents.md)",
    false,
    InstallObject,
});

bool InstallHelpers(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySim_CanTravel, reinterpret_cast<void*>(&CanTravel)},
        {addr::cSC4MySim_ShowAdvice, reinterpret_cast<void*>(&ShowAdvice)},
        {addr::cSC4MySim_PopBalloon, reinterpret_cast<void*>(&PopBalloon)},
        {addr::cSC4MySim_NextBalloon, reinterpret_cast<void*>(&NextBalloon)},
        {addr::cSC4MySim_QueueBalloon, reinterpret_cast<void*>(&QueueBalloon)},
        {addr::cSC4MySim_ClearBalloons, reinterpret_cast<void*>(&ClearBalloons)},
        {addr::cSC4MySim_ClearResidence, reinterpret_cast<void*>(&ClearResidence)},
        {addr::cSC4MySim_DropAdvice, reinterpret_cast<void*>(&DropAdvice)},
        {addr::cSC4MySim_OnBalloonTimeout, reinterpret_cast<void*>(&OnBalloonTimeout)},
        {addr::cSC4MySim_TryGoToWork, reinterpret_cast<void*>(&TryGoToWork)},
        {addr::cSC4MySim_LoadLuaContext, reinterpret_cast<void*>(&LoadLuaContext)},
        {addr::cSC4MySim_AtBalloonPlace, reinterpret_cast<void*>(&AtBalloonPlace)},
        {addr::cSC4MySim_DistanceTo, reinterpret_cast<void*>(&DistanceTo)},
        {addr::SC4_OccupantCondition, reinterpret_cast<void*>(&OccupantCondition)},
        {addr::cSC4MySim_ResetAdvisor, reinterpret_cast<void*>(&ResetAdvisor)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_helpersRegistered = registry::Register({
    "simulators.mysim_agents.helpers",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_CanTravel,
    "Our cSC4MySim helpers: trips, balloons, advice, distances (docs/mysim-agents.md)",
    false,
    InstallHelpers,
});

bool InstallLoading(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[2] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySim_LoadAdvice, reinterpret_cast<void*>(&LoadAdvice)},
        {addr::cSC4MySim_LoadTuning, reinterpret_cast<void*>(&LoadTuning)},
    };
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_loadingRegistered = registry::Register({
    "simulators.mysim_agents.loading",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_LoadAdvice,
    "Our cSC4MySim LoadAdvice and LoadTuning (docs/mysim-agents.md)",
    false,
    InstallLoading,
});

bool InstallMisc(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySim_PostChanged, reinterpret_cast<void*>(&PostChanged)},
        {addr::cSC4MySim_CancelMoveIn, reinterpret_cast<void*>(&CancelMoveIn)},
        {addr::cSC4MySim_OnScriptEvent, reinterpret_cast<void*>(&OnScriptEvent)},
        {addr::cSC4MySim_ArriveHome, reinterpret_cast<void*>(&ArriveHome)},
        {addr::cSC4MySim_FindNewJob, reinterpret_cast<void*>(&FindNewJob)},
        {addr::cSC4MySim_OnStranded, reinterpret_cast<void*>(&OnStranded)},
        {addr::cSC4MySim_RefreshLocation, reinterpret_cast<void*>(&RefreshLocation)},
        {addr::cSC4MySim_PlaceGhost, reinterpret_cast<void*>(&PlaceGhost)},
        {addr::cSC4MySim_Script0A57DE1F, reinterpret_cast<void*>(&Script0A57DE1F)},
        {addr::cSC4MySim_ScriptEA4D269C, reinterpret_cast<void*>(&ScriptEA4D269C)},
        {addr::cSC4MySim_ScriptAA4934B4, reinterpret_cast<void*>(&ScriptAA4934B4)},
        {addr::cSC4MySim_Script6A495F7F, reinterpret_cast<void*>(&Script6A495F7F)},
        {addr::cSC4MySim_Script6A4A5E9E, reinterpret_cast<void*>(&Script6A4A5E9E)},
        {addr::cSC4MySim_DefaultPedestrianKey, reinterpret_cast<void*>(&DefaultPedestrianKey)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_miscRegistered = registry::Register({
    "simulators.mysim_agents.misc",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_FindNewJob,
    "Our cSC4MySim jobs, stranding, script commands, ghost and location (docs/mysim-agents.md)",
    false,
    InstallMisc,
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
