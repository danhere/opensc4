// Our cSC4WeatherSimulator object (docs/weather.md, "Our object"): our
// factory registered under the game's class ID, our constructor,
// destructor, QueryInterface, AddRef and Release, and our own copies of
// the four vtables. The object has the game's layout (0x168 bytes), so
// every method, the game's or ours, works on it; the vtables are copied
// from the game's when the first object is made, after the other
// replacements have patched them. Off, the game's own object is used.
#include "core/director.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <cstdint>
#include <cstring>

namespace opensc4::weather_sim {
namespace {

constexpr uint32_t kClassId = 0x2990C1BC;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidWeather = 0xC6C66245;
constexpr uint32_t kIidListener = 0xE98B2F57;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0x168;
constexpr std::size_t kRefs = 0x14;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// The game's: 0x00AB3610 (deleting destructor in slot 24), 0x00AB35FC,
// 0x00AB35EC, 0x00AB35D4.
constexpr int kSlots = 25, kSlots4 = 4, kSlots8 = 4, kSlotsC = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, kRefs);
}

// As the game's (0x00725720).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidWeather:
    case kIidUnknown: offset = 0; break;
    case kIidListener: offset = 0x4; break;
    case kIidMessageTarget: offset = 0x8; break;
    case kIidSerializable: offset = 0xC; break;
    default: return 0;
    }
    *object = self + offset;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x00725A40): Shutdown and the deleting destructor (slot
// 24) when the last reference goes.
uint32_t __fastcall Release(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, kRefs);
    if (refs > 1)
        return --refs;
    Virtual<uint8_t>(self, 0x10);
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("weather object: destroying %p", static_cast<void*>(self));
    Virtual<void*>(self, 0x60, 1u);
    return 0;
}

void Destroy(char* self);

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall AddRefAt(char* part, void*)
{
    return AddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall ReleaseAt(char* part, void*)
{
    return Release(part - Offset, nullptr);
}

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    At<void**>(self, 0xC) = g_vtableC;
}

void Zero(char* self, std::size_t first, std::size_t end)
{
    std::memset(self + first, 0, end - first);
}

// As the game's constructor (0x00728C40): only the fields it sets.
void Construct(char* self)
{
    using RandomFn = void*(__thiscall*)(void* generator, int32_t seed);
    SetVtables(self);
    At<uint8_t>(self, 0x10) = 0;
    At<uint32_t>(self, 0x14) = 0;
    At<uint8_t>(self, 0x18) = 0;
    At<uint8_t>(self, 0x19) = 0;
    Zero(self, 0x1C, 0x4C);
    At<int32_t>(self, 0x60) = -1;
    At<uint32_t>(self, 0x64) = 0x41C00000; // 24
    At<uint32_t>(self, 0x70) = 0x41A00000; // 20
    At<uint32_t>(self, 0x4C) = 1;
    At<uint32_t>(self, 0x50) = 1;
    At<uint32_t>(self, 0x5C) = 1;
    At<uint32_t>(self, 0x54) = 8;
    At<uint32_t>(self, 0x58) = 8;
    At<uint32_t>(self, 0x68) = 0x3F000000;
    At<uint32_t>(self, 0x6C) = 0x3F000000;
    Zero(self, 0x74, 0x98);
    At<uint8_t>(self, 0x98) = 1;
    At<uint8_t>(self, 0x99) = 1;
    At<uint8_t>(self, 0x9A) = 1;
    Zero(self, 0x9C, 0xC4);
    At<uint32_t>(self, 0xC4) = 1;
    At<uint32_t>(self, 0xC8) = 1;
    At<uint32_t>(self, 0xCC) = 6;
    At<uint32_t>(self, 0xD0) = 0x3F733333;
    At<uint32_t>(self, 0xD4) = 0x3B656042;
    At<uint32_t>(self, 0xD8) = 0x3E800000;
    Zero(self, 0xDC, 0xF4);
    At<uint32_t>(self, 0xF8) = 0x3F000000;
    At<uint32_t>(self, 0xFC) = 0x3F000000;
    At<uint32_t>(self, 0x100) = 0x3ECCCCCD;
    At<uint32_t>(self, 0x104) = 0x43000000;
    At<uint32_t>(self, 0x108) = 0x3DF5C28F;
    At<uint32_t>(self, 0x10C) = 0x40800000;
    At<uint32_t>(self, 0x110) = 0x40;
    At<uint32_t>(self, 0x114) = 0xF;
    const uint32_t wind[] = {0x40400000, 0, 0x3F800000, 0x40400000, 0x3F800000, 0, 0x40400000,
                             0x3F800000, 0, 0x40400000, 0x3F800000, 0};
    std::memcpy(self + 0x118, wind, sizeof wind);
    reinterpret_cast<RandomFn>(addr::SC4Random_Construct)(self + 0x148, -1);
    At<int32_t>(self, 0x160) = -1;
    At<uint32_t>(self, 0x164) = 0x3E800000;
}

// As the game's destructor (0x00728ED0).
void Destroy(char* self)
{
    SetVtables(self);
    reinterpret_cast<ThisFn>(addr::SC4ShortVectorVector_Destroy)(self + 0xE8);
    reinterpret_cast<ThisFn>(addr::SC4ShortVectorVector_Destroy)(self + 0xDC);
    if (void* const p = At<void*>(self, 0xB0))
        Free(p);
    if (void* const p = At<void*>(self, 0x8C))
        Free(p);
    reinterpret_cast<ThisFn>(addr::SC4BitRowVector_Destroy)(self + 0x80);
    reinterpret_cast<ThisFn>(addr::SC4ShortVectorVector_Destroy)(self + 0x74);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4WeatherSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4WeatherSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4WeatherSimulator_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4WeatherSimulator_vftable_C), sizeof(g_vtableC));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[24] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[1] = reinterpret_cast<void*>(&AddRefAt<0xC>);
    g_vtableC[2] = reinterpret_cast<void*>(&ReleaseAt<0xC>);
}

// The factory, as the game's (0x00729310): operator new(0x168), then the
// constructor.
cIGZUnknown* Create()
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    auto* const self = static_cast<char*>(OperatorNew(kSize));
    if (!self)
        return nullptr;
    Construct(self);
    static int created = 0;
    if (created++ < 3)
        log::Write("weather object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool Apply(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registered = registry::Register({
    "simulators.weather.object",
    "simulators/weather",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4WeatherSimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/weather.md)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::weather_sim
