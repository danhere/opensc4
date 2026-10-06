// Import address table patching: this exe's own import of GetTickCount.
#include "core/iat.h"

#include <windows.h>

#include <cstdio>

using namespace opensc4;

namespace {

using TickFn = DWORD(WINAPI*)();
TickFn g_original = nullptr;

DWORD WINAPI FakeTickCount()
{
    return 12345;
}

int Fail(const char* what)
{
    std::printf("iat test FAILED: %s\n", what);
    return 1;
}

} // namespace

int main()
{
    if (GetTickCount() == 12345)
        return Fail("unlucky tick count; run again");
    if (!iat::Patch(nullptr, "KERNEL32.dll", "GetTickCount", &FakeTickCount, &g_original) || !g_original)
        return Fail("GetTickCount import not found");
    if (GetTickCount() != 12345)
        return Fail("calls through the import do not reach the replacement");
    if (g_original() == 12345)
        return Fail("the original pointer is not the original function");
    if (iat::Patch(nullptr, "KERNEL32.dll", "NoSuchFunctionAnywhere", &FakeTickCount, &g_original))
        return Fail("a function that is not imported was patched");
    TickFn replaced = nullptr;
    if (!iat::Patch(nullptr, "kernel32.dll", "GetTickCount", g_original, &replaced) || replaced != &FakeTickCount)
        return Fail("restoring (with a lower-case DLL name) failed");
    if (GetTickCount() == 12345)
        return Fail("the restored import still reaches the replacement");
    std::printf("iat test OK\n");
    return 0;
}
