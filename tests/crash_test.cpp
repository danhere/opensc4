// Crash handling test. Run without arguments, it starts itself with --crash
// and checks the result:
//   - the child crashes inside a guarded "hook" (a null write);
//   - the guard logs the hook's name, the top-level filter logs the exception
//     and writes a minidump, and the process ends with the exception code
//     (no Windows error dialog).
#include "core/crash.h"
#include "core/log.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

__declspec(noinline) void WriteThroughNull(volatile int* p)
{
    *p = 42;
}

void GuardedCrash()
{
    __try {
        WriteThroughNull(nullptr);
    } __except (opensc4::crash::HookFilter("test::GuardedCrash", GetExceptionInformation())) {
    }
}

int Fail(const char* what)
{
    std::fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

std::string ReadLog()
{
    wchar_t path[MAX_PATH];
    if (!opensc4::log::Directory(path))
        return {};
    lstrcatW(path, L"opensc4.log");
    std::string text;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") == 0 && f) {
        char buffer[4096];
        size_t n;
        while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
            text.append(buffer, n);
        std::fclose(f);
    }
    return text;
}

int CountDumps(DWORD pid, ULONGLONG* size)
{
    wchar_t pattern[MAX_PATH];
    opensc4::log::Directory(pattern);
    wchar_t suffix[40];
    wsprintfW(suffix, L"opensc4-crash-*-%lu.dmp", pid);
    lstrcatW(pattern, suffix);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
        return 0;
    int count = 0;
    do {
        ++count;
        *size = (static_cast<ULONGLONG>(found.nFileSizeHigh) << 32) | found.nFileSizeLow;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return count;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc > 1 && std::wcscmp(argv[1], L"--crash") == 0) {
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
        opensc4::crash::InstallHandler();
        GuardedCrash();
        return 0; // not reached
    }

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring commandLine = L"\"" + std::wstring(self) + L"\" --crash";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(self, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &child))
        return Fail("could not start the child");
    WaitForSingleObject(child.hProcess, 30000);
    DWORD exitCode = 0;
    GetExitCodeProcess(child.hProcess, &exitCode);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);

    if (exitCode != EXCEPTION_ACCESS_VIOLATION)
        return Fail("the child did not end with the access violation's code");
    ULONGLONG dumpSize = 0;
    if (CountDumps(child.dwProcessId, &dumpSize) != 1 || dumpSize == 0)
        return Fail("no minidump (or an empty one) for the child");

    const std::string text = ReadLog();
    char pidTag[32];
    std::snprintf(pidTag, sizeof(pidTag), "-%lu.dmp", child.dwProcessId);
    if (text.find("passed through hook test::GuardedCrash") == std::string::npos)
        return Fail("the guard did not log the hook name");
    if (text.find("exception 0xC0000005") == std::string::npos)
        return Fail("the top-level filter did not log the exception");
    if (text.find("writing address 0x00000000") == std::string::npos)
        return Fail("the access violation details were not logged");
    if (text.find(pidTag) == std::string::npos)
        return Fail("the minidump path was not logged");

    std::printf("crash test OK (minidump %llu bytes)\n", dumpSize);
    return 0;
}
