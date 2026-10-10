// The disaster managers' shared base (docs/disasters.md): the list of a
// manager's disaster instances. Replacement, off by default:
//
//   simulators.disasters.managers: QueryInterface, Init, Shutdown, adding
//     and removing instances (deferred while the list is walked), the count
//     of active instances and the message forwarding.
//
// `manager` is the object; the interface is at +8 and the message target at
// +0xC. GetInstances (4 bytes) stays the game's.
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

namespace field {
constexpr std::size_t kInited = 0x10;
constexpr std::size_t kInstances = 0x14; // list of instances
constexpr std::size_t kWalking = 0x18;   // the list is being walked
constexpr std::size_t kToAdd = 0x1C;     // instances to add after the walk
constexpr std::size_t kToRemove = 0x20;  // instances to remove after the walk
} // namespace field

struct Item { // std::list node
    Item* next;
    Item* previous;
    void* instance;
};

void PushBack(Item* list, void* instance)
{
    auto* const item = static_cast<Item*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(0xC));
    item->instance = instance;
    Item* const last = list->previous;
    item->next = list;
    item->previous = last;
    last->next = item;
    list->previous = item;
}

void Clear(Item* list)
{
    for (Item* item = list->next; item != list;) {
        Item* const next = item->next;
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(item);
        item = next;
    }
    list->next = list;
    list->previous = list;
}

Item* Find(Item* list, void* instance)
{
    for (Item* item = list->next; item != list; item = item->next) {
        if (item->instance == instance)
            return item;
    }
    return nullptr;
}

uint8_t __fastcall ManagerQueryInterface(char* manager, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    void* part = nullptr;
    if (iid == 0x021E9CBF)
        part = manager ? manager + 0x8 : nullptr;
    else if (iid == 0x452294AA)
        part = manager ? manager + 0xC : nullptr;
    *out = part;
    if (!part)
        return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cRZBaseUnknown_QueryInterface1)(
            manager, iid, out);
    Virtual<uint32_t>(manager, 0x4);
    return 1;
}

uint8_t __fastcall ManagerInit(char* interface, void*)
{
    At<uint8_t>(interface - 0x8, field::kInited) = 1;
    return 1;
}

uint32_t __fastcall AddInstanceToList(char* interface, void*, void* instance)
{
    char* const manager = interface - 0x8;
    if (At<uint8_t>(manager, field::kWalking)) {
        PushBack(At<Item*>(manager, field::kToAdd), instance);
        return 1;
    }
    Item* const list = At<Item*>(manager, field::kInstances);
    if (!Find(list, instance)) {
        PushBack(list, instance);
        Virtual<uint32_t>(instance, 0x4);
    }
    return 1;
}

uint8_t __fastcall RemoveInstanceFromList(char* interface, void*, void* instance)
{
    char* const manager = interface - 0x8;
    if (At<uint8_t>(manager, field::kWalking)) {
        PushBack(At<Item*>(manager, field::kToRemove), instance);
        return 1;
    }
    Item* const item = Find(At<Item*>(manager, field::kInstances), instance);
    if (!item)
        return 0;
    item->previous->next = item->next;
    item->next->previous = item->previous;
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(item);
    Virtual<uint32_t>(instance, 0x8);
    return 1;
}

int32_t __fastcall GetActiveInstanceCount(char* interface, void*)
{
    Item* const list = At<Item*>(interface - 0x8, field::kInstances);
    int32_t count = 0;
    for (Item* item = list->next; item != list;) {
        void* const instance = item->instance;
        item = item->next;
        if (Virtual<int32_t>(instance, 0x1C) == 1) // its phase
            ++count;
    }
    return count;
}

// The game's 0x005779C0.
void __fastcall FlushPending(char* manager, void*)
{
    char* const interface = manager + 0x8;
    Item* const toAdd = At<Item*>(manager, field::kToAdd);
    for (Item* item = toAdd->next; item != toAdd; item = item->next)
        Virtual<uint32_t>(interface, 0x20, item->instance);
    Clear(At<Item*>(manager, field::kToAdd));
    Item* const toRemove = At<Item*>(manager, field::kToRemove);
    for (Item* item = toRemove->next; item != toRemove; item = item->next)
        Virtual<uint8_t>(interface, 0x24, item->instance);
    Clear(At<Item*>(manager, field::kToRemove));
}

uint8_t __fastcall ManagerShutdown(char* interface, void*)
{
    char* const manager = interface - 0x8;
    if (!At<uint8_t>(manager, field::kInited))
        return 1;
    At<uint8_t>(manager, field::kWalking) = 1;
    Item* const list = At<Item*>(manager, field::kInstances);
    for (Item* item = list->next; item != list; item = item->next) {
        void* const instance = item->instance;
        Virtual<uint8_t>(instance, 0xC);
        Virtual<uint32_t>(instance, 0x8);
    }
    Clear(At<Item*>(manager, field::kInstances));
    At<uint8_t>(manager, field::kWalking) = 0;
    reinterpret_cast<void(__thiscall*)(void*)>(addr::cSC4DisasterManager_FlushPending)(manager);
    At<uint8_t>(manager, field::kInited) = 0;
    return 1;
}

// The message target at +0xC: the message, as its 0x65297976 interface, to
// the manager's own handler (main vtable slot 6).
uint8_t __fastcall ManagerDoMessage(char* target, void*, void* message)
{
    void* standard = nullptr;
    Virtual<uint8_t>(message, 0x0, uint32_t{0x65297976}, &standard);
    const uint8_t result = Virtual<uint8_t>(target - 0xC, 0x18, standard);
    if (standard)
        Virtual<uint32_t>(standard, 0x8);
    return result;
}

bool InstallManagers(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4DisasterManager_QueryInterface, reinterpret_cast<void*>(&ManagerQueryInterface)},
        {addr::cSC4DisasterManager_Init, reinterpret_cast<void*>(&ManagerInit)},
        {addr::cSC4DisasterManager_DoMessage, reinterpret_cast<void*>(&ManagerDoMessage)},
        {addr::cSC4DisasterManager_GetActiveInstanceCount, reinterpret_cast<void*>(&GetActiveInstanceCount)},
        {addr::cSC4DisasterManager_FlushPending, reinterpret_cast<void*>(&FlushPending)},
        {addr::cSC4DisasterManager_Shutdown, reinterpret_cast<void*>(&ManagerShutdown)},
        {addr::cSC4DisasterManager_AddInstanceToList, reinterpret_cast<void*>(&AddInstanceToList)},
        {addr::cSC4DisasterManager_RemoveInstanceFromList, reinterpret_cast<void*>(&RemoveInstanceFromList)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_managersRegistered = registry::Register({
    "simulators.disasters.managers",
    "simulators/disasters",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DisasterManager_Shutdown,
    "Our disaster manager base: the instance list (docs/disasters.md)",
    false,
    InstallManagers,
});

} // namespace
} // namespace opensc4::disasters
