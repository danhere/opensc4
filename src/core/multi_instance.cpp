// harness.multi_instance: lets several copies of the game run at once, so the
// parity harness can run saves in parallel (scripts/run-parity.sh,
// OPENSC4_PARITY_JOBS). Whatever single-instance check the exe makes goes
// through a named kernel object or a window search; this gives every named
// mutex, event and semaphore the exe creates or opens a per-process name
// (the name plus "-<pid>"), and hides other processes' windows from the
// exe's FindWindow calls. Every call is logged with its name, so the log shows what the
// check actually is.
//
// Applied at Phase::Attach, before the exe's own code runs. Only the exe's
// imports are wrapped (iat.h): Windows, Steam and our own code see the real
// objects. Nothing here touches the simulation.
#include "core/iat.h"
#include "core/log.h"
#include "core/registry.h"

#include <windows.h>

#include <cstdio>

namespace opensc4::multi_instance {
namespace {

using CreateMutexAFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
using CreateMutexWFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
using OpenMutexAFn = HANDLE(WINAPI*)(DWORD, BOOL, LPCSTR);
using OpenMutexWFn = HANDLE(WINAPI*)(DWORD, BOOL, LPCWSTR);
using CreateEventAFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR);
using CreateEventWFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
using CreateSemaphoreAFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, LONG, LONG, LPCSTR);
using FindWindowAFn = HWND(WINAPI*)(LPCSTR, LPCSTR);
using FindWindowWFn = HWND(WINAPI*)(LPCWSTR, LPCWSTR);

CreateMutexAFn g_createMutexA = nullptr;
CreateMutexWFn g_createMutexW = nullptr;
OpenMutexAFn g_openMutexA = nullptr;
OpenMutexWFn g_openMutexW = nullptr;
CreateEventAFn g_createEventA = nullptr;
CreateEventWFn g_createEventW = nullptr;
CreateSemaphoreAFn g_createSemaphoreA = nullptr;
FindWindowAFn g_findWindowA = nullptr;
FindWindowWFn g_findWindowW = nullptr;

// `name` with "-<pid>" appended, in `buffer`. Null stays null (unnamed objects
// are private to the process already).
const char* Unique(const char* name, char (&buffer)[MAX_PATH])
{
    if (!name)
        return nullptr;
    std::snprintf(buffer, MAX_PATH, "%s-%lu", name, GetCurrentProcessId());
    return buffer;
}

const wchar_t* Unique(const wchar_t* name, wchar_t (&buffer)[MAX_PATH])
{
    if (!name)
        return nullptr;
    std::swprintf(buffer, MAX_PATH, L"%ls-%lu", name, GetCurrentProcessId());
    return buffer;
}

HANDLE WINAPI CreateMutexAHook(LPSECURITY_ATTRIBUTES attributes, BOOL owner, LPCSTR name)
{
    char buffer[MAX_PATH];
    if (name)
        log::Write("multi_instance: CreateMutexA \"%s\"", name);
    return g_createMutexA(attributes, owner, Unique(name, buffer));
}

HANDLE WINAPI CreateMutexWHook(LPSECURITY_ATTRIBUTES attributes, BOOL owner, LPCWSTR name)
{
    wchar_t buffer[MAX_PATH];
    if (name)
        log::Write("multi_instance: CreateMutexW \"%ls\"", name);
    return g_createMutexW(attributes, owner, Unique(name, buffer));
}

HANDLE WINAPI OpenMutexAHook(DWORD access, BOOL inherit, LPCSTR name)
{
    char buffer[MAX_PATH];
    log::Write("multi_instance: OpenMutexA \"%s\"", name ? name : "");
    return g_openMutexA(access, inherit, Unique(name, buffer));
}

HANDLE WINAPI OpenMutexWHook(DWORD access, BOOL inherit, LPCWSTR name)
{
    wchar_t buffer[MAX_PATH];
    log::Write("multi_instance: OpenMutexW \"%ls\"", name ? name : L"");
    return g_openMutexW(access, inherit, Unique(name, buffer));
}

HANDLE WINAPI CreateEventAHook(LPSECURITY_ATTRIBUTES attributes, BOOL manual, BOOL initial, LPCSTR name)
{
    char buffer[MAX_PATH];
    if (name)
        log::Write("multi_instance: CreateEventA \"%s\"", name);
    return g_createEventA(attributes, manual, initial, Unique(name, buffer));
}

HANDLE WINAPI CreateEventWHook(LPSECURITY_ATTRIBUTES attributes, BOOL manual, BOOL initial, LPCWSTR name)
{
    wchar_t buffer[MAX_PATH];
    if (name)
        log::Write("multi_instance: CreateEventW \"%ls\"", name);
    return g_createEventW(attributes, manual, initial, Unique(name, buffer));
}

HANDLE WINAPI CreateSemaphoreAHook(LPSECURITY_ATTRIBUTES attributes, LONG initial, LONG maximum, LPCSTR name)
{
    char buffer[MAX_PATH];
    if (name)
        log::Write("multi_instance: CreateSemaphoreA \"%s\"", name);
    return g_createSemaphoreA(attributes, initial, maximum, Unique(name, buffer));
}

// A window of another process (another copy of the game) is not found; the
// exe's own windows still are.
HWND OwnOnly(HWND window)
{
    DWORD process = 0;
    if (window && GetWindowThreadProcessId(window, &process) && process != GetCurrentProcessId()) {
        SetLastError(ERROR_SUCCESS);
        return nullptr;
    }
    return window;
}

HWND WINAPI FindWindowAHook(LPCSTR className, LPCSTR title)
{
    const HWND found = g_findWindowA(className, title);
    const HWND result = OwnOnly(found);
    log::Write("multi_instance: FindWindowA class \"%s\" title \"%s\": %s",
               className && !IS_INTRESOURCE(className) ? className : "", title ? title : "",
               !found ? "none" : result ? "ours" : "another process's, hidden");
    return result;
}

HWND WINAPI FindWindowWHook(LPCWSTR className, LPCWSTR title)
{
    const HWND found = g_findWindowW(className, title);
    const HWND result = OwnOnly(found);
    log::Write("multi_instance: FindWindowW class \"%ls\" title \"%ls\": %s",
               className && !IS_INTRESOURCE(className) ? className : L"", title ? title : L"",
               !found ? "none" : result ? "ours" : "another process's, hidden");
    return result;
}

bool Apply(registry::Mode)
{
    int patched = 0;
    auto patch = [&](const char* dll, const char* function, auto hook, auto* original) {
        if (iat::Patch(nullptr, dll, function, hook, original))
            ++patched;
    };
    patch("KERNEL32.dll", "CreateMutexA", &CreateMutexAHook, &g_createMutexA);
    patch("KERNEL32.dll", "CreateMutexW", &CreateMutexWHook, &g_createMutexW);
    patch("KERNEL32.dll", "OpenMutexA", &OpenMutexAHook, &g_openMutexA);
    patch("KERNEL32.dll", "OpenMutexW", &OpenMutexWHook, &g_openMutexW);
    patch("KERNEL32.dll", "CreateEventA", &CreateEventAHook, &g_createEventA);
    patch("KERNEL32.dll", "CreateEventW", &CreateEventWHook, &g_createEventW);
    patch("KERNEL32.dll", "CreateSemaphoreA", &CreateSemaphoreAHook, &g_createSemaphoreA);
    patch("USER32.dll", "FindWindowA", &FindWindowAHook, &g_findWindowA);
    patch("USER32.dll", "FindWindowW", &FindWindowWHook, &g_findWindowW);
    log::Write("multi_instance: %d of the exe's imports wrapped (CreateMutex %s%s, OpenMutex %s%s, CreateEvent %s%s, "
               "CreateSemaphoreA %s, FindWindow %s%s)",
               patched, g_createMutexA ? "A" : "", g_createMutexW ? "W" : "", g_openMutexA ? "A" : "",
               g_openMutexW ? "W" : "", g_createEventA ? "A" : "", g_createEventW ? "W" : "",
               g_createSemaphoreA ? "A" : "", g_findWindowA ? "A" : "", g_findWindowW ? "W" : "");
    return true;
}

const bool g_registered = registry::Register({
    "harness.multi_instance",
    "harness",
    registry::Kind::Api,
    registry::Phase::Attach,
    0,
    "Per-process names for the exe's named kernel objects, and no window found by FindWindow, so several copies "
    "can run at once (parallel parity runs)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::multi_instance
