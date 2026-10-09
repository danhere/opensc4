// determinism.sim_timers (sim_timers.cpp): the MySim and advisor-system
// stopwatches on simulation time.
#pragma once

namespace opensc4::sim_timers {

// For replacements of code that owns stopwatches: takes `stopwatch` over as
// the original code's first read of it would (the takeover keys on the
// caller's address, which is ours in a replacement). Call it before each
// read; nothing happens when determinism.sim_timers is off.
void Claim(void* stopwatch);

} // namespace opensc4::sim_timers
