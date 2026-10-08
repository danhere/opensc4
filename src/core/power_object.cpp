// Our cSC4PowerSimulator object (docs/power.md, "Our object"): our factory
// registered under the game's class ID, our constructor, destructor,
// QueryInterface, AddRef and Release, and our own copies of the three
// vtables. The object has the game's layout (0x3C8 bytes, the same fields),
// so every method, the game's or ours, works on it; the vtables are copied
// from the game's when the first object is made, after the other
// replacements have patched them, so each method is the game's or ours as
// configured. Off, the game's own object is used (patched as configured).
#include "core/director.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <cstdint>
#include <cstring>

namespace opensc4::power_sim {
namespace {

constexpr uint32_t kClassId = 0x0990C005;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidPower = 0xE9E7BFF5;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0x3C8;
constexpr std::size_t kRefs = 0x10;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
using RandomFn = void*(__thiscall*)(void* generator, int32_t seed);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// The three vtables (the game's: 0x00AB28B8 with the deleting destructor in
// slot 52, 0x00AB28A4, 0x00AB288C; packed, no RTTI slots).
constexpr int kSlots = 53, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

// --- Identity --------------------------------------------------------------

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, kRefs);
}

// As the game's (0x006EABC0): the reference through the primary vtable.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidPower:
    case kIidUnknown: offset = 0; break;
    case kIidMessageTarget: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self + offset;
    Virtual<uint32_t>(self, 0x4); // AddRef
    return 1;
}

void Destroy(char* self);

// The count stays at 1 when the last reference goes: Shutdown, the
// destructor and operator delete, as the game's Release (0x006EB190) and
// deleting destructor (slot 52).
uint32_t __fastcall Release(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, kRefs);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10); // Shutdown
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("power object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    OperatorDelete(self);
    return 0;
}

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// The other interfaces' first three slots: this adjusted, then the above.
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

// --- Construction and destruction -------------------------------------------

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
}

void Zero(char* self, std::size_t first, std::size_t end)
{
    std::memset(self + first, 0, end - first);
}

// As the game's constructor (0x006EF7B0): only the fields it sets; the rest
// stay as operator new returned them.
void Construct(char* self)
{
    SetVtables(self);
    At<uint8_t>(self, 0xC) = 0;
    At<uint8_t>(self, 0xD) = 0;
    At<uint32_t>(self, 0x10) = 0;
    At<uint8_t>(self, 0x14) = 0;
    Zero(self, 0x18, 0x64);
    At<uint32_t>(self, 0x64) = Global<uint32_t>(addr::PowerTuning_DefaultPercentage);
    Zero(self, 0x68, 0x90);
    At<void*>(self, 0x298) = nullptr;
    auto** const plants = static_cast<void**>(Allocate(0x10));
    plants[0] = plants;
    plants[1] = plants;
    At<void*>(self, 0x298) = plants;
    Zero(self, 0x29C, 0x2A8);
    At<float>(self, 0x2AC) = 1.0f;
    At<uint32_t>(self, 0x2B0) = 0;
    At<float>(self, 0x2B4) = 1.0f;
    Zero(self, 0x2B8, 0x2D8);
    At<uint32_t>(self, 0x2DC) = 0;
    Zero(self, 0x308, 0x314);
    reinterpret_cast<RandomFn>(addr::SC4Random_Construct)(self + 0x318, -1);
    At<uint8_t>(self, 0x330) = 0;
    At<uint32_t>(self, 0x334) = 0;
    At<uint8_t>(self, 0x33A) = 0;
    Zero(self, 0x33C, 0x34C);
    Zero(self, 0x354, 0x360);
    // An empty string (cIGZString over a buffer of 8).
    auto* const text = static_cast<char*>(Allocate(8));
    At<char*>(self, 0x354) = text;
    At<char*>(self, 0x358) = text;
    At<char*>(self, 0x35C) = text + 8;
    text[0] = 0;
    At<uint32_t>(self, 0x350) = static_cast<uint32_t>(addr::SC4String_Vtable);
    Zero(self, 0x360, 0x370);
    At<uint32_t>(self, 0x374) = 0;
    Zero(self, 0x378, 0x3C8); // the five curves
    Zero(self, 0x2E0, 0x308);
}

// As the game's destructor (0x006EFAB0).
void Destroy(char* self)
{
    SetVtables(self);
    for (const std::size_t curve : {0x3B8, 0x3A8, 0x398, 0x388, 0x378})
        if (void* const points = At<void*>(self, curve))
            Free(points);
    At<uint32_t>(self, 0x350) = static_cast<uint32_t>(addr::SC4String_Vtable);
    if (void* const text = At<void*>(self, 0x354))
        Free(text);
    if (void* const pieces = At<void*>(self, 0x29C))
        Free(pieces);
    reinterpret_cast<ThisFn>(addr::SC4PairList_Destroy)(self + 0x298);
    reinterpret_cast<ThisFn>(addr::SC4ShortVectorVector_Destroy)(self + 0x84);
}

// The vtables: the game's as they are now (with whatever the other
// replacements swapped in), with our identity functions.
void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4PowerSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4PowerSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4PowerSimulator_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[52] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
}

// The factory, as the game's (0x006F00D0): operator new(0x3C8), then the
// constructor; GZCOM factories return objects with no references.
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
        log::Write("power object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool Apply(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registered = registry::Register({
    "simulators.power.object",
    "simulators/power",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4PowerSimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/power.md)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::power_sim
