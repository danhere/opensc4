#include "core/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace opensc4::log {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;

bool LogPath(wchar_t (&path)[MAX_PATH])
{
    if (!Directory(path))
        return false;
    const int length = lstrlenW(path);
    if (length + 12 >= MAX_PATH)
        return false;
    lstrcpyW(path + length, L"opensc4.log");
    return true;
}

} // namespace

bool Directory(wchar_t (&path)[MAX_PATH])
{
    DWORD length = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
    while (length > 0 && path[length - 1] != L'\\')
        --length;
    path[length] = L'\0';
    return length > 0;
}

void Write(const char* format, ...)
{
    // A line that fits in this buffer (prefix, text and line end) is built
    // here: this runs in the crash handler, under the loader lock and in the
    // game's threads, so the common case allocates nothing. A longer line
    // (the autotest's daily fingerprint line) is formatted a second time into
    // a block from the process heap and written whole, in one WriteFile. If
    // the heap has no room the line is cut to fit this buffer.
    char buffer[4096];
    char* line = buffer;
    SYSTEMTIME now;
    GetLocalTime(&now);
    int size = std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] ",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        GetCurrentThreadId());
    if (size < 0)
        return;

    va_list args;
    va_list again;
    va_start(args, format);
    va_copy(again, args);
    const int room = static_cast<int>(sizeof(buffer)) - size - 2;
    int body = std::vsnprintf(buffer + size, room, format, args);
    va_end(args);
    if (body < 0) {
        va_end(again);
        return;
    }
    if (body >= room) {
        char* const big = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(size) + body + 3));
        if (big) {
            std::memcpy(big, buffer, static_cast<std::size_t>(size));
            std::vsnprintf(big + size, static_cast<std::size_t>(body) + 1, format, again);
            line = big;
        } else {
            body = room - 1;
        }
    }
    va_end(again);
    size += body;
    line[size++] = '\r';
    line[size++] = '\n';

    wchar_t path[MAX_PATH];
    if (LogPath(path)) {
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
    if (line != buffer)
        HeapFree(GetProcessHeap(), 0, line);
}

} // namespace opensc4::log
