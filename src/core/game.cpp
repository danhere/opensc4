#include "core/game.h"

#include <windows.h>

#include <cstdint>

namespace opensc4::game {
namespace {

constexpr std::uintptr_t kBuildSentinelAddress = 0x6E5000;
constexpr unsigned char kBuild641Sentinel = 0x0F;

} // namespace

int BuildSentinel()
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(kBuildSentinelAddress), &info, sizeof(info)) ||
        info.State != MEM_COMMIT)
        return -1;
    constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                               PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(info.Protect & readable) || (info.Protect & PAGE_GUARD))
        return -1;
    return *reinterpret_cast<const unsigned char*>(kBuildSentinelAddress);
}

bool IsBuild641()
{
    return BuildSentinel() == kBuild641Sentinel;
}

} // namespace opensc4::game
