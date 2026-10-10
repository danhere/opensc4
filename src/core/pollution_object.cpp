// Our cSC4PollutionSimulator object (docs/pollution.md, "Our object"): our
// factory registered under the game's class ID, our constructor, destructor,
// QueryInterface, AddRef and Release, and our own copies of the four
// vtables. The object has the game's layout (0x238 bytes, the same fields),
// so every method, the game's or ours, works on it; the vtables are copied
// from the game's when the first object is made, after the other
// replacements have patched them, so each method is the game's or ours as
// configured. Off, the game's own object is used (patched as configured).
#include "core/pollution_sim.h"

#include "core/director.h"
#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <cstdint>
#include <cstring>

namespace opensc4::pollution_sim {
namespace {

constexpr uint32_t kClassId = 0x8990C065;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidPollution = 0x877181EC;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidUnnamed8 = 0x89C498BE;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0x238;
constexpr std::size_t kRefs = 0x14;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using DestroyListFn = void(__thiscall*)(void* list);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto DestroyList = reinterpret_cast<DestroyListFn>(addr::SC4List_Destroy2);

// The four vtables (the game's: 0x00AB2630, 0x00AB261C, 0x00AB2600,
// 0x00AB25E8; packed, no RTTI slots).
constexpr int kSlots = 71, kSlots4 = 4, kSlots8 = 7, kSlotsC = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];

// --- Identity --------------------------------------------------------------

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, kRefs);
}

uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidPollution:
    case kIidUnknown: offset = 0; break;
    case kIidMessageTarget: offset = 0x4; break;
    case kIidUnnamed8: offset = 0x8; break;
    case kIidSerializable: offset = 0xC; break;
    default: return 0;
    }
    *object = self + offset;
    AddRef(self, nullptr);
    return 1;
}

void Destroy(char* self);

// The count stays at 1 when the last reference goes: Shutdown, the
// destructor and operator delete, as the game's Release (0x006E5F20).
uint32_t __fastcall Release(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, kRefs);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10); // Shutdown
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("pollution object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    OperatorDelete(self);
    return 0;
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
    At<void**>(self, 0xC) = g_vtableC;
}

void Zero(char* self, std::size_t first, std::size_t end)
{
    std::memset(self + first, 0, end - first);
}

// As the game's constructor (0x006E56E0): only the fields it sets; the rest
// stay as operator new returned them.
void Construct(char* self)
{
    SetVtables(self);
    At<uint8_t>(self, 0x10) = 0;
    At<uint8_t>(self, 0x11) = 0;
    Zero(self, 0x14, 0x3C);
    for (std::size_t list = field::kWaterTreatmentList; list <= field::kRecyclingList; list += 4) {
        At<void*>(self, list) = nullptr;
        auto** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 12u));
        sentinel[0] = sentinel;
        sentinel[1] = sentinel;
        At<void*>(self, list) = sentinel;
    }
    At<uint8_t>(self, 0x78) = 1;
    At<uint8_t>(self, 0x79) = 1;
    At<uint8_t>(self, 0x7A) = 1;
    Zero(self, 0x7C, 0xA4);
    At<uint32_t>(self, 0xA4) = 1;
    Zero(self, 0xA8, 0xFC + 4); // the block vectors, the department, the two curves, +0xFC
    std::memcpy(self + 0x158, reinterpret_cast<const void*>(0x00A80990), 8);
    At<uint32_t>(self, field::kRecyclingCenterEffect) = Global<uint32_t>(addr::PollutionTuning_DefaultPercentage);
    std::memcpy(self + 0x190, reinterpret_cast<const void*>(0x00A80990), 8);
    Zero(self, 0x140, 0x158);
    Zero(self, 0x160, 0x190);
    Zero(self, 0x198, 0x1B0);
    At<uint32_t>(self, 0x1B8) = 0;
    Zero(self, 0x1C0, 0x1D4);
    At<uint8_t>(self, 0x1D8) = 1;
    At<uint8_t>(self, 0x1D9) = 1;
    At<uint8_t>(self, 0x1DA) = 1;
    Zero(self, 0x1DC, 0x204);
    At<uint32_t>(self, 0x204) = 1;
    At<uint8_t>(self, 0x20C) = 1;
    At<uint32_t>(self, 0x228) = 0; // the capacity scale
    Zero(self, 0x210, 0x228);
    Zero(self, 0x40, 0x60);
}

// As the game's destructor (0x006E4E20).
void Destroy(char* self)
{
    SetVtables(self);
    for (const std::size_t buffer : {0x1F0, 0xEC, 0xDC})
        if (void* const p = At<void*>(self, buffer))
            Free(p);
    if (void* const department = At<void*>(self, 0xD8))
        Virtual<uint32_t>(department, 0x8);
    for (const std::size_t buffer : {0xCC, 0xC0, 0xB4, 0xA8, 0x90})
        if (void* const p = At<void*>(self, buffer))
            Free(p);
    for (const std::size_t list : {field::kRecyclingList, field::kWasteToEnergyList, field::kIncineratorList,
                                   field::kWaterTreatmentList})
        DestroyList(self + list);
}

// The serialisable part's slot 5 (the game's 0x006E2CC0): the class ID.
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    return kClassId;
}

// Slots 5 and 6 of the +0x8 interface, which the game shares with other
// classes (0x00648EF0: false; 0x0088B410: three arguments, 0).
uint8_t __fastcall Unnamed8Slot5(void*, void*)
{
    return 0;
}

uint32_t __fastcall Unnamed8Slot6(void*, void*, uint32_t, uint32_t, uint32_t)
{
    return 0;
}

// The vtables: the game's as they are now (with whatever the other
// replacements swapped in), with our identity functions.
void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4PollutionSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4PollutionSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4PollutionSimulator_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4PollutionSimulator_vftable_C), sizeof(g_vtableC));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[1] = reinterpret_cast<void*>(&AddRefAt<0xC>);
    g_vtableC[2] = reinterpret_cast<void*>(&ReleaseAt<0xC>);
    if (reinterpret_cast<std::uintptr_t>(g_vtableC[5]) == 0x006E2CC0)
        g_vtableC[5] = reinterpret_cast<void*>(&GetGZCLSID);
    if (reinterpret_cast<std::uintptr_t>(g_vtable8[5]) == 0x00648EF0)
        g_vtable8[5] = reinterpret_cast<void*>(&Unnamed8Slot5);
    if (reinterpret_cast<std::uintptr_t>(g_vtable8[6]) == 0x0088B410)
        g_vtable8[6] = reinterpret_cast<void*>(&Unnamed8Slot6);
}

// The factory, as the game's (0x006E5F00): operator new(0x238), then the
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
        log::Write("pollution object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool Apply(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registered = registry::Register({
    "simulators.pollution.object",
    "simulators/pollution",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4PollutionSimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/pollution.md)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::pollution_sim
