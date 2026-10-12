// The framework's start and stop of the system services and its calls of
// the hooks (cGZFrameWork; docs/framework.md), on the game's own object.
// Replacements, each off by default, both put in when core.dll is injected
// (phase Attach): half of these functions run once, while the app starts.
//
//   framework.run.services  the services started in three bands of priority
//                           (0x00879F15, 0x00879FBC, 0x0087A046) and
//                           stopped in the same bands, the other way round
//                           (0x0087A4B6, 0x0087A527, 0x0087A59F)
//   framework.run.hooks     the seven functions that call the hooks:
//                           PreFrameWorkInit (0x0087A286), PreAppInit
//                           (0x0087A2D6), PostAppInit (0x0087A326),
//                           PreAppShutdown (0x0087A376), PostAppShutdown
//                           (0x0087A3C6), PostSystemServiceShutdown
//                           (0x0087A416), AbortiveQuit (0x0087A466)
//
// The bands: above -2,000,000 (the first to start, the last to stop),
// -2,999,999 to -2,000,000 (the app itself), below -2,999,999.
//
// Both groups work on a copy of the list they walk: the services by
// priority (0x00879ACE) and the hooks (0x0087A178), each called at its game
// address, so it is ours or the game's by its own setting
// (framework.services.registry, framework.services.hooks).
#include "core/address_check.h"
#include "core/framework_containers.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::framework {
namespace {

constexpr std::size_t kHooksBusy = 0xD4;

constexpr int32_t kAppBandTop = -2000000;
constexpr int32_t kAppBandBottom = -2999999;
constexpr uint32_t kIID_cIGZSystemService = 0x287FB697;
constexpr uint32_t kIID_cIGZApp = 0x66;

// A list of counted references made here, filled by the game's function
// and emptied again.
struct CopiedList {
    CopiedList()
    {
        sentinel = static_cast<char*>(Allocate(0xC));
        At<char*>(sentinel, kNext) = sentinel;
        At<char*>(sentinel, kPrevious) = sentinel;
    }
    ~CopiedList()
    {
        for (char* node = At<char*>(sentinel, kNext); node != sentinel;) {
            char* const gone = node;
            node = At<char*>(node, kNext);
            Release(At<void*>(gone, kValue));
            Free(gone);
        }
        Free(sentinel);
    }
    CopiedList(const CopiedList&) = delete;
    CopiedList& operator=(const CopiedList&) = delete;

    char* First() const { return At<char*>(sentinel, kNext); }
    bool AtEnd(char* node) const { return node == sentinel; }

    char* sentinel;
};

void ListServices(char* self, CopiedList& list, bool ascending)
{
    using Fn = void(__thiscall*)(char* self, char** list, uint8_t ascending);
    reinterpret_cast<Fn>(addr::cGZFrameWork_ListServicesByPriority)(self, &list.sentinel, ascending ? 1 : 0);
}

int32_t PriorityOf(void* service)
{
    return Virtual<int32_t>(service, 0x14);
}

// Init (slot 8), and on success SetServiceRunning(true) (slot 7).
bool Start(void* service)
{
    if (!Virtual<bool>(service, 0x20))
        return false;
    Virtual<void*>(service, 0x1C, uint8_t{1});
    return true;
}

// Shutdown (slot 9), and on success SetServiceRunning(false).
bool Stop(void* service)
{
    if (!Virtual<bool>(service, 0x24))
        return false;
    Virtual<void*>(service, 0x1C, uint8_t{0});
    return true;
}

// The game's 0x00879F15: the app (the framework's slot 37) joins the
// services as a system service, then the first band starts, highest
// priority first.
bool __fastcall StartFirstServices(char* self, void*)
{
    void* const app = Virtual<void*>(self, 0x94);
    void* appService = nullptr;
    Virtual<bool>(app, 0x0, kIID_cIGZSystemService, &appService);
    Virtual<bool>(self, 0xC, appService);
    {
        CopiedList services;
        ListServices(self, services, false);
        for (char* node = services.First(); !services.AtEnd(node); node = At<char*>(node, kNext)) {
            void* const service = At<void*>(node, kValue);
            if (PriorityOf(service) <= kAppBandTop)
                break;
            Start(service);
        }
    }
    Release(appService);
    return true;
}

// The game's 0x00879FBC: the app's band. A service of the band that fails
// its Init and answers the app's interface fails the start.
bool __fastcall StartAppServices(char* self, void*)
{
    CopiedList services;
    ListServices(self, services, false);
    for (char* node = services.First(); !services.AtEnd(node); node = At<char*>(node, kNext)) {
        void* const service = At<void*>(node, kValue);
        const int32_t priority = PriorityOf(service);
        if (priority > kAppBandTop)
            continue;
        if (priority < kAppBandBottom)
            break;
        if (Start(service))
            continue;
        void* app = nullptr;
        if (Virtual<bool>(service, 0x0, kIID_cIGZApp, &app)) {
            Virtual<uint32_t>(app, 0x8);
            return false;
        }
    }
    return true;
}

// The game's 0x0087A046: the last band.
bool __fastcall StartLastServices(char* self, void*)
{
    CopiedList services;
    ListServices(self, services, false);
    for (char* node = services.First(); !services.AtEnd(node); node = At<char*>(node, kNext)) {
        void* const service = At<void*>(node, kValue);
        if (PriorityOf(service) < kAppBandBottom)
            Start(service);
    }
    return true;
}

// The game's 0x0087A4B6: the last band stops, lowest priority first. False
// when a Shutdown says no.
bool __fastcall StopLastServices(char* self, void*)
{
    bool all = true;
    CopiedList services;
    ListServices(self, services, true);
    for (char* node = services.First(); !services.AtEnd(node); node = At<char*>(node, kNext)) {
        void* const service = At<void*>(node, kValue);
        if (PriorityOf(service) >= kAppBandBottom)
            break;
        if (!Stop(service))
            all = false;
    }
    return all;
}

// The game's 0x0087A527: the app's band stops.
bool __fastcall StopAppServices(char* self, void*)
{
    bool all = true;
    CopiedList services;
    ListServices(self, services, true);
    for (char* node = services.First(); !services.AtEnd(node); node = At<char*>(node, kNext)) {
        void* const service = At<void*>(node, kValue);
        const int32_t priority = PriorityOf(service);
        if (priority < kAppBandBottom)
            continue;
        if (priority > kAppBandTop)
            break;
        if (!Stop(service))
            all = false;
    }
    return all;
}

// The game's 0x0087A59F: the first band stops.
bool __fastcall StopFirstServices(char* self, void*)
{
    bool all = true;
    CopiedList services;
    ListServices(self, services, true);
    for (char* node = services.First(); !services.AtEnd(node); node = At<char*>(node, kNext)) {
        void* const service = At<void*>(node, kValue);
        if (PriorityOf(service) <= kAppBandTop)
            continue;
        if (!Stop(service))
            all = false;
    }
    return all;
}

// The seven hook callers: a copy of the hooks, each one's method at `slot`
// called in list order; nothing while the hooks are in use.
bool CallHooks(char* self, std::size_t slot)
{
    if (At<uint8_t>(self, kHooksBusy))
        return false;
    CopiedList hooks;
    using Fn = void(__thiscall*)(char* self, char** list);
    reinterpret_cast<Fn>(addr::cGZFrameWork_CopyHooks)(self, &hooks.sentinel);
    for (char* node = hooks.First(); !hooks.AtEnd(node); node = At<char*>(node, kNext))
        Virtual<uint8_t>(At<void*>(node, kValue), slot);
    return true;
}

bool __fastcall CallHooksPreFrameWorkInit(char* self, void*)
{
    return CallHooks(self, 0xC);
}

bool __fastcall CallHooksPreAppInit(char* self, void*)
{
    return CallHooks(self, 0x10);
}

bool __fastcall CallHooksPostAppInit(char* self, void*)
{
    return CallHooks(self, 0x14);
}

bool __fastcall CallHooksPreAppShutdown(char* self, void*)
{
    return CallHooks(self, 0x18);
}

bool __fastcall CallHooksPostAppShutdown(char* self, void*)
{
    return CallHooks(self, 0x1C);
}

bool __fastcall CallHooksPostSystemServiceShutdown(char* self, void*)
{
    return CallHooks(self, 0x20);
}

bool __fastcall CallHooksAbortiveQuit(char* self, void*)
{
    return CallHooks(self, 0x24);
}

struct Detour {
    std::uintptr_t address;
    void* ours;
};

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, detour.ours, &original))
            return false;
    }
    return true;
}

bool InstallServices(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZFrameWork_StartFirstServices, reinterpret_cast<void*>(&StartFirstServices)},
        {addr::cGZFrameWork_StartAppServices, reinterpret_cast<void*>(&StartAppServices)},
        {addr::cGZFrameWork_StartLastServices, reinterpret_cast<void*>(&StartLastServices)},
        {addr::cGZFrameWork_StopLastServices, reinterpret_cast<void*>(&StopLastServices)},
        {addr::cGZFrameWork_StopAppServices, reinterpret_cast<void*>(&StopAppServices)},
        {addr::cGZFrameWork_StopFirstServices, reinterpret_cast<void*>(&StopFirstServices)},
    };
    return InstallAll(detours);
}

bool InstallHooks(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZFrameWork_CallHooksPreFrameWorkInit, reinterpret_cast<void*>(&CallHooksPreFrameWorkInit)},
        {addr::cGZFrameWork_CallHooksPreAppInit, reinterpret_cast<void*>(&CallHooksPreAppInit)},
        {addr::cGZFrameWork_CallHooksPostAppInit, reinterpret_cast<void*>(&CallHooksPostAppInit)},
        {addr::cGZFrameWork_CallHooksPreAppShutdown, reinterpret_cast<void*>(&CallHooksPreAppShutdown)},
        {addr::cGZFrameWork_CallHooksPostAppShutdown, reinterpret_cast<void*>(&CallHooksPostAppShutdown)},
        {addr::cGZFrameWork_CallHooksPostSystemServiceShutdown,
         reinterpret_cast<void*>(&CallHooksPostSystemServiceShutdown)},
        {addr::cGZFrameWork_CallHooksAbortiveQuit, reinterpret_cast<void*>(&CallHooksAbortiveQuit)},
    };
    return InstallAll(detours);
}

const bool g_registeredServices = registry::Register({
    "framework.run.services",
    "framework/run",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::cGZFrameWork_StartFirstServices,
    "Our start and stop of the system services in their three bands of priority (docs/framework.md)",
    false,
    InstallServices,
});

const bool g_registeredHooks = registry::Register({
    "framework.run.hooks",
    "framework/run",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::cGZFrameWork_CallHooksPreFrameWorkInit,
    "Our calls of the framework hooks, from PreFrameWorkInit to PostSystemServiceShutdown "
    "(docs/framework.md)",
    false,
    InstallHooks,
});

} // namespace
} // namespace opensc4::framework
