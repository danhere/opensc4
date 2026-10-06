// diagnostics.trace.threads (P4): logs every thread the game starts, through
// the C runtime's _beginthreadex (0x009F5306, used by the game's thread class
// around 0x009208CB) and its own CreateThread import (0x00869B10), with the
// start routine, the argument, the argument's vtable if it is an object, and
// the caller. Threads that touch simulation state would make runs differ
// however the clock is controlled (docs/experiments/determinism.md).
#include "core/address_check.h"
#include "core/hook.h"
#include "core/iat.h"
#include "core/log.h"
#include "core/registry.h"

#include <windows.h>

#include <intrin.h>

#include <cstdint>

#include <opensc4/addresses.h>

namespace opensc4::thread_trace {
namespace {

// First word of `p` if p points at readable memory that starts with a pointer
// into the exe's image (a vtable), else 0.
uintptr_t VtableOf(void* p)
{
    if (!p)
        return 0;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return 0;
    const uintptr_t first = *static_cast<uintptr_t*>(p);
    return first >= 0x00400000 && first < 0x00C00000 ? first : 0;
}

using BeginThreadexFn = uintptr_t(__cdecl*)(void* security, unsigned stack, unsigned(__stdcall* start)(void*),
                                            void* arg, unsigned flags, unsigned* id);
BeginThreadexFn g_beginThreadex = nullptr;

uintptr_t __cdecl BeginThreadex(void* security, unsigned stack, unsigned(__stdcall* start)(void*), void* arg,
                                unsigned flags, unsigned* id)
{
    void* caller = _ReturnAddress();
    const uintptr_t result = g_beginThreadex(security, stack, start, arg, flags, id);
    log::Write("threads: _beginthreadex from %p: start %p, arg %p (vtable %08X), thread id %u", caller,
               reinterpret_cast<void*>(start), arg, static_cast<unsigned>(VtableOf(arg)), id ? *id : 0u);
    // The game's thread class passes a small record: routine, object, ...
    // Log its first words and the vtable of each, to name the owner.
    if (VtableOf(arg)) {
        auto* words = static_cast<void**>(arg);
        for (int i = 1; i < 4; ++i)
            log::Write("threads:   arg word %d: %p (vtable %08X)", i, words[i],
                       static_cast<unsigned>(VtableOf(words[i])));
    }
    return result;
}

using CreateThreadFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
CreateThreadFn g_createThread = nullptr;

HANDLE WINAPI GameCreateThread(LPSECURITY_ATTRIBUTES security, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID arg,
                               DWORD flags, LPDWORD id)
{
    void* caller = _ReturnAddress();
    DWORD localId = 0;
    HANDLE thread = g_createThread(security, stack, start, arg, flags, id ? id : &localId);
    log::Write("threads: CreateThread from %p: start %p, arg %p, thread id %lu", caller, reinterpret_cast<void*>(start),
               arg, id ? *id : localId);
    return thread;
}

bool Install(registry::Mode)
{
    log::Write("threads: tracing from thread %lu (the launcher's injection thread)", GetCurrentThreadId());
    bool ok = iat::Patch(nullptr, "KERNEL32.dll", "CreateThread", &GameCreateThread, &g_createThread);
    const addr::Entry* entry = addr_check::Find(addr::CRT_beginthreadex);
    ok = entry && hook::Install(*entry, &BeginThreadex, &g_beginThreadex) && ok;
    return ok;
}

const bool g_registered = registry::Register({
    "diagnostics.trace.threads",
    "diagnostics",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::CRT_beginthreadex,
    "Logs every thread the game starts, with its start routine and argument",
    false,
    Install,
});

} // namespace
} // namespace opensc4::thread_trace
