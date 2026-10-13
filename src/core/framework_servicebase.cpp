// The system service base class (cRZBaseSystemService: what the framework's
// services derive from; constructor 0x0091DE0F, 40 callers) on the game's
// own objects (docs/framework.md). Replacements, each off by default:
//
//   framework.service.lifetime   the two constructors (0x0091DE0F,
//                                0x0091DDCF), the destructor (0x0091DD53)
//                                and the deleting destructor (0x0091DDB3)
//   framework.service.interface  QueryInterface (0x0091DD67) and
//                                SetServiceRunning (0x0091DD91)
//
// Services are built and started while the app starts, so both groups go
// in when core.dll is injected (phase Attach).
//
// The object's base part (0x1C bytes): +0x00 vtable 0x00ACF1CC, +0x04 an
// unknown part (vtable 0x00ACF1B4, reference count at +0x08), +0x0C the
// service's ID, +0x10 its priority, +0x14 its tick priority, +0x18 a byte:
// running.
//
// Still the game's: Release (0x0091DD89) and the unknown part's three
// thunks (0x0091DD9B, 0x0091DDA3, 0x0091DDAB), which are 8 bytes each (a
// pointer adjustment and a jump); the getters, AddRef and the empty Init,
// Shutdown, OnTick and OnIdle, which are functions shared with unrelated
// classes; and the unknown base's own QueryInterface and Release.
//
// SetServiceRunning's detour is shared with diagnostics.trace.service_start
// (framework_servicebase.h).
#include "core/framework_servicebase.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::framework {
namespace {

constexpr std::size_t kUnknownPart = 0x04;
constexpr std::size_t kRefs = 0x08;
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kPriority = 0x10;
constexpr std::size_t kTickPriority = 0x14;
constexpr std::size_t kRunning = 0x18;

constexpr uint32_t kIID_cIGZSystemService = 0x287FB697;

char* Build(char* self, uint32_t id, int32_t priority, int32_t tickPriority)
{
    At<uint32_t>(self, kRefs) = 0;
    At<uint32_t>(self, kId) = id;
    At<int32_t>(self, kPriority) = priority;
    At<int32_t>(self, kTickPriority) = tickPriority;
    At<std::uintptr_t>(self, kUnknownPart) = addr::cRZBaseSystemService_UnknownVtable;
    At<std::uintptr_t>(self, 0) = addr::cRZBaseSystemService_Vtable;
    At<uint8_t>(self, kRunning) = 0;
    return self;
}

// The game's 0x0091DE0F: the tick priority is the priority.
char* __fastcall Construct(char* self, void*, uint32_t id, int32_t priority)
{
    return Build(self, id, priority, priority);
}

// The game's 0x0091DDCF.
char* __fastcall ConstructWithTickPriority(char* self, void*, uint32_t id, int32_t priority, int32_t tickPriority)
{
    return Build(self, id, priority, tickPriority);
}

// The game's 0x0091DD53: the class's vtables, then the unknown base's
// (0x0090D990). A subclass's destructor ends here.
void __fastcall Destroy(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::cRZBaseSystemService_Vtable;
    At<std::uintptr_t>(self, kUnknownPart) = addr::cRZBaseUnknown_Vtable;
}

// The game's 0x0091DDB3.
char* __fastcall DeletingDestructor(char* self, void*, uint8_t flags)
{
    reinterpret_cast<void(__thiscall*)(char*)>(addr::cRZBaseSystemService_Destroy)(self);
    if (flags & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(self);
    return self;
}

// The game's 0x0091DD67: the service's interface here, anything else to the
// unknown part's own QueryInterface (0x009457C6).
bool __fastcall QueryInterface(char* self, void*, uint32_t interfaceId, void** out)
{
    if (interfaceId == kIID_cIGZSystemService) {
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return true;
    }
    using Fn = bool(__thiscall*)(char*, uint32_t, void**);
    return reinterpret_cast<Fn>(addr::cRZBaseUnknown_QueryInterface1)(self + kUnknownPart, interfaceId, out);
}

// SetServiceRunning (0x0091DD91): the one detour, see the header.
ServiceRunningTrace g_trace = nullptr;
bool g_setRunningOurs = false;
bool g_setRunningHooked = false;
void* g_setRunningOriginal = nullptr;

uint8_t __fastcall SetServiceRunning(char* self, void*, uint8_t running)
{
    if (g_trace)
        g_trace(self, running);
    if (g_setRunningOurs) {
        At<uint8_t>(self, kRunning) = running;
        return running;
    }
    return reinterpret_cast<uint8_t(__thiscall*)(char*, uint8_t)>(g_setRunningOriginal)(self, running);
}

OPENSC4_THUNK(ConstructThunk, Construct, 8)
OPENSC4_THUNK(ConstructWithTickPriorityThunk, ConstructWithTickPriority, 12)
OPENSC4_THUNK(DeletingDestructorThunk, DeletingDestructor, 4)
OPENSC4_THUNK(QueryInterfaceThunk, QueryInterface, 8)
OPENSC4_THUNK(SetServiceRunningThunk, SetServiceRunning, 4)

// The game's destructor changes no register but ECX, and most of its 42
// callers are other destructors that jump to it: EAX is kept too.
__declspec(naked) void DestroyThunk()
{
    __asm push eax
    __asm push ecx
    __asm push edx
    __asm call Destroy
    __asm pop edx
    __asm pop ecx
    __asm pop eax
    __asm ret
}

bool HookSetServiceRunning()
{
    if (g_setRunningHooked)
        return true;
    if (!game::IsBuild641())
        return false;
    const addr::Entry* entry = addr_check::Find(addr::cRZBaseSystemService_SetServiceRunning);
    if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(&SetServiceRunningThunk), &g_setRunningOriginal))
        return false;
    g_setRunningHooked = true;
    return true;
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

bool InstallLifetime(registry::Mode)
{
    const Detour detours[] = {
        {addr::cRZBaseSystemService_cRZBaseSystemService, reinterpret_cast<void*>(&ConstructThunk)},
        {addr::cRZBaseSystemService_ConstructWithTickPriority,
         reinterpret_cast<void*>(&ConstructWithTickPriorityThunk)},
        {addr::cRZBaseSystemService_Destroy, reinterpret_cast<void*>(&DestroyThunk)},
        {addr::cRZBaseSystemService_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructorThunk)},
    };
    return InstallAll(detours);
}

bool InstallInterface(registry::Mode)
{
    const Detour detours[] = {
        {addr::cRZBaseSystemService_QueryInterface, reinterpret_cast<void*>(&QueryInterfaceThunk)},
    };
    if (!InstallAll(detours) || !HookSetServiceRunning())
        return false;
    g_setRunningOurs = true;
    return true;
}

const bool g_registeredLifetime = registry::Register({
    "framework.service.lifetime",
    "framework/service",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::cRZBaseSystemService_cRZBaseSystemService,
    "Our system service base: the two constructors, the destructor and the deleting destructor "
    "(docs/framework.md)",
    false,
    InstallLifetime,
});

const bool g_registeredInterface = registry::Register({
    "framework.service.interface",
    "framework/service",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::cRZBaseSystemService_QueryInterface,
    "Our system service base: QueryInterface and SetServiceRunning (docs/framework.md)",
    false,
    InstallInterface,
});

} // namespace

bool TraceSetServiceRunning(ServiceRunningTrace trace)
{
    if (!HookSetServiceRunning())
        return false;
    g_trace = trace;
    return true;
}

} // namespace opensc4::framework
