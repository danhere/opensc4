#pragma once

#include <windows.h>

#include <string>

namespace opensc4::launcher {

struct LaunchResult {
    bool ok = false;
    std::wstring error;
    PROCESS_INFORMATION process{};
};

// Starts `exe` suspended, loads `dll` into it on a remote thread, then resumes
// the main thread. The DLL is loaded before the game's entry point (and so
// before its CRT and WinMain) run. On success the caller owns the handles in
// `process`.
LaunchResult LaunchWithDll(const std::wstring& exe,
                           const std::wstring& args,
                           const std::wstring& dll);

} // namespace opensc4::launcher
