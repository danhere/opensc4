// What the framework's classes share on the game's layouts (docs/
// framework.md): the game's STL allocator, counted references, the lock
// object and the links of a list.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::framework {

inline void* Allocate(uint32_t size)
{
    return reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(size);
}

inline void Free(void* memory)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory);
}

inline void AddRef(void* object)
{
    if (object)
        Virtual<uint32_t>(object, 0x4);
}

inline void Release(void* object)
{
    if (object)
        Virtual<uint32_t>(object, 0x8);
}

// A lock object (8 bytes: slot 1 locks, slot 2 unlocks), held while this
// lives.
struct Lock {
    explicit Lock(void* lock) : lock_(lock) { Virtual<void>(lock_, 0x4); }
    ~Lock() { Virtual<uint32_t>(lock_, 0x8); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    void* lock_;
};

// A list is a pointer to its sentinel node; a node is {next, previous,
// value...}.
inline constexpr std::size_t kNext = 0x0;
inline constexpr std::size_t kPrevious = 0x4;
inline constexpr std::size_t kValue = 0x8;

inline void LinkBefore(char* position, char* node)
{
    char* const previous = At<char*>(position, kPrevious);
    At<char*>(node, kNext) = position;
    At<char*>(node, kPrevious) = previous;
    At<char*>(previous, kNext) = node;
    At<char*>(position, kPrevious) = node;
}

inline void Unlink(char* node)
{
    char* const next = At<char*>(node, kNext);
    char* const previous = At<char*>(node, kPrevious);
    At<char*>(previous, kNext) = next;
    At<char*>(next, kPrevious) = previous;
}

} // namespace opensc4::framework
