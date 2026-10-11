// The map window's occupant filter (class 0xCA0E3852, 0x30 bytes, constructor
// 0x0079FE60, factory 0x007A0420, vtables 0x00AB7E44 and, for the
// serialisable part at +4, 0x00AB7E2C): what a map layer shows of the city's
// occupants. The one function of the map window's third part (0x007A2150 to
// 0x007A42D0) that is not the map's picture drawing:
//
//   - Read (the serialisable part's slot 4): the filter loaded from a stream.
//
// Replacement, off by default:
//
//   ui.map.part3: the function above.
//
// The rest of the range is the map's picture drawing and stays the game's
// (docs/ui.md): 0x007A2380, 0x007A2740, 0x007A2F60 (which runs to 0x007A4884
// and holds the list's 0x007A32B0 and 0x007A3EA0) and the three grid
// converters 0x007A2AE0, 0x007A2C70 and 0x007A2E00 that only it calls.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::ui {
namespace {

// Offsets from the serialisable part (the object's +4).
namespace filter {
constexpr std::size_t kTypes = 0x8;      // vector of 32-bit values
constexpr std::size_t kNumbers = 0x14;   // vector of 32-bit values
constexpr std::size_t kWater = 0x20;     // the network manager's water pipe filter (slot 18), or null
constexpr std::size_t kSubway = 0x24;    // the network manager's subway filter (slot 19), or null
constexpr std::size_t kFlagA = 0x28;
constexpr std::size_t kFlagB = 0x29;
constexpr std::size_t kFlagC = 0x2A;
} // namespace filter

constexpr uint16_t kVersion = 2;

struct Int32Vector {
    int32_t* begin;
    int32_t* end;
    int32_t* capacity;
};

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void* NetworkManager()
{
    using Fn = void*(__cdecl*)();
    return reinterpret_cast<Fn>(addr::SC4_GetNetworkManager)();
}

// The game's inlined reference holder: assign.
void Assign(char* self, std::size_t offset, void* object)
{
    void* const old = At<void*>(self, offset);
    if (object == old)
        return;
    if (object)
        AddRef(object);
    At<void*>(self, offset) = object;
    if (old)
        Release(old);
}

// The game's inlined reference holder: clear.
void Clear(char* self, std::size_t offset)
{
    void* const old = At<void*>(self, offset);
    if (!old)
        return;
    At<void*>(self, offset) = nullptr;
    Release(old);
}

// The game's 0x007A2150. self is the serialisable part (the object's +4).
uint32_t __fastcall Read(char* self, void*, void* stream)
{
    // The original leaves the version and the count unset before the stream
    // fills them.
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != kVersion)
        return 0;

    uint32_t index = 0;
    Virtual<uint8_t>(stream, 0x20, &index);
    int32_t value = 0;
    Int32Vector& types = At<Int32Vector>(self, filter::kTypes);
    using ResizeFn = void(__thiscall*)(void*, uint32_t, int32_t*);
    reinterpret_cast<ResizeFn>(addr::SC4Uint32Vector_Resize)(&types, index, &value);
    for (index = 0; index < static_cast<uint32_t>(types.end - types.begin); ++index)
        Virtual<uint8_t>(stream, 0x24, &types.begin[index]);

    Virtual<uint8_t>(stream, 0x20, &index);
    Int32Vector& numbers = At<Int32Vector>(self, filter::kNumbers);
    value = 0;
    const uint32_t size = static_cast<uint32_t>(numbers.end - numbers.begin);
    if (index < size) {
        numbers.end = numbers.begin + index;
    } else {
        using InsertFn = void(__thiscall*)(void*, int32_t*, uint32_t, int32_t*);
        reinterpret_cast<InsertFn>(addr::SC4Uint32Vector_InsertFill)(&numbers, numbers.end, index - size, &value);
    }
    for (index = 0; index < static_cast<uint32_t>(numbers.end - numbers.begin); ++index) {
        Virtual<uint8_t>(stream, 0x24, &value);
        numbers.begin[index] = value;
    }

    // The original reads the bytes into its argument's slot: a byte the
    // stream does not fill keeps what was there.
    uint32_t slot = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x14, &slot);
    At<uint8_t>(self, filter::kFlagA) = static_cast<uint8_t>(slot) != 0;
    Virtual<uint8_t>(stream, 0x14, &slot);
    At<uint8_t>(self, filter::kFlagB) = static_cast<uint8_t>(slot) != 0;

    Virtual<uint8_t>(stream, 0x14, &slot);
    if (static_cast<uint8_t>(slot) == 0)
        Clear(self, filter::kWater);
    else
        Assign(self, filter::kWater, Virtual<void*>(NetworkManager(), 0x48));

    Virtual<uint8_t>(stream, 0x14, &slot);
    if (static_cast<uint8_t>(slot) == 0)
        Clear(self, filter::kSubway);
    else
        Assign(self, filter::kSubway, Virtual<void*>(NetworkManager(), 0x4C));

    Virtual<uint8_t>(stream, 0x14, &slot);
    At<uint8_t>(self, filter::kFlagC) = static_cast<uint8_t>(slot) != 0;

    using BaseFn = uint32_t(__thiscall*)(void*, void*);
    return reinterpret_cast<BaseFn>(addr::SC4OccupantFilter_Read)(self, stream);
}

bool InstallMap3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[1] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7A2150, reinterpret_cast<void*>(&Read)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_map3Registered = registry::Register({
    "ui.map.part3",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7A2150,
    "Our map window's occupant filter loaded from a stream (docs/ui.md)",
    false,
    InstallMap3,
});

} // namespace
} // namespace opensc4::ui
