#include "core/hook.h"

#include "core/address_check.h"
#include "core/foreign_sites.h"
#include "core/log.h"

#include <MinHook.h>

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace opensc4::hook {
namespace {

std::mutex g_mutex;
bool g_initialized = false;
bool g_batch = false;
std::vector<std::uintptr_t> g_installed;

bool EnsureInitialized()
{
    if (g_initialized)
        return true;
    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        log::Write("hook: MH_Initialize failed: %s", MH_StatusToString(status));
        return false;
    }
    g_initialized = true;
    return true;
}

} // namespace

bool InstallAt(const char* name, std::uintptr_t target, const unsigned char* expect, std::size_t expectSize,
               void* detour, void** original)
{
    std::lock_guard lock(g_mutex);

    const std::string found = addr_check::DescribeMismatch(target, expect, expectSize);
    if (!found.empty()) {
        log::Write("hook: refusing %s at 0x%08X: %s", name, static_cast<unsigned>(target), found.c_str());
        return false;
    }
    // Another DLL changed the middle of this function (foreign_sites.h):
    // leave it to the game, so that change stays in effect, and hand back
    // the game's own function as the original.
    if (const std::string patched = foreign_sites::PatchedInside(target); !patched.empty()) {
        log::Write("hook: leaving %s at 0x%08X to the game: patched inside by %s", name, static_cast<unsigned>(target),
                   patched.c_str());
        if (original)
            *original = reinterpret_cast<void*>(target);
        return true;
    }
    if (!EnsureInitialized())
        return false;

    void* const address = reinterpret_cast<void*>(target);
    MH_STATUS status = MH_CreateHook(address, detour, original);
    if (status == MH_OK)
        status = g_batch ? MH_QueueEnableHook(address) : MH_EnableHook(address);
    if (status != MH_OK) {
        log::Write("hook: %s at 0x%08X failed: %s", name, static_cast<unsigned>(target), MH_StatusToString(status));
        MH_RemoveHook(address);
        return false;
    }
    g_installed.push_back(target);
    log::Write("hook: installed %s at 0x%08X", name, static_cast<unsigned>(target));
    return true;
}

bool Install(const addr::Entry& entry, void* detour, void** original)
{
    if (entry.kind != addr::Kind::Function) {
        log::Write("hook: refusing %s: not a function entry", entry.name);
        return false;
    }
    if (entry.expectSize < 5) {
        log::Write("hook: refusing %s: the database needs at least 5 expected bytes to patch safely", entry.name);
        return false;
    }
    return InstallAt(entry.name, entry.address, entry.expect, entry.expectSize, detour, original);
}

void BeginBatch()
{
    std::lock_guard lock(g_mutex);
    g_batch = true;
}

void EndBatch()
{
    std::lock_guard lock(g_mutex);
    g_batch = false;
    if (!g_initialized)
        return;
    const MH_STATUS status = MH_ApplyQueued();
    if (status != MH_OK)
        log::Write("hook: enabling the queued hooks failed: %s", MH_StatusToString(status));
}

void RemoveAll()
{
    std::lock_guard lock(g_mutex);
    for (const std::uintptr_t target : g_installed) {
        MH_DisableHook(reinterpret_cast<void*>(target));
        MH_RemoveHook(reinterpret_cast<void*>(target));
    }
    g_installed.clear();
}

bool IsInstalledAt(std::uintptr_t target)
{
    std::lock_guard lock(g_mutex);
    return std::find(g_installed.begin(), g_installed.end(), target) != g_installed.end();
}

std::size_t InstalledCount()
{
    std::lock_guard lock(g_mutex);
    return g_installed.size();
}

} // namespace opensc4::hook
