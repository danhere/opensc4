// determinism.lights_at_once: the view's day and night kept out of what a
// test run compares (docs/experiments/determinism.md, "Nightfall and the
// lot developer's shuffle"). For test runs only: play keeps the game's.
//
// The view's clock runs on a stopwatch and is stepped with frames; when it
// turns to night or back, the lot developer shuffles its buildings
// (0x006C68C0, from message 0x44778234) and changes the lights of a few of
// them every 200 ms of the frame timer (0x006CBD60), and the prop developer
// does the same with its timed props (0x006F0EC0). When the turn comes and
// how far the stepping has got (saved with the city) follow the frames, so
// two runs of a save in which a turn falls differ though nothing of the
// simulation does. Here:
//   - the clock of a loaded city counts simulation ticks and is stepped at
//     the start of each simulation day, by the autotest;
//   - the shuffle is replaced: no shuffle, and the game's own finish
//     (0x006CBE50), which changes every building and ends the stepping;
//   - the timed props are finished with the game's function that switches
//     them all (0x006F10E0);
//   - a city saved in the middle of either stepping is finished when the
//     autotest has loaded it.
#include "core/address_check.h"
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/sim_timers.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <string>

namespace opensc4::lights_at_once {
namespace {

// The lot developer's fields (lot_sim.h).
constexpr std::size_t kNightState = 0x9C; // 1 or 2 while the buildings are stepped

struct DequeIterator {
    char* current;
    char* first;
    char* last;
    char** node;
};

using ShuffleFn = void(__cdecl*)(DequeIterator first, DequeIterator last);
ShuffleFn g_shuffle = nullptr;
bool g_on = false;

// Every building to the state the stepping was working towards, by the
// game's function; the stepping is idle after it.
void Finish()
{
    char* const developer = Global<char*>(addr::SC4_Service43D20);
    if (!developer)
        return;
    const uint32_t state = At<uint32_t>(developer, kNightState);
    if (state != 1 && state != 2)
        return;
    using FinishFn = void(__thiscall*)(void* self, uint32_t state);
    reinterpret_cast<FinishFn>(addr::cSC4LotDeveloper_FinishNightBuildings)(developer, state);
}

// The prop developer (the city's slot 41) steps its timed props the same
// way after a turn (0x006F0EC0: +0xA34 props every 200 ms of the frame
// timer, +0xA2C the next one, +0xA30 the state, +0xA38 the milliseconds, all
// saved). Nothing of its own starts the stepping that could be replaced, so
// it is finished from outside with the game's function that switches every
// timed prop at once (0x006F10E0), and the milliseconds are cleared.
void FinishProps()
{
    void* const city = Global<void*>(addr::SC4_City);
    char* const props = city ? Virtual<char*>(city, 0xA4) : nullptr;
    if (!props)
        return;
    const uint32_t state = At<uint32_t>(props, 0xA30);
    if (state != 1 && state != 2)
        return;
    using SetFn = void(__thiscall*)(void* self, uint8_t on);
    reinterpret_cast<SetFn>(addr::cSC4PropDeveloper_SetTimedProps)(props, state == 1 ? 1 : 0);
    At<uint32_t>(props, 0xA38) = 0;
}

// The view's clock (cSC424HourClock, the service at 0x00B43DBC) adds the
// milliseconds of its stopwatch (+0x50) to its seconds of the day at every
// frame (0x00445820) and posts the turn when the hour crosses between day
// and night. On the clock that stopwatch follows how many frames a run
// took, so the turn came at another point of each run. Under
// determinism.sim_timers it counts simulation ticks instead.
char* g_clock = nullptr; // the clock, once a city's has been taken over

void ClockOnSimulationTime()
{
    char* const clock = Global<char*>(addr::SC4_Clock43DBC);
    if (!clock)
        return;
    static bool logged = false;
    // A GZStopwatch starts with its unit; the clock's is 3.
    if (At<int32_t>(clock, 0x50) != 3) {
        if (!logged)
            log::Write("lights_at_once: no stopwatch where the view's clock keeps it; it stays on the clock");
        logged = true;
        return;
    }
    sim_timers::Claim(clock + 0x50);
    if (!logged)
        log::Write("lights_at_once: the view's clock counts simulation ticks, a day at a time");
    logged = true;
    g_clock = clock;
}

// The clock's own tick comes with frames, and a run does not have the same
// number of frames between two simulation days each time: the seconds went
// in at other moments and the turn with them. Once a city's clock is on
// simulation time its tick is left out when a frame asks, and made at the
// start of each simulation day instead.
using TickFn = uint8_t(__fastcall*)(void* part, void* edx, uint32_t unused);
TickFn g_tick = nullptr;
bool g_dayTick = false;

uint8_t __fastcall Tick(void* part, void*, uint32_t unused)
{
    if (g_clock && part == g_clock + 0xC && !g_dayTick)
        return 1;
    return g_tick(part, nullptr, unused);
}

void OnDay(const autotest::Day&)
{
    if (!g_on || !g_clock || !g_tick)
        return;
    g_dayTick = true;
    g_tick(g_clock + 0xC, nullptr, 0);
    g_dayTick = false;
    // A turn made just now, on the run's last day, comes after the day's
    // fingerprint and before the save.
    Finish();
    FinishProps();
}

void __cdecl Shuffle(DequeIterator, DequeIterator)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("lights_at_once: first turn of the view's day; every building at once");
    }
    Finish();
    FinishProps();
}

bool Install(registry::Mode)
{
    const addr::Entry* shuffle = addr_check::Find(addr::cSC4LotDeveloper_Fn_006C68C0);
    g_on = shuffle && hook::Install(*shuffle, &Shuffle, &g_shuffle);
    if (const addr::Entry* tick = g_on ? addr_check::Find(addr::cSC424HourClock_Tick) : nullptr)
        hook::Install(*tick, &Tick, &g_tick);
    return g_on;
}

// A city saved while its lights were being stepped.
void OnLoad(cISC4City*)
{
    if (g_on && game::IsBuild641()) {
        Finish();
        FinishProps();
        ClockOnSimulationTime();
    }
}

// At every daily fingerprint, the last of which comes just before the save:
// the timed props a turn since the last one left in steps. Adds nothing to
// the line.
void OnFingerprint(cISC4City*, std::string&)
{
    if (g_on && game::IsBuild641())
        FinishProps();
}

const bool g_registered = registry::Register({
    "determinism.lights_at_once",
    "determinism",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LotDeveloper_Fn_006C68C0,
    "Test runs: the view's clock steps once a simulation day, and at its nightfall the lights change at once",
    false,
    Install,
});

const autotest::LoadHook kLoad{"lights_at_once", 20, &OnLoad};
const autotest::FingerprintPart kFingerprint{"lights_at_once", 1, &OnFingerprint};
const autotest::DayHook kDay{"lights_at_once", 1, &OnDay};

} // namespace
} // namespace opensc4::lights_at_once
