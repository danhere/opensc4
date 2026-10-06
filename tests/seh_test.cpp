// The SEH pattern OpenSC4 relies on, under clang-cl for 32-bit x86: __try
// catches a fault raised in a function called from the block. A fault raised
// by an instruction directly inside the block is not reliably caught (with
// /EHsc or /EHa, depending on optimisation), so code must never depend on that
// (src/core/crash.h). This test catches a compiler change that breaks the
// supported pattern.
#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace {

__declspec(noinline) uint32_t Load(uintptr_t address)
{
    return *reinterpret_cast<const volatile uint32_t*>(address);
}

// 1: read succeeded, 0: the fault was caught.
int GuardedLoad(uintptr_t address)
{
    __try {
        Load(address);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

} // namespace

int main()
{
    uint32_t local = 5;
    if (GuardedLoad(reinterpret_cast<uintptr_t>(&local)) != 1) {
        std::puts("FAIL: a valid read was reported as a fault");
        return 1;
    }
    if (GuardedLoad(0) != 0 || GuardedLoad(0x7FFF0000) != 0) {
        std::puts("FAIL: a fault in a called function was not caught");
        return 1;
    }
    std::puts("seh test OK");
    return 0;
}
