// core.dll: injected into SimCity 4.exe by the launcher.
// Skeleton only. Hooking, GZCOM director and logging land in later P0 work.
#include "common/abi.h"

#include <windows.h>

extern "C" __declspec(dllexport) const char* __cdecl OpenSC4_Version()
{
    return OPENSC4_VERSION;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}
