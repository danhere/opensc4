// Trace hooks: entry detours that log calls into the game and pass them
// through unchanged. Off unless OPENSC4_TRACE=1. They exercise the hook layer
// on real game functions and record what the game does during test runs.
#pragma once

namespace opensc4::trace {

bool Enabled();

// Installs every trace hook. Call once, after all plugins have loaded.
void Install();

} // namespace opensc4::trace
