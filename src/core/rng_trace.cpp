// diagnostics.trace.rng (P4): every draw from the game's random number
// generators goes into a running hash per thread: cRZRandom::Advance (the LCG
// under the integer range draws), cRZRandom::NextFloat and
// cRZRandomSuper::Next (Mersenne Twister). With fixed seeds two runs of the
// same save draw the same numbers in the same order for as long as they take
// the same path, so the first day whose hash differs, and a dump of that day
// (OPENSC4_RNG_DUMP_DAY=<day number>, 0 or all; OPENSC4_RNG_DUMP_FILE), show
// where they part and on which thread (docs/experiments/determinism.md).
//
// Frame-level code (particles, automata, the interface) draws a varying
// number of times; to compare simulations, drop callers whose counts differ
// between two runs that agree, and every generator object they use
// (determinism.md, "Third round").
//
// The hash covers the generator kind, the caller (the return address into the
// game) and the value; not the generator object, whose address differs from
// run to run. The dump adds the first code addresses found further up the
// stack, to name the caller's callers, and the generator object.
#include "core/rng_trace.h"

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

namespace opensc4::rng_trace {
namespace {

constexpr int kSlots = 16;

struct Slot {
    std::atomic<uint64_t> count{0};
    std::atomic<uint32_t> hash{2166136261u}; // written only by the slot's thread
};
Slot g_slots[kSlots];
std::atomic<int> g_nextSlot{1}; // slot 0 is the main thread
std::atomic<bool> g_active{false};
DWORD g_mainThread = 0;
thread_local int t_slot = -1;

std::atomic<bool> g_dumping{false};
int g_dumpDay = -1;
bool g_dumpAll = false;
std::FILE* g_dump = nullptr;
CRITICAL_SECTION g_dumpLock;

uint32_t Fnv(uint32_t h, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    return h;
}

bool IsCode(uint32_t v)
{
    return v >= 0x00401000 && v < 0x00A80000;
}

// `retSlot` points at the return address into the game.
void Record(uint32_t kind, void** retSlot, uint32_t value, void* self)
{
    if (t_slot < 0) {
        t_slot = GetCurrentThreadId() == g_mainThread ? 0 : g_nextSlot.fetch_add(1);
        if (t_slot >= kSlots)
            t_slot = kSlots - 1;
        if (t_slot != 0)
            log::Write("rng: thread %lu draws as t%d (first caller %p)", GetCurrentThreadId(), t_slot, *retSlot);
    }
    const uint32_t ret = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*retSlot));
    Slot& slot = g_slots[t_slot];
    slot.hash.store(Fnv(Fnv(Fnv(slot.hash.load(std::memory_order_relaxed), kind), ret), value),
                    std::memory_order_relaxed);
    const uint64_t n = slot.count.fetch_add(1, std::memory_order_relaxed);
    if (!g_dumping.load(std::memory_order_relaxed))
        return;
    uint32_t up[3] = {};
    int found = 0;
    auto* words = reinterpret_cast<uint32_t*>(retSlot + 1);
    for (int i = 0; i < 64 && found < 3; ++i)
        if (IsCode(words[i]))
            up[found++] = words[i];
    EnterCriticalSection(&g_dumpLock);
    if (g_dump)
        std::fprintf(g_dump, "t%d %llu k%u %08X %08X %08X %08X %08X %p\n", t_slot,
                     static_cast<unsigned long long>(n), kind, ret, up[0], up[1], up[2], value, self);
    LeaveCriticalSection(&g_dumpLock);
}

// The detours are thunks that leave every register as the original left it.
// Callers rely on more than the calling convention promises: cRZRandom::Advance
// changes only EAX and EDX, and a C++ detour that also changed ECX crashed the
// game in a caller that kept `this` in ECX across the call. NextFloat returns
// in ST0, which C++ would round to a double, so for it the generator's new
// state (+4) is recorded instead of the result. RecordRegs does no floating
// point, so the x87 stack survives it.
void* g_advance = nullptr;
void* g_nextFloat = nullptr;
void* g_nextMt = nullptr;

void __cdecl RecordRegs(uint32_t kind, void** retSlot, uint32_t eax, void* self)
{
    Record(kind, retSlot, kind == 1 ? static_cast<uint32_t*>(self)[1] : eax, self);
}

// Stack after the original returns and the three pushes:
// [esp] edx, [+4] ecx, [+8] eax, [+12] self (ecx on entry), [+16] return address.
#define OPENSC4_RNG_THUNK(name, original, kind) \
    __declspec(naked) void name()               \
    {                                           \
        __asm push ecx                          \
        __asm call dword ptr [original]         \
        __asm push eax                          \
        __asm push ecx                          \
        __asm push edx                          \
        __asm lea ecx, [esp + 16]               \
        __asm push dword ptr [esp + 12]         \
        __asm push eax                          \
        __asm push ecx                          \
        __asm push kind                         \
        __asm call RecordRegs                   \
        __asm add esp, 16                       \
        __asm pop edx                           \
        __asm pop ecx                           \
        __asm pop eax                           \
        __asm add esp, 4                        \
        __asm ret                               \
    }

OPENSC4_RNG_THUNK(AdvanceThunk, g_advance, 0)
OPENSC4_RNG_THUNK(NextFloatThunk, g_nextFloat, 1)
OPENSC4_RNG_THUNK(NextMtThunk, g_nextMt, 2)
#undef OPENSC4_RNG_THUNK

bool Install(registry::Mode)
{
    g_mainThread = GetCurrentThreadId();
    InitializeCriticalSection(&g_dumpLock);
    char value[MAX_PATH];
    DWORD length = GetEnvironmentVariableA("OPENSC4_RNG_DUMP_DAY", value, sizeof(value));
    if (length > 0 && length < sizeof(value)) {
        g_dumpAll = lstrcmpA(value, "all") == 0;
        g_dumpDay = g_dumpAll ? 0 : std::atoi(value);
    }
    length = GetEnvironmentVariableA("OPENSC4_RNG_DUMP_FILE", value, sizeof(value));
    if (g_dumpDay >= 0 && length > 0 && length < sizeof(value) && fopen_s(&g_dump, value, "w") != 0)
        g_dump = nullptr;
    // Day 0: from now (before the city loads) to the first new day; "all":
    // the whole run.
    g_dumping = g_dump && g_dumpDay == 0;
    const addr::Entry* advance = addr_check::Find(addr::cRZRandom_Advance);
    const addr::Entry* nextFloat = addr_check::Find(addr::cRZRandom_NextFloat);
    const addr::Entry* nextMt = addr_check::Find(addr::cRZRandomSuper_Next);
    const bool ok = advance && nextFloat && nextMt &&
                    hook::Install(*advance, reinterpret_cast<void*>(&AdvanceThunk), &g_advance) &&
                    hook::Install(*nextMt, reinterpret_cast<void*>(&NextMtThunk), &g_nextMt) &&
                    hook::Install(*nextFloat, reinterpret_cast<void*>(&NextFloatThunk), &g_nextFloat);
    g_active = ok;
    log::Write("rng: tracing %s (main thread %lu, dump day %d%s)", ok ? "on" : "FAILED", g_mainThread, g_dumpDay,
               g_dump ? "" : ", no dump file");
    return ok;
}

const bool g_registered = registry::Register({
    "diagnostics.trace.rng",
    "diagnostics",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cRZRandom_Advance,
    "Running hash per thread of every random number the game draws, for determinism tests",
    false,
    Install,
});

} // namespace

void Describe(char* out, std::size_t size)
{
    if (!size)
        return;
    out[0] = '\0';
    if (!g_active)
        return;
    std::size_t used = static_cast<std::size_t>(std::snprintf(out, size, " | rng"));
    const int slots = g_nextSlot.load() < kSlots ? g_nextSlot.load() : kSlots;
    for (int i = 0; i < slots && used < size; ++i)
        used += static_cast<std::size_t>(std::snprintf(out + used, size - used, " t%d %llu/%08X", i,
                                                       static_cast<unsigned long long>(g_slots[i].count.load()),
                                                       g_slots[i].hash.load()));
}

void OnNewDay(int day)
{
    if (!g_active || !g_dump)
        return;
    const bool dump = g_dumpAll || day == g_dumpDay;
    EnterCriticalSection(&g_dumpLock);
    std::fprintf(g_dump, "day %d%s\n", day, dump ? " (dumping)" : "");
    std::fflush(g_dump);
    LeaveCriticalSection(&g_dumpLock);
    g_dumping = dump;
}

} // namespace opensc4::rng_trace
