// determinism.lights_at_once: at the view's nightfall and daybreak every
// building's lights change at once (docs/experiments/determinism.md,
// "Nightfall and the lot developer's shuffle"). For test runs only.
//
// When the view's clock turns to night or back the lot developer shuffles
// its buildings (0x006C68C0, from message 0x44778234) and then changes the
// lights of a few of them every 200 ms of the frame timer (0x006CBD60).
// How far that has got (+0x8C, +0x9C) is saved with the city, so two runs
// of a save in which a turn falls write different records though nothing
// of the simulation differs. Here the shuffle is replaced: no shuffle, and
// the game's own finish (0x006CBE50), which changes every building and
// ends the stepping. A city saved in the middle of the stepping is
// finished when the autotest has loaded it.
#include "core/address_check.h"
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

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
    return g_on;
}

// A city saved while its lights were being stepped.
void OnLoad(cISC4City*)
{
    if (g_on && game::IsBuild641()) {
        Finish();
        FinishProps();
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
    "Test runs: at nightfall and daybreak every building's lights change at once, not in steps on the frame timer",
    false,
    Install,
});

const autotest::LoadHook kLoad{"lights_at_once", 20, &OnLoad};
const autotest::FingerprintPart kFingerprint{"lights_at_once", 1, &OnFingerprint};

} // namespace
} // namespace opensc4::lights_at_once
