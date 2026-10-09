// Our cSC4CivicBuildingSimulator (build 641): what the parts share
// (docs/civic.md).
#pragma once

#include "core/game_access.h"

#include <cstddef>
#include <cstdint>

namespace opensc4::civic_sim {

// Fields of cSC4CivicBuildingSimulator (offsets from the object's start;
// 0x48 bytes).
namespace field {
constexpr std::size_t kRefs = 0x0C;
constexpr std::size_t kInitialised = 0x10;  // uint8
constexpr std::size_t kDepartment = 0x14;   // the budget department Init makes (a reference; saved)
constexpr std::size_t kExcluded = 0x18;     // hash set of uint32 (property 0xEA2E078B of the city's buildings)
constexpr std::size_t kStatuses = 0x2C;     // hash map: building instance -> Status (slots 6, 7)
constexpr std::size_t kZooBuildings = 0x40; // list of occupants with flag 0x1917 (the ape escape draw)
constexpr std::size_t kLandmarks = 0x44;    // list of occupants with a department (property 0xAA59670C)
} // namespace field

// A hash table as the game's (SGI hashtable): a word, the buckets (begin,
// end, capacity), the count. Nodes: next, key, value.
namespace table {
constexpr std::size_t kBuckets = 0x4;
constexpr std::size_t kBucketsEnd = 0x8;
constexpr std::size_t kCount = 0x10;
} // namespace table

// The calls the game makes by address from these functions.
template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    using Fn = R(__thiscall*)(void*, Args...);
    return reinterpret_cast<Fn>(address)(self, args...);
}

template <typename R, typename... Args>
R CallC(std::uintptr_t address, Args... args)
{
    using Fn = R(__cdecl*)(Args...);
    return reinterpret_cast<Fn>(address)(args...);
}

void FirstCall(const char* name, bool& logged);

} // namespace opensc4::civic_sim
