// Foreign patch sites: a function whose known site another DLL patched is
// left to the game by the hook layer (core/foreign_sites.h):
//   - an untouched site does not count;
//   - a JMP from the site into allocated memory (as NAM's stubs) counts;
//   - hooking that function then installs nothing, reports success and hands
//     back the function itself as the original.
#include "core/foreign_sites.h"
#include "core/hook.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// Long enough for an 8-byte head and a 5-byte JMP after it.
__declspec(noinline) int Compute(int a, int b)
{
    volatile int x = a;
    for (int i = 0; i < b; ++i)
        x = x * 3 + 1;
    for (int i = 0; i < a; ++i)
        x = x ^ (x >> 2);
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

void Write(std::uintptr_t address, const unsigned char* bytes, std::size_t size)
{
    DWORD old = 0;
    VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(reinterpret_cast<void*>(address), bytes, size);
    VirtualProtect(reinterpret_cast<void*>(address), size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
}

} // namespace

int main()
{
    namespace hook = opensc4::hook;
    namespace foreign = opensc4::foreign_sites;
    const auto target = reinterpret_cast<std::uintptr_t>(&Compute);
    const std::uintptr_t site = target + 8;
    const int expected = Compute(2, 3);

    unsigned char head[8];
    std::memcpy(head, reinterpret_cast<const void*>(target), sizeof(head));
    unsigned char saved[5];
    std::memcpy(saved, reinterpret_cast<const void*>(site), sizeof(saved));

    foreign::AddForTests(target, site);
    if (!foreign::PatchedInside(target).empty())
        return Fail("an untouched site counted as patched");

    // A stub outside every module, as another DLL's would be.
    void* stub = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stub)
        return Fail("could not allocate the stub");
    unsigned char jump[5] = {0xE9};
    const std::int32_t rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(stub) - (site + 5));
    std::memcpy(jump + 1, &rel, sizeof(rel));
    Write(site, jump, sizeof(jump));

    if (foreign::PatchedInside(target).empty())
        return Fail("a JMP into allocated memory did not count as patched");
    if (!hook::InstallAt("Compute (patched inside)", target, head, sizeof(head), &Detour, &g_original))
        return Fail("a function patched inside made the hook fail instead of being left alone");
    if (hook::IsInstalledAt(target) || hook::InstalledCount() != 0)
        return Fail("a hook was installed on a function patched inside");
    if (reinterpret_cast<std::uintptr_t>(g_original) != target)
        return Fail("the original is not the function itself");

    // With the site restored the hook goes in as usual.
    Write(site, saved, sizeof(saved));
    g_original = nullptr;
    if (!hook::InstallAt("Compute", target, head, sizeof(head), &Detour, &g_original))
        return Fail("could not install the hook once the site was restored");
    if (Compute(2, 3) != expected + 1000000)
        return Fail("the detour was not called");
    hook::RemoveAll();

    std::puts("foreign sites test OK");
    return 0;
}
