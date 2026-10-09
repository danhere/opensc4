// Our cSC4TrafficNetworkMap object under the game's class ID: factory,
// constructor, destructor and identity; every other method comes from the
// game's vtables as patched by the other parts (docs/traffic-map.md).
#include "core/trafficmap.h"

#include "core/director.h"
#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <cstdint>
#include <cstring>

namespace opensc4::trafficmap {
namespace {

constexpr uint32_t kClassId = 0x6A0F82B2;
constexpr uint32_t kInterfaceId = 0xCA0F81B9;

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using TableFn = void(__thiscall*)(void* table);
using BucketsFn = void(__thiscall*)(void* table, uint32_t hint);
using SeedFn = void(__thiscall*)(void* random, uint32_t seed);

void* g_vtable[52];
void* g_vtable4[3];
void* g_vtable8[4];
void* g_vtableC[6];

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    At<void**>(self, 0xC) = g_vtableC;
}

// As the game's (0x004AE350): the interface and the three parts; anything
// else is refused without a reference.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    switch (iid) {
    case kInterfaceId:
        *object = self;
        break;
    case 1:
        *object = self + 0x4;
        break;
    case 0x452294AA:
        *object = self + 0x8;
        break;
    case 0xE4FDA3D4:
        *object = self + 0xC;
        break;
    default:
        return 0;
    }
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x004AFAA0): the last reference shuts the map down (slot 4)
// and deletes it (slot 45).
uint32_t __fastcall Release(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, field::kRefCount);
    if (refs > 1)
        return --refs;
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0xB4, 1u);
    return 0;
}

// As the game's destructor (0x004B3A30) and deleting destructor (0x004B3FE0):
// the three hash tables emptied and their buckets freed, in reverse order of
// construction. The grid is freed by Shutdown, not here; the generator has no
// destructor.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("trafficmap object: destroying %p", static_cast<void*>(self));
    SetVtables(self);
    const auto free = reinterpret_cast<FreeFn>(addr::SC4_Free);
    reinterpret_cast<TableFn>(addr::SC4PtrVectorHashMap_Clear)(self + field::kCellObjects);
    if (void* const buckets = Table(self, field::kCellObjects).buckets)
        free(buckets);
    reinterpret_cast<TableFn>(addr::SC4LaneTable_Clear)(self + field::kLanes);
    if (void* const buckets = Table(self, field::kLanes).buckets)
        free(buckets);
    reinterpret_cast<TableFn>(addr::SC4SignalTable_Clear)(self + field::kSignals);
    if (void* const buckets = Table(self, field::kSignals).buckets)
        free(buckets);
    if (flags & 1)
        reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(self);
    return self;
}

uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }

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

// An empty hash table with buckets for `hint` entries, as the constructor's
// inline code and _M_initialize_buckets (0x00829BC0).
void ConstructTable(char* self, std::size_t offset)
{
    Hashtable& table = Table(self, offset);
    table.buckets = nullptr;
    table.bucketsEnd = nullptr;
    table.bucketsCapacity = nullptr;
    table.count = 0;
    reinterpret_cast<BucketsFn>(addr::SC4Uint32HashSet_Construct)(&table, 100);
}

// As the game's constructor (0x004B3EB0), in its order.
void Construct(char* self)
{
    SetVtables(self);
    At<uint8_t>(self, field::kInitialized) = 0;
    At<uint32_t>(self, field::kRefCount) = 0;
    At<void*>(self, field::kGrid) = nullptr;
    At<int32_t>(self, field::kWidth) = 0;
    At<int32_t>(self, field::kHeight) = 0;
    At<uint8_t>(self, field::kShift) = 0;
    At<uint8_t>(self, field::kFlag25) = 0;
    ConstructTable(self, field::kSignals);
    At<uint8_t>(self, field::kSignalsChanged) = 0;
    ConstructTable(self, field::kLanes);
    At<uint32_t>(self, field::kUnknown54) = 0;
    At<uint32_t>(self, field::kUnknown58) = 0;
    At<uint8_t>(self, field::kRectDirty) = 0;
    for (std::size_t i = 0; i < 4; ++i)
        At<int32_t>(self, field::kRect + 4 * i) = 0;
    for (std::size_t i = 0; i < 3; ++i)
        At<uint32_t>(self, field::kViewPoint + 4 * i) = 0;
    reinterpret_cast<SeedFn>(addr::SC4Random_ConstructSeeded)(self + field::kRandom, 0xFFFFFFFFu);
    At<void*>(self, field::kView) = nullptr;
    At<void*>(self, field::kOccupantManager) = nullptr;
    At<void*>(self, field::kFilterA) = nullptr;
    At<void*>(self, field::kFilterB) = nullptr;
    At<uint8_t>(self, field::kPaused) = 0;
    At<int32_t>(self, field::kWalkX) = -1;
    At<int32_t>(self, field::kWalkZ) = 0;
    ConstructTable(self, field::kCellObjects);
    At<uint8_t>(self, field::kBlinkA) = 1;
    At<uint8_t>(self, field::kBlinkB) = 1;
    At<uint32_t>(self, field::kUpdateTimer) = 0;
    At<uint32_t>(self, field::kBlinkTimerA) = 0;
    At<uint32_t>(self, field::kBlinkTimerB) = 0;
    At<int32_t>(self, field::kUpdateX) = -1;
    At<int32_t>(self, field::kUpdateZ) = -1;
}

// Copies of the four vtables (with whatever the other parts swapped in, as
// the first object is made after PostAppInit), our identity on top.
void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4TrafficNetworkMap_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4TrafficNetworkMap_vftable_0x4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4TrafficNetworkMap_vftable_0x8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4TrafficNetworkMap_vftable_0xC), sizeof(g_vtableC));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[45] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[1] = reinterpret_cast<void*>(&AddRefAt<0xC>);
    g_vtableC[2] = reinterpret_cast<void*>(&ReleaseAt<0xC>);
    g_vtableC[5] = reinterpret_cast<void*>(&GetGZCLSID);
}

// The factory, as the game's (0x004B42E0).
cIGZUnknown* Create()
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    auto* const self = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(field::kSize));
    if (!self)
        return nullptr;
    Construct(self);
    static int created = 0;
    if (created++ < 3)
        log::Write("trafficmap object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool ApplyObject(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registeredObject = registry::Register({
    "simulators.trafficmap.object",
    "simulators/trafficmap",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4TrafficNetworkMap object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/traffic-map.md)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::trafficmap
