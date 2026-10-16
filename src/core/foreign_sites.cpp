#include "core/foreign_sites.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace opensc4::foreign_sites {
namespace {

struct Site {
    std::uintptr_t function; // entry of the game function holding the site
    std::uintptr_t site;     // where the other DLL writes its JMP
};

// The JMP sites inside functions OpenSC4 replaces. Add a row whenever a
// replacement takes over a function the census lists (patch-census.md,
// "Functions and areas we must not replace early").
std::vector<Site>& Sites()
{
    static std::vector<Site> sites = {
        // Network Addon Mod DLL (nam-dll), traffic simulator (docs/traffic.md)
        {0x0070EAB0, 0x0070EAE7}, // SetWay: vertical path separation
        {0x007140E0, 0x00714222}, // OnNetworkBuilding: tunnels for RHW, street, light rail
        {0x0071A860, 0x0071AB60}, // OnCellsChanged: vertical path separation
        // Network Addon Mod DLL, path finder
        {0x006D7960, 0x006D79BF}, // IsGoal: commute-loop fix
        {0x006D7E50, 0x006D7EBE}, // SetUpGoals: commute-loop fix
        {0x006D7E50, 0x006D7EF0}, // SetUpGoals: commute-loop fix
        {0x006D91B0, 0x006D95B1}, // Search: vertical path separation
        // Network Addon Mod DLL, query tool (docs/tools.md)
        {0x004D07C0, 0x004D09D1}, // network traffic text: monorail in the rail table (dual network transit)
    };
    return sites;
}

HMODULE ModuleAt(std::uintptr_t address)
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(address), &module);
    return module;
}

bool Readable(std::uintptr_t address, std::size_t size)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) || info.State != MEM_COMMIT)
        return false;
    constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(info.Protect & readable) || (info.Protect & PAGE_GUARD))
        return false;
    return address + size <= reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
}

// Empty unless `site` holds a JMP or CALL rel32 that leaves its own module.
std::string Describe(std::uintptr_t site)
{
    if (!Readable(site, 5))
        return {};
    const auto* bytes = reinterpret_cast<const unsigned char*>(site);
    if (bytes[0] != 0xE9 && bytes[0] != 0xE8)
        return {};
    std::int32_t rel;
    std::memcpy(&rel, bytes + 1, sizeof(rel));
    const std::uintptr_t destination = site + 5 + rel;
    const HMODULE target = ModuleAt(destination);
    if (target && target == ModuleAt(site))
        return {};
    char path[MAX_PATH] = "allocated memory";
    if (target)
        GetModuleFileNameA(target, path, MAX_PATH);
    const char* slash = std::strrchr(path, '\\');
    char text[MAX_PATH + 64];
    std::snprintf(text, sizeof(text), "%s at 0x%08X (%s to 0x%08X)", slash ? slash + 1 : path,
                  static_cast<unsigned>(site), bytes[0] == 0xE9 ? "JMP" : "CALL",
                  static_cast<unsigned>(destination));
    return text;
}

} // namespace

std::string PatchedInside(std::uintptr_t function)
{
    for (const Site& s : Sites()) {
        if (s.function != function)
            continue;
        if (std::string found = Describe(s.site); !found.empty())
            return found;
    }
    return {};
}

void AddForTests(std::uintptr_t function, std::uintptr_t site)
{
    Sites().push_back({function, site});
}

} // namespace opensc4::foreign_sites
