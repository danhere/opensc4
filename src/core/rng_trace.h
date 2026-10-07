// diagnostics.trace.rng (rng_trace.cpp): follows every draw from the game's
// random number generators, per thread, to find where two runs of the same
// save part (docs/experiments/determinism.md).
#pragma once

#include <cstddef>

namespace opensc4::rng_trace {

// Writes " | rng t0 <count>/<hash> t1 ..." for the autotest's daily
// fingerprint: per thread (t0 is the main thread, others numbered in the
// order they first draw), the number of draws so far and a running hash of
// (generator kind, caller, value). Writes nothing if the trace is off.
void Describe(char* out, std::size_t size);

// Called by the autotest at the start of each simulation day (from inside the
// simulator's tick). Starts or stops OPENSC4_RNG_DUMP_DAY's dump (0: from
// before the city loads to the first new day; "all": the whole run).
void OnNewDay(int day);

} // namespace opensc4::rng_trace
