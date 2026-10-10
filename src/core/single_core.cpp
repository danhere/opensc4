// fixes.single_core: the multicore crash (docs/crashes.md). Players avoid
// it by running the game on one core (Task Manager affinity, or a shortcut
// with "start /affinity 1"); the game's own reference counts are plain
// increments with no lock (docs/crashes.md, 0x0096DA20), so threads on two
// cores at once can lose an update. Applied at Phase::Attach, before the
// game's static initialisers create any thread, so every thread inherits the
// mask. The core is [fixes] core in opensc4.ini (default: the first core the
// process may use).
#include "core/log.h"
#include "core/registry.h"
#include "core/settings.h"

#include <windows.h>

#include <cstdlib>

namespace opensc4::single_core {
namespace {

bool Apply(registry::Mode)
{
    DWORD_PTR processMask = 0, systemMask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask) || !processMask) {
        log::Write("single_core: no affinity mask (error %lu)", GetLastError());
        return false;
    }
    DWORD_PTR mask = processMask & (~processMask + 1); // lowest allowed core
    const std::string chosen = settings::Get("fixes", "core", "");
    if (!chosen.empty()) {
        const unsigned long core = std::strtoul(chosen.c_str(), nullptr, 10);
        const DWORD_PTR wanted = core < sizeof(DWORD_PTR) * 8 ? DWORD_PTR(1) << core : 0;
        if (wanted & processMask)
            mask = wanted;
        else
            log::Write("single_core: [fixes] core = %s is not a core this process may use; using the first", chosen.c_str());
    }
    if (!SetProcessAffinityMask(GetCurrentProcess(), mask)) {
        log::Write("single_core: SetProcessAffinityMask failed (error %lu)", GetLastError());
        return false;
    }
    unsigned long index = 0;
    while (!((mask >> index) & 1))
        ++index;
    log::Write("single_core: the game runs on core %lu only (allowed mask was %llX)", index,
               static_cast<unsigned long long>(processMask));
    return true;
}

const bool g_registered = registry::Register({
    "fixes.single_core",
    "fixes",
    registry::Kind::Api,
    registry::Phase::Attach,
    0,
    "Runs the game on one CPU core, the community's fix for the multicore crash",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::single_core
