// Unattended in-game test runs: load a city from the current region, run the
// simulation for a while with no user input, log what happened, then quit
// without saving. Driven from a framework tick on the game's main thread.
//
// Environment (all optional except the first):
//   OPENSC4_TEST_CITY     "1" or "auto": the most populated city in the region
//                         the game opens on; otherwise a substring of the
//                         city's save-file path
//   OPENSC4_TEST_SECONDS  real seconds to run the simulation (default 180)
//   OPENSC4_TEST_SPEED    simulation speed as the game numbers it: 0 (slowest)
//                         to 2 (fastest, default). cSC4Simulator ignores any
//                         other value (0x00702430).
//   OPENSC4_TEST_CRASH    "1": once the city is running, crash on purpose inside
//                         a guarded section, to test the crash handler chain
//   OPENSC4_TEST_PRELOAD  DLLs to load (full paths, separated by ';') when
//                         core.dll's director is created, before the game loads
//                         later plugins. For plugin dependencies that installers
//                         put in the game's Apps folder, such as imgui.dll for
//                         render-services: Steam's launch rebuilds PATH, so
//                         PATH cannot supply them.
#pragma once

namespace opensc4::autotest {

bool Enabled();

// Call from the director's PostAppInit hook.
void Start();

// Loads OPENSC4_TEST_PRELOAD. Call when the director is created.
void PreloadLibraries();

} // namespace opensc4::autotest
