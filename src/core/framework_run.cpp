// The framework's start and stop of the system services and its calls of
// the hooks (cGZFrameWork; docs/framework.md), on the game's own object.
// Replacements, each off by default, all put in when core.dll is injected
// (phase Attach): most of these functions run once, while the app starts.
//
//   framework.run.services  the services started in three bands of priority
//                           (0x00879F15, 0x00879FBC, 0x0087A046) and
//                           stopped in the same bands, the other way round
//                           (0x0087A4B6, 0x0087A527, 0x0087A59F)
//   framework.run.sequence  the start-up (0x0087AF13), the run phase
//                           (0x0087957A) and the shutdown (0x0087AB07)
//                           with their caller (0x0087B0B9) and four
//                           helpers of the shutdown
//   framework.run.loop      the main loop (0x008795BD)
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
#include <initializer_list>

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

// ---- The sequence: start-up, run, shutdown ----

constexpr std::size_t kState = 0xCC;
constexpr std::size_t kCommandLine = 0xE8;
constexpr std::size_t kQuitByFlag = 0x124;
constexpr std::size_t kQuitRequested = 0x140;
constexpr std::size_t kQuitCode = 0x144;
constexpr std::size_t kTickOn = 0xB8;
constexpr std::size_t kComObject = 0x08;
constexpr std::size_t kHooksList = 0x84;
constexpr std::size_t kServiceTree = 0x88;
constexpr std::size_t kServiceTreeCount = 0x8C;
constexpr std::size_t kServiceTable = 0x94;
constexpr std::size_t kIdleList = 0xA8;
constexpr std::size_t kIdleParked = 0xAC;
constexpr std::size_t kTickList = 0xB0;
constexpr std::size_t kTickParked = 0xB4;
constexpr std::size_t kLock = 0xE0;

char*& TheFramework()
{
    return Global<char*>(addr::GZFrameWork_Instance);
}

void*& TheApp()
{
    return Global<void*>(addr::GZFrameWork_App);
}

int32_t& ExitCode()
{
    return Global<int32_t>(addr::GZFrameWork_ExitCode);
}

void SetState(int32_t state)
{
    At<int32_t>(TheFramework(), kState) = state;
}

// One of the hook callers, at its game address: ours or the game's by
// framework.run.hooks.
void CallHooksAt(std::uintptr_t address)
{
    reinterpret_cast<uint8_t(__thiscall*)(char*)>(address)(TheFramework());
}

// The game's 0x0087AF13. States: 0 the COM object is set up and added as a
// service and the app's three early callbacks run; 1 the app's
// PreFrameWorkInit (the exe's directors, the app's services, the plugin
// DLLs) and the hooks' PreFrameWorkInit; 3 the first services and the
// hooks' PreAppInit; 4 the app's own start, then the tick goes on; 5 the
// last services; 6 the app's PostFrameWorkInit and the hooks' PostAppInit.
// The exit code is -1 when the app's PreFrameWorkInit or its start fails.
bool __cdecl Init(void* commandLine, uint8_t quitByFlag)
{
    SetState(0);
    reinterpret_cast<void(__thiscall*)(char*, void*)>(addr::cGZFrameWork_CopyCommandLine)(TheFramework(),
                                                                                         commandLine);
    reinterpret_cast<void(__cdecl*)(char*, uint32_t)>(addr::RZCmdLine_Prepare)(TheFramework() + kCommandLine, 0);
    At<uint8_t>(TheFramework(), kQuitByFlag) = quitByFlag;

    void* const com = Virtual<void*>(TheFramework(), 0x5C);
    if (Virtual<bool>(com, 0x24))
        Virtual<void*>(com, 0x2C, uint8_t{1});
    void* comService = nullptr;
    if (Virtual<bool>(com, 0x0, kIID_cIGZSystemService, &comService)) {
        Virtual<bool>(TheFramework(), 0xC, comService);
        Virtual<uint32_t>(comService, 0x8);
    }
    Virtual<uint8_t>(TheApp(), 0x34);
    Virtual<uint8_t>(TheApp(), 0x30);
    Virtual<uint8_t>(TheApp(), 0x2C);

    SetState(1);
    if (!Virtual<bool>(TheApp(), 0x18)) {
        Virtual<void>(com, 0x28);
        ExitCode() = -1;
        return false;
    }
    CallHooksAt(addr::cGZFrameWork_CallHooksPreFrameWorkInit);
    SetState(2);
    SetState(3);
    Virtual<bool>(TheFramework(), 0xA4);
    CallHooksAt(addr::cGZFrameWork_CallHooksPreAppInit);
    SetState(4);
    if (!Virtual<bool>(TheFramework(), 0xA8)) {
        ExitCode() = -1;
        return false;
    }
    Virtual<void>(TheFramework(), 0x48, uint8_t{1});
    SetState(5);
    Virtual<bool>(TheFramework(), 0xAC);
    SetState(6);
    Virtual<uint8_t>(TheApp(), 0x1C);
    CallHooksAt(addr::cGZFrameWork_CallHooksPostAppInit);
    return ExitCode() == 0;
}

// The game's 0x0087957A: state 7; the app's GZRun, and when it says no the
// main loop (slot 48).
bool __cdecl RunPhase()
{
    void* const app = Virtual<void*>(TheFramework(), 0x94);
    SetState(7);
    if (!Virtual<bool>(app, 0x20))
        Virtual<void>(TheFramework(), 0xC0);
    return ExitCode() == 0;
}

// The game's 0x0087AA81.
bool __fastcall EmptyServices(char* self, void*)
{
    Lock lock(self + kLock);
    for (const std::size_t list : {kIdleList, kIdleParked, kTickList, kTickParked}) {
        char* const sentinel = At<char*>(self, list);
        for (char* node = At<char*>(sentinel, kNext); node != sentinel;) {
            char* const gone = node;
            node = At<char*>(node, kNext);
            Release(At<void*>(gone, kValue));
            Free(gone);
        }
        At<char*>(sentinel, kNext) = sentinel;
        At<char*>(sentinel, kPrevious) = sentinel;
    }
    // The table: every node's service released, the buckets cleared.
    char* const table = self + kServiceTable;
    for (char** bucket = At<char**>(table, 0x4); bucket != At<char**>(table, 0x8); ++bucket) {
        for (char* node = *bucket; node;) {
            char* const gone = node;
            node = At<char*>(node, kNext);
            Release(At<void*>(gone, 0x8));
            Free(gone);
        }
        *bucket = nullptr;
    }
    At<uint32_t>(table, 0x10) = 0;
    // The tree: its first node erased until none is left.
    while (At<uint32_t>(self, kServiceTreeCount) != 0) {
        char* const first = At<char*>(At<char*>(self, kServiceTree), 0x8);
        reinterpret_cast<void(__thiscall*)(char*, char*)>(addr::cGZFrameWork_ServiceTree_Erase)(self + kServiceTree,
                                                                                               first);
    }
    return true;
}

// The game's 0x00879922.
bool __fastcall EmptyHooks(char* self, void*)
{
    Lock lock(self + kLock);
    char* const sentinel = At<char*>(self, kHooksList);
    for (char* node = At<char*>(sentinel, kNext); node != sentinel;) {
        char* const gone = node;
        node = At<char*>(node, kNext);
        Release(At<void*>(gone, kValue));
        Free(gone);
    }
    At<char*>(sentinel, kNext) = sentinel;
    At<char*>(sentinel, kPrevious) = sentinel;
    return true;
}

// The game's 0x008793FD: the COM object's RealShutdown (it frees the
// plugin DLLs) under the framework's lock.
void __fastcall ShutdownCOM(char* self, void*)
{
    Lock lock(self + kLock);
    Virtual<void>(self + kComObject, 0x28);
}

// The game's 0x008794CC.
void __cdecl ReleaseGlobals()
{
    for (const std::uintptr_t global :
         {addr::GZFrameWork_App, addr::GZFrameWork_Instance, addr::GZFrameWork_Instance2}) {
        void*& object = Global<void*>(global);
        if (object) {
            Virtual<uint32_t>(object, 0x8);
            object = nullptr;
        }
    }
}

// The game's 0x0087AB07. States 8 to 10, the hooks' PreAppShutdown, the
// app's PreFrameWorkShutdown, the last services stopped; 11, the app's
// band stopped, the hooks' PostAppShutdown; 12, the first services
// stopped, the tick off, the hooks' PostSystemServiceShutdown; then the
// lists emptied and the COM object shut down; 13, 14, the globals released.
bool __cdecl Shutdown()
{
    void* const app = Virtual<void*>(TheFramework(), 0x94);
    SetState(8);
    SetState(9);
    SetState(10);
    CallHooksAt(addr::cGZFrameWork_CallHooksPreAppShutdown);
    Virtual<uint8_t>(app, 0x24);
    Virtual<bool>(TheFramework(), 0xB0);
    SetState(11);
    Virtual<bool>(TheFramework(), 0xB4);
    CallHooksAt(addr::cGZFrameWork_CallHooksPostAppShutdown);
    SetState(12);
    Virtual<bool>(TheFramework(), 0xB8);
    Virtual<void>(TheFramework(), 0x48, uint8_t{0});
    CallHooksAt(addr::cGZFrameWork_CallHooksPostSystemServiceShutdown);
    EmptyServices(TheFramework(), nullptr);
    EmptyHooks(TheFramework(), nullptr);
    ShutdownCOM(TheFramework(), nullptr);
    SetState(13);
    SetState(14);
    ReleaseGlobals();
    const bool clean = ExitCode() == 0;
    Global<void*>(addr::GZFrameWork_Instance) = nullptr;
    Global<void*>(addr::GZFrameWork_Instance2) = nullptr;
    return clean;
}

// The game's 0x0087B0B9.
int32_t __cdecl Run(void* commandLine, uint8_t quitByFlag)
{
    if (Init(commandLine, quitByFlag))
        RunPhase();
    Shutdown();
    return ExitCode();
}

// The game's 0x008795BD: with the tick off the loop waits for a message;
// every message there is goes to its window (WM_QUIT ends the loop with
// its wParam as the exit code); with the tick on OnIdle (slot 16) runs
// once per pass. The quit flag ends it too, when the run asked for that.
// The four Windows functions are called through the exe's import slots, so
// whatever stands in for them there still does.
void __fastcall MainLoop(char* self, void*)
{
    struct Message {
        void* window;
        uint32_t message;
        uintptr_t wParam;
        intptr_t lParam;
        uint32_t time;
        int32_t x;
        int32_t y;
    } message;
    using WaitFn = int32_t(__stdcall*)();
    using PeekFn = int32_t(__stdcall*)(Message*, void*, uint32_t, uint32_t, uint32_t);
    using MessageFn = intptr_t(__stdcall*)(const Message*);
    constexpr uint32_t kQuitMessage = 0x12;
    for (;;) {
        if (!At<uint8_t>(self, kTickOn))
            Global<WaitFn>(addr::User32_WaitMessage)();
        while (Global<PeekFn>(addr::User32_PeekMessageA)(&message, nullptr, 0, 0, 1)) {
            if (message.message == kQuitMessage) {
                ExitCode() = static_cast<int32_t>(message.wParam);
                return;
            }
            Global<MessageFn>(addr::User32_TranslateMessage)(&message);
            Global<MessageFn>(addr::User32_DispatchMessageA)(&message);
        }
        if (At<uint8_t>(self, kTickOn))
            Virtual<void>(self, 0x40);
        if (At<uint8_t>(self, kQuitByFlag) && At<uint8_t>(self, kQuitRequested)) {
            ExitCode() = At<int32_t>(self, kQuitCode);
            return;
        }
    }
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

bool InstallSequence(registry::Mode)
{
    const Detour detours[] = {
        {addr::GZFrameWork_Run, reinterpret_cast<void*>(&Run)},
        {addr::GZFrameWork_Init, reinterpret_cast<void*>(&Init)},
        {addr::GZFrameWork_RunPhase, reinterpret_cast<void*>(&RunPhase)},
        {addr::GZFrameWork_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::GZFrameWork_ReleaseGlobals, reinterpret_cast<void*>(&ReleaseGlobals)},
        {addr::cGZFrameWork_EmptyServices, reinterpret_cast<void*>(&EmptyServices)},
        {addr::cGZFrameWork_EmptyHooks, reinterpret_cast<void*>(&EmptyHooks)},
        {addr::cGZFrameWork_ShutdownCOM, reinterpret_cast<void*>(&ShutdownCOM)},
    };
    return InstallAll(detours);
}

bool InstallLoop(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZFrameWork_MainLoop, reinterpret_cast<void*>(&MainLoop)},
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

const bool g_registeredSequence = registry::Register({
    "framework.run.sequence",
    "framework/run",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::GZFrameWork_Run,
    "Our start-up, run and shutdown sequence of the framework (docs/framework.md)",
    false,
    InstallSequence,
});

const bool g_registeredLoop = registry::Register({
    "framework.run.loop",
    "framework/run",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::cGZFrameWork_MainLoop,
    "Our main loop: Windows messages, then the framework's OnIdle (docs/framework.md)",
    false,
    InstallLoop,
});

} // namespace
} // namespace opensc4::framework
