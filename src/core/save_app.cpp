// See save_app.h. The translations themselves are in save_app_1.cpp
// (SaveCity) and save_app_2.cpp (LoadCity).
#include "core/save_app.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

namespace opensc4::saves {

uint8_t __fastcall SaveCityOurs(char* self, void* edx, void* path, uint32_t fast);
bool __fastcall LoadCityOurs(void* self, void* edx, void* path, void* holder);

namespace {

using SaveFn = bool(__fastcall*)(void* self, void* edx, const void* path, bool fast);
using LoadFn = bool(__fastcall*)(void* self, void* edx, void* path, void* city);

bool g_on = false;
void* g_save = nullptr;
void* g_load = nullptr;

bool __fastcall SaveDetour(void* self, void* edx, const void* path, bool fast)
{
    return SaveCity(g_save, self, edx, path, fast);
}

bool __fastcall LoadDetour(void* self, void* edx, void* path, void* city)
{
    return LoadCity(g_load, self, edx, path, city);
}

// Runs after every PostAppInit replacement, so the wrappers have their
// detours by now: an address one of them holds is left to it.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    g_on = true;
    const addr::Entry* save = addr_check::Find(addr::cSC4App_SaveCity);
    const addr::Entry* load = addr_check::Find(addr::cSC4App_LoadCity);
    if (!save || !load)
        return false;
    const bool saveWrapped = hook::IsInstalledAt(addr::cSC4App_SaveCity);
    const bool loadWrapped = hook::IsInstalledAt(addr::cSC4App_LoadCity);
    if (!saveWrapped && !hook::Install(*save, reinterpret_cast<void*>(&SaveDetour), &g_save))
        return false;
    if (!loadWrapped && !hook::Install(*load, reinterpret_cast<void*>(&LoadDetour), &g_load))
        return false;
    log::Write("saves: SaveCity ours %s, LoadCity ours %s", saveWrapped ? "inside another feature's detour" : "by our detour",
               loadWrapped ? "inside another feature's detour" : "by our detour");
    return true;
}

const bool g_registered = registry::Register({
    "saves.app",
    "saves",
    registry::Kind::Detour,
    registry::Phase::LateInit,
    addr::cSC4App_SaveCity,
    "Our cSC4App::SaveCity and LoadCity; features that wrap the two reach ours through save_app.h",
    false,
    Install,
});

} // namespace

bool SaveCity(void* original, void* self, void* edx, const void* path, bool fast)
{
    if (g_on)
        return SaveCityOurs(static_cast<char*>(self), edx, const_cast<void*>(path), fast ? 1u : 0u) != 0;
    return reinterpret_cast<SaveFn>(original)(self, edx, path, fast);
}

bool LoadCity(void* original, void* self, void* edx, void* path, void* city)
{
    if (g_on)
        return LoadCityOurs(self, edx, path, city);
    return reinterpret_cast<LoadFn>(original)(self, edx, path, city);
}

} // namespace opensc4::saves
