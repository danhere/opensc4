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

// For our translations of the crew's code (crew_work.cpp and the workers'
// part): the stopwatch calls the crew makes, served exactly as the game's
// call from the crew's range (0x00605490 to 0x00608C50) is under the switch:
// call by call on the simulation counter, without taking the stopwatch over.
// Each calls the method's original code (behind this switch's thunk), so
// the answer is the game's own arithmetic on the counter: Elapsed is
// GZStopwatch::ElapsedMilliseconds' mode-4 product of the counter's ticks
// and a float factor, truncated, which for n checks of 10 ms can give 10n - 1
// (as the game's call from Tick at 0x00607CCB gets). With the switch off,
// or its hook on that method not in, each calls the game's function at its
// address (GZStopwatch::Start 0x0089058F, GZStopwatch::ElapsedMilliseconds
// 0x00890198, SC4Stopwatch::Stop 0x008905F0), as the crew's code would.
void CrewStopwatchStart(void* stopwatch);
uint32_t CrewStopwatchElapsed(void* stopwatch);
void CrewStopwatchStop(void* stopwatch);

} // namespace opensc4::sim_timers
