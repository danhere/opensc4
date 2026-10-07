// determinism.fixed_ticks (P4): the simulator runs a fixed number of ticks
// per frame instead of as many as fit into a time budget.
//
// cSC4Simulator::RunTimeSlice (0x00703E70) runs ticks until the stopwatch at
// +0x190 passes a budget of 1000 / fps cap minus the time spent outside the
// simulation since the last slice (rendering, interface, messages), at least
// 15 ms. Even under the virtual clock that time is the number of clock reads
// made by frame-level code, which differs from run to run, so the number of
// ticks per frame, and with it the order of ticks and per-frame work, did too
// (docs/experiments/determinism.md). Here that stopwatch reports "not yet"
// for the first OPENSC4_FIXED_TICKS - 1 checks of a slice and "expired" on
// the next (default 60), without reading any clock.
//
// Inside the ticks, the simulation clock (0x00702A80: minutes at +0xB8,
// hours at +0xBC, then the day) holds a step back until its pacing stopwatch
// (+0x48 of the simulator, +0x40 of the slice's `this`) shows the minimum
// time per step (+0x80) has passed, while the other tick work goes on; so
// how much work a day got depended on elapsed time too. That stopwatch
// always reports enough time here: a step happens as soon as its work is
// done.
//
// The tick work includes the idle dispatcher (0x00702DF0), which gives each
// SimIdle agent an allowance of a third of the slice budget (+0x70 here,
// +0x78 of the simulator) and reads the slice stopwatch too. That budget was
// computed from the time outside the simulation (stopwatch +0x58), so it
// varied, and with it how much idle work, and therefore which tick ended a
// day. The +0x58 stopwatch reports 0 inside a slice: the budget is always
// 1000 / fps cap.
//
// OPENSC4_STOPWATCH_SURVEY=1 also logs every GZStopwatch the game reads, with
// its mode (0 = rdtsc, which the virtual clock cannot see) and first caller.
#include "core/fixed_ticks.h"

#include "core/address_check.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/virtual_clock.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <opensc4/addresses.h>

namespace opensc4::fixed_ticks {
namespace {

void* g_elapsed = nullptr;   // GZStopwatch::ElapsedMilliseconds (original)
void* g_sliceSw = nullptr;   // the running slice's stopwatch, or null
void* g_gateSw = nullptr;    // the simulation clock's pacing stopwatch while a slice runs
void* g_frameSw = nullptr;   // time since the last slice, read while a slice runs
uint32_t g_checks = 0;       // stopwatch checks in the running slice
uint32_t g_ticksPerSlice = 60;
int g_depth = 0;
bool g_survey = false;
bool g_active = false;
uint64_t g_totalChecks = 0; // slice stopwatch checks over all slices

// Survey: each stopwatch once (main thread or not; a race only risks a
// duplicate line).
constexpr int kSeen = 256;
void* g_seen[kSeen];
int g_seenCount = 0;

void __cdecl SurveyStopwatch(int* stopwatch, void* caller)
{
    for (int i = 0; i < g_seenCount; ++i)
        if (g_seen[i] == stopwatch)
            return;
    if (g_seenCount == kSeen)
        return;
    g_seen[g_seenCount++] = stopwatch;
    log::Write("stopwatch: %p mode %d, first read from %p (thread %lu)%s", static_cast<void*>(stopwatch), stopwatch[0],
               caller, GetCurrentThreadId(), stopwatch[0] == 0 ? " RDTSC" : "");
}

// Slice stopwatch checks go into the clock trace (virtual_clock.h), so
// traces show where each tick and idle dispatch ends.
void __cdecl TraceSliceCheck(const void* stack)
{
    virtual_clock::TraceEvent('S', stack);
}

// Leaves registers as the original would for every stopwatch but the slice's
// (callers may rely on more than the calling convention promises; see
// rng_trace.cpp). For the slice's: EAX = 0 ("not yet") or 0xFFFFFFFF; for
// the pacing stopwatch always 0xFFFFFFFF; for the time since the last slice 0.
__declspec(naked) void ElapsedThunk()
{
    __asm {
        cmp ecx, dword ptr [g_sliceSw]
        je fixed
        cmp ecx, dword ptr [g_gateSw]
        je expired
        cmp ecx, dword ptr [g_frameSw]
        je zero
        cmp byte ptr [g_survey], 0
        je pass
        push eax
        push ecx
        push edx
        push dword ptr [esp + 12]
        push ecx
        call SurveyStopwatch
        add esp, 8
        pop edx
        pop ecx
        pop eax
    pass:
        jmp dword ptr [g_elapsed]
    fixed:
        push ecx
        push edx
        lea eax, [esp + 8]
        push eax
        call TraceSliceCheck
        add esp, 4
        pop edx
        pop ecx
        inc dword ptr [g_checks]
        mov eax, dword ptr [g_checks]
        cmp eax, dword ptr [g_ticksPerSlice]
        jae expired
    zero:
        xor eax, eax
        ret
    expired:
        or eax, 0xFFFFFFFF
        ret
    }
}

using SliceFn = bool(__fastcall*)(void* self, void* edx, uint32_t arg);
SliceFn g_slice = nullptr;

bool __fastcall Slice(void* self, void* edx, uint32_t arg)
{
    if (g_depth++ == 0) {
        g_sliceSw = static_cast<char*>(self) + 0x190;
        g_gateSw = static_cast<char*>(self) + 0x40;
        g_frameSw = static_cast<char*>(self) + 0x58;
        g_checks = 0;
    }
    const bool result = g_slice(self, edx, arg);
    if (--g_depth == 0) {
        g_sliceSw = nullptr;
        g_gateSw = nullptr;
        g_frameSw = nullptr;
        g_totalChecks += g_checks;
    }
    return result;
}

bool Install(registry::Mode)
{
    if (const char* n = std::getenv("OPENSC4_FIXED_TICKS"); n && std::atoi(n) > 0)
        g_ticksPerSlice = static_cast<uint32_t>(std::atoi(n));
    const char* survey = std::getenv("OPENSC4_STOPWATCH_SURVEY");
    g_survey = survey && survey[0] == '1';
    const addr::Entry* slice = addr_check::Find(addr::cSC4Simulator_RunTimeSlice);
    const addr::Entry* elapsed = addr_check::Find(addr::GZStopwatch_ElapsedMilliseconds);
    const bool ok = slice && elapsed && hook::Install(*elapsed, reinterpret_cast<void*>(&ElapsedThunk), &g_elapsed) &&
                    hook::Install(*slice, &Slice, &g_slice);
    log::Write("fixed_ticks: %u tick(s) per frame%s, %s", g_ticksPerSlice, g_survey ? ", stopwatch survey" : "",
               ok ? "installed" : "FAILED");
    g_active = ok;
    return ok;
}

const bool g_registered = registry::Register({
    "determinism.fixed_ticks",
    "determinism",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Simulator_RunTimeSlice,
    "The simulator runs a fixed number of ticks per frame instead of filling a time budget",
    false,
    Install,
});

} // namespace

bool InSlice()
{
    return g_depth > 0;
}

void Describe(char* out, std::size_t size)
{
    if (!size)
        return;
    out[0] = '\0';
    if (g_active)
        std::snprintf(out, size, " | checks %llu", static_cast<unsigned long long>(g_totalChecks + g_checks));
}

} // namespace opensc4::fixed_ticks
