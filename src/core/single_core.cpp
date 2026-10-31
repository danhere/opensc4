// fixes.single_core: the multicore crash (docs/crashes.md). Players avoid
// it by running the game on one core (Task Manager affinity, or a shortcut
// with "start /affinity 1"); the game's own reference counts are plain
// increments with no lock (docs/crashes.md, 0x0096DA20), so threads on two
// cores at once can lose an update. Applied at Phase::Attach, before the
// game's static initialisers create any thread, so every thread inherits the
// mask. The core is [fixes] core in opensc4.ini. Without it each copy of the
// game takes the first allowed core no other running copy holds (a named
// mutex per core, kept until the process ends), so several copies at once
// (harness.multi_instance, parallel parity jobs) don't all share core 0. When
// every core is held it falls back to the first.
#include "core/log.h"
#include "core/registry.h"
#include "core/settings.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace opensc4::single_core {
namespace {

// The first allowed core no other copy of the game holds, claimed for the
// rest of this process by a named mutex (closed by Windows on exit, crash
// included); 0 when every allowed core is held.
DWORD_PTR ClaimFreeCore(DWORD_PTR processMask)
{
    for (unsigned index = 0; index < sizeof(DWORD_PTR) * 8; ++index) {
        const DWORD_PTR bit = DWORD_PTR(1) << index;
        if (!(processMask & bit))
            continue;
        char name[64];
        std::snprintf(name, sizeof(name), "Local\\OpenSC4 core %u", index);
        const HANDLE claim = CreateMutexA(nullptr, FALSE, name);
        if (!claim)
            continue;
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return bit; // kept open on purpose: the claim lasts until exit
        CloseHandle(claim);
    }
    return 0;
}

bool Apply(registry::Mode)
{
    DWORD_PTR processMask = 0, systemMask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask) || !processMask) {
        log::Write("single_core: no affinity mask (error %lu)", GetLastError());
        return false;
    }
    DWORD_PTR mask = processMask & (~processMask + 1); // lowest allowed core
    const std::string chosen = settings::Get("fixes", "core", "");
    if (chosen.empty()) {
        if (const DWORD_PTR free = ClaimFreeCore(processMask))
            mask = free;
        else
            log::Write("single_core: every allowed core is held by another copy; sharing the first");
    } else {
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
    true,
    true, // a fix of the original's bug: always on (registry.h)
});

} // namespace
} // namespace opensc4::single_core
