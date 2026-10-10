// The effect disaster (docs/disasters.md): cSC4DisasterManagerEffect
// (disaster 0x6A7B4397) and cSC4DisasterInstanceEffect: a disaster that is
// nothing but a visual effect a tool started; the instance registers the
// effect's cell with the layer and ends when the effect reports it is
// over. Replacement, off by default:
//
//   simulators.disasters.effect: the manager's effect part (the start, the
//     first instance's location and stop) and its QueryInterface, and the
//     instance (Init, Shutdown, the messages, the location).
//
// The objects' constructors and destructors stay the game's. The
// instance's interface is at +8 and its message target at +0xC; the
// manager's effect part (0xCA7B3F3C) is at +0x24.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <utility>

namespace opensc4::disasters {
namespace {

constexpr uint32_t kEffectDisaster = 0x6A7B4397;
constexpr uint32_t kEffectOver = 0x2A7B582D;
constexpr uint32_t kEffectMoved = 0x2A7B582E;

namespace field { // the instance
constexpr std::size_t kX = 0x10;
constexpr std::size_t kZ = 0x14;
constexpr std::size_t kInited = 0x18;
constexpr std::size_t kLayer = 0x20;
constexpr std::size_t kManager = 0x24;
constexpr std::size_t kEffect = 0x28;
} // namespace field

// The manager.

uint8_t __fastcall EffectManagerQueryInterface(char* manager, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid == 0xCA7B3F3C) {
        void* const part = manager ? manager + 0x24 : nullptr;
        *out = part;
        if (part) {
            Virtual<uint32_t>(manager, 0x4);
            return 1;
        }
    }
    return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cSC4DisasterManager_QueryInterface)(
        manager, iid, out);
}

// The first instance on the manager's list, from the effect part.
char* FirstInstance(char* extra)
{
    if (!Virtual<int32_t>(extra - 0x1C, 0x28))
        return nullptr;
    char* const list = At<char*>(extra - 0x10, 0x0);
    char* const part = At<char*>(At<char*>(list, 0x0), 0x8);
    return part ? part - 0x8 : nullptr;
}

void __fastcall EffectManagerSetLocation(char* extra, void*, const float* position)
{
    if (char* const instance = FirstInstance(extra))
        Virtual<void>(instance, 0x1C, position);
}

void __fastcall EffectManagerStopEffect(char* extra, void*)
{
    if (char* const instance = FirstInstance(extra))
        Virtual<void>(instance, 0x20);
}

// The instance.

uint8_t __fastcall EffectQueryInterface(char* instance, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid != 0x22223BA4)
        return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cRZBaseUnknown_QueryInterface1)(
            instance, iid, out);
    *out = instance + 0x8;
    Virtual<uint32_t>(instance, 0x4);
    return 1;
}

uint8_t __fastcall EffectGetLocation(char* part, void*, int32_t* x, int32_t* z)
{
    *x = At<int32_t>(part, 0x8);
    *z = At<int32_t>(part, 0xC);
    return 1;
}

char* __fastcall EffectGetManager(char* part, void*)
{
    char* const manager = At<char*>(part, 0x1C);
    return manager ? manager + 0x8 : nullptr;
}

uint8_t __fastcall EffectEnd(char* part, void*)
{
    return Virtual<uint8_t>(part, 0xC);
}

void __fastcall EffectStopEffect(char* instance, void*)
{
    int32_t value = 0;
    Virtual<void>(At<void*>(instance, field::kEffect), 0x30, int32_t{0xB}, &value, int32_t{1});
}

// The game's 0x00577010.
uint8_t __fastcall EffectInit(char* instance, void*)
{
    if (At<uint8_t>(instance, field::kInited))
        return 1;
    At<uint8_t>(instance, field::kInited) = 1;
    if (void* const effect = At<void*>(instance, field::kEffect)) {
        Virtual<void>(effect, 0x28, kEffectOver, instance);
        void* who = instance;
        Virtual<void>(At<void*>(instance, field::kEffect), 0x34, int32_t{0xD}, &who, int32_t{1});
        Virtual<void>(At<void*>(instance, field::kEffect), 0xC, int32_t{0});
    }
    void* const layer = Virtual<void*>(Global<void*>(addr::SC4_City), 0x120);
    At<void*>(instance, field::kLayer) = layer;
    Virtual<uint32_t>(layer, 0x4);
    if (char* const manager = Virtual<char*>(At<void*>(instance, field::kLayer), 0x1C, kEffectDisaster)) {
        At<char*>(instance, field::kManager) = manager - 0x8;
        Virtual<uint32_t>(manager - 0x8, 0x4);
    }
    char* const part = instance + 0x8;
    Virtual<uint32_t>(At<char*>(instance, field::kManager) + 0x8, 0x20, part); // AddInstanceToList
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        Virtual<void>(server, 0x14, instance + 0xC, kEffectOver);
        Virtual<void>(server, 0x14, instance + 0xC, kEffectMoved);
    }
    Virtual<uint32_t>(At<void*>(instance, field::kLayer), 0x20, part); // DisasterHasBegun
    return 1;
}

// The game's 0x005770E0, on the interface at +8.
uint8_t __fastcall EffectShutdown(char* part, void*)
{
    char* const instance = part - 0x8;
    if (!At<uint8_t>(instance, field::kInited))
        return 1;
    At<uint8_t>(instance, field::kInited) = 0;
    if (void* const effect = At<void*>(instance, field::kEffect)) {
        if (Virtual<uint8_t>(effect, 0x14))
            Virtual<void>(At<void*>(instance, field::kEffect), 0x10, int32_t{1});
    }
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        Virtual<void>(server, 0x18, instance + 0xC, kEffectOver);
        Virtual<void>(server, 0x18, instance + 0xC, kEffectMoved);
    }
    if (At<void*>(instance, field::kLayer)) {
        Virtual<void>(At<void*>(instance, field::kLayer), 0x24);             // DisasterHasEnded
        Virtual<uint8_t>(At<void*>(instance, field::kLayer), 0x38, part);    // UnregisterDisasterLocations
        Virtual<uint32_t>(At<void*>(instance, field::kLayer), 0x8);
        At<void*>(instance, field::kLayer) = nullptr;
    }
    Virtual<uint8_t>(At<char*>(instance, field::kManager) + 0x8, 0x24, part); // RemoveInstanceFromList
    if (void* const manager = At<void*>(instance, field::kManager)) {
        Virtual<uint32_t>(manager, 0x8);
        At<void*>(instance, field::kManager) = nullptr;
    }
    return 1;
}

// The game's 0x005771B0.
void __fastcall EffectSetLocation(char* instance, void*, const float* position)
{
    Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &At<int32_t>(instance, field::kX),
                  &At<int32_t>(instance, field::kZ));
    Virtual<uint8_t>(At<void*>(instance, field::kLayer), 0x34, instance + 0x8, At<int32_t>(instance, field::kX),
                     At<int32_t>(instance, field::kZ), int32_t{0}, int32_t{0}, int32_t{0});
}

uint8_t __fastcall EffectDoMessage(char* target, void*, void* message)
{
    char* const instance = target - 0xC;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == kEffectOver) {
        if (Virtual<char*>(message, 0x24) == instance)
            Virtual<uint8_t>(target - 0x4, 0xC); // Shutdown
    } else if (type == kEffectMoved) {
        if (Virtual<char*>(message, 0x28) == instance) {
            At<int32_t>(instance, field::kX) = Virtual<int32_t>(message, 0x38);
            At<int32_t>(instance, field::kZ) = Virtual<int32_t>(message, 0x4C);
        }
    }
    return 1;
}

// The effect part's slot 3: the tool's start.
char* __fastcall EffectManagerStart(char*, void*, void* effect, const float* position, uint32_t type)
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x2C);
    char* const instance =
        memory ? reinterpret_cast<char*(__thiscall*)(void*, void*, uint32_t)>(addr::cSC4DisasterInstanceEffect_Construct)(
                     memory, effect, type)
               : nullptr;
    EffectInit(instance, nullptr);
    Virtual<void>(instance, 0x1C, position);
    return instance + 0x8;
}

bool InstallEffect(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4DisasterManagerEffect_QueryInterface, reinterpret_cast<void*>(&EffectManagerQueryInterface)},
        {addr::cSC4DisasterInstanceEffect_DoMessage, reinterpret_cast<void*>(&EffectDoMessage)},
        {addr::cSC4DisasterInstanceEffect_GetLocation, reinterpret_cast<void*>(&EffectGetLocation)},
        {addr::cSC4DisasterInstanceEffect_GetManager, reinterpret_cast<void*>(&EffectGetManager)},
        {addr::cSC4DisasterInstanceEffect_QueryInterface, reinterpret_cast<void*>(&EffectQueryInterface)},
        {addr::cSC4DisasterInstanceEffect_End, reinterpret_cast<void*>(&EffectEnd)},
        {addr::cSC4DisasterInstanceEffect_StopEffect, reinterpret_cast<void*>(&EffectStopEffect)},
        {addr::cSC4DisasterInstanceEffect_Init, reinterpret_cast<void*>(&EffectInit)},
        {addr::cSC4DisasterInstanceEffect_Shutdown, reinterpret_cast<void*>(&EffectShutdown)},
        {addr::cSC4DisasterInstanceEffect_SetLocation, reinterpret_cast<void*>(&EffectSetLocation)},
        {addr::cSC4DisasterManagerEffect_SetLocation, reinterpret_cast<void*>(&EffectManagerSetLocation)},
        {addr::cSC4DisasterManagerEffect_StopEffect, reinterpret_cast<void*>(&EffectManagerStopEffect)},
        {addr::cSC4DisasterManagerEffect_Start, reinterpret_cast<void*>(&EffectManagerStart)},
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

const bool g_effectRegistered = registry::Register({
    "simulators.disasters.effect",
    "simulators/disasters",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DisasterManagerEffect_Start,
    "Our effect disaster manager and instance (docs/disasters.md)",
    false,
    InstallEffect,
});

} // namespace
} // namespace opensc4::disasters
