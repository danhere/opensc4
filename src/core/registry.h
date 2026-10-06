// Replacement registry (docs/architecture.md, "Replacement registry").
//
// Every replacement of original game behaviour (a GZCOM class, a detour, a
// vtable swap) registers itself here with an id and an apply function. Its
// mode comes from configuration:
//   original  leave the game's code alone (the default for everything)
//   ours      use our implementation
//   compare   run both, log divergences, keep the original's result
//             (only for replacements that support it: pure functions)
//
// Modes are read from opensc4.ini next to core.dll,
//   [replacements]
//   simulators.pollution.proxy = ours
// and can be overridden for a run with OPENSC4_REPLACE, a comma-separated list
// of id=mode pairs. A key ending in ".*" covers every id with that prefix
// (subsystem toggles, e.g. "simulators.* = ours"); an exact id wins over a
// wildcard, and a longer wildcard over a shorter one. Replacements are applied only at fixed points of startup
// (Phase), never while a city is running.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace opensc4::registry {

enum class Mode { Original, Ours, Compare };
enum class Kind { Gzcom, Detour, Vtable, Api }; // Api: through a public game interface, no patching

// When a replacement is applied.
enum class Phase {
    DirectorInit, // core.dll's GZCOM director is created: register class factories here
    PreAppInit,   // services and graphics drivers exist, no video mode or city yet: swap objects here
    PostAppInit,  // all plugins have loaded and patched: install hooks here
    LateInit,     // after every PostAppInit replacement: instrumentation (coverage)
};

struct Replacement {
    const char* id;          // dotted, lowercase: "simulators.pollution.proxy"
    const char* subsystem;   // "simulators/pollution"
    Kind kind;
    Phase phase;
    std::uintptr_t target;   // address database entry, or the class ID for Gzcom
    const char* description;
    bool supportsCompare;
    // Applies the replacement in `mode` (never called with Mode::Original).
    // Returns false if it could not be applied; the game then keeps the original.
    bool (*apply)(Mode mode);
};

// Adds a replacement. Returns true so it can initialise a static:
//   const bool registered = registry::Register({...});
bool Register(const Replacement& replacement);

// Reads the configuration (from `iniPath`, then OPENSC4_REPLACE) and applies
// every replacement of `phase` whose mode is not Original, logging one line
// per replacement. `iniPath` empty: opensc4.ini next to the module.
void Apply(Phase phase, const std::wstring& iniPath = {});

// The mode a replacement ended up in (Original if unknown or not applied).
Mode ModeOf(const char* id);

const char* ToString(Mode mode);

// Number of registered replacements (for tests).
std::size_t Count();

// Forgets every applied mode (for tests).
void ResetForTests();

} // namespace opensc4::registry
