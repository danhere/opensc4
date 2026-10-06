// determinism.fixed_seed: every seed the game gives its random number
// generators is replaced by a fixed sequence, so two runs of the same save
// start from the same random state. Seeds normally come from the clock (seed
// 0xFFFFFFFF, or values built from the local time in static initialisers),
// which changes terrain texture variation, flora and automata on every load
// (docs/renderer.md, "Screenshot harness").
//
// Applied at Phase::Attach so it sees the static initialisers. The n-th seed
// call gets Mix(base + n), whatever the game asked for; the order of seed
// calls is the same in every run as long as the game takes the same path.
// OPENSC4_SEED sets the base (default 0x5EED).
#include "core/address_check.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

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

uint32_t NextSeed(uint32_t requested, const char* which, void* caller)
{
    const uint32_t n = g_calls.fetch_add(1);
    const uint32_t seed = Mix(g_base + n);
    // The first few show what the game asked for; after that it is noise.
    if (n < 8 || g_logAll)
        log::Write("fixed_seed: %s seed #%u: 0x%08X -> 0x%08X (thread %lu, from %p)", which, n, requested, seed,
                   GetCurrentThreadId(), caller);
    return seed;
}

// The detours only compute a number and tail-call the original: nothing here
// can fault, so no SEH guard (crash.h).
void __fastcall SeedLcg(void* self, void* edx, uint32_t seed)
{
    g_seedLcg(self, edx, NextSeed(seed, "cRZRandom", _ReturnAddress()));
}

void __fastcall SeedMt(void* self, void* edx, uint32_t seed)
{
    g_seedMt(self, edx, NextSeed(seed, "cRZRandomSuper", _ReturnAddress()));
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

// OPENSC4_RAND_TRACE=<file>: every rand() call's caller and thread, as
// pairs of 32-bit words, in call order, to find where two runs part.
using RandFn = int(__cdecl*)();
RandFn g_rand = nullptr;
std::FILE* g_randTrace = nullptr;
uint32_t g_randBuffer[512]; // flushed often: the game may exit without unloading us cleanly
size_t g_randUsed = 0;
CRITICAL_SECTION g_randLock;

int __cdecl Rand()
{
    const int value = g_rand();
    EnterCriticalSection(&g_randLock);
    g_randBuffer[g_randUsed++] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()));
    g_randBuffer[g_randUsed++] = GetCurrentThreadId();
    if (g_randUsed == sizeof(g_randBuffer) / sizeof(g_randBuffer[0])) {
        std::fwrite(g_randBuffer, sizeof(uint32_t), g_randUsed, g_randTrace);
        std::fflush(g_randTrace);
        g_randUsed = 0;
    }
    LeaveCriticalSection(&g_randLock);
    return value;
}

bool Apply(registry::Mode)
{
    char value[32];
    const DWORD length = GetEnvironmentVariableA("OPENSC4_SEED", value, sizeof(value));
    if (length > 0 && length < sizeof(value))
        g_base = static_cast<uint32_t>(std::strtoul(value, nullptr, 0));
    g_logAll = GetEnvironmentVariableA("OPENSC4_SEED_LOG", value, sizeof(value)) == 1 && value[0] == '1';
    const addr::Entry* lcg = addr_check::Find(addr::cRZRandom_Seed);
    const addr::Entry* mt = addr_check::Find(addr::cRZRandomSuper_Seed);
    const addr::Entry* srand = addr_check::Find(addr::CRT_srand);
    const bool ok = lcg && mt && srand && hook::Install(*lcg, &SeedLcg, &g_seedLcg) &&
                    hook::Install(*mt, &SeedMt, &g_seedMt) && hook::Install(*srand, &Srand, &g_srand);
    char tracePath[MAX_PATH];
    const DWORD traceLength = GetEnvironmentVariableA("OPENSC4_RAND_TRACE", tracePath, sizeof(tracePath));
    if (ok && traceLength > 0 && traceLength < sizeof(tracePath)) {
        InitializeCriticalSection(&g_randLock);
        const addr::Entry* rand = addr_check::Find(addr::CRT_rand);
        if (fopen_s(&g_randTrace, tracePath, "wb") == 0 && rand && hook::Install(*rand, &Rand, &g_rand))
            log::Write("fixed_seed: tracing rand() callers to %s", tracePath);
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
} // namespace opensc4::fixed_seed
