// determinism.virtual_clock (P4 experiment): the game's main thread gets a
// virtual QueryPerformanceCounter that advances by a fixed step on every call,
// so how much simulation fits into a frame no longer depends on how fast the
// machine is. The simulator runs its ticks until a stopwatch built on QPC
// passes the frame's budget (0x00703E70); with real time, the number of ticks
// per frame differs from run to run (docs/experiments/determinism.md).
//
// Switched on at PostAppInit on the main thread: from then on that thread's
// counter starts at the real value and moves OPENSC4_VCLOCK_STEP_US
// microseconds (default 1000) per call, so stopwatches already running stay
// continuous. Other threads keep the real clock, so their timing cannot change
// the main thread's sequence. Only the exe's import is wrapped (iat.h): our
// own code, the drivers and Windows see real time.
#include "core/virtual_clock.h"

#include "core/iat.h"
#include "core/log.h"
#include "core/registry.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace opensc4::virtual_clock {
namespace {

using QpcFn = BOOL(WINAPI*)(LARGE_INTEGER*);
QpcFn g_realQpc = nullptr;

DWORD g_mainThread = 0;
int64_t g_virtual = 0; // main thread only
int64_t g_step = 0;    // counter ticks per call

// Calls per second on the main thread, logged for the first 30 s.
std::atomic<uint64_t> g_calls{0};
int64_t g_lastReport = 0;
int g_reports = 0;

// After ReturnToRealTime: real time plus this offset (main thread).
std::atomic<bool> g_real{false};
int64_t g_offset = 0;

BOOL WINAPI VirtualQpc(LARGE_INTEGER* count)
{
    if (GetCurrentThreadId() != g_mainThread)
        return g_realQpc(count);
    if (g_real) {
        const BOOL ok = g_realQpc(count);
        count->QuadPart += g_offset;
        return ok;
    }
    g_virtual += g_step;
    count->QuadPart = g_virtual;
    ++g_calls;
    // Report against real time (our own, unwrapped import).
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    if (g_lastReport == 0) {
        g_lastReport = now.QuadPart;
    } else if (g_reports < 3 && now.QuadPart - g_lastReport >= frequency.QuadPart * 10) {
        log::Write("virtual_clock: %llu main-thread QPC calls in 10 s",
                   static_cast<unsigned long long>(g_calls.exchange(0)));
        g_lastReport = now.QuadPart;
        ++g_reports;
    }
    return TRUE;
}

} // namespace

void ReturnToRealTime()
{
    if (!g_realQpc || g_real || GetCurrentThreadId() != g_mainThread)
        return;
    LARGE_INTEGER now;
    g_realQpc(&now);
    // Never backwards: continue from the virtual value.
    g_offset = g_virtual + g_step - now.QuadPart;
    g_real = true;
    log::Write("virtual_clock: main thread back on real time (offset %lld ticks)", static_cast<long long>(g_offset));
}

namespace {

bool Install(registry::Mode)
{
    LARGE_INTEGER frequency, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    const char* text = std::getenv("OPENSC4_VCLOCK_STEP_US");
    const int64_t stepUs = text && *text ? std::atoll(text) : 1000;
    g_step = frequency.QuadPart * stepUs / 1000000;
    g_virtual = now.QuadPart;
    g_mainThread = GetCurrentThreadId();
    if (!iat::Patch(nullptr, "KERNEL32.dll", "QueryPerformanceCounter", &VirtualQpc, &g_realQpc)) {
        log::Write("virtual_clock: the exe does not import QueryPerformanceCounter");
        return false;
    }
    log::Write("virtual_clock: main thread %lu, %lld us per call (%lld counter ticks)", g_mainThread,
               static_cast<long long>(stepUs), static_cast<long long>(g_step));
    return true;
}

const bool g_registered = registry::Register({
    "determinism.virtual_clock",
    "determinism",
    registry::Kind::Api,
    registry::Phase::PostAppInit,
    0,
    "The main thread's QueryPerformanceCounter advances a fixed step per call (P4 determinism experiment)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::virtual_clock
