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
// continuous. Every other thread gets its own virtual counter too, from its
// first call: the city-load worker (0x0048E460) takes jobs under a stopwatch
// budget, and with real time on that thread how much it had done when the
// main thread used its results varied (one run in four differed by a tick on
// Rush Hour; docs/experiments/determinism.md). OPENSC4_VCLOCK_ALL_THREADS=0
// leaves other threads on real time. Only the exe's import is wrapped
// (iat.h): our own code, the drivers and Windows see real time.
#include "core/virtual_clock.h"

#include "core/fixed_ticks.h"
#include "core/iat.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/settings.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
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

// [determinism] pace = realtime (play): the main thread is virtual only
// inside simulation slices; rendering, the interface and saving see real
// time, so animation and scrolling run at their normal speed.
bool g_slicesOnly = false;

// After ReturnToRealTime: real time plus this offset (main thread).
std::atomic<bool> g_real{false};
int64_t g_offset = 0;

// Every other thread gets its own virtual counter too, starting from the
// real value on its first call (OPENSC4_VCLOCK_ALL_THREADS=0: real time).
bool g_allThreads = true;
thread_local int64_t t_virtual = 0;

// OPENSC4_VCLOCK_CALLERS=<file>: main-thread calls counted per caller (the
// return address and the next two code addresses up the stack) and written
// per simulation day, to find code whose number of clock reads differs from
// run to run: through it, real time leaks into virtual time.
struct CallerCount {
    uint32_t key[3];
    uint32_t count;
};
constexpr uint32_t kCallerSlots = 8192;
CallerCount g_callerCounts[kCallerSlots];
std::FILE* g_callersFile = nullptr;

void CountCaller(void** retSlot)
{
    uint32_t key[3] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*retSlot)), 0, 0};
    // Skip the argument; look for the next two code addresses.
    auto* words = reinterpret_cast<uint32_t*>(retSlot + 2);
    for (int i = 0, found = 1; i < 48 && found < 3; ++i)
        if (words[i] >= 0x00401000 && words[i] < 0x00A80000)
            key[found++] = words[i];
    uint32_t h = (key[0] * 2654435761u) ^ (key[1] * 40503u) ^ key[2];
    for (uint32_t probe = 0; probe < kCallerSlots; ++probe) {
        CallerCount& slot = g_callerCounts[(h + probe) % kCallerSlots];
        if (slot.count == 0 || (slot.key[0] == key[0] && slot.key[1] == key[1] && slot.key[2] == key[2])) {
            slot.key[0] = key[0];
            slot.key[1] = key[1];
            slot.key[2] = key[2];
            ++slot.count;
            return;
        }
    }
}

// OPENSC4_VCLOCK_TRACE=<file>: every main-thread virtual read in order, one
// line each ("Q" for QueryPerformanceCounter, "T" for a trapped rdtsc, then
// the first three code addresses up the stack), with a line per simulation
// day. Under the determinism replacements two runs read the clock in the same
// order for as long as they take the same path, so the first differing line
// shows where control flow parts, even in code that draws no random numbers.
// Needs determinism.fixed_ticks, which says when a simulation slice runs.
std::FILE* g_traceFile = nullptr;

void TraceRead(char kind, const uint32_t* stack)
{
    // Only reads inside simulation time slices: frame-level code (rendering,
    // the interface) reads the clock in an order that varies between runs.
    if (!fixed_ticks::InSlice())
        return;
    uint32_t found[3] = {};
    for (int i = 0, n = 0; i < 64 && n < 3; ++i)
        if (stack[i] >= 0x00401000 && stack[i] < 0x00A80000)
            found[n++] = stack[i];
    std::fprintf(g_traceFile, "%c %08X %08X %08X\n", kind, found[0], found[1], found[2]);
}

BOOL WINAPI VirtualQpc(LARGE_INTEGER* count)
{
    if (GetCurrentThreadId() != g_mainThread) {
        if (!g_allThreads)
            return g_realQpc(count);
        if (t_virtual == 0) {
            LARGE_INTEGER now;
            g_realQpc(&now);
            t_virtual = now.QuadPart;
        }
        t_virtual += g_step;
        count->QuadPart = t_virtual;
        return TRUE;
    }
    if (g_real) {
        const BOOL ok = g_realQpc(count);
        count->QuadPart += g_offset;
        return ok;
    }
    if (g_slicesOnly && !fixed_ticks::InSlice())
        return g_realQpc(count);
    g_virtual += g_step;
    count->QuadPart = g_virtual;
    ++g_calls;
    if (g_callersFile)
        CountCaller(static_cast<void**>(_AddressOfReturnAddress()));
    if (g_traceFile)
        TraceRead('Q', static_cast<const uint32_t*>(_AddressOfReturnAddress()));
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

// Simulation code also reads the CPU's time stamp counter directly, where no
// import hook sees it: deadline helpers that bound how much work a call may
// do (0x0068BF50 sets now + ms, 0x0068BF80 checks it; about 25 simulator
// functions), a cycles-per-millisecond estimate made at every time slice and
// used to turn those milliseconds into cycles (0x005D9370), and two more.
// Each of these rdtsc instructions (0F 31) becomes int3; nop, and the
// breakpoint gets a counter derived from the virtual clock instead: the main
// thread's virtual counter advances kTscReadStepUs per read and is scaled to
// kTscHz, so cycles per millisecond come out constant and a deadline of n ms
// passes after a fixed number of checks. Other threads get the real counter.
struct RdtscSite {
    uintptr_t address;
    const char* where;
};
constexpr RdtscSite kRdtscSites[] = {
    {0x005D9394, "0x005D9370 cycles-per-millisecond estimate"},
    {0x0068BF55, "0x0068BF50 deadline = now + ms"},
    {0x0068BF81, "0x0068BF80 deadline passed?"},
    {0x0070E2B4, "0x0070E2B0"},
    {0x0072081B, "0x00720430"},
    {0x00720A76, "0x00720430"},
    {0x00720ADB, "0x00720430"},
};
constexpr int64_t kTscHz = 3000000000;
constexpr int64_t kTscReadStepUs = 100;
int64_t g_tscStep = 0;      // counter ticks per read
int64_t g_qpcFrequency = 1;
std::atomic<uint32_t> g_tscReads{0};
bool g_sitesPatched = false; // the game's rdtsc sites read the virtual counter

uint64_t VirtualTsc()
{
    int64_t counter;
    if (g_real) {
        LARGE_INTEGER now;
        g_realQpc(&now);
        counter = now.QuadPart + g_offset;
    } else {
        g_virtual += g_tscStep;
        counter = g_virtual;
    }
    return static_cast<uint64_t>(counter) * static_cast<uint64_t>(kTscHz / g_qpcFrequency);
}

LONG CALLBACK RdtscTrap(EXCEPTION_POINTERS* info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT)
        return EXCEPTION_CONTINUE_SEARCH;
    const auto address = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    for (const RdtscSite& site : kRdtscSites) {
        if (site.address != address)
            continue;
        const bool main = GetCurrentThreadId() == g_mainThread && !(g_slicesOnly && !fixed_ticks::InSlice());
        const uint64_t value = main ? VirtualTsc() : __rdtsc();
        if (main && g_traceFile && !g_real)
            TraceRead('T', reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(info->ContextRecord->Esp)));
        g_tscReads.fetch_add(1, std::memory_order_relaxed);
        info->ContextRecord->Eax = static_cast<DWORD>(value);
        info->ContextRecord->Edx = static_cast<DWORD>(value >> 32);
        info->ContextRecord->Eip = static_cast<DWORD>(address + 2);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Wall-clock time: the exe's time() and clock() read GetSystemTimeAsFileTime,
// and some simulation code seeds generators from time() itself (0x005CED80;
// the traffic simulator's Init and the prop developer did the same), so
// fixed_seed's replacement of clock seeds does not reach them. Every call
// returns the same instant (2026-01-01 00:00:00 UTC by default,
// OPENSC4_VCLOCK_WALL = a FILETIME value to change it). It cannot advance
// with the virtual counter, whose value at the end of loading differs from
// run to run; nothing in the game was found waiting on wall time.
uint64_t g_wallTime = 134116992000000000ull; // 2026-01-01 00:00:00 UTC
using FileTimeFn = void(WINAPI*)(LPFILETIME);
FileTimeFn g_realFileTime = nullptr;
using LocalTimeFn = void(WINAPI*)(LPSYSTEMTIME);
LocalTimeFn g_realLocalTime = nullptr;

void WINAPI FrozenSystemTimeAsFileTime(LPFILETIME out)
{
    out->dwLowDateTime = static_cast<DWORD>(g_wallTime);
    out->dwHighDateTime = static_cast<DWORD>(g_wallTime >> 32);
}

void WINAPI FrozenLocalTime(LPSYSTEMTIME out)
{
    FILETIME ft{static_cast<DWORD>(g_wallTime), static_cast<DWORD>(g_wallTime >> 32)};
    FileTimeToSystemTime(&ft, out); // the instant as UTC, whatever the machine's time zone
}

bool PatchRdtscSites()
{
    if (!AddVectoredExceptionHandler(1, &RdtscTrap))
        return false;
    int patched = 0;
    for (const RdtscSite& site : kRdtscSites) {
        auto* code = reinterpret_cast<unsigned char*>(site.address);
        DWORD old = 0;
        if (code[0] != 0x0F || code[1] != 0x31 || !VirtualProtect(code, 2, PAGE_EXECUTE_READWRITE, &old)) {
            log::Write("virtual_clock: no rdtsc at %p (%s): %02X %02X; left alone", static_cast<void*>(code), site.where,
                       code[0], code[1]);
            continue;
        }
        code[0] = 0xCC; // int3
        code[1] = 0x90; // nop
        VirtualProtect(code, 2, old, &old);
        FlushInstructionCache(GetCurrentProcess(), code, 2);
        ++patched;
    }
    log::Write("virtual_clock: %d of %d rdtsc sites read the virtual counter", patched,
               static_cast<int>(sizeof kRdtscSites / sizeof kRdtscSites[0]));
    g_sitesPatched = patched > 0;
    return patched > 0;
}

} // namespace

uint64_t ReadTsc(const void* stack)
{
    if (!g_sitesPatched)
        return __rdtsc();
    const bool main = GetCurrentThreadId() == g_mainThread && !(g_slicesOnly && !fixed_ticks::InSlice());
    const uint64_t value = main ? VirtualTsc() : __rdtsc();
    if (main && g_traceFile && !g_real)
        TraceRead('T', static_cast<const uint32_t*>(stack));
    g_tscReads.fetch_add(1, std::memory_order_relaxed);
    return value;
}

void OnNewDay(int day)
{
    if (g_traceFile && GetCurrentThreadId() == g_mainThread) {
        std::fprintf(g_traceFile, "day %d\n", day);
        std::fflush(g_traceFile);
    }
    if (!g_callersFile || GetCurrentThreadId() != g_mainThread)
        return;
    std::fprintf(g_callersFile, "day %d virtual %lld\n", day, static_cast<long long>(g_virtual));
    for (CallerCount& slot : g_callerCounts) {
        if (slot.count)
            std::fprintf(g_callersFile, "%08X %08X %08X %u\n", slot.key[0], slot.key[1], slot.key[2], slot.count);
        slot = CallerCount{};
    }
    std::fflush(g_callersFile);
}

void TraceEvent(char kind, const void* stack)
{
    if (g_traceFile && GetCurrentThreadId() == g_mainThread && !g_real)
        TraceRead(kind, static_cast<const uint32_t*>(stack));
}

void OnSliceStart()
{
    if (!g_slicesOnly || g_real || !g_realQpc || GetCurrentThreadId() != g_mainThread)
        return;
    // Inside slices the clock continues from the time outside: a stopwatch the
    // interface starts and the simulation reads would otherwise compare two
    // clocks and never expire.
    LARGE_INTEGER now;
    g_realQpc(&now);
    if (now.QuadPart > g_virtual)
        g_virtual = now.QuadPart;
}

void ReturnToRealTime()
{
    if (!g_realQpc || g_real || GetCurrentThreadId() != g_mainThread)
        return;
    LARGE_INTEGER now;
    g_realQpc(&now);
    // Never backwards: continue from the virtual value.
    g_offset = g_virtual + g_step - now.QuadPart;
    g_real = true;
    log::Write("virtual_clock: main thread back on real time (offset %lld ticks; %u rdtsc reads trapped)",
               static_cast<long long>(g_offset), g_tscReads.load());
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
    g_slicesOnly = settings::Get("determinism", "pace", "off") == "realtime";
    const char* all = std::getenv("OPENSC4_VCLOCK_ALL_THREADS");
    g_allThreads = !(all && all[0] == '0');
    const char* callers = std::getenv("OPENSC4_VCLOCK_CALLERS");
    if (callers && *callers && fopen_s(&g_callersFile, callers, "w") != 0)
        g_callersFile = nullptr;
    const char* trace = std::getenv("OPENSC4_VCLOCK_TRACE");
    if (trace && *trace && fopen_s(&g_traceFile, trace, "w") != 0)
        g_traceFile = nullptr;
    if (g_traceFile)
        std::setvbuf(g_traceFile, nullptr, _IOFBF, 1 << 20);
    if (!iat::Patch(nullptr, "KERNEL32.dll", "QueryPerformanceCounter", &VirtualQpc, &g_realQpc)) {
        log::Write("virtual_clock: the exe does not import QueryPerformanceCounter");
        return false;
    }
    g_qpcFrequency = frequency.QuadPart;
    g_tscStep = frequency.QuadPart * kTscReadStepUs / 1000000;
    const char* tsc = std::getenv("OPENSC4_VCLOCK_TSC");
    if (!(tsc && tsc[0] == '0'))
        PatchRdtscSites();
    if (const char* wall = std::getenv("OPENSC4_VCLOCK_WALL"); wall && *wall)
        g_wallTime = std::strtoull(wall, nullptr, 0);
    const bool fileTime = iat::Patch(nullptr, "KERNEL32.dll", "GetSystemTimeAsFileTime", &FrozenSystemTimeAsFileTime,
                                     &g_realFileTime);
    const bool localTime = iat::Patch(nullptr, "KERNEL32.dll", "GetLocalTime", &FrozenLocalTime, &g_realLocalTime);
    // clock() (0x009F361F) is now minus the start time the C runtime noted at
    // startup in real time (64 bits at 0x00BAD6B0, set by __inittime
    // 0x009F3662). Moving the start to the frozen instant makes clock() 0.
    auto* clockStart = reinterpret_cast<volatile uint64_t*>(0x00BAD6B0);
    FILETIME realNow;
    GetSystemTimeAsFileTime(&realNow);
    const uint64_t realWall = (static_cast<uint64_t>(realNow.dwHighDateTime) << 32) | realNow.dwLowDateTime;
    const bool plausible = *clockStart <= realWall && realWall - *clockStart < 36000000000ull; // within 1 h
    if (plausible)
        *clockStart = g_wallTime;
    log::Write("virtual_clock: wall clock frozen at FILETIME %llu (GetSystemTimeAsFileTime %s, GetLocalTime %s, "
               "clock() start %s)",
               static_cast<unsigned long long>(g_wallTime), fileTime ? "ok" : "not imported",
               localTime ? "ok" : "not imported", plausible ? "moved" : "NOT FOUND, left alone");
    log::Write("virtual_clock: main thread %lu%s%s, %lld us per call (%lld counter ticks)", g_mainThread,
               g_slicesOnly ? " (inside simulation slices only)" : "", g_allThreads ? " and all other threads" : "",
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
