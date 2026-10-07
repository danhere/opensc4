#include "core/diagnostics.h"

#include "core/address_check.h"
#include "core/coverage.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include <opensc4/addresses.h>

namespace opensc4::diagnostics {
namespace {

std::wstring NextToLog(const wchar_t* name)
{
    wchar_t dir[MAX_PATH];
    return log::Directory(dir) ? std::wstring(dir) + name : std::wstring();
}

// diagnostics.coverage: one-shot breakpoints on every function in
// opensc4-functions.csv (tools/ghidra/ExportFunctions.java, copied next to
// core.dll by scripts/run-autotest.sh).
bool ApplyCoverage(registry::Mode)
{
    const std::wstring list = NextToLog(L"opensc4-functions.csv");
    const std::vector<coverage::Function> functions = coverage::LoadFunctionList(list);
    if (functions.empty()) {
        log::Write("coverage: no function list at %ls", list.c_str());
        return false;
    }
    return coverage::Start(functions) > 0;
}

const bool g_coverage = registry::Register({
    "diagnostics.coverage",
    "diagnostics",
    registry::Kind::Detour,
    registry::Phase::LateInit,
    0,
    "Records which game functions run (one-shot breakpoints at every function entry)",
    false,
    ApplyCoverage,
});

// diagnostics.heap_shift: OPENSC4_HEAP_SHIFT=<bytes> allocates that many bytes
// from the game's own malloc (0x009F066E) when the director starts, and keeps
// them, so every later block on the game's heap (including the chunks its
// pool allocators carve objects from) moves. Runs that differ only in this
// show whether, and where, the game's results depend on heap addresses
// (docs/experiments/determinism.md, "Heap layout").
bool ApplyHeapShift(registry::Mode)
{
    char value[32];
    const DWORD length = GetEnvironmentVariableA("OPENSC4_HEAP_SHIFT", value, sizeof(value));
    const unsigned long bytes = length > 0 && length < sizeof(value) ? std::strtoul(value, nullptr, 0) : 0;
    const addr::Entry* entry = addr_check::Find(addr::CRT_malloc);
    if (!entry || bytes == 0) {
        log::Write("heap_shift: %s", entry ? "OPENSC4_HEAP_SHIFT not set" : "malloc not found");
        return false;
    }
    using MallocFn = void*(__cdecl*)(size_t);
    void* block = reinterpret_cast<MallocFn>(entry->address)(bytes);
    log::Write("heap_shift: %lu bytes from the game's malloc at %p, kept", bytes, block);
    return block != nullptr;
}

const bool g_heapShift = registry::Register({
    "diagnostics.heap_shift",
    "diagnostics",
    registry::Kind::Api,
    registry::Phase::DirectorInit,
    addr::CRT_malloc,
    "Allocates OPENSC4_HEAP_SHIFT bytes from the game's heap at startup, to move later allocations",
    false,
    ApplyHeapShift,
});

// determinism.zero_alloc (experiment): the game's allocators (operator new
// 0x005E55E0, the STL allocator 0x0090CF54, malloc 0x009F066E) return
// zero-filled memory, so code that reads memory it never wrote sees the same
// zeros in every run instead of whatever the heap held there.
using AllocFn = void*(__cdecl*)(size_t);
AllocFn g_operatorNew = nullptr;
AllocFn g_stlAllocate = nullptr;
AllocFn g_malloc = nullptr;

void* __cdecl ZeroOperatorNew(size_t size)
{
    void* p = g_operatorNew(size);
    if (p)
        std::memset(p, 0, size);
    return p;
}

void* __cdecl ZeroStlAllocate(size_t size)
{
    void* p = g_stlAllocate(size);
    if (p)
        std::memset(p, 0, size);
    return p;
}

void* __cdecl ZeroMalloc(size_t size)
{
    void* p = g_malloc(size);
    if (p)
        std::memset(p, 0, size);
    return p;
}

bool ApplyZeroAlloc(registry::Mode)
{
    const addr::Entry* opNew = addr_check::Find(addr::SC4_operator_new);
    const addr::Entry* stl = addr_check::Find(addr::SC4_stl_allocate);
    const addr::Entry* crt = addr_check::Find(addr::CRT_malloc);
    const bool ok = opNew && stl && crt && hook::Install(*opNew, &ZeroOperatorNew, &g_operatorNew) &&
                    hook::Install(*stl, &ZeroStlAllocate, &g_stlAllocate) && hook::Install(*crt, &ZeroMalloc, &g_malloc);
    log::Write("zero_alloc: %s", ok ? "the game's allocations are zero-filled" : "FAILED");
    return ok;
}

const bool g_zeroAlloc = registry::Register({
    "determinism.zero_alloc",
    "determinism",
    registry::Kind::Detour,
    registry::Phase::DirectorInit,
    addr::SC4_operator_new,
    "The game's allocations are zero-filled, so reads of unwritten memory do not depend on heap contents",
    false,
    ApplyZeroAlloc,
});

} // namespace

void OnShutdown()
{
    if (registry::ModeOf("diagnostics.coverage") == registry::Mode::Original)
        return;
    const coverage::Summary summary = coverage::GetSummary();
    const std::wstring path = NextToLog(L"opensc4-coverage.csv");
    const bool written = coverage::WriteHits(path);
    coverage::Stop();
    log::Write("coverage: %zu of %zu instrumented functions ran (%.1f%%); %ls %ls", summary.hit, summary.planted,
               summary.planted ? 100.0 * summary.hit / summary.planted : 0.0, written ? L"written to" : L"could not write",
               path.c_str());
}

} // namespace opensc4::diagnostics
