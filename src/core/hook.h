// Entry-only inline detours (MinHook behind a small wrapper).
//
// Rules (docs/architecture.md, "Hooking layer"):
//   - hooks go at function entries listed in the address database, never
//     mid-function;
//   - the entry's expected bytes must still be there: if another DLL already
//     patched the function, or this is a different build, the hook is refused
//     and the owner of the foreign patch is logged;
//   - if another DLL patched a known site inside the function
//     (foreign_sites.h), nothing is installed: the game's function, with
//     that DLL's change, keeps running, `original` receives the function
//     itself, and the call reports success so the rest of a replacement
//     still applies.
#pragma once

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::hook {

// Installs and enables a detour at `target`. `original` receives a pointer
// that calls the original function. `expect`/`expectSize` are the bytes the
// target must start with. Returns false (and logs why) on any failure.
bool InstallAt(const char* name, std::uintptr_t target, const unsigned char* expect, std::size_t expectSize,
               void* detour, void** original);

// Same, for an address database entry (must be kind Function).
bool Install(const addr::Entry& entry, void* detour, void** original);

template <typename Fn>
bool Install(const addr::Entry& entry, Fn detour, Fn* original)
{
    return Install(entry, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
}

template <typename Fn>
bool InstallAt(const char* name, std::uintptr_t target, const unsigned char* expect, std::size_t expectSize,
               Fn detour, Fn* original)
{
    return InstallAt(name, target, expect, expectSize, reinterpret_cast<void*>(detour),
                     reinterpret_cast<void**>(original));
}

// Between BeginBatch and EndBatch, InstallAt creates hooks but only queues
// them, and EndBatch enables them all at once. MinHook suspends every other
// thread of the process to enable hooks, and finding them costs about 40 ms
// on a busy machine, so one by one 138 hooks took 5 s of every game start.
// Nothing may call a hooked function between the two.
void BeginBatch();
void EndBatch();

// Disables and removes every hook installed through this module.
void RemoveAll();

// Number of hooks currently installed.
std::size_t InstalledCount();

// Whether this module has a hook installed at `target`.
bool IsInstalledAt(std::uintptr_t target);

} // namespace opensc4::hook
