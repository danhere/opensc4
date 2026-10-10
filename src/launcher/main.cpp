// opensc4.exe: starts SimCity 4.exe suspended, injects core.dll and resumes.
#include "common/abi.h"
#include "launcher/inject.h"
#include "launcher/mirror.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace {

// SimCity 4 Deluxe on Steam.
constexpr const wchar_t* kSteamAppId = L"24780";

// The Steam build's DRM stub hands off to Steam and exits when the game was not
// started by Steam, which would leave the relaunched game without core.dll.
// Steam marks its launches with these variables; set them (unless Steam already
// did) so the stub runs the game in our process. Steam still checks ownership.
// Other builds ignore them.
void SetSteamLaunchEnvironment()
{
    for (const wchar_t* name : {L"SteamAppId", L"SteamGameId"}) {
        if (GetEnvironmentVariableW(name, nullptr, 0) == 0)
            SetEnvironmentVariableW(name, kSteamAppId);
    }
}

void Usage()
{
    std::fwprintf(stderr,
        L"usage: opensc4 [--game <path to SimCity 4.exe>] [--core <path to core.dll>] [--wait] [--large-address]\n"
        L"               [-- <game arguments>]\n"
        L"       opensc4 --version\n"
        L"       opensc4 --check-core\n"
        L"\n"
        L"Without --game, SimCity 4.exe is looked for next to opensc4.exe.\n"
        L"Without --core, core.dll next to opensc4.exe is injected. When core.dll is also\n"
        L"installed in a Plugins folder, pass that copy so the game sees one module.\n"
        L"--large-address runs the game from a mirror of its install in %%LOCALAPPDATA%%\\opensc4\\laa\n"
        L"whose exe is large-address-aware (4 GB instead of 2 GB; the install is not changed).\n");
}

std::wstring ModuleDirectory()
{
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, length);
    return dir.substr(0, dir.find_last_of(L"\\/"));
}

std::wstring FullPath(const std::wstring& path)
{
    wchar_t full[MAX_PATH];
    const DWORD length = GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
    return length && length < MAX_PATH ? std::wstring(full, length) : path;
}

bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// Quotes an argument for CreateProcess if it contains spaces or tabs.
// Game arguments are simple switches, so embedded quotes are not handled.
std::wstring QuoteArg(const std::wstring& arg)
{
    return arg.find_first_of(L" \t") == std::wstring::npos ? arg : L"\"" + arg + L"\"";
}

int CheckCore()
{
    HMODULE core = LoadLibraryW(L"core.dll");
    if (!core) {
        std::fwprintf(stderr, L"core.dll failed to load (error %lu)\n", GetLastError());
        return 1;
    }
    using VersionFn = const char*(__cdecl*)();
    auto version = reinterpret_cast<VersionFn>(GetProcAddress(core, "OpenSC4_Version"));
    if (!version || std::strcmp(version(), OPENSC4_VERSION) != 0) {
        std::fwprintf(stderr, L"core.dll version mismatch\n");
        return 1;
    }
    std::printf("core.dll %s OK\n", version());
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring game;
    std::wstring core;
    std::wstring gameArgs;
    bool wait = false;
    bool largeAddress = false;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--version") {
            std::printf("opensc4 %s\n", OPENSC4_VERSION);
            return 0;
        } else if (arg == L"--check-core") {
            return CheckCore();
        } else if (arg == L"--game" && i + 1 < argc) {
            game = argv[++i];
        } else if (arg == L"--core" && i + 1 < argc) {
            core = argv[++i];
        } else if (arg == L"--wait") {
            wait = true;
        } else if (arg == L"--large-address") {
            largeAddress = true;
        } else if (arg == L"--") {
            for (++i; i < argc; ++i)
                gameArgs += (gameArgs.empty() ? L"" : L" ") + QuoteArg(argv[i]);
        } else {
            Usage();
            return 2;
        }
    }

    const std::wstring launcherDir = ModuleDirectory();
    game = FullPath(game.empty() ? launcherDir + L"\\SimCity 4.exe" : game);
    core = FullPath(core.empty() ? launcherDir + L"\\core.dll" : core);

    if (!FileExists(game)) {
        std::fwprintf(stderr, L"SimCity 4.exe not found at %ls\n\n", game.c_str());
        Usage();
        return 2;
    }
    if (!FileExists(core)) {
        std::fwprintf(stderr, L"core.dll not found at %ls\n", core.c_str());
        return 2;
    }

    if (largeAddress) {
        wchar_t local[MAX_PATH];
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
        if (!length || length >= MAX_PATH) {
            std::fwprintf(stderr, L"opensc4: LOCALAPPDATA is not set\n");
            return 1;
        }
        std::wstring error;
        bool rebuilt = false;
        const std::wstring mirrored = opensc4::launcher::PrepareLargeAddressMirror(
            game, std::wstring(local, length) + L"\\opensc4\\laa", &rebuilt, error);
        if (mirrored.empty()) {
            std::fwprintf(stderr, L"opensc4: --large-address: %ls\n", error.c_str());
            return 1;
        }
        std::wprintf(L"opensc4: large-address-aware mirror (%ls) at %ls\n", rebuilt ? L"built" : L"reused", mirrored.c_str());
        game = mirrored;
    }

    SetSteamLaunchEnvironment();
    auto result = opensc4::launcher::LaunchWithDll(game, gameArgs, core);
    if (!result.ok) {
        std::fwprintf(stderr, L"opensc4: %ls\n", result.error.c_str());
        return 1;
    }
    std::wprintf(L"opensc4: started %ls (pid %lu) with core.dll\n", game.c_str(), result.process.dwProcessId);

    DWORD exitCode = 0;
    if (wait) {
        WaitForSingleObject(result.process.hProcess, INFINITE);
        GetExitCodeProcess(result.process.hProcess, &exitCode);
        std::wprintf(L"opensc4: game exited with code %lu\n", exitCode);
    }
    CloseHandle(result.process.hThread);
    CloseHandle(result.process.hProcess);
    return static_cast<int>(exitCode);
}
