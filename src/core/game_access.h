// Reading the game's objects from our code: fields by offset, globals by
// address, virtual functions by vtable offset (build 641 layouts; see the
// simulators' docs for what each offset is).
#pragma once

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4 {

template <typename T>
inline T& At(void* object, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

template <typename T>
inline T& Global(std::uintptr_t address)
{
    return *reinterpret_cast<T*>(address);
}

// Calls the virtual function at byte offset `offset` of `object`'s vtable.
template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t offset, Args... args)
{
    using Fn = R(__thiscall*)(void*, Args...);
    const auto fn = reinterpret_cast<Fn>((*static_cast<void***>(object))[offset / sizeof(void*)]);
    return fn(object, args...);
}

// The node allocator of the game's lists (SC4List), fetched from the framework on first
// use as the game's list helpers (0x00413DD0, 0x00413F70) do.
inline void* ListNodeAllocator()
{
    using GetFrameworkFn = void*(__cdecl*)();
    using ReleaseRefFn = void(__thiscall*)(void** ref);
    void*& allocator = Global<void*>(addr::SC4List_NodeAllocator2);
    if (!allocator) {
        void* fetched = nullptr;
        if (void* framework = reinterpret_cast<GetFrameworkFn>(addr::RZGetFramework)())
            Virtual<uint8_t>(framework, 0x14, 0x3AE4BEA3u, 0x3AE4BEABu, &fetched);
        allocator = fetched;
        reinterpret_cast<ReleaseRefFn>(addr::SC4_ReleaseRef)(&fetched);
    }
    return allocator;
}

} // namespace opensc4
