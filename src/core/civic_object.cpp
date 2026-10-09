// Our cSC4CivicBuildingSimulator object (docs/civic.md, "Our object"): our
// factory registered under the game's class ID, our constructor,
// destructor, QueryInterface, AddRef and Release, and our own copies of the
// three vtables. The object has the game's layout (0x48 bytes), so every
// method, the game's or ours, works on it; the vtables are copied from the
// game's when the first object is made, after the other replacements have
// patched them. Off, the game's own object is used.
#include "core/civic_sim.h"
#include "core/director.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <cstdint>
#include <cstring>

namespace opensc4::civic_sim {
namespace {

constexpr uint32_t kClassId = 0x0A413481;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidCivic = 0x8A41322F;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0x48;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// The three vtables (the game's: 0x00AB0C7C with the deleting destructor in
// slot 9, 0x00AB0C6C, 0x00AB0C54).
constexpr int kSlots = 10, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

// --- Identity --------------------------------------------------------------

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, field::kRefs);
}

// As the game's (0x0069D6C0).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidCivic:
    case kIidUnknown: offset = 0; break;
    case kIidMessageTarget: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self + offset;
    Virtual<uint32_t>(self, 0x4); // AddRef
    return 1;
}

// As the game's (0x0069D7A0): the deleting destructor (slot 9) when the
// last reference goes, without Shutdown.
uint32_t __fastcall Release(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, field::kRefs);
    if (refs > 1)
        return --refs;
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("civic object: destroying %p", static_cast<void*>(self));
    Virtual<void*>(self, 0x24, 1u);
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

// --- Construction and destruction -------------------------------------------

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
}

// A hash table's buckets (100), as the game's SC4Uint32HashSet::Construct.
void ConstructTable(char* self, std::size_t offset)
{
    using ConstructFn = void(__thiscall*)(void* table, uint32_t buckets);
    char* const table = self + offset;
    At<uint32_t>(table, 0x4) = 0;
    At<uint32_t>(table, 0x8) = 0;
    At<uint32_t>(table, 0xC) = 0;
    At<uint32_t>(table, 0x10) = 0;
    reinterpret_cast<ConstructFn>(addr::SC4Uint32HashSet_Construct)(table, 100);
}

void ConstructList(char* self, std::size_t offset)
{
    using NodesFn = void**(__thiscall*)(void* list, uint32_t count, uint32_t unused);
    At<void*>(self, offset) = nullptr;
    void** const head = reinterpret_cast<NodesFn>(addr::SC4List_AllocateNodes)(self + offset, 1, 0);
    head[0] = head;
    head[1] = head;
    At<void**>(self, offset) = head;
}

// As the game's constructor (0x0069E670).
void Construct(char* self)
{
    SetVtables(self);
    At<uint32_t>(self, field::kRefs) = 0;
    At<uint8_t>(self, field::kInitialised) = 0;
    At<void*>(self, field::kDepartment) = nullptr;
    ConstructTable(self, field::kExcluded);
    ConstructTable(self, field::kStatuses);
    ConstructList(self, field::kZooBuildings);
    ConstructList(self, field::kLandmarks);
}

// As the game's destructor (0x0069E500).
void Destroy(char* self)
{
    SetVtables(self);
    reinterpret_cast<ThisFn>(addr::SC4List_Destroy2)(self + field::kLandmarks);
    reinterpret_cast<ThisFn>(addr::SC4List_Destroy2)(self + field::kZooBuildings);
    reinterpret_cast<ThisFn>(addr::cSC4CivicBuildingSimulator_ClearStatuses)(self + field::kStatuses);
    if (void* const buckets = At<void*>(self, field::kStatuses + table::kBuckets))
        Free(buckets);
    reinterpret_cast<ThisFn>(addr::SC4Uint32HashSet_Clear)(self + field::kExcluded);
    if (void* const buckets = At<void*>(self, field::kExcluded + table::kBuckets))
        Free(buckets);
    if (void* const department = At<void*>(self, field::kDepartment))
        Virtual<uint32_t>(department, 0x8);
}

// The vtables: the game's as they are now (with whatever the other
// replacements swapped in), with our identity functions.
void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4CivicBuildingSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4CivicBuildingSimulator_vftable_4),
                sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4CivicBuildingSimulator_vftable_8),
                sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[9] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
}

// The factory, as the game's (0x0069E800): operator new(0x48), then the
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
        log::Write("civic object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool Apply(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registered = registry::Register({
    "simulators.civic.object",
    "simulators/civic",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4CivicBuildingSimulator object under the game's class ID: factory, constructor, destructor, "
    "identity; methods from the (patched) vtables (docs/civic.md)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::civic_sim
