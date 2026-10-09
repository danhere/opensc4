// Our cSC4AuraSimulator object (docs/aura.md, "Our object"): our factory
// registered under the game's class ID, our constructor, destructor,
// QueryInterface, AddRef and Release, and our own copies of the three
// vtables. The object has the game's layout (0x160 bytes, the same
// fields), so every method, the game's or ours, works on it; the vtables
// are copied from the game's when the first object is made, after the
// other replacements have patched them, so each method is the game's or
// ours as configured. Off, the game's own object is used (patched as
// configured).
#include "core/director.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <cstdint>
#include <cstring>

namespace opensc4::aura_sim {
namespace {

constexpr uint32_t kClassId = 0x0990BDD8;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidAura = 0xC773E880;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0x160;
constexpr std::size_t kRefs = 0x10;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// The three vtables (the game's: 0x00AB07C8 with the deleting destructor in
// slot 16, 0x00AB07B8, 0x00AB07A0; packed, no RTTI slots).
constexpr int kSlots = 17, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

// --- Identity --------------------------------------------------------------

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, kRefs);
}

// As the game's (0x0068B9C0): the reference through the primary vtable.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidAura:
    case kIidUnknown: offset = 0; break;
    case kIidMessageTarget: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self + offset;
    Virtual<uint32_t>(self, 0x4); // AddRef
    return 1;
}

// The count stays at 1 when the last reference goes: Shutdown, then the
// deleting destructor (slot 16), as the game's Release (0x0068BFB0).
uint32_t __fastcall Release(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, kRefs);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10); // Shutdown
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("aura object: destroying %p", static_cast<void*>(self));
    Virtual<void*>(self, 0x40, 1u);
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

// As the game's constructor (0x0068D280): only the fields it sets (not the
// state +0x70, +0x74 or the lot iterator +0x78); the rest stay as operator
// new returned them.
void Construct(char* self)
{
    using RandomFn = void*(__thiscall*)(void* generator, int32_t seed);
    SetVtables(self);
    At<uint8_t>(self, 0xC) = 0;
    At<uint32_t>(self, 0x10) = 0;
    reinterpret_cast<RandomFn>(addr::SC4Random_Construct)(self + 0x18, -1);
    Zero(self, 0x30, 0x3C);
    At<uint32_t>(self, 0x3C) = 4;
    Zero(self, 0x40, 0x60);
    auto* const header = static_cast<char*>(Allocate(0x18)); // the map of department grids
    At<void*>(self, 0x5C) = header;
    At<uint32_t>(self, 0x60) = 0;
    At<uint8_t>(header, 0) = 0;
    At<void*>(header, 0x4) = nullptr;
    At<void*>(header, 0x8) = header;
    At<void*>(header, 0xC) = header;
    Zero(self, 0x68, 0x70);
    Zero(self, 0x80, 0x94);
    Zero(self, 0xC4, 0xD4);
    auto** const clusters = static_cast<void**>(Allocate(0x14));
    clusters[0] = clusters;
    clusters[1] = clusters;
    At<void*>(self, 0xD0) = clusters;
    At<uint8_t>(self, 0xD4) = 0;
    Zero(self, 0xD8, 0x104);
    At<uint32_t>(self, 0x104) = 0x3F19999A; // 0.6f
    At<uint32_t>(self, 0x108) = 0x3ECCCCCD; // 0.4f
    Zero(self, 0x10C, 0x118);
    Zero(self, 0x138, 0x150);
    At<uint16_t>(self, 0x150) = 0;
    Zero(self, 0x154, 0x15C);
    At<uint16_t>(self, 0x15C) = 0;
    for (std::size_t offset = 0x94; offset <= 0xC0; offset += 4)
        At<uint32_t>(self, offset) = 0xBF800000; // -1.0f
    Zero(self, 0x118, 0x138);
    At<uint32_t>(self, 0x7C) = 0xFFFFFFFF;
}

// As the game's destructor (0x0068D110).
void Destroy(char* self)
{
    SetVtables(self);
    if (void* const curves = At<void*>(self, 0xDC))
        Free(curves);
    void** const clusters = At<void**>(self, 0xD0);
    for (void** node = static_cast<void**>(clusters[0]); node != clusters;) {
        void** const next = static_cast<void**>(node[0]);
        Free(node);
        node = next;
    }
    clusters[0] = clusters;
    clusters[1] = clusters;
    Free(clusters);
    if (void* const low = At<void*>(self, 0xC4))
        Free(low);
    char* const header = At<char*>(self, 0x5C);
    if (At<uint32_t>(self, 0x60)) {
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4Uint32Map_EraseTree)(At<void*>(header, 0x4));
        At<void*>(header, 0x8) = header;
        At<void*>(header, 0x4) = nullptr;
        At<void*>(header, 0xC) = header;
        At<uint32_t>(self, 0x60) = 0;
    }
    if (header)
        Free(header);
}

// The vtables: the game's as they are now (with whatever the other
// replacements swapped in), with our identity functions.
void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4AuraSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4AuraSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4AuraSimulator_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[16] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
}

// The factory, as the game's (0x0068D6E0): operator new(0x160), then the
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
        log::Write("aura object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool Apply(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registered = registry::Register({
    "simulators.aura.object",
    "simulators/aura",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4AuraSimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/aura.md)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::aura_sim
