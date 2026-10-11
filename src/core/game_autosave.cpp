// game.autosave: saves the city every few minutes of play and keeps the last
// few of those saves as copies (docs/autosave.md). An improvement beyond the
// original, off by default; the save itself is the game's own
// (cISC4App::SaveCity), so the city file is what Ctrl+S would have written.
//
// The rules for when a save is allowed (a city that has been established,
// saving not disabled, no modal window, not paused, the game in front) and
// the idle-service shape come from sc4-auto-save by Nicholas Hayes
// (https://github.com/0xC0000054/sc4-auto-save), used under the MIT License:
//
//   Copyright (c) 2023, 2024 Nicholas Hayes
//
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to permit
//   persons to whom the Software is furnished to do so, subject to the
//   following conditions:
//
//   The above copyright notice and this permission notice shall be included
//   in all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
//   NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
//   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
//   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
//   USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Where the mod follows pause and focus messages, this asks the simulator and
// Windows on every idle call instead; the copies are ours.
#include "core/log.h"
#include "core/registry.h"
#include "core/settings.h"

#include <GZServPtrs.h>
#include <cIGZFrameWork.h>
#include <cIGZWinMgr.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4Simulator.h>
#include <cRZBaseString.h>
#include <cRZBaseSystemService.h>

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace opensc4::game_autosave {
namespace {

constexpr uint32_t kServiceId = 0x4F534336;
// sc4-auto-save's own service: if that plugin is installed too, it saves and
// we stand down.
constexpr uint32_t kModServiceId = 0x3FA8CBB9;

struct Options {
    ULONGLONG intervalMs = 15 * 60000;
    bool fast = true;         // no region thumbnail update, ~1 s instead of ~11
    bool countPaused = false; // paused time counts towards the next save
    bool background = false;  // save while another program is in front
    int keep = 3;             // copies kept per city; 0: none
};

std::wstring Widen(const std::string& text)
{
    const int n = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wide(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, wide.data(), n);
    return wide;
}

// "...\Regions\<region>\City - X.sc4" -> "...\AutoSave\<region>\City - X",
// made if missing; empty if the path has no such shape.
std::wstring CopyFolder(const std::wstring& path)
{
    const size_t file = path.find_last_of(L"\\/");
    const size_t region = file == std::wstring::npos || file == 0 ? file : path.find_last_of(L"\\/", file - 1);
    const size_t regions = region == std::wstring::npos || region == 0 ? region : path.find_last_of(L"\\/", region - 1);
    if (regions == std::wstring::npos)
        return {};
    std::wstring name = path.substr(file + 1);
    if (const size_t dot = name.find_last_of(L'.'); dot != std::wstring::npos)
        name.resize(dot);
    std::wstring folder = path.substr(0, regions) + L"\\AutoSave";
    CreateDirectoryW(folder.c_str(), nullptr);
    folder += L"\\" + path.substr(region + 1, file - region - 1);
    CreateDirectoryW(folder.c_str(), nullptr);
    folder += L"\\" + name;
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

// Deletes the oldest .sc4 files in `folder` until `keep` are left.
void Prune(const std::wstring& folder, int keep)
{
    struct Copy {
        ULONGLONG written;
        std::wstring name;
    };
    std::vector<Copy> copies;
    WIN32_FIND_DATAW found;
    const HANDLE find = FindFirstFileW((folder + L"\\*.sc4").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            copies.push_back({(static_cast<ULONGLONG>(found.ftLastWriteTime.dwHighDateTime) << 32) |
                                  found.ftLastWriteTime.dwLowDateTime,
                              found.cFileName});
    } while (FindNextFileW(find, &found));
    FindClose(find);
    std::sort(copies.begin(), copies.end(), [](const Copy& a, const Copy& b) { return a.written > b.written; });
    for (size_t i = static_cast<size_t>(keep); i < copies.size(); ++i)
        DeleteFileW((folder + L"\\" + copies[i].name).c_str());
}

// A copy of the city file just saved, named by the game date; a second save
// on the same game day replaces that day's copy.
void KeepCopy(cISC4City* city, int keep)
{
    cRZBaseString own;
    city->GetCitySaveFilePath(own);
    cISC4Simulator* sim = city->GetSimulator();
    if (!sim || !own.ToChar() || !*own.ToChar())
        return;
    const std::wstring path = Widen(own.ToChar());
    const std::wstring folder = CopyFolder(path);
    if (folder.empty()) {
        log::Write("autosave: cannot place a copy of %ls", path.c_str());
        return;
    }
    uint32_t year = 0, month = 0, day = 0, dayOfYear = 0, weekDay = 0;
    sim->GetSimDate(&year, &month, &day, &dayOfYear, &weekDay);
    wchar_t date[16];
    std::swprintf(date, 16, L"%04u-%02u-%02u", year, month, day);
    const std::wstring copy = folder + L"\\" + date + L".sc4";
    const bool ok = CopyFileW(path.c_str(), copy.c_str(), FALSE) != 0;
    log::Write("autosave: copy %ls %s", copy.c_str(), ok ? "kept" : "FAILED");
    if (ok)
        Prune(folder, keep);
}

bool InFront()
{
    DWORD process = 0;
    const HWND front = GetForegroundWindow();
    return front && GetWindowThreadProcessId(front, &process) && process == GetCurrentProcessId();
}

class AutoSaveService final : public cRZBaseSystemService {
public:
    explicit AutoSaveService(const Options& options) : cRZBaseSystemService(kServiceId, 1000000), options_(options) {}

    bool Init() override { return true; }
    bool Shutdown() override { return true; }

    bool OnIdle(uint32_t) override
    {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG step = now - last_;
        last_ = now;
        cISC4AppPtr app;
        cISC4City* city = app ? app->GetCity() : nullptr;
        if (city != city_) {
            city_ = city;
            playedMs_ = 0;
        }
        // Nothing to save before the city has been established.
        cISC4Simulator* sim = city && city->GetEstablished() ? city->GetSimulator() : nullptr;
        if (!sim)
            return true;
        const bool paused = sim->IsAnyPaused();
        if (options_.countPaused || !paused)
            playedMs_ += step;
        if (playedMs_ < options_.intervalMs || paused || city->IsSaveDisabled())
            return true;
        if (!options_.background && !InFront())
            return true;
        cIGZWinMgrPtr windows;
        if (!windows || windows->IsModal())
            return true;
        if (!checkedMod_) {
            checkedMod_ = true;
            cIGZSystemService* mod = nullptr;
            cIGZFrameWork* framework = RZGetFrameWork();
            if (framework && framework->GetSystemService(kModServiceId, GZIID_cIGZSystemService,
                                                         reinterpret_cast<void**>(&mod)) && mod) {
                mod->Release();
                framework->RemoveFromOnIdle(this);
                log::Write("autosave: the sc4-auto-save plugin is installed; leaving the saving to it");
                return true;
            }
        }
        const bool saved = app->SaveCity(options_.fast);
        log::Write("autosave: city %s after %llu ms", saved ? "saved" : "NOT saved (the game's SaveCity failed)",
                   GetTickCount64() - now);
        if (saved && options_.keep > 0)
            KeepCopy(city, options_.keep);
        playedMs_ = 0;
        last_ = GetTickCount64();
        return true;
    }

private:
    const Options options_;
    ULONGLONG last_ = GetTickCount64();
    ULONGLONG playedMs_ = 0;
    cISC4City* city_ = nullptr;
    bool checkedMod_ = false;
};

bool Install(registry::Mode)
{
    Options options;
    const int minutes = std::clamp(std::atoi(settings::Get("autosave", "minutes", "15").c_str()), 1, 120);
    options.intervalMs = static_cast<ULONGLONG>(minutes) * 60000;
    // For unattended tests only: an interval in seconds.
    char seconds[16];
    if (const DWORD n = GetEnvironmentVariableA("OPENSC4_TEST_AUTOSAVE_SECONDS", seconds, sizeof(seconds));
        n > 0 && n < sizeof(seconds) && std::atoi(seconds) > 0)
        options.intervalMs = static_cast<ULONGLONG>(std::atoi(seconds)) * 1000;
    options.fast = settings::GetBool("autosave", "fast", true);
    options.countPaused = settings::GetBool("autosave", "count_paused", false);
    options.background = settings::GetBool("autosave", "background", false);
    options.keep = std::clamp(std::atoi(settings::Get("autosave", "keep", "3").c_str()), 0, 100);
    cIGZFrameWork* framework = RZGetFrameWork();
    auto* service = new AutoSaveService(options);
    const bool ok = framework && framework->AddSystemService(service) && framework->AddToOnIdle(service);
    log::Write("autosave: every %llu s of play, %s save, %d copies kept: %s", options.intervalMs / 1000,
               options.fast ? "fast" : "full", options.keep, ok ? "started" : "FAILED to start");
    return ok;
}

const bool g_registered = registry::Register({
    "game.autosave",
    "game",
    registry::Kind::Api,
    registry::Phase::PostAppInit,
    0,
    "Saves the city every few minutes of play and keeps the last few saves as copies",
    false,
    Install,
});

} // namespace
} // namespace opensc4::game_autosave
