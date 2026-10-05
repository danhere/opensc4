#include "core/trace.h"

#include "core/address_check.h"
#include "core/hook.h"
#include "core/log.h"

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

bool __fastcall LoadCity(cISC4App* self, void* edx, SC4String const& path, cRZAutoRefCount<cISC4RegionalCity>* city)
{
    log::Write("trace: cSC4App::LoadCity(\"%s\")", path.ToChar());
    const ULONGLONG start = GetTickCount64();
    const bool loaded = g_loadCity(self, edx, path, city);
    log::Write("trace: cSC4App::LoadCity returned %s after %llu ms", loaded ? "true" : "false",
               GetTickCount64() - start);
    return loaded;
}

} // namespace

bool Enabled()
{
    wchar_t value[4];
    return GetEnvironmentVariableW(L"OPENSC4_TRACE", value, 4) == 1 && value[0] == L'1';
}

void Install()
{
    if (const addr::Entry* entry = addr_check::Find(addr::cSC4App_LoadCity))
        hook::Install(*entry, &LoadCity, &g_loadCity);
}

} // namespace opensc4::trace
