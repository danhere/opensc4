// core.dll: injected into SimCity 4.exe by the launcher, and also loaded by the
// game as a GZCOM plugin when it sits in a Plugins folder (director.cpp).
#include "common/abi.h"
#include "core/game.h"
#include "core/log.h"

#include <windows.h>

extern "C" __declspec(dllexport) const char* __cdecl OpenSC4_Version()
{
    return OPENSC4_VERSION;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        wchar_t host[MAX_PATH] = L"?";
        GetModuleFileNameW(nullptr, host, MAX_PATH);
        const int sentinel = opensc4::game::BuildSentinel();
        opensc4::log::Write("core.dll %s attached: pid %lu, host %ls, build %s (sentinel %d)",
            OPENSC4_VERSION, GetCurrentProcessId(), host,
            opensc4::game::IsBuild641() ? "1.1.641" : "unknown", sentinel);
    }
    return TRUE;
}
