// determinism.sim_timers: the stopwatches the MySim agents, the advisor
// system and the network construction crews read run on simulation time
// (docs/experiments/determinism.md, "Simulation timers"; docs/netlot.md,
// "The crew's clocks and seeds").
//
// An active MySim (cSC4MySim, cSC4MyStreetSim) acts on real-time timers: its
// message handler does its work only when a GZStopwatch shows more than a
// couple of milliseconds since the last time, its advice balloons expire by
// another (0x00552BD0, every 500 ms), and the advisor system runs every
// advisor (a MySim is one) when a third shows 500 ms. Under the virtual
// clock those stopwatches read a counter that advances with every clock read
// anywhere, so how often a MySim acted depended on frame-level work and two
// runs of the same save parted on the first day.
//
// Here a stopwatch read by that code (the caller's return address in the
// MySim, dispatch, street-sim or advisor-system code, or in the balloon
// timer) is taken over: from then on, every one of its clock reads returns a
// counter that advances 10 ms per simulation tick (determinism.fixed_ticks'
// slice checks) and nothing else. It is rebased when taken over if it was
// running. The counter is read through the virtual clock (virtual_clock.h),
// from thunks on the stopwatch methods that read the clock.
//
// The network construction crews (cSC4NetworkConstructionCrew) are timed
// the same way, call by call: a piece is done when its job's stopwatch shows
// the piece's time (twice that while a worker vehicle is still on its way),
// and the workers move by the frame message's time. Both followed clock
// reads, so the day a road became usable did too. Their stopwatches go on
// the counter for every call from the crew's code, and the frame time the
// crew's Tick gets is the simulation time of the last frame
// (CrewFrameMicroseconds). Our translation of the crew (crew_work.cpp)
// calls the stopwatch from core.dll, outside that range: it goes through
// CrewStopwatchStart, CrewStopwatchElapsed and CrewStopwatchStop, which serve
// it as the range's calls are served. When our Tick is at 0x00607C30
// (simulators.crew.running, crew_sim::TickIsOurs) the hook here stays off
// that entry and our Tick asks CrewFrameMicroseconds itself.
#include "core/address_check.h"
#include "core/crew_work.h"
#include "core/fixed_ticks.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/sim_timers.h"
#include "core/virtual_clock.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>

namespace opensc4::sim_timers {
namespace {

// Code whose stopwatches run on simulation time.
constexpr struct {
    uint32_t begin;
    uint32_t end;
} kOwners[] = {
    {0x00424B00, 0x0042E300}, // cSC4MySim
    {0x00433000, 0x00434800}, // cSC4MySimDispatch
    {0x0043A050, 0x0043EBA0}, // the MySim tools (0x0043A050 to 0x0043D0F0) and cSC4MyStreetSim with its tool (0x0043D0F0 to 0x0043EB90)
    {0x00552BD0, 0x00552D00}, // the MySim balloon timer
    {0x0041A990, 0x0041B240}, // cSC4AdvisorSystem: runs its advisors (MySims among them) every 500 ms
};

// Code whose stopwatch calls are on simulation time call by call, without
// taking the stopwatch over: cSC4NetworkConstructionCrew (0x00605490 to
// 0x00608C4B). Its stopwatches (+0x20 of each 0x80-byte job, mode 4, in the
// vector at +0x48) are started (0x00607B4A, 0x00608BF7), read (0x00607CCB)
// and stopped (0x00606855) by the crew's own code only, and their memory is
// freed with the crew: the stopwatch's destructor (0x00A6D837) is a bare
// ret, so a taken-over address would stay in g_owned and put whatever
// stopwatch is made there later on simulation time, and a long game would
// fill the table.
constexpr struct {
    uint32_t begin;
    uint32_t end;
} kCallOwners[] = {
    {0x00605490, 0x00608C50}, // cSC4NetworkConstructionCrew
};

constexpr int kMaxOwned = 256;
void* g_owned[kMaxOwned];
int g_ownedCount = 0;
DWORD g_mainThread = 0;
bool g_read = false; // the flag virtual_clock checks (through SetSimTimerRead)
bool g_byCall = false; // the running read is a call-by-call owner's
bool g_elapsedServed = false; // fixed_ticks passes ElapsedMilliseconds' other stopwatches to ThunkElapsed
int64_t g_byCallLast = 0;

int64_t Counter()
{
    const int64_t perTick = virtual_clock::QpcFrequency() / 100; // 10 ms
    int64_t value = virtual_clock::QpcFrequency() * 1000 + static_cast<int64_t>(fixed_ticks::Checks()) * perTick;
    // Between slices Checks() still counts the last slice's checks, and a
    // slice that makes none (the simulator paused) takes them back; a crew
    // reads between slices (its Tick), so for the crews the counter never
    // goes back.
    if (g_byCall) {
        if (value < g_byCallLast)
            value = g_byCallLast;
        g_byCallLast = value;
    }
    return value;
}

bool Owner(uint32_t address)
{
    for (const auto& range : kOwners)
        if (address >= range.begin && address < range.end)
            return true;
    return false;
}

bool CallOwner(uint32_t address)
{
    for (const auto& range : kCallOwners)
        if (address >= range.begin && address < range.end)
            return true;
    return false;
}

// Before a stopwatch method reads the clock: whether this stopwatch is ours
// (taking it over when the caller is owning code), and if so the clock reads
// that follow return the simulation counter.
void* g_Elapsed = nullptr;

// Whether a stopwatch is ours, taking it over if `claim` (the caller is
// owning code) and it is not yet.
bool Take(void* stopwatch, bool claim, uint32_t caller)
{
    for (int i = 0; i < g_ownedCount; ++i)
        if (g_owned[i] == stopwatch)
            return true;
    if (!claim)
        return false;
    if (g_ownedCount == kMaxOwned) {
        static bool logged = false;
        if (!logged)
            log::Write("sim_timers: more than %d stopwatches; %p stays on the clock", kMaxOwned, stopwatch);
        logged = true;
        return false;
    }
    g_owned[g_ownedCount++] = stopwatch;
    auto* const start = reinterpret_cast<int64_t*>(static_cast<char*>(stopwatch) + 8);
    if (*start != 0)
        *start = Counter();
    if (g_ownedCount <= 16)
        log::Write("sim_timers: stopwatch %p (mode %d) on simulation time, first read from %08X", stopwatch,
                   *static_cast<int*>(stopwatch), caller);
    return true;
}

void __cdecl Enter(void* stopwatch, uint32_t caller)
{
    if (!g_Elapsed)
        g_Elapsed = fixed_ticks::ElapsedOriginal();
    if (GetCurrentThreadId() != g_mainThread)
        return;
    const bool byCall = CallOwner(caller);
    if (!byCall && !Take(stopwatch, Owner(caller), caller))
        return;
    g_byCall = byCall;
    g_read = true;
    virtual_clock::SetSimTimerRead(true);
}

// As Enter for a caller in kCallOwners, for our crew's calls (the caller's
// address is ours, so Enter would not see it).
void EnterCrew()
{
    if (GetCurrentThreadId() != g_mainThread)
        return;
    g_byCall = true;
    g_read = true;
    virtual_clock::SetSimTimerRead(true);
}

void __cdecl Leave()
{
    if (g_read) {
        g_read = false;
        g_byCall = false;
        virtual_clock::SetSimTimerRead(false);
    }
}

// A thunk per method: Enter with this and the return address, the original
// called with copies of its stack arguments (Args of them), Leave, and back
// with every register as the original left it (the float methods return in
// ST0).
#define SIM_TIMER_ENTER                                                                                                \
    __asm push eax                                                                                                    \
    __asm push ecx                                                                                                    \
    __asm push edx                                                                                                    \
    __asm push dword ptr [esp + 12]                                                                                   \
    __asm push ecx                                                                                                    \
    __asm call Enter                                                                                                  \
    __asm add esp, 8                                                                                                  \
    __asm pop edx                                                                                                     \
    __asm pop ecx                                                                                                     \
    __asm pop eax
#define SIM_TIMER_LEAVE                                                                                                \
    __asm push eax                                                                                                    \
    __asm push ecx                                                                                                    \
    __asm push edx                                                                                                    \
    __asm call Leave                                                                                                  \
    __asm pop edx                                                                                                     \
    __asm pop ecx                                                                                                     \
    __asm pop eax
#define SIM_TIMER_THUNK0(name)                                                                                         \
    void* g_##name = nullptr;                                                                                         \
    __declspec(naked) void Thunk##name()                                                                              \
    {                                                                                                                  \
        SIM_TIMER_ENTER                                                                                               \
        __asm call dword ptr [g_##name]                                                                               \
        SIM_TIMER_LEAVE                                                                                               \
        __asm ret                                                                                                     \
    }
#define SIM_TIMER_THUNK1(name)                                                                                         \
    void* g_##name = nullptr;                                                                                         \
    __declspec(naked) void Thunk##name()                                                                              \
    {                                                                                                                  \
        SIM_TIMER_ENTER                                                                                               \
        __asm push dword ptr [esp + 4]                                                                                \
        __asm call dword ptr [g_##name]                                                                               \
        SIM_TIMER_LEAVE                                                                                               \
        __asm ret 4                                                                                                   \
    }
#define SIM_TIMER_THUNK2(name)                                                                                         \
    void* g_##name = nullptr;                                                                                         \
    __declspec(naked) void Thunk##name()                                                                              \
    {                                                                                                                  \
        SIM_TIMER_ENTER                                                                                               \
        __asm push dword ptr [esp + 8]                                                                                \
        __asm push dword ptr [esp + 8]                                                                                \
        __asm call dword ptr [g_##name]                                                                               \
        SIM_TIMER_LEAVE                                                                                               \
        __asm ret 8                                                                                                   \
    }

// ElapsedMilliseconds (its pointer is set on the first Enter).
__declspec(naked) void ThunkElapsed()
{
    SIM_TIMER_ENTER
    __asm call dword ptr [g_Elapsed]
    SIM_TIMER_LEAVE
    __asm ret
}
SIM_TIMER_THUNK1(Elapsed2)
SIM_TIMER_THUNK1(Elapsed3)
SIM_TIMER_THUNK2(Elapsed4)
SIM_TIMER_THUNK1(Elapsed5)
SIM_TIMER_THUNK0(Restart)
SIM_TIMER_THUNK0(Start)
SIM_TIMER_THUNK0(Stop)

// cSC4NetworkConstructionCrew::Tick (0x00607C30, `ret 4`): its DoMessage
// (0x00607F14) hands it the frame message's microseconds, which only its
// workers' moves (0x00606500) use. Only computes a number and calls the
// original: no SEH guard (crash.h).
using CrewTickFn = void(__fastcall*)(void* crew, void* edx, uint32_t microseconds);
void* g_crewTick = nullptr;

void __fastcall CrewTick(void* crew, void* edx, uint32_t microseconds)
{
    reinterpret_cast<CrewTickFn>(g_crewTick)(crew, edx, CrewFrameMicroseconds(microseconds));
}

bool Install(registry::Mode)
{
    g_mainThread = GetCurrentThreadId();
    virtual_clock::SetSimTimerCounter(&Counter);
    const struct {
        std::uintptr_t address;
        void* thunk;
        void** original;
    } hooks[] = {
        {addr::SC4Stopwatch_Elapsed2, reinterpret_cast<void*>(&ThunkElapsed2), &g_Elapsed2},
        {addr::SC4Stopwatch_GetElapsed, reinterpret_cast<void*>(&ThunkElapsed3), &g_Elapsed3},
        {addr::SC4Stopwatch_Elapsed4, reinterpret_cast<void*>(&ThunkElapsed4), &g_Elapsed4},
        {addr::SC4Stopwatch_Elapsed5, reinterpret_cast<void*>(&ThunkElapsed5), &g_Elapsed5},
        {addr::GZStopwatch_Start, reinterpret_cast<void*>(&ThunkRestart), &g_Restart},
        {addr::SC4Stopwatch_Start, reinterpret_cast<void*>(&ThunkStart), &g_Start},
        {addr::SC4Stopwatch_Stop, reinterpret_cast<void*>(&ThunkStop), &g_Stop},
        {addr::cSC4NetworkConstructionCrew_Tick, reinterpret_cast<void*>(&CrewTick), &g_crewTick},
    };
    for (const auto& h : hooks) {
        if (h.address == addr::cSC4NetworkConstructionCrew_Tick && crew_sim::TickIsOurs()) {
            // Our Tick is at that entry (simulators.crew.running, installed
            // at PreAppInit) and asks CrewFrameMicroseconds itself.
            log::Write("sim_timers: crew Tick is ours (simulators.crew.running): no hook on 0x%08X",
                       static_cast<unsigned>(h.address));
            continue;
        }
        const addr::Entry* found = addr_check::Find(h.address);
        if (!found || !hook::Install(*found, h.thunk, h.original))
            return false;
    }
    // ElapsedMilliseconds is fixed_ticks' hook (which this needs; either may
    // install first); stopwatches it does not handle come here, and the
    // original is looked up on the first call (Enter).
    fixed_ticks::SetElapsedPass(reinterpret_cast<void*>(&ThunkElapsed));
    g_elapsedServed = true;
    log::Write("sim_timers: installed (MySim, advisor and construction crew stopwatches on simulation time, 10 ms per "
               "tick)");
    return true;
}

const bool g_registered = registry::Register({
    "determinism.sim_timers",
    "determinism",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::GZStopwatch_Start, // restarts (data/addresses/simulator.toml)
    "The MySim, advisor-system and construction crew stopwatches run on simulation time (10 ms a tick), not on "
    "clock reads",
    false,
    Install,
});

} // namespace

uint32_t CrewFrameMicroseconds(uint32_t frameMicroseconds)
{
    if (!g_mainThread)
        return frameMicroseconds;
    // The frame service (0x004487F0) holds a frame's time to at most 500 ms
    // (+0x54 of the service, 0x00448EA9); a frame of the default 60 checks
    // is 600 ms of simulation time.
    constexpr uint32_t kMicrosecondsPerCheck = 10000, kFrameMax = 500000;
    const uint32_t checks = fixed_ticks::LastSliceChecks();
    const uint32_t simulated = checks >= kFrameMax / kMicrosecondsPerCheck ? kFrameMax : checks * kMicrosecondsPerCheck;
    static int logged = 0;
    if (logged < 8) {
        ++logged;
        log::Write("sim_timers: crew frame time %u us from the frame service, %u us of simulation time",
                   frameMicroseconds, simulated);
    }
    return simulated;
}

using StopwatchFn = void(__thiscall*)(void* stopwatch);
using ElapsedFn = uint32_t(__thiscall*)(void* stopwatch);

void CrewStopwatchStart(void* stopwatch)
{
    if (!g_mainThread || !g_Restart) {
        reinterpret_cast<StopwatchFn>(addr::GZStopwatch_Start)(stopwatch);
        return;
    }
    EnterCrew();
    reinterpret_cast<StopwatchFn>(g_Restart)(stopwatch);
    Leave();
}

uint32_t CrewStopwatchElapsed(void* stopwatch)
{
    // The game's call goes through fixed_ticks' hook, which passes a crew
    // stopwatch (never the slice's, the pacing one or the frame one) to
    // ThunkElapsed; without that pass the game's call reaches the method
    // with no thunk at all.
    void* const original = fixed_ticks::ElapsedOriginal();
    if (!g_mainThread || !g_elapsedServed || !original)
        return reinterpret_cast<ElapsedFn>(addr::GZStopwatch_ElapsedMilliseconds)(stopwatch);
    EnterCrew();
    const uint32_t milliseconds = reinterpret_cast<ElapsedFn>(original)(stopwatch);
    Leave();
    return milliseconds;
}

void CrewStopwatchStop(void* stopwatch)
{
    if (!g_mainThread || !g_Stop) {
        reinterpret_cast<StopwatchFn>(addr::SC4Stopwatch_Stop)(stopwatch);
        return;
    }
    EnterCrew();
    reinterpret_cast<StopwatchFn>(g_Stop)(stopwatch);
    Leave();
}

void Claim(void* stopwatch)
{
    if (g_mainThread && GetCurrentThreadId() == g_mainThread)
        Take(stopwatch, true, 0);
}

} // namespace opensc4::sim_timers
