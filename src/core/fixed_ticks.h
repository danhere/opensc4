// determinism.fixed_ticks (fixed_ticks.cpp): the simulator runs a fixed
// number of ticks per frame and its clock does not wait for elapsed time.
#pragma once

#include <cstddef>

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

} // namespace opensc4::fixed_ticks
