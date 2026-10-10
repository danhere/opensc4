// The toxic spill (docs/disasters.md): cSC4DisasterManagerToxic (class
// 0xEBFB0402, disaster 0xEBFB0404) and its one cSC4DisasterInstanceToxic,
// which keeps the toxic cells and counts their time down. Replacement, off
// by default:
//
//   simulators.disasters.toxic: the manager (QueryInterface, Init, Shutdown,
//     the start at a cell, the ToxicSpill command, the counts) and the
//     instance (Init, Shutdown, adding a cell, the tick, the counts).
//
// The objects, their constructors and destructors stay the game's.
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

constexpr uint32_t kToxic = 0xEBFB0404;
constexpr uint32_t kCommand = 0x0BFB41BA;

namespace instance {
constexpr std::size_t kInited = 0x10;
constexpr std::size_t kLayer = 0x14;     // the layer's interface
constexpr std::size_t kManager = 0x18;   // the manager object
constexpr std::size_t kStopwatch = 0x20;
constexpr std::size_t kNextTick = 0x38;  // in the stopwatch's milliseconds
constexpr std::size_t kCells = 0x3C;     // list of toxic occupants
} // namespace instance

namespace occupant {
constexpr std::size_t kTime = 0x38;
constexpr std::size_t kX = 0x40;
constexpr std::size_t kZ = 0x44;
} // namespace occupant

struct Item { // std::list node
    Item* next;
    Item* previous;
    char* occupant;
};

void* Layer()
{
    return Virtual<void*>(Global<void*>(addr::SC4_City), 0x120);
}

// The manager.

uint8_t __fastcall ToxicManagerQueryInterface(char* manager, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid != 0x4C1E8345)
        return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cSC4DisasterManager_QueryInterface)(
            manager, iid, out);
    *out = manager ? manager + 0x24 : nullptr;
    Virtual<uint32_t>(manager, 0x4);
    return 1;
}

uint8_t __fastcall ToxicManagerInit(char* interface, void*)
{
    const uint8_t result =
        reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterManager_Init)(interface);
    Virtual<void>(Global<void*>(addr::SC4_Commands), 0x18, kCommand, "ToxicSpill", int32_t{0});
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x14, interface + 0x4, kCommand);
    return result;
}

uint8_t __fastcall ToxicManagerShutdown(char* interface, void*)
{
    Virtual<void>(Global<void*>(addr::SC4_Commands), 0x24, kCommand);
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x18, interface + 0x4, kCommand);
    return reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterManager_Shutdown)(interface);
}

// The game's 0x0057EFD0: the instance, with a reference (two when it has
// just been made, as in the game).
char* __fastcall GetInstance(char* manager, void*)
{
    Item* const list = At<Item*>(manager, 0x14);
    if (list->next == list) {
        void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x40);
        char* const made = memory ? reinterpret_cast<char*(__thiscall*)(void*)>(
                                        addr::cSC4DisasterInstanceToxic_Construct)(memory)
                                  : nullptr;
        reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterInstanceToxic_Init)(made);
        Virtual<uint32_t>(made, 0x4);
        return made;
    }
    char* found = nullptr;
    Virtual<uint8_t>(list->next->occupant, 0x0, uint32_t{0xEBFB0403}, &found);
    return found;
}

char* Instance(char* manager)
{
    char* const found = reinterpret_cast<char*(__thiscall*)(void*)>(addr::cSC4DisasterManagerToxic_GetInstance)(manager);
    if (found)
        Virtual<uint32_t>(found, 0x4);
    return found;
}

void Release(char* object)
{
    if (object)
        Virtual<uint32_t>(object, 0x8);
}

void* __fastcall ToxicAttemptDisasterStart(char* interface, void*, int32_t x, int32_t z)
{
    char* const toxic = Instance(interface - 0x8);
    reinterpret_cast<uint8_t(__thiscall*)(void*, int32_t, int32_t, uint32_t)>(addr::cSC4DisasterInstanceToxic_AddToxic)(
        toxic, x, z, 0x78);
    Release(toxic);
    return nullptr;
}

// Main vtable slot 6: the ToxicSpill command's three numbers.
uint8_t __fastcall ToxicOnMessage(char* manager, void*, void* message)
{
    if (Virtual<uint32_t>(message, 0x10) != kCommand)
        return 1;
    void* const arguments = Virtual<void*>(message, 0x28);
    int32_t values[3];
    for (int32_t i = 0; i < 3; ++i)
        values[i] = Virtual<int32_t>(Virtual<void*>(arguments, 0x14, i), 0x6C);
    char* const toxic = Instance(manager);
    reinterpret_cast<uint8_t(__thiscall*)(void*, int32_t, int32_t, uint32_t)>(addr::cSC4DisasterInstanceToxic_AddToxic)(
        toxic, values[0], values[1], static_cast<uint32_t>(values[2]));
    Release(toxic);
    return 1;
}

// The interface at +0x24.
int32_t __fastcall ToxicManagerCountInRect(char* extra, void*, const int32_t* rect)
{
    char* const manager = extra - 0x24;
    Item* const list = At<Item*>(manager, 0x14);
    if (list->next == list)
        return 0;
    char* const toxic = Instance(manager);
    const int32_t count =
        reinterpret_cast<int32_t(__thiscall*)(void*, const int32_t*)>(addr::cSC4DisasterInstanceToxic_CountInRect)(toxic,
                                                                                                                 rect);
    Release(toxic);
    return count;
}

int32_t __fastcall ToxicManagerCount(char* extra, void*)
{
    char* const manager = extra - 0x24;
    Item* const list = At<Item*>(manager, 0x14);
    if (list->next == list)
        return 0;
    char* const toxic = Instance(manager);
    Item* const cells = At<Item*>(toxic, instance::kCells);
    int32_t count = 0;
    for (Item* item = cells->next; item != cells; item = item->next)
        ++count;
    Release(toxic);
    return count;
}

// The instance.

uint8_t __fastcall ToxicQueryInterface(char* toxic, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid == 0x22223BA4)
        *out = toxic ? toxic + 0x8 : nullptr;
    else if (iid == 0xEBFB0403)
        *out = toxic;
    else
        return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cRZBaseUnknown_QueryInterface1)(
            toxic, iid, out);
    Virtual<uint32_t>(toxic, 0x4);
    return 1;
}

uint8_t __fastcall ToxicInit(char* toxic, void*)
{
    if (At<uint8_t>(toxic, instance::kInited))
        return 1;
    At<uint8_t>(toxic, instance::kInited) = 1;
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4Stopwatch_Start)(toxic + instance::kStopwatch);
    void* const layer = Layer();
    At<void*>(toxic, instance::kLayer) = layer;
    Virtual<uint32_t>(layer, 0x4);
    if (char* const manager = Virtual<char*>(At<void*>(toxic, instance::kLayer), 0x1C, kToxic)) {
        At<char*>(toxic, instance::kManager) = manager - 0x8;
        Virtual<uint32_t>(manager - 0x8, 0x4);
    }
    char* const interface = toxic + 0x8;
    Virtual<uint32_t>(At<char*>(toxic, instance::kManager) + 0x8, 0x20, interface); // AddInstanceToList
    if (void* const ticks = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
        Virtual<void>(ticks, 0x14, toxic + 0xC, int32_t{0xFF});
    Virtual<uint32_t>(At<void*>(toxic, instance::kLayer), 0x20, interface); // DisasterHasBegun
    return 1;
}

uint8_t __fastcall ToxicShutdown(char* interface, void*)
{
    char* const toxic = interface - 0x8;
    if (!At<uint8_t>(toxic, instance::kInited))
        return 1;
    At<uint8_t>(toxic, instance::kInited) = 0;
    if (void* const ticks = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
        Virtual<void>(ticks, 0x18, toxic + 0xC, int32_t{0xFF});
    if (void* const layer = At<void*>(toxic, instance::kLayer)) {
        Virtual<void>(layer, 0x24);            // DisasterHasEnded
        Virtual<uint8_t>(At<void*>(toxic, instance::kLayer), 0x38, interface); // UnregisterDisasterLocations
        Virtual<uint32_t>(At<void*>(toxic, instance::kLayer), 0x8);
        At<void*>(toxic, instance::kLayer) = nullptr;
    }
    Virtual<uint8_t>(At<char*>(toxic, instance::kManager) + 0x8, 0x24, interface); // RemoveInstanceFromList
    if (char* const manager = At<char*>(toxic, instance::kManager)) {
        Virtual<uint32_t>(manager, 0x8);
        At<char*>(toxic, instance::kManager) = nullptr;
    }
    return 1;
}

int32_t __fastcall ToxicCountInRect(char* toxic, void*, const int32_t* rect)
{
    Item* const cells = At<Item*>(toxic, instance::kCells);
    int32_t count = 0;
    for (Item* item = cells->next; item != cells; item = item->next) {
        const int32_t x = At<int32_t>(item->occupant, occupant::kX);
        const int32_t z = At<int32_t>(item->occupant, occupant::kZ);
        if (x >= rect[0] && z >= rect[1] && x <= rect[2] && z <= rect[3])
            ++count;
    }
    return count;
}

// The game's 0x0057EE40.
void __fastcall ToxicTick(char* toxic, void*)
{
    const uint32_t now = reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::GZStopwatch_ElapsedMilliseconds)(
        toxic + instance::kStopwatch);
    if (now < At<uint32_t>(toxic, instance::kNextTick))
        return;
    At<uint32_t>(toxic, instance::kNextTick) = now + 1000;
    for (Item* item = At<Item*>(toxic, instance::kCells)->next; item != At<Item*>(toxic, instance::kCells);) {
        char* const cell = item->occupant;
        Item* const next = item->next;
        uint32_t time = At<uint32_t>(cell, occupant::kTime);
        if (const uint32_t responders = Virtual<uint32_t>(cell, 0x10))
            time = time >= responders * 4 ? time - responders * 4 : 0;
        else
            --time;
        At<uint32_t>(cell, occupant::kTime) = time;
        if (!time) {
            char* const part = cell + 0x4;
            Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0x54, part, int32_t{1}, int32_t{0});
            Virtual<void>(part, 0x10);
            item->previous->next = item->next;
            item->next->previous = item->previous;
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(item);
            Virtual<uint32_t>(cell, 0x8);
            Virtual<uint8_t>(Layer(), 0x3C, toxic + 0x8, part); // UnregisterDisasterLocationByOccupant
        }
        item = next;
    }
    Item* const cells = At<Item*>(toxic, instance::kCells);
    if (cells->next == cells)
        Virtual<uint8_t>(toxic + 0x8, 0xC); // Shutdown
}

uint8_t __fastcall ToxicDoMessage(char* target, void*, void* message)
{
    if (Virtual<uint32_t>(message, 0x10) == 0x86AD10EE)
        reinterpret_cast<void(__thiscall*)(void*)>(addr::cSC4DisasterInstanceToxic_Tick)(target - 0xC);
    return 1;
}

uint8_t __fastcall AddToxic(char* toxic, void*, int32_t x, int32_t z, uint32_t time)
{
    Item* const cells = At<Item*>(toxic, instance::kCells);
    for (Item* item = cells->next; item != cells; item = item->next) {
        if (At<int32_t>(item->occupant, occupant::kX) == x && At<int32_t>(item->occupant, occupant::kZ) == z) {
            At<uint32_t>(item->occupant, occupant::kTime) += time;
            return 1;
        }
    }
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x5C);
    char* const cell = memory ? reinterpret_cast<char*(__thiscall*)(void*)>(addr::cSC4ToxicOccupant_Construct)(memory)
                              : nullptr;
    Virtual<uint32_t>(cell, 0x4);
    reinterpret_cast<void(__thiscall*)(void*, int32_t, int32_t)>(addr::cSC4ToxicOccupant_SetCell)(cell, x, z);
    char* const part = cell + 0x4;
    Virtual<void>(part, 0xC);
    At<uint32_t>(cell, occupant::kTime) = time;
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0x50, part, int32_t{0});
    Item* const list = At<Item*>(toxic, instance::kCells);
    auto* const item = static_cast<Item*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(0xC));
    item->occupant = cell;
    Item* const last = list->previous;
    item->next = list;
    item->previous = last;
    last->next = item;
    list->previous = item;
    Virtual<uint8_t>(Layer(), 0x34, toxic + 0x8, x, z, part, int32_t{0}, int32_t{0}); // RegisterDisasterLocation
    return 1;
}

bool InstallToxic(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4DisasterManagerToxic_QueryInterface, reinterpret_cast<void*>(&ToxicManagerQueryInterface)},
        {addr::cSC4DisasterInstanceToxic_QueryInterface, reinterpret_cast<void*>(&ToxicQueryInterface)},
        {addr::cSC4DisasterInstanceToxic_Shutdown, reinterpret_cast<void*>(&ToxicShutdown)},
        {addr::cSC4DisasterManagerToxic_Init, reinterpret_cast<void*>(&ToxicManagerInit)},
        {addr::cSC4DisasterManagerToxic_Shutdown, reinterpret_cast<void*>(&ToxicManagerShutdown)},
        {addr::cSC4DisasterInstanceToxic_Init, reinterpret_cast<void*>(&ToxicInit)},
        {addr::cSC4DisasterInstanceToxic_CountInRect, reinterpret_cast<void*>(&ToxicCountInRect)},
        {addr::cSC4DisasterInstanceToxic_Tick, reinterpret_cast<void*>(&ToxicTick)},
        {addr::cSC4DisasterInstanceToxic_DoMessage, reinterpret_cast<void*>(&ToxicDoMessage)},
        {addr::cSC4DisasterManagerToxic_GetInstance, reinterpret_cast<void*>(&GetInstance)},
        {addr::cSC4DisasterInstanceToxic_AddToxic, reinterpret_cast<void*>(&AddToxic)},
        {addr::cSC4DisasterManagerToxic_CountInRect, reinterpret_cast<void*>(&ToxicManagerCountInRect)},
        {addr::cSC4DisasterManagerToxic_Count, reinterpret_cast<void*>(&ToxicManagerCount)},
        {addr::cSC4DisasterManagerToxic_AttemptDisasterStart, reinterpret_cast<void*>(&ToxicAttemptDisasterStart)},
        {addr::cSC4DisasterManagerToxic_OnMessage, reinterpret_cast<void*>(&ToxicOnMessage)},
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

const bool g_toxicRegistered = registry::Register({
    "simulators.disasters.toxic",
    "simulators/disasters",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DisasterInstanceToxic_Tick,
    "Our toxic spill manager and instance (docs/disasters.md)",
    false,
    InstallToxic,
});

} // namespace
} // namespace opensc4::disasters
