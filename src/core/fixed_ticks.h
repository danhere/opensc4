// determinism.fixed_ticks (fixed_ticks.cpp): the simulator runs a fixed
// number of ticks per frame and its clock does not wait for elapsed time.
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::fixed_ticks {

// Writes " | checks <n>" for the autotest's daily fingerprint: the slice
// stopwatch checks made so far, including the running slice's (called from
// inside it, this pins a moment to a tick). The number of slices is not
// included: slices also run while the city loads, paused, and their count
// varies. Writes nothing if determinism.fixed_ticks is off.
void Describe(char* out, std::size_t size);

// True while a simulation time slice runs (main thread): code running now is
// simulation ticks, not rendering or the interface.
bool InSlice();

// Slice stopwatch checks so far (one per simulation tick), and for
// determinism.sim_timers: the original
// GZStopwatch::ElapsedMilliseconds and where the stopwatches it does not
// own go instead of it.
uint64_t Checks();
void* ElapsedOriginal();
void SetElapsedPass(void* thunk);

// [determinism] pace = realtime: slices are held back to follow a real-time
// schedule for the game speed (play with a deterministic simulation).
bool Paced();

// Logs how many slices pacing ran and held back.
void LogPace();

} // namespace opensc4::fixed_ticks
