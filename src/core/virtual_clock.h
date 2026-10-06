// determinism.virtual_clock (virtual_clock.cpp): a fixed-step clock for the
// game's main thread, for P4 determinism experiments.
#pragma once

namespace opensc4::virtual_clock {

// Hands the main thread back to real time, offset so its counter keeps
// running forward from the virtual value. The game's save waits on something
// that virtual time breaks (SaveCity hung), so the autotest calls this before
// saving. No effect if the virtual clock is off.
void ReturnToRealTime();

} // namespace opensc4::virtual_clock
