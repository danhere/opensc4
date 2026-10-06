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

#include <atomic>
#include <cstdint>
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

uint32_t NextSeed(uint32_t requested, const char* which)
{
    const uint32_t n = g_calls.fetch_add(1);
    const uint32_t seed = Mix(g_base + n);
    // The first few show what the game asked for; after that it is noise.
    if (n < 8)
        log::Write("fixed_seed: %s seed #%u: 0x%08X -> 0x%08X", which, n, requested, seed);
    return seed;
}

// The detours only compute a number and tail-call the original: nothing here
// can fault, so no SEH guard (crash.h).
void __fastcall SeedLcg(void* self, void* edx, uint32_t seed)
{
    g_seedLcg(self, edx, NextSeed(seed, "cRZRandom"));
}

void __fastcall SeedMt(void* self, void* edx, uint32_t seed)
{
    g_seedMt(self, edx, NextSeed(seed, "cRZRandomSuper"));
}

bool Apply(registry::Mode)
{
    char value[32];
    const DWORD length = GetEnvironmentVariableA("OPENSC4_SEED", value, sizeof(value));
    if (length > 0 && length < sizeof(value))
        g_base = static_cast<uint32_t>(std::strtoul(value, nullptr, 0));
    const addr::Entry* lcg = addr_check::Find(addr::cRZRandom_Seed);
    const addr::Entry* mt = addr_check::Find(addr::cRZRandomSuper_Seed);
    const bool ok = lcg && mt && hook::Install(*lcg, &SeedLcg, &g_seedLcg) && hook::Install(*mt, &SeedMt, &g_seedMt);
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
