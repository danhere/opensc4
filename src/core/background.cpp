// harness.background: keeps a test run from taking over the desktop. The
// game's window never becomes the foreground window and sits at the bottom
// of the z-order (behind whatever the user is working in, still drawn, so
// screenshots are unaffected), and the process runs at below-normal priority.
// Clicking the window in the taskbar still brings it up as usual.
//
// Applied at Phase::Attach, before the exe's own code runs. Only the exe's
// and core.dll's imports are wrapped (iat.h), so Windows' own modules see
// the real functions. Nothing here touches the simulation: the parity runs'
// determinism does not depend on timing (docs/parity.md).
#include "core/iat.h"
#include "core/log.h"
#include "core/registry.h"

#include <windows.h>

namespace opensc4::background {
namespace {

using CreateWindowExAFn = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                        LPVOID);
using ShowWindowFn = BOOL(WINAPI*)(HWND, int);
using SetForegroundWindowFn = BOOL(WINAPI*)(HWND);
using SetActiveWindowFn = HWND(WINAPI*)(HWND);
using BringWindowToTopFn = BOOL(WINAPI*)(HWND);
using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);

struct Imports {
    CreateWindowExAFn createWindowExA = nullptr;
    ShowWindowFn showWindow = nullptr;
    SetForegroundWindowFn setForegroundWindow = nullptr;
    SetActiveWindowFn setActiveWindow = nullptr;
    BringWindowToTopFn bringWindowToTop = nullptr;
    SetWindowPosFn setWindowPos = nullptr;
};

// One set per wrapped module: each import may already point somewhere else
// (another wrapper), so each keeps its own original.
Imports g_exe;
Imports g_core;

bool TopLevel(HWND window)
{
    return window && !(GetWindowLongA(window, GWL_STYLE) & WS_CHILD);
}

// Behind every other top-level window, without activating it.
void SendToBack(HWND window)
{
    SetWindowPos(window, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

// Shows that would activate the window become their non-activating twins.
int Quiet(int command)
{
    switch (command) {
    case SW_SHOWNORMAL:
    case SW_RESTORE:
    case SW_SHOWMAXIMIZED:
    case SW_SHOWDEFAULT:
        return SW_SHOWNOACTIVATE;
    case SW_SHOW:
        return SW_SHOWNA;
    case SW_SHOWMINIMIZED:
        return SW_SHOWMINNOACTIVE;
    default:
        return command;
    }
}

BOOL Show(ShowWindowFn original, HWND window, int command)
{
    if (!TopLevel(window))
        return original(window, command);
    const int quiet = Quiet(command);
    log::Write("background: ShowWindow %d -> %d", command, quiet);
    const BOOL result = original(window, quiet);
    if (quiet == SW_SHOWNOACTIVATE || quiet == SW_SHOWNA)
        SendToBack(window);
    return result;
}

// A top-level window created visible is created hidden and then shown
// quietly: a visible creation activates it.
HWND Create(CreateWindowExAFn original, DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style, int x, int y,
            int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
{
    const bool topLevel = !(style & WS_CHILD);
    if (topLevel)
        exStyle &= ~WS_EX_TOPMOST;
    const bool visible = topLevel && (style & WS_VISIBLE);
    const HWND window = original(exStyle, className, title, visible ? style & ~WS_VISIBLE : style, x, y, width,
                                 height, parent, menu, instance, param);
    if (window && visible) {
        log::Write("background: window \"%s\" created visible; shown without activation", title ? title : "");
        ShowWindow(window, SW_SHOWNOACTIVATE);
        SendToBack(window);
    }
    return window;
}

BOOL SetPos(SetWindowPosFn original, HWND window, HWND after, int x, int y, int width, int height, UINT flags)
{
    if (TopLevel(window)) {
        flags |= SWP_NOACTIVATE;
        if (after == HWND_TOP || after == HWND_TOPMOST || after == HWND_NOTOPMOST)
            flags |= SWP_NOZORDER;
    }
    return original(window, after, x, y, width, height, flags);
}

BOOL WINAPI SetForegroundWindowHook(HWND)
{
    log::Write("background: SetForegroundWindow ignored");
    return TRUE;
}

HWND WINAPI SetActiveWindowHook(HWND)
{
    log::Write("background: SetActiveWindow ignored");
    return GetActiveWindow();
}

BOOL WINAPI BringWindowToTopHook(HWND)
{
    return TRUE;
}

// The hooks proper, one instantiation per module's set of originals.
template <Imports& imports>
struct Hooks {
    static HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style, int x, int y,
                                      int width, int height, HWND parent, HMENU menu, HINSTANCE instance,
                                      LPVOID param)
    {
        return Create(imports.createWindowExA, exStyle, className, title, style, x, y, width, height, parent, menu,
                      instance, param);
    }
    static BOOL WINAPI ShowWindow(HWND window, int command)
    {
        return Show(imports.showWindow, window, command);
    }
    static BOOL WINAPI SetWindowPos(HWND window, HWND after, int x, int y, int width, int height, UINT flags)
    {
        return SetPos(imports.setWindowPos, window, after, x, y, width, height, flags);
    }
};

template <Imports& imports>
int Wrap(void* module)
{
    int patched = 0;
    auto patch = [&](const char* function, auto hook, auto* original) {
        if (iat::Patch(module, "USER32.dll", function, hook, original))
            ++patched;
    };
    patch("CreateWindowExA", &Hooks<imports>::CreateWindowExA, &imports.createWindowExA);
    patch("ShowWindow", &Hooks<imports>::ShowWindow, &imports.showWindow);
    patch("SetWindowPos", &Hooks<imports>::SetWindowPos, &imports.setWindowPos);
    patch("SetForegroundWindow", &SetForegroundWindowHook, &imports.setForegroundWindow);
    patch("SetActiveWindow", &SetActiveWindowHook, &imports.setActiveWindow);
    patch("BringWindowToTop", &BringWindowToTopHook, &imports.bringWindowToTop);
    return patched;
}

bool Apply(registry::Mode)
{
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Apply), &self);
    const int exe = Wrap<g_exe>(nullptr);
    // core.dll's own window (renderer.d3d11).
    const int core = self ? Wrap<g_core>(self) : 0;
    const BOOL lowered = SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    log::Write("background: %d of the exe's and %d of core.dll's window imports wrapped; priority %s", exe, core,
               lowered ? "below normal" : "unchanged");
    return true;
}

const bool g_registered = registry::Register({
    "harness.background",
    "harness",
    registry::Kind::Api,
    registry::Phase::Attach,
    0,
    "The game's window never takes the foreground and stays behind other windows, at below-normal priority, so "
    "test runs leave the desktop usable",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::background
