// determinism.virtual_clock (virtual_clock.cpp): a fixed-step clock for the
// game's main thread, for P4 determinism experiments.
#pragma once

#include <cstdint>

namespace opensc4::virtual_clock {

// Hands the main thread back to real time, offset so its counter keeps
// running forward from the virtual value. The game's save waits on something
// that virtual time breaks (SaveCity hung), so the autotest calls this before
// saving. No effect if the virtual clock is off.
void ReturnToRealTime();

// Called by the autotest at the start of each simulation day: writes and
// resets OPENSC4_VCLOCK_CALLERS's per-caller counts.
void OnNewDay(int day);

// Adds a line to OPENSC4_VCLOCK_TRACE (if on, main thread only): `kind`
// and the first code addresses at or above `stack`. For events that are not
// clock reads, such as determinism.fixed_ticks' slice stopwatch checks ('S').
void TraceEvent(char kind, const void* stack);

// For our replacements of game code with an rdtsc site the clock traps:
// the same value and side effects (count, trace) as that site's read.
// The real counter when the sites are not patched.
uint64_t ReadTsc(const void* stack);

} // namespace opensc4::virtual_clock
