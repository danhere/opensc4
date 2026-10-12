// determinism.fixed_seed: seeds that the game takes from the clock are
// replaced by fixed ones, so two runs of the same save start from the same
// random state. Clock seeds are seed 0xFFFFFFFF (time() ^ clock()) and the
// values static initialisers build from the local time; they change terrain
// texture variation, flora and automata on every load (docs/renderer.md,
// "Screenshot harness").
//
// Applied at Phase::Attach so it sees the static initialisers. Until
// EndStartup (after PostAppInit) the n-th seed call gets Mix(base + n): the
// order of startup seed calls is fixed. After it, seeds the game computes
// itself pass through: simulation code seeds local generators from positions
// (cSC4TractDeveloper::UpdatePopulation seeds from the tract coordinates),
// and an earlier version that replaced those by a global counter made them
// depend on how many generators frame-level code had created
// (docs/experiments/determinism.md). A clock seed after startup gets a
// counter of its own call site (the first caller outside cRZRandom), so other
// sites cannot shift it. OPENSC4_SEED sets the base (default 0x5EED).
//
// game.seed (game_seed.cpp) shares these hooks: it needs every generator that
// lives in the exe's own data (37 in build 641, seeded by static
// initialisers), because their state at city load depends on everything
// drawn since startup. ReseedForCity seeds each of them from the city's seed
// and its address, and from then on clock seeds come from the city's seed
// too.
#include "core/fixed_seed.h"

#include "core/address_check.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/settings.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <opensc4/addresses.h>

namespace opensc4::fixed_seed {
namespace {

using SeedFn = void(__fastcall*)(void* self, void* edx, uint32_t seed);
SeedFn g_seedLcg = nullptr;
SeedFn g_seedMt = nullptr;

uint32_t g_base = 0x5EED;
std::atomic<uint32_t> g_calls{0};

// splitmix32-style finaliser: distinct, well-spread seeds from a counter.
uint32_t Mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352D;
    x ^= x >> 15;
    x *= 0x846CA68B;
    x ^= x >> 16;
    return x;
}

// OPENSC4_SEED_LOG=1: log every seed call with its thread and caller, to
// check that runs seed their generators in the same order
// (docs/experiments/determinism.md).
bool g_logAll = false;

std::atomic<bool> g_startupDone{false};

// Which replacements use the hooks: determinism.fixed_seed replaces seeds
// from the start; game.seed only records generators until the first
// ReseedForCity, then replaces clock seeds.
bool g_fixedOn = false;
bool g_hooksInstalled = false;
bool g_hooksOk = false;
std::atomic<bool> g_cityReseeded{false};

// Generators inside the exe's image: never freed, so they can be reseeded
// at any time. Recorded at their first seed call.
struct StaticGenerator {
    void* self;
    bool mt; // cRZRandomSuper (Mersenne Twister), else cRZRandom (LCG)
};
constexpr int kStatics = 256;
StaticGenerator g_statics[kStatics];
int g_staticCount = 0;
uintptr_t g_imageStart = 0;
uintptr_t g_imageEnd = 0;
CRITICAL_SECTION g_staticLock;

void RecordStatic(void* self, bool mt)
{
    const auto address = reinterpret_cast<uintptr_t>(self);
    if (address < g_imageStart || address >= g_imageEnd)
        return;
    EnterCriticalSection(&g_staticLock);
    int i = 0;
    while (i < g_staticCount && g_statics[i].self != self)
        ++i;
    if (i == g_staticCount && g_staticCount < kStatics)
        g_statics[g_staticCount++] = {self, mt};
    LeaveCriticalSection(&g_staticLock);
}

// The first code address up the stack from `retSlot` outside the cRZRandom
// functions (0x00913600-0x00913C40): the code that constructed or seeded the
// generator, not the shared constructor.
// SiteOverride's site and thread.
uint32_t g_siteOverride = 0;
DWORD g_siteOverrideThread = 0;

uint32_t CallSite(void** retSlot)
{
    if (g_siteOverride && GetCurrentThreadId() == g_siteOverrideThread)
        return g_siteOverride;
    auto* words = reinterpret_cast<uint32_t*>(retSlot);
    for (int i = 0; i < 32; ++i) {
        const uint32_t v = words[i];
        if (v >= 0x00401000 && v < 0x00A80000 && !(v >= 0x00913600 && v < 0x00913C40))
            return v;
    }
    return 0;
}

// Clock seeds after startup, counted per call site. Rare, so a lock and a
// small table do.
constexpr int kSites = 512;
uint32_t g_siteKeys[kSites];
uint32_t g_siteCounts[kSites];
int g_siteCount = 0;
CRITICAL_SECTION g_siteLock;

// Generators the game seeds from the time of day itself, not with 0xFFFFFFFF:
// the prop developer's (made in cSC4PropDeveloper::Create) and the traffic
// simulator's (Init, 0x0071F1A5; our own Init calls from core.dll and is not
// covered). Under the virtual clock their seeds are constant; in play they
// change every second. After ReseedForCity they count as clock seeds.
bool IsTimeSeedSite(uint32_t site)
{
    return site == 0x006F34B6 || site == 0x0071F1A5;
}

uint32_t NextSeed(uint32_t requested, const char* which, void** retSlot)
{
    const uint32_t n = g_calls.fetch_add(1);
    // game.seed alone: the game's own seeds until a city has been reseeded.
    if (!g_fixedOn && !g_cityReseeded.load())
        return requested;
    const bool startup = g_fixedOn && !g_startupDone.load();
    if (!startup && requested != 0xFFFFFFFF && !(g_cityReseeded.load() && IsTimeSeedSite(CallSite(retSlot)))) {
        if (g_logAll)
            log::Write("fixed_seed: %s seed #%u: 0x%08X kept (thread %lu, site %08X)", which, n, requested,
                       GetCurrentThreadId(), CallSite(retSlot));
        return requested;
    }
    uint32_t seed = Mix(g_base + n);
    uint32_t site = 0;
    if (!startup) {
        site = CallSite(retSlot);
        EnterCriticalSection(&g_siteLock);
        int i = 0;
        while (i < g_siteCount && g_siteKeys[i] != site)
            ++i;
        if (i == g_siteCount && g_siteCount < kSites)
            g_siteKeys[g_siteCount++] = site;
        const uint32_t k = i < kSites ? ++g_siteCounts[i] : 0;
        LeaveCriticalSection(&g_siteLock);
        seed = Mix(Mix(g_base ^ site) + k);
    }
    // The first few show what the game asked for; after that it is noise.
    if (n < 8 || g_logAll || (!startup && n < 2000))
        log::Write("fixed_seed: %s seed #%u: 0x%08X -> 0x%08X (thread %lu, %s %08X)", which, n, requested, seed,
                   GetCurrentThreadId(), startup ? "from" : "clock seed at site", startup ? CallSite(retSlot) : site);
    return seed;
}

// The detours only compute a number and tail-call the original: nothing here
// can fault, so no SEH guard (crash.h).
void __fastcall SeedLcg(void* self, void* edx, uint32_t seed)
{
    RecordStatic(self, false);
    g_seedLcg(self, edx, NextSeed(seed, "cRZRandom", static_cast<void**>(_AddressOfReturnAddress())));
}

void __fastcall SeedMt(void* self, void* edx, uint32_t seed)
{
    RecordStatic(self, true);
    g_seedMt(self, edx, NextSeed(seed, "cRZRandomSuper", static_cast<void**>(_AddressOfReturnAddress())));
}

// The C runtime's srand: only Lua's math.randomseed calls it, but simulation
// code draws from the same per-thread rand() state. Pinned to the base seed.
using SrandFn = void(__cdecl*)(unsigned seed);
SrandFn g_srand = nullptr;

void __cdecl Srand(unsigned seed)
{
    log::Write("fixed_seed: srand(0x%08X) from %p on thread %lu -> 0x%08X", seed, _ReturnAddress(),
               GetCurrentThreadId(), g_base);
    g_srand(g_base);
}

// The C runtime's rand(): one sequence per thread, which the simulation
// (the cell walks' starts and steps 0x0049CBE0 and 0x0049D1C0, the shuffles
// 0x005CF610, 0x006C68C0 and 0x006DB010, the landfill's pictures 0x006BCFC0,
// Lua's math.random) shares on the main thread with the sound code, which
// draws when a sound starts or on a real-time stopwatch (0x004EE560,
// 0x008D2CFD, 0x008D3442, 0x008D546F). How many sounds had started when a
// walk picked its start changed from run to run, and the flammability and
// weather walks with it (docs/experiments/determinism.md). The sound code's
// draws come from a sequence of their own.
struct SoundCaller {
    uintptr_t start, end;
};
constexpr SoundCaller kSoundCallers[] = {
    {0x004EE560, 0x004EE970}, // an audio event handler: a wait of 1 to 4 s
    {0x008D2CFD, 0x008D2E60}, // GZSound
    {0x008D3442, 0x008D3486},
    {0x008D546F, 0x008D54C3},
};
std::atomic<uint32_t> g_soundState{0x5EED50FDu};

int NextSoundRand()
{
    // The C runtime's own generator, on our state.
    const uint32_t next = g_soundState.load(std::memory_order_relaxed) * 214013u + 2531011u;
    g_soundState.store(next, std::memory_order_relaxed);
    return static_cast<int>((next >> 16) & 0x7FFF);
}

// OPENSC4_RAND_TRACE=<file>: every rand() call's caller and thread, as
// pairs of 32-bit words, in call order, to find where two runs part (the
// sound code's calls too).
using RandFn = int(__cdecl*)();
RandFn g_rand = nullptr;
std::FILE* g_randTrace = nullptr;
uint32_t g_randBuffer[512]; // flushed often: the game may exit without unloading us cleanly
size_t g_randUsed = 0;
CRITICAL_SECTION g_randLock;

int __cdecl Rand()
{
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    bool sound = false;
    for (const SoundCaller& range : kSoundCallers)
        sound = sound || (caller >= range.start && caller < range.end);
    const int value = sound ? NextSoundRand() : g_rand();
    if (!g_randTrace)
        return value;
    EnterCriticalSection(&g_randLock);
    g_randBuffer[g_randUsed++] = static_cast<uint32_t>(caller);
    g_randBuffer[g_randUsed++] = GetCurrentThreadId();
    if (g_randUsed == sizeof(g_randBuffer) / sizeof(g_randBuffer[0])) {
        std::fwrite(g_randBuffer, sizeof(uint32_t), g_randUsed, g_randTrace);
        std::fflush(g_randTrace);
        g_randUsed = 0;
    }
    LeaveCriticalSection(&g_randLock);
    return value;
}

// The seed hooks, for whichever of determinism.fixed_seed and game.seed is
// applied first (both at Phase::Attach).
bool InstallHooks()
{
    if (g_hooksInstalled)
        return g_hooksOk;
    g_hooksInstalled = true;
    InitializeCriticalSection(&g_siteLock);
    InitializeCriticalSection(&g_staticLock);
    const auto* image = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    g_imageStart = reinterpret_cast<uintptr_t>(image);
    g_imageEnd = g_imageStart + nt->OptionalHeader.SizeOfImage;
    const addr::Entry* lcg = addr_check::Find(addr::cRZRandom_Seed);
    const addr::Entry* mt = addr_check::Find(addr::cRZRandomSuper_Seed);
    g_hooksOk = lcg && mt && hook::Install(*lcg, &SeedLcg, &g_seedLcg) && hook::Install(*mt, &SeedMt, &g_seedMt);
    // The sound code's rand() calls leave the simulation's sequence alone
    // (OPENSC4_SHARED_RAND=1: as the game has it).
    char shared[4];
    if (GetEnvironmentVariableA("OPENSC4_SHARED_RAND", shared, sizeof(shared)) == 0) {
        const addr::Entry* rand = addr_check::Find(addr::CRT_rand);
        if (!rand || !hook::Install(*rand, &Rand, &g_rand))
            log::Write("fixed_seed: rand() not hooked; the sound code shares the simulation's sequence");
    }
    return g_hooksOk;
}

bool Apply(registry::Mode)
{
    g_fixedOn = true;
    char value[32];
    const DWORD length = GetEnvironmentVariableA("OPENSC4_SEED", value, sizeof(value));
    if (length > 0 && length < sizeof(value))
        g_base = static_cast<uint32_t>(std::strtoul(value, nullptr, 0));
    g_logAll = GetEnvironmentVariableA("OPENSC4_SEED_LOG", value, sizeof(value)) == 1 && value[0] == '1';
    const addr::Entry* srand = addr_check::Find(addr::CRT_srand);
    const bool ok = srand && InstallHooks() && hook::Install(*srand, &Srand, &g_srand);
    char tracePath[MAX_PATH];
    const DWORD traceLength = GetEnvironmentVariableA("OPENSC4_RAND_TRACE", tracePath, sizeof(tracePath));
    if (ok && g_rand && traceLength > 0 && traceLength < sizeof(tracePath)) {
        InitializeCriticalSection(&g_randLock);
        std::FILE* trace = nullptr;
        if (fopen_s(&trace, tracePath, "wb") == 0) {
            g_randTrace = trace;
            log::Write("fixed_seed: tracing rand() callers to %s", tracePath);
        }
    }
    log::Write("fixed_seed: base 0x%08X, %s", g_base, ok ? "installed" : "FAILED");
    return ok;
}

const bool g_registered = registry::Register({
    "determinism.fixed_seed",
    "determinism",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::cRZRandom_Seed,
    "Seeds the game's random number generators from a fixed sequence instead of the clock",
    false,
    Apply,
});

} // namespace

int SoundRand()
{
    return g_rand ? NextSoundRand() : reinterpret_cast<int(__cdecl*)()>(addr::CRT_rand)();
}

bool InstallForGameSeed()
{
    return InstallHooks();
}

namespace {

int ReseedStatics(uint32_t seed)
{
    EnterCriticalSection(&g_staticLock);
    const int count = g_staticCount;
    for (int i = 0; i < count; ++i) {
        const StaticGenerator& g = g_statics[i];
        const uint32_t value = Mix(seed ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g.self)));
        // The originals, not the detours: these are not the game's requests.
        (g.mt ? g_seedMt : g_seedLcg)(g.self, nullptr, value);
    }
    LeaveCriticalSection(&g_staticLock);
    return count;
}

} // namespace

void ReseedForCity(uint32_t seed)
{
    if (!g_hooksOk)
        return;
    EnterCriticalSection(&g_siteLock);
    g_base = seed;
    for (int i = 0; i < g_siteCount; ++i)
        g_siteCounts[i] = 0;
    LeaveCriticalSection(&g_siteLock);
    const int count = ReseedStatics(seed);
    g_cityReseeded = true;
    log::Write("fixed_seed: %d static generators reseeded from 0x%08X; clock seeds from it too", count, seed);
}

void EndStartup()
{
    // [game] test_scramble = <n>: every static generator in a state made from
    // n, as a long session before the first city would leave them (to test
    // game.seed; docs/seeds.md).
    const std::string scramble = settings::Get("game", "test_scramble", "");
    if (g_hooksOk && !scramble.empty()) {
        const uint32_t value = static_cast<uint32_t>(std::strtoul(scramble.c_str(), nullptr, 0));
        log::Write("fixed_seed: %d static generators scrambled with 0x%08X (test)", ReseedStatics(value), value);
    }
    if (registry::ModeOf("determinism.fixed_seed") == registry::Mode::Original || g_startupDone.exchange(true))
        return;
    log::Write("fixed_seed: startup over after %u seed calls; from now on only clock seeds are replaced",
               g_calls.load());
}

SiteOverride::SiteOverride(uint32_t site) : previous_(g_siteOverride)
{
    g_siteOverride = site;
    g_siteOverrideThread = GetCurrentThreadId();
}

SiteOverride::~SiteOverride()
{
    g_siteOverride = previous_;
}

} // namespace opensc4::fixed_seed
