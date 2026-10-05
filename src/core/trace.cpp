// Trace hooks: entry detours that log calls into the game and pass them
// through unchanged. Registered as replacements (registry.h) under
// "diagnostics.trace.*"; set one to "ours" to enable it. They exercise the hook
// layer on real game functions and record what the game does in test runs.
#include "core/address_check.h"
#include "core/crash.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <SC4String.h>
#include <cISC4App.h>
#include <cISC4RegionalCity.h>
#include <cRZAutoRefCount.h>

#include <windows.h>

#include <opensc4/addresses.h>

namespace opensc4::trace {
namespace {

// Detours for __thiscall member functions are written as __fastcall free
// functions: `this` arrives in ECX, EDX is unused, the remaining arguments are
// on the stack and the callee pops them, as with __thiscall.

// cSC4App::LoadCity (address database: cSC4App::LoadCity).
using LoadCityFn = bool(__fastcall*)(cISC4App* self, void* edx, SC4String const& path,
                                     cRZAutoRefCount<cISC4RegionalCity>* city);
LoadCityFn g_loadCity = nullptr;

bool LoadCityBody(cISC4App* self, void* edx, SC4String const& path, cRZAutoRefCount<cISC4RegionalCity>* city)
{
    log::Write("trace: cSC4App::LoadCity(\"%s\")", path.ToChar());
    const ULONGLONG start = GetTickCount64();
    const bool loaded = g_loadCity(self, edx, path, city);
    log::Write("trace: cSC4App::LoadCity returned %s after %llu ms", loaded ? "true" : "false",
               GetTickCount64() - start);
    return loaded;
}

// Hook entry: only the SEH guard lives here (see crash.h).
bool __fastcall LoadCity(cISC4App* self, void* edx, SC4String const& path, cRZAutoRefCount<cISC4RegionalCity>* city)
{
    __try {
        return LoadCityBody(self, edx, path, city);
    } __except (crash::HookFilter("cSC4App::LoadCity", GetExceptionInformation())) {
        return false;
    }
}

bool ApplyLoadCity(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4App_LoadCity);
    return entry && hook::Install(*entry, &LoadCity, &g_loadCity);
}

const bool g_registered = registry::Register({
    "diagnostics.trace.loadcity",
    "diagnostics",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4App_LoadCity,
    "Logs every city load and its duration",
    false,
    ApplyLoadCity,
});

} // namespace
} // namespace opensc4::trace
