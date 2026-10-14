// determinism.sim_timers (sim_timers.cpp): the MySim, advisor-system and
// construction crew stopwatches on simulation time.
#pragma once

#include <cstdint>

namespace opensc4::sim_timers {

// For replacements of code that owns stopwatches: takes `stopwatch` over as
// the original code's first read of it would (the takeover keys on the
// caller's address, which is ours in a replacement). Call it before each
// read; nothing happens when determinism.sim_timers is off.
void Claim(void* stopwatch);

// The frame time a construction crew's Tick (0x00607C30) is given in place
// of the frame message's `frameMicroseconds`: the simulation time of the
// last frame (10 ms a check of determinism.fixed_ticks), at most the frame
// service's 500 ms, 0 for a frame that ran no simulation. The switch hooks
// the game's Tick to pass it; a replacement of Tick calls this itself (and
// the hook must then not be on the same entry). `frameMicroseconds` comes
// back unchanged when the switch is off.
uint32_t CrewFrameMicroseconds(uint32_t frameMicroseconds);

} // namespace opensc4::sim_timers
