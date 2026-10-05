// Hook layer test: detours a function in this program and checks that
//   - a hook with the wrong expected bytes is refused;
//   - an installed hook redirects calls, and the trampoline reaches the original;
//   - hooking an entry that is already patched (as another DLL would) is refused;
//   - removing the hook restores both behaviour and bytes.
#include "core/hook.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// Long enough for a 5-byte JMP; noinline so calls go through the entry.
__declspec(noinline) int Compute(int a, int b)
{
    volatile int x = a;
    for (int i = 0; i < b; ++i)
        x = x * 3 + 1;
    return x;
}

using ComputeFn = int (*)(int, int);
ComputeFn g_original = nullptr;

int Detour(int a, int b)
{
    return g_original(a, b) + 1000000;
}

int Fail(const char* what)
{
    std::fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

} // namespace

int main()
{
    namespace hook = opensc4::hook;
    const auto target = reinterpret_cast<std::uintptr_t>(&Compute);
    const int expected = Compute(2, 3);

    unsigned char head[8];
    std::memcpy(head, reinterpret_cast<const void*>(target), sizeof(head));
    unsigned char wrong[8];
    std::memcpy(wrong, head, sizeof(wrong));
    wrong[0] ^= 0xFF;

    if (hook::InstallAt("Compute (wrong bytes)", target, wrong, sizeof(wrong), &Detour, &g_original))
        return Fail("installed a hook although the expected bytes were wrong");

    if (!hook::InstallAt("Compute", target, head, sizeof(head), &Detour, &g_original))
        return Fail("could not install the hook");
    if (Compute(2, 3) != expected + 1000000)
        return Fail("the detour was not called, or the trampoline did not reach the original");

    ComputeFn second = nullptr;
    if (hook::InstallAt("Compute (already patched)", target, head, sizeof(head), &Detour, &second))
        return Fail("hooked over an existing patch");
    if (hook::InstalledCount() != 1)
        return Fail("unexpected number of installed hooks");

    hook::RemoveAll();
    if (Compute(2, 3) != expected)
        return Fail("the original behaviour was not restored");
    if (std::memcmp(reinterpret_cast<const void*>(target), head, sizeof(head)) != 0)
        return Fail("the original bytes were not restored");

    std::puts("hook test OK");
    return 0;
}
