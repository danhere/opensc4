// game.seed: one seed per city for all of the game's randomness, to show and
// to share ("try 1F2E-3D4C on this city").
//
// The game's random numbers come from generator objects. 37 of them live in
// the exe's own data (build 641), seeded once at startup by static
// initialisers (the building developer's, the lot developer's, disasters,
// vehicles, prop timers...); the rest are made later, seeded from positions (tracts) or from
// the clock. So what a city draws after loading depends on everything drawn
// since the game started: the region view, other cities, the menus. Here,
// when a city starts loading (cSC4App::LoadCity, which makes a few objects
// with generators of their own before the city) or a city object is made
// otherwise (cSC4City's constructor: founding a city), every static
// generator is seeded from the city's seed and its own address, and clock seeds from then on come from the seed and their call
// site (fixed_seed.cpp). The same save with the same seed then starts from
// the same random state, whatever came before.
//
// Same random state is not yet the same game: in normal play how much
// simulation runs per frame follows real time, so two players' cities draw in
// a different order within days. With the determinism replacements (as in
// parity tests, docs/parity.md) a seed replays exactly (docs/seeds.md).
//
// The seed: [game] seed in opensc4.ini, 8 hex digits ("1F2E-3D4C", the dash
// optional) or "random" (the default: a new one for every city load). In the
// game, the cheat box (Ctrl+X) takes "seed" to show the city's seed and
// "seed 1F2E-3D4C" (or "seed random") to choose the one the next city load
// uses. The window title shows the current one.
#include "core/game_seed.h"

#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/seed_code.h"
#include "core/settings.h"

#include <GZCLSIDDefs.h>
#include <GZServPtrs.h>
#include <cIGZCheatCodeManager.h>
#include <cIGZMessage2Standard.h>
#include <cIGZMessageServer2.h>
#include <cIGZMessageTarget2.h>
#include <cIGZString.h>
#include <cISC4App.h>
#include <cRZBaseString.h>

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <opensc4/addresses.h>

namespace opensc4::game_seed {
namespace {

using seed_code::Format;
using seed_code::Parse;

constexpr uint32_t kMsgPostCityInit = 0x26D31EC1;
constexpr uint32_t kMsgCheatIssued = 0x230E27AC;
constexpr uint32_t kSeedCheatId = 0x4F534345; // "OSCE"

bool g_on = false;
bool g_haveChosen = false; // a seed chosen for the next load (else random)
uint32_t g_chosen = 0;
bool g_haveCurrent = false; // the loaded city's seed
uint32_t g_current = 0;

uint32_t RandomSeed()
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter); // our import: real time even under the virtual clock
    uint32_t x = static_cast<uint32_t>(counter.QuadPart) ^ static_cast<uint32_t>(counter.QuadPart >> 32) ^
                 (GetCurrentProcessId() << 16) ^ static_cast<uint32_t>(GetTickCount64());
    x ^= x >> 16;
    x *= 0x7FEB352D;
    x ^= x >> 15;
    x *= 0x846CA68B;
    x ^= x >> 16;
    return x;
}

HWND g_window = nullptr;

BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
        return TRUE;
    g_window = hwnd;
    return FALSE;
}

HWND GameWindow()
{
    if (!g_window || !IsWindow(g_window)) {
        g_window = nullptr;
        EnumWindows(FindGameWindow, 0);
    }
    return g_window;
}

void ShowTitle()
{
    HWND hwnd = GameWindow();
    if (!hwnd)
        return;
    char title[96];
    if (g_haveCurrent)
        std::snprintf(title, sizeof(title), "SimCity 4 - seed %s", Format(g_current).c_str());
    else
        std::snprintf(title, sizeof(title), "SimCity 4");
    SetWindowTextA(hwnd, title);
}

void Tell(const std::string& text)
{
    log::Write("seed: %s", text.c_str());
    if (GetEnvironmentVariableA("OPENSC4_TEST_CITY", nullptr, 0) > 0)
        return; // unattended runs: the log only
    MessageBoxA(GameWindow(), text.c_str(), "OpenSC4 seed", MB_OK | MB_ICONINFORMATION);
}

std::string NextLoadText()
{
    return g_haveChosen ? "The next city you load or found uses seed " + Format(g_chosen) + "."
                        : "The next city you load or found gets a new random seed.";
}

// "seed", "seed <code>", "seed random": the full cheat string.
void OnCheat(const char* text)
{
    std::string rest = text ? text : "";
    size_t i = 0;
    while (i < rest.size() && !std::isspace(static_cast<unsigned char>(rest[i])))
        ++i;
    rest = rest.substr(i);
    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())))
        rest.erase(rest.begin());
    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.back())))
        rest.pop_back();

    if (rest.empty()) {
        const std::string now = g_haveCurrent ? "This city's seed is " + Format(g_current) + "."
                                              : "No city is loaded.";
        Tell(now + "\n\n" + NextLoadText() +
             "\n\nType \"seed 1F2E-3D4C\" to choose a seed, or \"seed random\".");
        return;
    }
    if (_stricmp(rest.c_str(), "random") == 0) {
        g_haveChosen = false;
        Tell(NextLoadText());
        return;
    }
    uint32_t seed = 0;
    if (!Parse(rest, &seed)) {
        Tell("\"" + rest + "\" is not a seed. A seed is 8 hex digits, like 1F2E-3D4C.");
        return;
    }
    g_haveChosen = true;
    g_chosen = seed;
    Tell(NextLoadText() + (g_haveCurrent ? "\n\nThis city keeps " + Format(g_current) +
                                               " until it is loaded again."
                                         : std::string()));
}

bool g_loading = false; // inside LoadCity: its city constructor needs no second reseed

void Reseed(const char* why)
{
    g_current = g_haveChosen ? g_chosen : RandomSeed();
    g_haveCurrent = true;
    fixed_seed::ReseedForCity(g_current);
    log::Write("seed: %s with seed %s (%s)", why, Format(g_current).c_str(), g_haveChosen ? "chosen" : "random");
}

// Main thread. Our part cannot fault, so no SEH guard (crash.h).
using LoadCityFn = bool(__fastcall*)(void* self, void* edx, void* path, void* city);
LoadCityFn g_loadCity = nullptr;

bool __fastcall LoadCity(void* self, void* edx, void* path, void* city)
{
    Reseed("city loads");
    g_loading = true;
    const bool ok = g_loadCity(self, edx, path, city);
    g_loading = false;
    return ok;
}

using CityCtorFn = void*(__fastcall*)(void* self, void* edx);
CityCtorFn g_cityCtor = nullptr;

void* __fastcall CityCtor(void* self, void* edx)
{
    if (!g_loading)
        Reseed("city made");
    return g_cityCtor(self, edx);
}

class Listener final : public cIGZMessageTarget2 {
public:
    bool QueryInterface(uint32_t riid, void** object) override
    {
        if (riid == GZCLSID::kcIGZMessageTarget2 || riid == GZIID_cIGZUnknown) {
            *object = static_cast<cIGZMessageTarget2*>(this);
            AddRef();
            return true;
        }
        *object = nullptr;
        return false;
    }
    // Static: lives for the whole process.
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }

    bool DoMessage(cIGZMessage2* message) override
    {
        const uint32_t type = message->GetType();
        if (type == kMsgPostCityInit) {
            ShowTitle();
            RegisterCheat(); // in case the app had no cheat manager at startup
        } else if (type == kMsgCheatIssued) {
            auto* standard = static_cast<cIGZMessage2Standard*>(message);
            if (static_cast<uint32_t>(standard->GetData1()) == kSeedCheatId) {
                const auto* text = static_cast<cIGZString*>(standard->GetVoid2());
                OnCheat(text ? text->ToChar() : nullptr);
            }
        }
        return true;
    }

    void RegisterCheat()
    {
        if (cheatRegistered_)
            return;
        cISC4AppPtr app;
        cIGZCheatCodeManager* cheats = app ? app->GetCheatCodeManager() : nullptr;
        if (!cheats)
            return;
        cheatRegistered_ = cheats->RegisterCheatCode(kSeedCheatId, cRZBaseString("seed")) &&
                           cheats->AddNotification2(this, 0);
        log::Write("seed: cheat \"seed\" %s", cheatRegistered_ ? "registered" : "NOT registered");
    }

private:
    bool cheatRegistered_ = false;
};

Listener g_listener;

bool Apply(registry::Mode)
{
    const std::string value = settings::Get("game", "seed", "random");
    if (_stricmp(value.c_str(), "random") != 0) {
        g_haveChosen = Parse(value, &g_chosen);
        if (!g_haveChosen)
            log::Write("seed: [game] seed = \"%s\" is not 8 hex digits; using random seeds", value.c_str());
    }
    const addr::Entry* load = addr_check::Find(addr::cSC4App_LoadCity);
    const addr::Entry* ctor = addr_check::Find(addr::cSC4City_cSC4City);
    g_on = load && ctor && fixed_seed::InstallForGameSeed() && hook::Install(*load, &LoadCity, &g_loadCity) &&
           hook::Install(*ctor, &CityCtor, &g_cityCtor);
    log::Write("seed: %s, %s", g_on ? "installed" : "FAILED",
               g_haveChosen ? ("seed " + Format(g_chosen)).c_str() : "a random seed per city");
    return g_on;
}

const bool g_registered = registry::Register({
    "game.seed",
    "game",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::cSC4City_cSC4City,
    "One seed per city for the game's random numbers, shown in the title and set with the \"seed\" cheat",
    false,
    Apply,
});

} // namespace

void Start()
{
    if (!g_on)
        return;
    cIGZMessageServer2Ptr server;
    const bool ok = server && server->AddNotification(&g_listener, kMsgPostCityInit);
    log::Write("seed: city messages %s", ok ? "subscribed" : "NOT subscribed");
    g_listener.RegisterCheat();
}

} // namespace opensc4::game_seed
