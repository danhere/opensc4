// core.dll: injected into SimCity 4.exe by the launcher.
// For now it only records where it was loaded. Hooking, the GZCOM director and
// real logging land in later P0 work.
#include "common/abi.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace {

// Build 641 has 0x0F at this address; ecosystem DLLs use it as a fallback
// version check (docs/patch-census.md, section 1).
constexpr std::uintptr_t kBuildSentinelAddress = 0x6E5000;
constexpr unsigned char kBuild641Sentinel = 0x0F;

// Returns the byte at `address`, or -1 if it is not readable memory.
int ReadByteIfMapped(std::uintptr_t address)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) || info.State != MEM_COMMIT)
        return -1;
    constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                               PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(info.Protect & readable) || (info.Protect & PAGE_GUARD))
        return -1;
    return *reinterpret_cast<const unsigned char*>(address);
}

// Appends one line to opensc4.log next to core.dll. Win32 only: this runs
// under the loader lock.
void LogAttach(HINSTANCE instance)
{
    wchar_t logPath[MAX_PATH];
    DWORD length = GetModuleFileNameW(instance, logPath, MAX_PATH);
    while (length > 0 && logPath[length - 1] != L'\\')
        --length;
    if (length == 0 || length + 12 >= MAX_PATH)
        return;
    lstrcpyW(logPath + length, L"opensc4.log");

    wchar_t hostPath[MAX_PATH] = L"?";
    GetModuleFileNameW(nullptr, hostPath, MAX_PATH);

    SYSTEMTIME now;
    GetLocalTime(&now);

    const int sentinel = ReadByteIfMapped(kBuildSentinelAddress);
    const char* build = sentinel == kBuild641Sentinel ? "1.1.641" : "unknown";

    char line[1024];
    const int size = std::snprintf(line, sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u core.dll %s attached: pid %lu, host %ls, build %s (sentinel %d)\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        OPENSC4_VERSION, GetCurrentProcessId(), hostPath, build, sentinel);
    if (size <= 0)
        return;

    HANDLE file = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(size < static_cast<int>(sizeof(line)) ? size : sizeof(line) - 1),
              &written, nullptr);
    CloseHandle(file);
}

} // namespace

extern "C" __declspec(dllexport) const char* __cdecl OpenSC4_Version()
{
    return OPENSC4_VERSION;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        LogAttach(instance);
    }
    return TRUE;
}
