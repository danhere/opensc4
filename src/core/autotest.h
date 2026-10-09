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
//   OPENSC4_TEST_SHOTS    a folder (Windows path): instead of running the
//                         simulation, keep it paused and save the frame at
//                         every zoom (1-5) and rotation (0-3) there as
//                         z<zoom>-r<rotation>.png, then quit. The camera
//                         starts where the save left it, so runs line up.
//                         Compare runs with tools/shotdiff.py.
//   OPENSC4_TEST_DAYS     game days to run instead of real seconds: once the
//                         simulation's day number reaches the start day plus
//                         this, pause it (0: right after loading, before any
//                         simulation time passes)
//   OPENSC4_TEST_SAVE     "1": when the run ends, save the city in place, as
//                         the game's own Save does, then quit. This rewrites
//                         the city's file and its neighbours' in the region,
//                         so use it only in a disposable user folder
//                         (scripts/run-save-test.sh). P4 determinism tests.
//   OPENSC4_TEST_QUICK    "1": skip what a test run does not need: wait 1 s
//                         instead of 5 on the region view, and save with the
//                         game's fast save, which leaves out the region
//                         thumbnails (~1 s instead of ~11). Parity runs
//                         (scripts/run-parity.sh) use it by default.
//   OPENSC4_TEST_FINGERPRINT  "1": while running, log a fingerprint of the
//                         simulation every game day (population, funds, lot
//                         count, checksums of the traffic, land value and
//                         pollution grids), to find when two runs diverge
//                         (docs/experiments/determinism.md)
//   OPENSC4_TEST_SHOT_SETTLE  milliseconds to wait after changing the view
//                         before saving the frame (default 3000)
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
