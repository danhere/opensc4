#include "core/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace opensc4::log {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;

bool LogPath(wchar_t (&path)[MAX_PATH])
{
    DWORD length = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
    while (length > 0 && path[length - 1] != L'\\')
        --length;
    if (length == 0 || length + 12 >= MAX_PATH)
        return false;
    lstrcpyW(path + length, L"opensc4.log");
    return true;
}

} // namespace

void Write(const char* format, ...)
{
    char line[1024];
    SYSTEMTIME now;
    GetLocalTime(&now);
    int size = std::snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] ",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        GetCurrentThreadId());
    if (size < 0)
        return;

    va_list args;
    va_start(args, format);
    const int body = std::vsnprintf(line + size, sizeof(line) - size - 2, format, args);
    va_end(args);
    if (body < 0)
        return;
    size += body < static_cast<int>(sizeof(line)) - size - 2 ? body : static_cast<int>(sizeof(line)) - size - 3;
    line[size++] = '\r';
    line[size++] = '\n';

    wchar_t path[MAX_PATH];
    if (!LogPath(path))
        return;

    AcquireSRWLockExclusive(&g_lock);
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(size), &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace opensc4::log
