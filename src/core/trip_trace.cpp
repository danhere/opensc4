// diagnostics.trace.trips (P4): logs every trip source the traffic
// simulator's path work takes from its queue (0x007107F0, called by the trip
// loops 0x0071D020 and 0x0071D440): the cell coordinates and the source
// object's vtable, one line each, to OPENSC4_TRIP_TRACE (a file). Comparing
// two runs shows whether they process the same sources in the same order
// (docs/experiments/determinism.md, "Heap layout").
#include "core/address_check.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include <opensc4/addresses.h>

namespace opensc4::trip_trace {
namespace {

void* g_next = nullptr;
std::FILE* g_file = nullptr;
uint32_t g_count = 0;

void __cdecl LogSource(uint32_t result, void** object, const int16_t* x, const int16_t* z)
{
    if (!(result & 0xFF))
        return;
    const void* source = object ? *object : nullptr;
    const uint32_t vtable = source ? *static_cast<const uint32_t*>(source) : 0;
    std::fprintf(g_file, "%u %d %d %08X\n", g_count++, x ? *x : -1, z ? *z : -1, vtable);
}

// thiscall, three stack arguments (RET 0xC): re-pushes them for the original,
// then logs, leaving EAX, ECX and EDX as the original left them.
__declspec(naked) void NextSourceThunk()
{
    __asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        call dword ptr [g_next]
        push eax
        push ecx
        push edx
        push dword ptr [esp + 24]
        push dword ptr [esp + 24]
        push dword ptr [esp + 24]
        push eax
        call LogSource
        add esp, 16
        pop edx
        pop ecx
        pop eax
        ret 12
    }
}

// OPENSC4_PATH_TRACE=<file>: one line per pathfinder call (0x006D91B0): the
// fields its early exits depend on, on entry (+0x66 start count, +0x70
// flags, +0x80 mode, +0x84 network mask, +0xC4 resume flag, +0x120 open-set
// size) and the result, to find the first search two runs decide
// differently.
void* g_search = nullptr;
std::FILE* g_pathFile = nullptr;
uint32_t g_searches = 0;

void Fields(const unsigned char* q, char* out, size_t size)
{
    std::snprintf(out, size, "%04X %08X %08X %02X %02X %08X", *reinterpret_cast<const uint16_t*>(q + 0x66),
                  *reinterpret_cast<const uint32_t*>(q + 0x70), *reinterpret_cast<const uint32_t*>(q + 0x80), q[0x84],
                  q[0xC4], *reinterpret_cast<const uint32_t*>(q + 0x120));
}

char g_entry[96];

void __cdecl SearchEntry(const unsigned char* query)
{
    Fields(query, g_entry, sizeof g_entry);
}

void __cdecl SearchExit(uint32_t result, const unsigned char* query)
{
    char after[96];
    Fields(query, after, sizeof after);
    std::fprintf(g_pathFile, "%u in %s -> %u out %s\n", g_searches++, g_entry, result, after);
}

// fastcall, no stack arguments: logs on entry, calls the original, logs the
// result, and returns with EAX, ECX and EDX as the original left them.
__declspec(naked) void SearchThunk()
{
    __asm {
        push ecx
        push ecx
        call SearchEntry
        add esp, 4
        mov ecx, dword ptr [esp]
        call dword ptr [g_search]
        push eax
        push ecx
        push edx
        push dword ptr [esp + 12]
        push eax
        call SearchExit
        add esp, 8
        pop edx
        pop ecx
        pop eax
        add esp, 4
        ret
    }
}

bool Install(registry::Mode)
{
    char pathTrace[MAX_PATH];
    const DWORD pathLength = GetEnvironmentVariableA("OPENSC4_PATH_TRACE", pathTrace, sizeof(pathTrace));
    if (pathLength > 0 && pathLength < sizeof(pathTrace) && fopen_s(&g_pathFile, pathTrace, "w") == 0) {
        const addr::Entry* search = addr_check::Find(addr::TrafficPathfinder_Search);
        if (search && hook::Install(*search, reinterpret_cast<void*>(&SearchThunk), &g_search))
            log::Write("trips: dumping pathfinder queries");
    }
    char path[MAX_PATH];
    const DWORD length = GetEnvironmentVariableA("OPENSC4_TRIP_TRACE", path, sizeof(path));
    if (length == 0 || length >= sizeof(path) || fopen_s(&g_file, path, "w") != 0) {
        log::Write("trips: OPENSC4_TRIP_TRACE not set or not writable");
        return false;
    }
    std::setvbuf(g_file, nullptr, _IOFBF, 1 << 16);
    const addr::Entry* entry = addr_check::Find(addr::cSC4TrafficSimulator_NextTripSource);
    const bool ok = entry && hook::Install(*entry, reinterpret_cast<void*>(&NextSourceThunk), &g_next);
    log::Write("trips: tracing trip sources %s", ok ? "on" : "FAILED");
    return ok;
}

const bool g_registered = registry::Register({
    "diagnostics.trace.trips",
    "diagnostics",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_NextTripSource,
    "Logs the order in which the traffic simulator takes trip sources from its queue",
    false,
    Install,
});

} // namespace
} // namespace opensc4::trip_trace
