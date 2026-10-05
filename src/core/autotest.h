// Unattended in-game test runs: load a city from the current region, run the
// simulation for a while with no user input, log what happened, then quit
// without saving. Driven from a framework tick on the game's main thread.
//
// Environment (all optional except the first):
//   OPENSC4_TEST_CITY     "1" or "auto": the most populated city in the region
//                         the game opens on; otherwise a substring of the
//                         city's save-file path
//   OPENSC4_TEST_SECONDS  real seconds to run the simulation (default 180)
//   OPENSC4_TEST_SPEED    simulation speed, 1-3 (default 3)
#pragma once

namespace opensc4::autotest {

bool Enabled();

// Call from the director's PostAppInit hook.
void Start();

} // namespace opensc4::autotest
