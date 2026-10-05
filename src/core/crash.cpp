#include "core/crash.h"

#include "core/log.h"

#include <dbghelp.h>

#include <cstring>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace opensc4::crash {
namespace {

LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;
LONG g_handling = 0;

void DescribeAddress(const void* address, char (&out)[MAX_PATH + 32])
{
    HMODULE module = nullptr;
    char name[MAX_PATH] = "no module";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<const char*>(address), &module)) {
        GetModuleFileNameA(module, name, MAX_PATH);
        const char* slash = std::strrchr(name, '\\');
        wsprintfA(out, "0x%08X (%s+0x%X)", reinterpret_cast<unsigned>(address), slash ? slash + 1 : name,
                  static_cast<unsigned>(static_cast<const char*>(address) - reinterpret_cast<const char*>(module)));
        return;
    }
    wsprintfA(out, "0x%08X (%s)", reinterpret_cast<unsigned>(address), name);
}

// Writes opensc4-crash-<date>-<time>-<pid>.dmp next to the log with the
// system's dbghelp.dll (the game folder ships an XP-era copy that fails).
void WriteMinidump(EXCEPTION_POINTERS* exception)
{
    wchar_t system[MAX_PATH];
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    if (length == 0 || length + 14 >= MAX_PATH)
        return;
    lstrcatW(system, L"\\dbghelp.dll");
    HMODULE dbghelp = LoadLibraryW(system);
    if (!dbghelp) {
        log::Write("crash: could not load %ls", system);
        return;
    }
    using WriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                      PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    const auto writeDump = reinterpret_cast<WriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));

    wchar_t path[MAX_PATH];
    SYSTEMTIME now;
    GetLocalTime(&now);
    if (!writeDump || !log::Directory(path))
        return;
    wchar_t name[64];
    wsprintfW(name, L"opensc4-crash-%04u%02u%02u-%02u%02u%02u-%lu.dmp", now.wYear, now.wMonth, now.wDay, now.wHour,
              now.wMinute, now.wSecond, GetCurrentProcessId());
    if (lstrlenW(path) + lstrlenW(name) >= MAX_PATH)
        return;
    lstrcatW(path, name);

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        log::Write("crash: could not create %ls", path);
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                                 MiniDumpWithUnloadedModules);
    const BOOL written = writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &info, nullptr, nullptr);
    CloseHandle(file);
    log::Write(written ? "crash: minidump written to %ls" : "crash: minidump failed (%ls)", path);
}

// Logs the exception and writes a minidump, once per process.
void Report(EXCEPTION_POINTERS* exception, const char* kind)
{
    // A second crash while handling the first is not reported again.
    if (InterlockedExchange(&g_handling, 1) != 0)
        return;
    const EXCEPTION_RECORD* record = exception->ExceptionRecord;
    char where[MAX_PATH + 32];
    DescribeAddress(record->ExceptionAddress, where);
    log::Write("crash: %s exception 0x%08lX at %s, thread %lu", kind, record->ExceptionCode, where,
               GetCurrentThreadId());
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
        log::Write("crash: %s address 0x%08X",
                   record->ExceptionInformation[0] == 0 ? "reading" :
                   record->ExceptionInformation[0] == 1 ? "writing" : "executing",
                   static_cast<unsigned>(record->ExceptionInformation[1]));
    WriteMinidump(exception);
}

bool IsFatal(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_IN_PAGE_ERROR:
        return true;
    default:
        return false;
    }
}

bool InThisModule(const void* address)
{
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCWSTR>(address), &module) &&
           module == reinterpret_cast<HMODULE>(&__ImageBase);
}

// Sees every exception before any __try frame. SimCity 4 catches crashes in
// its main loop with its own __except handler (it never installs a top-level
// filter), so this is the only way to report a crash in our code before the
// game's handler ends the process. Only fatal exceptions raised inside this
// module are reported: OpenSC4 code never uses SEH for control flow, so a
// fault here is always a bug. Crashes in game code get the game's own report.
LONG CALLBACK FirstChanceHandler(EXCEPTION_POINTERS* exception)
{
    const EXCEPTION_RECORD* record = exception->ExceptionRecord;
    if (IsFatal(record->ExceptionCode) && InThisModule(record->ExceptionAddress))
        Report(exception, "fatal");
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* exception)
{
    Report(exception, "unhandled");
    // With no previous filter (outside the game), end the process quietly
    // instead of showing the Windows error dialog.
    return g_previous ? g_previous(exception) : EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void InstallHandler()
{
    LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(TopLevelFilter);
    if (previous != TopLevelFilter)
        g_previous = previous;
    static bool vehInstalled = false;
    if (!vehInstalled)
        vehInstalled = AddVectoredExceptionHandler(1, FirstChanceHandler) != nullptr;
    log::Write("crash: handlers installed (previous top-level filter %p, first-chance handler %s)",
               reinterpret_cast<void*>(g_previous), vehInstalled ? "on" : "FAILED");
}

int HookFilter(const char* hook, EXCEPTION_POINTERS* exception)
{
    char where[MAX_PATH + 32];
    DescribeAddress(exception->ExceptionRecord->ExceptionAddress, where);
    log::Write("crash: exception 0x%08lX at %s passed through hook %s", exception->ExceptionRecord->ExceptionCode,
               where, hook);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace opensc4::crash
