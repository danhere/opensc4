// Our cSC4Simulator (build 641), second part: the tuning, Init, Shutdown,
// DoMessage with the date query and the two cheats, and the save (Write,
// Read), each a replacement on the original object (docs/simulator.md).
#include "core/address_check.h"
#include "core/core_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::core_sim {
namespace {

using FreeFn = void(__cdecl*)(void* p);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

void* Service(std::uintptr_t global) { return Global<void*>(global); }

constexpr uint32_t kDoPostCityInit = 0x26D31EC1;
constexpr uint32_t kDateQuery = 0x8BAC4B43;
constexpr uint32_t kCheat = 0x230E27AC;
constexpr uint32_t kFpsCheat = 0x0AC07E24;
constexpr uint32_t kDateCheat = 0x8A78BEFF;

struct GzString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

void DropString(GzString& text)
{
    text.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (text.begin)
        Free(text.begin);
}

// --- The tuning (0x007025B0) ----------------------------------------------------------

// Exemplar 0xE9590E52: the time per step at each speed (property
// 0x89590E6D, three values) and the animation time dilations (0x89590E6E,
// defaults 0, 1, 1.5, 2, 1).
void __fastcall LoadTuning(char* self, void*)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    using FloatVectorFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* vector);
    At<uint32_t>(self, field::kStepsPerSpeed) = 5000;
    At<uint32_t>(self, field::kStepsPerSpeed + 4) = 1000;
    At<uint32_t>(self, field::kStepsPerSpeed + 8) = 100;
    const uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0xE9590E52};
    void* exemplar = nullptr;
    if (Virtual<uint8_t>(Service(addr::SC4_ResourceManager), 0xC, key, 0xA52160F5u, &exemplar, 0u, 0u)) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        if (void* const property = Virtual<void*>(holder, 0x24, 0x89590E6Du)) {
            void* const value = Virtual<void*>(property, 0x14);
            const uint32_t* const steps = Virtual<const uint32_t*>(value, 0x19C);
            if (steps && Virtual<int32_t>(value, 0x10) == 3) {
                At<uint32_t>(self, field::kStepsPerSpeed) = steps[0];
                At<uint32_t>(self, field::kStepsPerSpeed + 4) = steps[1];
                At<uint32_t>(self, field::kStepsPerSpeed + 8) = steps[2];
            }
        }
        uint32_t* const dilations = At<uint32_t*>(self, field::kDilations);
        dilations[0] = 0;
        dilations[1] = 0x3F800000; // 1.0f
        dilations[2] = 0x3FC00000; // 1.5f
        dilations[3] = 0x40000000; // 2.0f
        dilations[4] = 0x3F800000;
        reinterpret_cast<FloatVectorFn>(addr::SC4_GetPropertyFloatVector)(holder, 0x89590E6Eu,
                                                                          self + field::kDilations);
    }
    if (exemplar)
        Virtual<uint32_t>(exemplar, 0x8);
}

// --- Init and Shutdown (slots 3, 4) -----------------------------------------------------

// Hidden-paused until DoPostCityInit; the tuning, the messages, the date
// command, the cursors at the end of their maps, the speed, the watches,
// the system service in the framework, the two cheats (the date cheat only
// where the service at 0x00B43C94 allows, its slot 21).
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    using StringFn = void*(__thiscall*)(void* string, const char* text);
    using RangeFn = void*(__thiscall*)(void* string, const char* first, const char* last, void* tag);
    if (At<uint8_t>(self, field::kInitialised))
        return 1;
    char* const service = self + 0x8;
    char* const target = self + 0x24;
    At<uint8_t>(self, field::kInitialised) = 1;
    At<int32_t>(self, field::kHiddenPaused) = 1;
    CallThis<void>(addr::cSC4Simulator_ServiceNoOp, service);
    CallThis<void>(addr::cSC4Simulator_LoadTuning, self);
    if (!At<void*>(self, field::kMessageServer)) {
        At<void*>(self, field::kMessageServer) = Service(addr::SC4_MessageServer);
        Virtual<uint32_t>(At<void*>(self, field::kMessageServer), 0x4);
        Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x14, target, kDoPostCityInit);
        Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x14, target, kDateQuery);
    }
    if (void* const commands = Service(addr::SC4_Commands))
        Virtual<uint8_t>(commands, 0x18, kDateQuery, reinterpret_cast<const char*>(0x00AB2EF0), 0u);
    for (int type = 0; type < kAgentTypes; ++type)
        Cursor(self, type) = AgentHeader(self, type);
    if (At<int32_t>(self, field::kSpeed) == 3)
        Virtual<uint8_t>(self, 0x70, 0); // SetSimSpeed
    else
        CallThis<void>(addr::cSC4Simulator_ApplySpeed, self, At<int32_t>(self, field::kSpeed));
    CallThis<void>(addr::SC4Stopwatch_Start, self + field::kStepWatch);
    CallThis<void>(addr::SC4Stopwatch_Start, self + field::kWatch60);
    void* const framework = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetFramework)();
    Virtual<uint8_t>(framework, 0xC, service);
    Virtual<uint8_t>(framework, 0x24, service);
    void* const cheats = Service(addr::SC4_CheatCodes);
    if (!cheats)
        return 1;
    GzString name{};
    const char* const text = Global<const char*>(0x00B0D458);
    char tag;
    reinterpret_cast<RangeFn>(addr::SC4String_FromRange)(&name.begin, text, text + std::strlen(text), &tag);
    name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    name.zero = 0;
    Virtual<uint8_t>(cheats, 0xC, kFpsCheat, &name);
    DropString(name);
    Virtual<uint8_t>(cheats, 0x1C, target, kFpsCheat);
    if (Virtual<uint8_t>(Service(addr::SC4_Service43C94), 0x54)) {
        reinterpret_cast<StringFn>(addr::SC4String_FromText)(&name, Global<const char*>(0x00B0D45C));
        Virtual<uint8_t>(cheats, 0xC, kDateCheat, &name);
        DropString(name);
        Virtual<uint8_t>(cheats, 0x1C, target, kDateCheat);
    }
    return 1;
}

// The date back to 1 January 2000 (the game's steps: a copy of the date's
// day, the date made from month, day and year, a full date copied from it;
// its day and last word kept), speed 3, every agent removed, the messages,
// the date command and the framework service dropped.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialised))
        return 1;
    At<uint8_t>(self, field::kInitialised) = 0;
    char* const target = self + 0x24;
    if (void* const cheats = Service(addr::SC4_CheatCodes)) {
        Virtual<uint8_t>(cheats, 0x20, target, kFpsCheat);
        Virtual<uint8_t>(cheats, 0x10, kFpsCheat);
        Virtual<uint8_t>(cheats, 0x20, target, kDateCheat);
        Virtual<uint8_t>(cheats, 0x10, kDateCheat);
    }
    alignas(4) char dates[0x20];
    char* const date = self + field::kDate;
    CallThis<void*>(addr::cRZDate_FromDate, dates, date);
    void* const made = CallThis<void*>(addr::SC4Date_Construct, dates + 0x8, 1u, 1u, 2000u);
    CallThis<void*>(addr::cRZDate_FromShortDate, dates + 0x10, made);
    At<uint32_t>(date, 0xC) = At<uint32_t>(dates, 0x1C);
    At<uint32_t>(date, 0x4) = At<uint32_t>(dates, 0x14);
    CallThis<void>(addr::cRZDate_Destroy, dates + 0x10);
    At<int32_t>(self, field::kSpeed) = 3;
    Virtual<uint8_t>(self, 0x5C); // RemoveAllAgents
    if (void*& messages = At<void*>(self, field::kMessageServer)) {
        Virtual<uint8_t>(messages, 0x18, target, kDoPostCityInit);
        Virtual<uint8_t>(messages, 0x18, target, kDateQuery);
        Virtual<uint32_t>(messages, 0x8);
        messages = nullptr;
    }
    if (void* const commands = Service(addr::SC4_Commands))
        Virtual<uint8_t>(commands, 0x24, kDateQuery);
    if (void* const framework = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetFramework)()) {
        Virtual<uint8_t>(framework, 0x28, self + 0x8);
        Virtual<uint8_t>(framework, 0x10, self + 0x8);
    }
    CallThis<void>(addr::cSC4Simulator_ServiceNoOp, self + 0x8);
    return 1;
}

using LifecycleFn = uint8_t(__fastcall*)(char*, void*);
LifecycleFn g_originalInit = nullptr;
LifecycleFn g_originalShutdown = nullptr;
using TuningFn = void(__fastcall*)(char*, void*);
TuningFn g_originalTuning = nullptr;

template <typename Fn>
bool Detour(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

bool InstallInit(registry::Mode)
{
    return game::IsBuild641() && Detour(addr::cSC4Simulator_Init, &Init, &g_originalInit) &&
           Detour(addr::cSC4Simulator_Shutdown, &Shutdown, &g_originalShutdown) &&
           Detour(addr::cSC4Simulator_LoadTuning, &LoadTuning, &g_originalTuning);
}

const bool g_registeredInit = registry::Register({
    "simulators.core.init",
    "simulators/core",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Simulator_Init,
    "Our simulator Init, Shutdown and tuning load (docs/simulator.md)",
    false,
    InstallInit,
});

// --- The cheats (0x00703420) and DoMessage (+0x24 vtable 0x00AB2F44, slot 3) --------------

// The frame rate cap cheat sets +0x84 from its first word; the date cheat
// sets the date (its slot 6) from two or three words (the year defaults to
// the current one) and steps the clock.
uint8_t __fastcall OnCheat(char* self, void*, uint32_t cheat, void* command)
{
    static bool logged = false;
    FirstCall("OnCheat", logged);
    using AtolFn = int32_t(__cdecl*)(const char* text);
    const auto atol = reinterpret_cast<AtolFn>(addr::SC4_atol);
    if (cheat != kFpsCheat && cheat != kDateCheat)
        return 1;
    alignas(4) char words[0x3C];
    CallThis<void*>(addr::SC4Tokenizer_SC4Tokenizer, words, Virtual<const char*>(command, 0x14));
    const int32_t count = CallThis<int32_t>(addr::SC4Tokenizer_Count, words);
    if (cheat == kFpsCheat) {
        if (count > 1)
            At<int32_t>(self, field::kFpsCap) = atol(CallThis<const char*>(addr::SC4Tokenizer_Word, words, 1u));
    } else if (count >= 3) {
        const int32_t first = atol(CallThis<const char*>(addr::SC4Tokenizer_Word, words, 1u));
        const int32_t second = atol(CallThis<const char*>(addr::SC4Tokenizer_Word, words, 2u));
        const int32_t third = CallThis<int32_t>(addr::SC4Tokenizer_Count, words) > 3
                                  ? atol(CallThis<const char*>(addr::SC4Tokenizer_Word, words, 3u))
                                  : Virtual<int32_t>(self + field::kDateInterface, 0x34);
        Virtual<uint32_t>(self + field::kDateInterface, 0x18, first, second, third);
        CallThis<void>(addr::cSC4Simulator_ClockStep, self);
    }
    CallThis<void>(addr::SC4Tokenizer_Destroy, words);
    return 1;
}

// The date query (0x8BAC4B43, from the date command): for each of the six
// questions (void 1 slot 11) the date's answer as a script number (void 2
// slot 7); the first unknown one ends it.
void AnswerDateQuery(char* self, void* request, void* response)
{
    struct Question {
        std::uintptr_t name;
        std::size_t slot;
    };
    static const Question kQuestions[] = {{0x00AB2F3C, 0x34}, {0x00AB2F34, 0x5C}, {0x00AB2F24, 0x3C},
                                          {0x00AB2F18, 0x30}, {0x00AB2F0C, 0x38}, {0x00AB2F04, 0x68}};
    void* const date = self + field::kDateInterface;
    for (uint32_t i = 0; i < 6; ++i) {
        const Question* match = nullptr;
        for (const Question& question : kQuestions)
            if (Virtual<uint8_t>(request, 0x2C, i, reinterpret_cast<const char*>(question.name), 0u)) {
                match = &question;
                break;
            }
        if (!match)
            return;
        const uint32_t answer = Virtual<uint32_t>(date, match->slot);
        alignas(4) char number[0x14];
        void* const value = CallThis<void*>(addr::SC4ScriptNumber_FromUint, number, answer);
        Virtual<uint32_t>(response, 0x1C, i, value);
        CallThis<void>(addr::SC4ScriptNumber_Destroy, number);
    }
}

uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - 0x24;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    void* const request = Virtual<void*>(message, 0x28);
    void* const response = Virtual<void*>(message, 0x3C);
    if (type == kCheat) {
        const uint32_t cheat = Virtual<uint32_t>(message, 0x24);
        CallThis<uint8_t>(addr::cSC4Simulator_OnCheat, self, cheat, Virtual<void*>(message, 0x3C));
    } else if (type == kDoPostCityInit) {
        Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x18, part, kDoPostCityInit);
        Virtual<uint32_t>(self, 0x34); // HiddenResume
    } else if (type == kDateQuery) {
        AnswerDateQuery(self, request, response);
    }
    return 1;
}

bool InstallMessages(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4Simulator_vftable_24);
    if (vtable[3] != addr::cSC4Simulator_DoMessage) {
        log::Write("simulator messages: slot 3 holds %08X; not installing", static_cast<unsigned>(vtable[3]));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(&vtable[3], sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    vtable[3] = reinterpret_cast<std::uintptr_t>(&DoMessage);
    VirtualProtect(&vtable[3], sizeof(*vtable), old, &old);
    return true;
}

using CheatFn = uint8_t(__fastcall*)(char*, void*, uint32_t, void*);
CheatFn g_originalCheat = nullptr;

bool InstallMessagesAndCheats(registry::Mode mode)
{
    return InstallMessages(mode) && Detour(addr::cSC4Simulator_OnCheat, &OnCheat, &g_originalCheat);
}

const bool g_registeredMessages = registry::Register({
    "simulators.core.messages",
    "simulators/core",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4Simulator_vftable_24,
    "Our simulator DoMessage: DoPostCityInit, the date query, the two cheats (docs/simulator.md)",
    false,
    InstallMessagesAndCheats,
});

// --- Saving: cIGZSerializable (vtable 0x00AB2FA0) at +0x4 ---------------------------------
//
// Version 4: +0x2D and the established flag (uint8 each), the pause count,
// the speed and the sim time (int32 each), the time per tick (uint32), for
// each agent type its data (uint32) and whether its unit passed (bool), the
// date. Before version 4 a stopwatch followed the agent types (read and
// dropped).

uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    using BoolFn = void(__cdecl*)(void* stream, uint32_t value);
    using DateFn = void(__cdecl*)(void* stream, void* date);
    char* const self = part - 0x4;
    Virtual<uint32_t>(stream, 0x1C, uint32_t{4});
    Virtual<uint32_t>(stream, 0x14, static_cast<uint32_t>(At<uint8_t>(self, field::kSaved2D)));
    Virtual<uint32_t>(stream, 0x14, static_cast<uint32_t>(At<uint8_t>(self, field::kEstablished)));
    Virtual<uint32_t>(stream, 0x20, At<uint32_t>(self, field::kPaused));
    Virtual<uint32_t>(stream, 0x20, At<uint32_t>(self, field::kSpeed));
    Virtual<uint32_t>(stream, 0x20, At<uint32_t>(self, field::kSaved40));
    Virtual<uint32_t>(stream, 0x24, At<uint32_t>(self, field::kMaxMsPerTick));
    for (int type = 0; type < kAgentTypes; ++type) {
        Virtual<uint32_t>(stream, 0x24, At<uint32_t>(self, field::kAgentData + 4 * type));
        reinterpret_cast<BoolFn>(addr::SC4Stream_WriteBool)(stream, At<uint8_t>(self, field::kAgentDue + type));
    }
    reinterpret_cast<DateFn>(addr::SC4Stream_WriteDate)(stream, self + field::kDate);
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    using BoolFn = void(__cdecl*)(void* stream, uint8_t* value);
    using ObjectFn = void(__cdecl*)(void* stream, void* object);
    char* const self = part - 0x4;
    uint16_t version = 0;
    Virtual<uint32_t>(stream, 0x1C, &version);
    if (version > 4)
        return 0;
    Virtual<uint32_t>(stream, 0x14, &At<uint8_t>(self, field::kSaved2D));
    Virtual<uint32_t>(stream, 0x14, &At<uint8_t>(self, field::kEstablished));
    Virtual<uint32_t>(stream, 0x20, &At<int32_t>(self, field::kPaused));
    Virtual<uint32_t>(stream, 0x20, &At<int32_t>(self, field::kSpeed));
    Virtual<uint32_t>(stream, 0x20, &At<int32_t>(self, field::kSaved40));
    Virtual<uint32_t>(stream, 0x24, &At<uint32_t>(self, field::kMaxMsPerTick));
    for (int type = 0; type < kAgentTypes; ++type) {
        Virtual<uint32_t>(stream, 0x24, &At<uint32_t>(self, field::kAgentData + 4 * type));
        reinterpret_cast<BoolFn>(addr::SC4Stream_ReadBool)(stream, &At<uint8_t>(self, field::kAgentDue + type));
    }
    if (version < 4) {
        alignas(8) char watch[0x18];
        CallThis<void*>(addr::SC4Stopwatch_Construct, watch, 4u);
        reinterpret_cast<ObjectFn>(addr::SC4Stream_ReadStopwatch)(stream, watch);
        CallThis<void>(addr::SC4Stopwatch_Destruct, watch);
    }
    reinterpret_cast<ObjectFn>(addr::SC4Stream_ReadDate)(stream, self + field::kDate);
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

using SerializeFn = uint8_t(__fastcall*)(char*, void*, void*);
SerializeFn g_originalWrite = nullptr;
SerializeFn g_originalRead = nullptr;

bool InstallSave(registry::Mode)
{
    return game::IsBuild641() && Detour(addr::cSC4Simulator_Write, &Write, &g_originalWrite) &&
           Detour(addr::cSC4Simulator_Read, &Read, &g_originalRead);
}

const bool g_registeredSave = registry::Register({
    "simulators.core.save",
    "simulators/core",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Simulator_Write,
    "Our simulator Write (version 4) and Read (versions up to 4) (docs/simulator.md)",
    false,
    InstallSave,
});

} // namespace
} // namespace opensc4::core_sim
