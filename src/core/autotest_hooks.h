// What each simulator adds to the autotest (src/core/autotest.h): its part of
// the daily fingerprint line and its OPENSC4_TEST_* switches. Each simulator
// registers its own from its <sim>_autotest.cpp with a static registrar, so
// simulators worked on in parallel never edit the same lines of autotest.cpp.
//
// `order` places a hook among the others of its kind (lowest first; ties by
// name). Fingerprint parts keep the order the line had when they lived in
// autotest.cpp (power 10, water 20, ..., civic 130). A new simulator picks an
// unused number; parts can be moved freely, but two runs only compare when
// both come from builds with the same order.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <cISC4SimGrid.h>

class cISC4City;
class cISC4Region;

namespace opensc4::autotest {

// Appends " | <name> ..." (or nothing, or " | <name> none") to `line`.
using FingerprintFn = void (*)(cISC4City* city, std::string& line);

// Runs once, right after the city has loaded.
using LoadHookFn = void (*)(cISC4City* city);

// Runs once with the region open, just before the city is loaded from it
// (the region screen is still up then).
using RegionHookFn = void (*)(cISC4Region* region);

// The new day, in the simulator's tick. `first` is the run's start day;
// `counted` is false when the run is timed in real seconds, not game days
// (no day hook does anything then).
struct Day {
    cISC4City* city;
    int32_t day;
    int32_t first;
    bool counted;
};
using DayHookFn = void (*)(const Day& day);

void RegisterFingerprintPart(const char* name, int order, FingerprintFn fn);
void RegisterLoadHook(const char* name, int order, LoadHookFn fn);
void RegisterRegionHook(const char* name, int order, RegionHookFn fn);
void RegisterDayHook(const char* name, int order, DayHookFn fn);

struct FingerprintPart {
    FingerprintPart(const char* name, int order, FingerprintFn fn) { RegisterFingerprintPart(name, order, fn); }
};
struct LoadHook {
    LoadHook(const char* name, int order, LoadHookFn fn) { RegisterLoadHook(name, order, fn); }
};
struct RegionHook {
    RegionHook(const char* name, int order, RegionHookFn fn) { RegisterRegionHook(name, order, fn); }
};
struct DayHook {
    DayHook(const char* name, int order, DayHookFn fn) { RegisterDayHook(name, order, fn); }
};

// The value of an environment variable; empty when unset.
std::string Env(const char* name);
int EnvInt(const char* name, int fallback);

// `name`=<n>: true on the n-th day of a run counted in days.
bool OnRunDay(const Day& day, const char* name);
// `name`=<n>: true every n days of a run counted in days (not on its first).
bool EveryRunDays(const Day& day, const char* name);

// Runs `run(context)` with the window manager's DoModalWin replaced by one
// that calls `player(window, depth)` and answers what it answers (as
// ui_autotest.cpp's dialogs do). False when there is no window manager.
bool RunModalPlayer(uint32_t (*player)(void* window, int depth), void (*run)(void* context), void* context);

// A message of the given type with void 1 = object and data 2/3, to a
// simulator's cIGZMessageTarget2 at +0x4 (its slot 3).
void Deliver(void* simulator, uint32_t type, void* object, uint32_t data2, uint32_t data3);

// FNV-1a over every tract value of a simulation grid; 0 for no grid.
template <typename T>
uint32_t GridChecksum(cISC4SimGrid<T>* grid)
{
    if (!grid)
        return 0;
    uint32_t h = 2166136261u;
    const int32_t nx = grid->GetTractCountX(), nz = grid->GetTractCountZ();
    for (int32_t z = 0; z < nz; ++z)
        for (int32_t x = 0; x < nx; ++x) {
            const T v = grid->GetTractValue(x, z);
            const auto* b = reinterpret_cast<const uint8_t*>(&v);
            for (size_t i = 0; i < sizeof v; ++i)
                h = (h ^ b[i]) * 16777619u;
        }
    return h;
}

// FNV-1a over `size` bytes.
inline uint32_t BytesChecksum(const void* data, size_t size)
{
    uint32_t h = 2166136261u;
    const auto* b = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
        h = (h ^ b[i]) * 16777619u;
    return h;
}

} // namespace opensc4::autotest
