// fixes.pin_plugin_dlls: the crash on exit with plugin DLLs (docs/crashes.md).
// When the game shuts down, its GZCOM library list frees every plugin DLL
// (cGZCOMLibrary::Free, FreeLibrary at 0x0087B3D1) while objects and threads
// of those DLLs can still be in use, and the game then calls into unloaded
// code. sc4fix turns that call into NOPs. Without patching anything, OpenSC4
// pins each GZCOM plugin DLL (every loaded module but the game's own and
// ours that exports GZDllGetGZCOMDirector) once all plugins have loaded, so
// FreeLibrary leaves it in memory until the process ends.
#include "core/log.h"
#include "core/registry.h"

#include <windows.h>

#include <tlhelp32.h>

namespace opensc4::pin_plugin_dlls {
namespace {

bool Apply(registry::Mode)
{
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Apply), &self);
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        log::Write("pin_plugin_dlls: no module snapshot (error %lu)", GetLastError());
        return false;
    }
    unsigned pinned = 0;
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    for (BOOL more = Module32FirstW(snapshot, &module); more; more = Module32NextW(snapshot, &module)) {
        if (module.hModule == self || module.hModule == GetModuleHandleW(nullptr) ||
            !GetProcAddress(module.hModule, "GZDllGetGZCOMDirector"))
            continue;
        HMODULE handle = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, module.szExePath, &handle)) {
            ++pinned;
            log::Write("pin_plugin_dlls: pinned %ls", module.szExePath);
        } else {
            log::Write("pin_plugin_dlls: could not pin %ls (error %lu)", module.szExePath, GetLastError());
        }
    }
    CloseHandle(snapshot);
    log::Write("pin_plugin_dlls: %u plugin DLLs stay loaded until the game exits", pinned);
    return true;
}

const bool g_registered = registry::Register({
    "fixes.pin_plugin_dlls",
    "fixes",
    registry::Kind::Api,
    registry::Phase::PostAppInit,
    0,
    "Keeps GZCOM plugin DLLs loaded until the game exits, so shutdown cannot call into an unloaded DLL",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::pin_plugin_dlls
