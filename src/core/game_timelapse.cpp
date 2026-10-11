// game.timelapse: keeps a copy of the city every time it is saved, named by
// the game date, so its growth can be played back later as a timelapse
// (scripts/render-timelapse.sh, docs/timelapse.md).
//
// Copies only after the game's own save has finished (cSC4App::SaveCity,
// 0x00455E20, which the in-place save calls too), so nothing the game writes
// changes. They go to <user folder>\Timelapse\<region>\<city file name>\
// <yyyy-mm-dd>.sc4 ([timelapse] folder replaces <user folder>\Timelapse); a
// second save on the same game day replaces that day's copy.
#include "core/address_check.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/save_app.h"
#include "core/settings.h"

#include <GZServPtrs.h>
#include <cIGZString.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4Simulator.h>
#include <cRZBaseString.h>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include <opensc4/addresses.h>

namespace opensc4::game_timelapse {
namespace {

std::string g_folder; // [timelapse] folder, or empty: next to Regions

std::wstring Widen(const std::string& text)
{
    const int n = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wide(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, wide.data(), n);
    return wide;
}

// "...\Regions\<region>\City - X.sc4" -> {"...", "<region>", "City - X"}.
bool SplitCityPath(const std::wstring& path, std::wstring* userDir, std::wstring* region, std::wstring* city)
{
    const size_t file = path.find_last_of(L"\\/");
    if (file == std::wstring::npos)
        return false;
    const size_t regionStart = path.find_last_of(L"\\/", file - 1);
    if (regionStart == std::wstring::npos)
        return false;
    const size_t regionsStart = path.find_last_of(L"\\/", regionStart - 1);
    if (regionsStart == std::wstring::npos)
        return false;
    *userDir = path.substr(0, regionsStart);
    *region = path.substr(regionStart + 1, file - regionStart - 1);
    std::wstring name = path.substr(file + 1);
    const size_t dot = name.find_last_of(L'.');
    *city = dot == std::wstring::npos ? name : name.substr(0, dot);
    return true;
}

void KeepCopy(const std::wstring& path)
{
    cISC4AppPtr app;
    cISC4City* city = app ? app->GetCity() : nullptr;
    cISC4Simulator* sim = city ? city->GetSimulator() : nullptr;
    if (!sim)
        return;
    uint32_t year = 0, month = 0, day = 0, dayOfYear = 0, weekDay = 0;
    sim->GetSimDate(&year, &month, &day, &dayOfYear, &weekDay);
    std::wstring userDir, region, name;
    if (!SplitCityPath(path, &userDir, &region, &name)) {
        log::Write("timelapse: cannot place a copy of %ls", path.c_str());
        return;
    }
    std::wstring folder = g_folder.empty() ? userDir + L"\\Timelapse" : Widen(g_folder);
    for (const std::wstring& part : {std::wstring(), L"\\" + region, L"\\" + region + L"\\" + name})
        CreateDirectoryW((folder + part).c_str(), nullptr);
    wchar_t date[16];
    std::swprintf(date, 16, L"%04u-%02u-%02u", year, month, day);
    const std::wstring copy = folder + L"\\" + region + L"\\" + name + L"\\" + date + L".sc4";
    const bool ok = CopyFileW(path.c_str(), copy.c_str(), FALSE) != 0;
    log::Write("timelapse: %ls -> %ls %s", path.c_str(), copy.c_str(), ok ? "kept" : "COPY FAILED");
}

using SaveFn = bool(__fastcall*)(void* self, void* edx, const cIGZString& path, bool fast);
SaveFn g_save = nullptr;

// Our part runs after the game's save and only reads; it cannot fault on the
// game's state, so no SEH guard (crash.h).
bool __fastcall SaveCity(void* self, void* edx, const cIGZString& path, bool fast)
{
    const bool ok = saves::SaveCity(reinterpret_cast<void*>(g_save), self, edx, &path, fast);
    // The city's own file: the game saves there whatever path it is given
    // (the autotest found SaveCity(path) saving in place).
    cISC4AppPtr app;
    cISC4City* city = app ? app->GetCity() : nullptr;
    if (ok && city) {
        cRZBaseString own;
        city->GetCitySaveFilePath(own);
        if (own.ToChar() && *own.ToChar())
            KeepCopy(Widen(own.ToChar()));
    }
    return ok;
}

bool Install(registry::Mode)
{
    g_folder = settings::Get("timelapse", "folder", "");
    const addr::Entry* save = addr_check::Find(addr::cSC4App_SaveCity);
    const bool ok = save && hook::Install(*save, &SaveCity, &g_save);
    log::Write("timelapse: a copy of the city after every save, %s", ok ? "installed" : "FAILED");
    return ok;
}

const bool g_registered = registry::Register({
    "game.timelapse",
    "game",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4App_SaveCity,
    "Keeps a dated copy of the city after every save, for scripts/render-timelapse.sh",
    false,
    Install,
});

} // namespace
} // namespace opensc4::game_timelapse
