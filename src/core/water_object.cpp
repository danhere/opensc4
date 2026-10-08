// Our cSC4PlumbingSimulator object (docs/water.md, "Our object"): our factory
// registered under the game's class ID, our constructor, destructor,
// QueryInterface, AddRef and Release, and our own copies of the three
// vtables. The object has the game's layout (0x1C8 bytes, the same fields),
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

namespace opensc4::water_sim {
namespace {

constexpr uint32_t kClassId = 0x0990C075;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidWater = 0x47782CCC;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0x1C8;
constexpr std::size_t kRefs = 0x10;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// The three vtables (the game's: 0x00AB2228 with the deleting destructor in
// slot 29, 0x00AB2214, 0x00AB21FC; packed, no RTTI slots).
constexpr int kSlots = 30, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

// --- Identity --------------------------------------------------------------

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, kRefs);
}

// As the game's (0x006DA3C0): the reference through the primary vtable.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidWater:
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
// destructor and operator delete, as the game's Release (0x006DA610) and
// deleting destructor (slot 29).
uint32_t __fastcall Release(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, kRefs);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10); // Shutdown
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("water object: destroying %p", static_cast<void*>(self));
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

// As the game's constructor (0x006DC3D0): only the fields it sets; the rest
// stay as operator new returned them.
void Construct(char* self)
{
    using DequeInitFn = void(__thiscall*)(void* deque, uint32_t count);
    SetVtables(self);
    At<uint8_t>(self, 0xC) = 0;
    At<uint8_t>(self, 0xD) = 0;
    At<uint32_t>(self, 0x10) = 0;
    At<uint8_t>(self, 0x14) = 0;
    Zero(self, 0x18, 0x38);
    for (const std::size_t deque : {0x38, 0x60}) {
        Zero(self, deque, deque + 0x28);
        reinterpret_cast<DequeInitFn>(addr::SC4CellDeque_Init)(self + deque, 0);
    }
    Zero(self, 0x88, 0xA8);
    At<uint32_t>(self, 0xA8) = 7;
    At<uint32_t>(self, 0xAC) = 2;
    At<uint32_t>(self, 0xB0) = 1;
    At<uint32_t>(self, 0xB4) = 20;
    At<uint32_t>(self, 0xB8) = 100;
    At<void*>(self, 0xBC) = nullptr;
    auto** const producers = static_cast<void**>(Allocate(0x38));
    producers[0] = producers;
    producers[1] = producers;
    At<void*>(self, 0xBC) = producers;
    At<uint32_t>(self, 0xC0) = 9;
    At<uint8_t>(self, 0xC4) = 0;
    At<uint8_t>(self, 0xC5) = 0;
    Zero(self, 0xC8, 0xEC);
    Zero(self, 0xF0, 0x150);
    Zero(self, 0x150, 0x170); // two curves
    At<void*>(self, 0x170) = nullptr;
    auto** const pipes = static_cast<void**>(Allocate(0xC));
    pipes[0] = pipes;
    pipes[1] = pipes;
    At<void*>(self, 0x170) = pipes; // the game then inserts no elements (0x006DB400, count 0)
    Zero(self, 0x174, 0x184);
    At<float>(self, 0x184) = 0.3f;
    Zero(self, 0x188, 0x19C);
    At<float>(self, 0x19C) = 1.2f;
    Zero(self, 0x1A0, 0x1C0); // two curves
    At<uint32_t>(self, 0x1C0) = 0;
}

// As the game's destructor (0x006DC730).
void Destroy(char* self)
{
    using DequeDestroyFn = void(__thiscall*)(void* deque);
    SetVtables(self);
    for (const std::size_t curve : {0x1B0, 0x1A0})
        if (void* const points = At<void*>(self, curve))
            Free(points);
    reinterpret_cast<ThisFn>(addr::SC4PairList_Destroy)(self + 0x170);
    for (const std::size_t curve : {0x160, 0x150})
        if (void* const points = At<void*>(self, curve))
            Free(points);
    reinterpret_cast<ThisFn>(addr::SC4PairList_Destroy)(self + 0xBC);
    if (void* const department = At<void*>(self, 0xA4))
        Virtual<uint32_t>(department, 0x8);
    reinterpret_cast<DequeDestroyFn>(addr::SC4CellDeque_Destroy)(self + 0x60);
    reinterpret_cast<DequeDestroyFn>(addr::SC4CellDeque_Destroy)(self + 0x38);
}

// The vtables: the game's as they are now (with whatever the other
// replacements swapped in), with our identity functions.
void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4PlumbingSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4PlumbingSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4PlumbingSimulator_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[29] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
}

// The factory, as the game's (0x006DC9A0): operator new(0x1C8), then the
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
        log::Write("water object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool Apply(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registered = registry::Register({
    "simulators.water.object",
    "simulators/water",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4PlumbingSimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/water.md)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::water_sim
