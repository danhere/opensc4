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
// Real-time pacing ([determinism] pace = realtime, for play; docs/seeds.md):
// with the stopwatches fixed the simulation runs as fast as it can, whatever
// the speed setting. Pacing holds whole slices back instead: a slice runs
// only while the game clock is behind a real-time schedule for the current
// speed. What a slice does stays the same; only when it runs follows real
// time, and the frames in between do no simulation work.
//
// OPENSC4_STOPWATCH_SURVEY=1 also logs every GZStopwatch the game reads, with
// its mode (0 = rdtsc, which the virtual clock cannot see) and first caller.
#include "core/fixed_ticks.h"

#include "core/address_check.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/settings.h"
#include "core/virtual_clock.h"

#include <GZServPtrs.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4Simulator.h>

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

// Pacing. The game clock has no finer unit the simulator keeps than the day
// (GetSimTime stays 0), so slices are spread over each day by the number the
// previous day took: the allowance grows at slices-per-day / ms-per-day and
// every slice run costs one.
bool g_pace = false;
double g_paceScale = 1.0;      // OPENSC4_PACE_SCALE: faster (>1) or slower, for tests
double g_allowance = 0.0;      // slices
int32_t g_paceDay = -1;        // game day of the last decision
uint32_t g_dayslices = 0;      // slices run so far today
double g_slicesPerDay = 30.0;  // what the previous day took
ULONGLONG g_lastReal = 0;      // GetTickCount64 at the last decision
uint64_t g_slicesRun = 0, g_slicesHeld = 0;

// Real milliseconds per game day by speed (GetSimSpeed 0 to 2), close to the
// unmodified game's on a small city: turtle 2.1 s per day, cheetah as fast as
// the machine allowed (about 0.3 s).
double MsPerDay(int32_t speed)
{
    return (speed <= 0 ? 2000.0 : speed == 1 ? 1000.0 : 300.0) / g_paceScale;
}

// Whether the slice about to start should run (main thread).
bool PaceAllows()
{
    cISC4AppPtr app;
    cISC4City* city = app ? app->GetCity() : nullptr;
    cISC4Simulator* sim = city ? city->GetSimulator() : nullptr;
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG elapsed = g_lastReal ? now - g_lastReal : 0;
    g_lastReal = now;
    if (!sim || sim->IsAnyPaused())
        return true; // loading or paused: slices advance no game time
    const int32_t day = sim->GetSimDateNumber();
    if (day != g_paceDay) {
        if (g_paceDay >= 0 && g_dayslices > 0)
            g_slicesPerDay = g_dayslices;
        g_paceDay = day;
        g_dayslices = 0;
    }
    const int32_t speed = sim->GetSimSpeed();
    static int32_t lastSpeed = -1;
    if (speed != lastSpeed) {
        log::Write("fixed_ticks: pacing at speed %d, %.0f ms per game day", speed, MsPerDay(speed));
        lastSpeed = speed;
    }
    const double rate = g_slicesPerDay / MsPerDay(speed);
    g_allowance += static_cast<double>(elapsed < 250 ? elapsed : 250) * rate; // no catching up after a stall
    if (g_allowance > 2.0)
        g_allowance = 2.0;
    if (g_allowance < 1.0)
        return false;
    g_allowance -= 1.0;
    ++g_dayslices;
    return true;
}

// The cycle clock's estimate made at every slice (0x005D9370: milliseconds per
// cycle, a moving average over counter deltas, in floats) drifts towards the
// virtual clock's 3 GHz along a path that depends on how many clock reads came
// between two slices, which pacing changes (paused slices, held slices); one
// deadline check more or less then changes the idle work. Paced, it is the
// value it converges to.
using CalibrateFn = void(__fastcall*)(float* self, void* edx);
CalibrateFn g_calibrate = nullptr;
float g_cyclesPerMs = 0.0f; // replacement value; 0: log only (OPENSC4_LOG_CYCLES=1)
uint32_t g_calibrations = 0;
float g_lastLogged = -1.0f;
int g_changesLogged = 0;

void __fastcall Calibrate(float* self, void* edx)
{
    g_calibrate(self, edx);
    ++g_calibrations;
    if (*self != g_lastLogged && g_changesLogged < 40) {
        log::Write("fixed_ticks: cycles estimate %.9g (call %u)%s", static_cast<double>(*self), g_calibrations,
                   g_cyclesPerMs > 0.0f ? ", replaced" : "");
        g_lastLogged = *self;
        ++g_changesLogged;
    }
    if (g_cyclesPerMs > 0.0f)
        *self = g_cyclesPerMs;
}

bool __fastcall Slice(void* self, void* edx, uint32_t arg)
{
    if (g_depth == 0 && g_pace) {
        if (!PaceAllows()) {
            ++g_slicesHeld;
            return true;
        }
        ++g_slicesRun;
        virtual_clock::OnSliceStart();
    }
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
    g_pace = settings::Get("determinism", "pace", "off") == "realtime";
    if (const char* scale = std::getenv("OPENSC4_PACE_SCALE"); scale && std::atof(scale) > 0)
        g_paceScale = std::atof(scale);
    if (g_pace)
        g_cyclesPerMs = 1.0f / 3000000.0f; // virtual_clock's kTscHz, per millisecond
    const addr::Entry* calibrate = addr_check::Find(addr::SC4CycleClock_Calibrate);
    if (calibrate && (g_pace || std::getenv("OPENSC4_LOG_CYCLES")))
        hook::Install(*calibrate, &Calibrate, &g_calibrate);
    const addr::Entry* slice = addr_check::Find(addr::cSC4Simulator_RunTimeSlice);
    const addr::Entry* elapsed = addr_check::Find(addr::GZStopwatch_ElapsedMilliseconds);
    const bool ok = slice && elapsed && hook::Install(*elapsed, reinterpret_cast<void*>(&ElapsedThunk), &g_elapsed) &&
                    hook::Install(*slice, &Slice, &g_slice);
    log::Write("fixed_ticks: %u tick(s) per frame%s%s, %s", g_ticksPerSlice, g_survey ? ", stopwatch survey" : "",
               g_pace ? ", paced to real time" : "", ok ? "installed" : "FAILED");
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

bool Paced()
{
    return g_pace;
}

void LogPace()
{
    if (g_pace)
        log::Write("fixed_ticks: pacing ran %llu slices and held back %llu",
                   static_cast<unsigned long long>(g_slicesRun), static_cast<unsigned long long>(g_slicesHeld));
}

} // namespace opensc4::fixed_ticks
