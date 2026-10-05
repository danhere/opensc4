#include "core/coverage.h"

#include "core/log.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace opensc4::coverage {
namespace {

constexpr unsigned char kInt3 = 0xCC;
constexpr std::uint32_t kNotHit = 0xFFFFFFFF;
constexpr std::uint32_t kRestoredByStop = 0xFFFFFFFE; // not hit; byte restored by Stop

// Planted entries, sorted by address; read-only once Start returns, so the
// exception handler can search it without locking.
std::vector<Function> g_planted;
std::unique_ptr<std::atomic<std::uint32_t>[]> g_firstHitMs; // kNotHit until hit
std::atomic<std::size_t> g_hits{0};
std::size_t g_listed = 0;
ULONGLONG g_startTick = 0;
PVOID g_handler = nullptr;
std::atomic<bool> g_active{false};

void WriteCodeByte(std::uintptr_t address, unsigned char value)
{
    DWORD old;
    auto* p = reinterpret_cast<unsigned char*>(address);
    VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old);
    *p = value;
    VirtualProtect(p, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 1);
}

const Function* FindPlanted(std::uintptr_t address)
{
    const auto it = std::lower_bound(g_planted.begin(), g_planted.end(), address,
                                     [](const Function& f, std::uintptr_t a) { return f.address < a; });
    return it != g_planted.end() && it->address == address ? &*it : nullptr;
}

LONG CALLBACK BreakpointHandler(EXCEPTION_POINTERS* exception)
{
    if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT || !g_active.load())
        return EXCEPTION_CONTINUE_SEARCH;
    const auto address = reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress);
    const Function* function = FindPlanted(address);
    if (!function)
        return EXCEPTION_CONTINUE_SEARCH; // someone else's breakpoint

    // Not hit before (kNotHit): record it and restore the byte. Already
    // recorded or restored by Stop: another thread restores or restored it.
    const std::size_t index = function - g_planted.data();
    std::uint32_t expected = kNotHit;
    const auto elapsed = static_cast<std::uint32_t>(GetTickCount64() - g_startTick);
    if (g_firstHitMs[index].compare_exchange_strong(expected, elapsed)) {
        WriteCodeByte(address, function->firstByte);
        g_hits.fetch_add(1);
    }
    // Another thread may have restored the byte already; either way, run the
    // real first instruction.
    exception->ContextRecord->Eip = static_cast<DWORD>(address);
    return EXCEPTION_CONTINUE_EXECUTION;
}

} // namespace

std::vector<Function> LoadFunctionList(const std::wstring& path)
{
    std::vector<Function> functions;
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
        return functions;
    char line[512];
    bool header = true;
    while (std::fgets(line, sizeof(line), file)) {
        if (header) {
            header = false;
            continue;
        }
        char* end = nullptr;
        const unsigned long address = std::strtoul(line, &end, 16);
        if (!end || *end != ',')
            continue;
        const char* size = end + 1;
        const char* first = std::strchr(size, ',');
        if (!first)
            continue;
        functions.push_back({address, static_cast<unsigned char>(std::strtoul(first + 1, nullptr, 16))});
    }
    std::fclose(file);
    return functions;
}

std::size_t Start(const std::vector<Function>& functions)
{
    if (g_active.load())
        return g_planted.size();
    g_listed = functions.size();

    std::vector<Function> candidates = functions;
    std::sort(candidates.begin(), candidates.end(),
              [](const Function& a, const Function& b) { return a.address < b.address; });
    candidates.erase(std::unique(candidates.begin(), candidates.end(),
                                 [](const Function& a, const Function& b) { return a.address == b.address; }),
                     candidates.end());

    // Keep entries that are readable and still start with the listed byte.
    for (const Function& f : candidates) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(f.address), &info, sizeof(info)) || info.State != MEM_COMMIT)
            continue;
        if (!(info.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            continue;
        if (*reinterpret_cast<const unsigned char*>(f.address) == f.firstByte && f.firstByte != kInt3)
            g_planted.push_back(f);
    }
    g_firstHitMs.reset(new std::atomic<std::uint32_t>[g_planted.size()]);
    for (std::size_t i = 0; i < g_planted.size(); ++i)
        g_firstHitMs[i].store(kNotHit);

    g_startTick = GetTickCount64();
    g_handler = AddVectoredExceptionHandler(1, BreakpointHandler);
    g_active.store(true);

    // One VirtualProtect per run of entries on the same page.
    DWORD old = 0;
    std::uintptr_t page = 0;
    for (const Function& f : g_planted) {
        const std::uintptr_t fpage = f.address & ~static_cast<std::uintptr_t>(0xFFF);
        if (fpage != page) {
            if (page)
                VirtualProtect(reinterpret_cast<void*>(page), 0x1000, old, &old);
            page = fpage;
            VirtualProtect(reinterpret_cast<void*>(page), 0x1000, PAGE_EXECUTE_READWRITE, &old);
        }
        *reinterpret_cast<unsigned char*>(f.address) = kInt3;
    }
    if (page)
        VirtualProtect(reinterpret_cast<void*>(page), 0x1000, old, &old);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);

    log::Write("coverage: %zu of %zu listed functions instrumented (%zu skipped: patched or not executable)",
               g_planted.size(), g_listed, g_listed - g_planted.size());
    return g_planted.size();
}

Summary GetSummary()
{
    return {g_listed, g_planted.size(), g_hits.load()};
}

bool WriteHits(const std::wstring& path)
{
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file)
        return false;
    std::fprintf(file, "address,first_hit_ms\n");
    for (std::size_t i = 0; i < g_planted.size(); ++i) {
        const std::uint32_t ms = g_firstHitMs[i].load();
        if (ms < kRestoredByStop)
            std::fprintf(file, "0x%08X,%u\n", static_cast<unsigned>(g_planted[i].address), ms);
    }
    std::fclose(file);
    return true;
}

void Stop()
{
    if (!g_active.load())
        return;
    // Restore every unhit entry while the handler is still active: a thread
    // reaching one of them meanwhile is rewound until its byte is back.
    for (std::size_t i = 0; i < g_planted.size(); ++i) {
        std::uint32_t expected = kNotHit;
        if (g_firstHitMs[i].compare_exchange_strong(expected, kRestoredByStop))
            WriteCodeByte(g_planted[i].address, g_planted[i].firstByte);
    }
    g_active.store(false);
    if (g_handler)
        RemoveVectoredExceptionHandler(g_handler);
    g_handler = nullptr;
}

} // namespace opensc4::coverage
