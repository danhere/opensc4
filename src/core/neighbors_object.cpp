// Our cSC4NeighborsSimulator (build 641), fifth part: Init and our own object
// under the game's class ID (docs/neighbors.md).
#include "core/neighbors_sim.h"

#include "core/address_check.h"
#include "core/director.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace opensc4::neighbors_sim {
namespace {

using ListFn = void(__thiscall*)(void* list);
using TextFn = void*(__thiscall*)(void* string, const char* text);
using CurveLoadFn = void(__thiscall*)(void* curve, void* property);
using CurveInitFn = void(__thiscall*)(void* curve);
using AllocateNodesFn = ListNode*(__thiscall*)(void* list, uint32_t count, uint32_t zero);
using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using DateFn = void*(__thiscall*)(void* date, uint32_t day);
using DateDefaultFn = void*(__thiscall*)(void* date);
using DateDtorFn = void(__thiscall*)(void* date);

void* Region() { return Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x90); }

struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

struct Date {
    const void* vtable;
    uint32_t day;
    const void* face;
    uint32_t zero;
};

void* NewList(std::uintptr_t allocator)
{
    auto* const sentinel = static_cast<ListNode*>(Virtual<void*>(NodeAllocatorAt(allocator), 0xC, 0xCu));
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    return sentinel;
}

// The last node of a referencing list unlinked, its object released, the
// node freed.
void PopBack(ListNode* head, std::uintptr_t allocator)
{
    ListNode* const node = head->prev;
    node->prev->next = node->next;
    node->next->prev = node->prev;
    if (node->value)
        Virtual<uint32_t>(node->value, 0x8);
    Virtual<void>(NodeAllocatorAt(allocator), 0x10, node);
}

// A date `delta` days on from `date` (its slot 35) into a new date, handed
// to the deal's setter at `slot`.
void Shift(void* deal, std::size_t slot, Date* date, uint32_t delta)
{
    const uint32_t day = Virtual<uint32_t>(&date->face, 0x8C, delta);
    Date shifted{};
    reinterpret_cast<DateFn>(addr::cRZDate_cRZDate)(&shifted, day);
    Virtual<void>(deal, slot, &shifted.face);
    reinterpret_cast<DateDtorFn>(addr::cRZDate_Destroy)(&shifted);
}

// 0x006D38F0 (slot 3): the tuning exemplar 6534284A-7A4A8458-6A469021 and
// its price curve (0x0A469353, into +0x2C); registration with the simulator
// service (6, 4 and 10); the messages; the city; our connections repaired
// against the region; each adjacent city's place in the region refreshed;
// the deals rebuilt from the adjacent cities' copies (clones not to be
// broadcast, their dates moved by the time since their last update), then
// shared.
uint8_t __fastcall Init(void* self, void*)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("neighbors: first Init");
    }
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    if (!At<void*>(self, field::kTuning)) {
        constexpr uint32_t kKey[3] = {0x6534284A, 0x7A4A8458, 0x6A469021};
        Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, kKey, 0xA52160F5u,
                         &At<void*>(self, field::kTuning), 0u, 0u);
        if (void* const tuning = At<void*>(self, field::kTuning)) {
            if (void* const property = Virtual<void*>(Virtual<void*>(tuning, 0x1C), 0x24, 0x0A469353u))
                reinterpret_cast<CurveLoadFn>(addr::SC4Curve_LoadFromProperty)(static_cast<char*>(self) + field::kCurve,
                                                                             property);
        }
    }
    uint8_t& once = Global<uint8_t>(addr::cSC4NeighborsSimulator_Started);
    if (!once)
        once = 1;
    void* const target = static_cast<char*>(self) + 0x4;
    if (void* const simulators = Global<void*>(addr::SC4_SimulatorService)) {
        GameString name{};
        reinterpret_cast<TextFn>(addr::SC4String_FromText)(&name,
                                                           reinterpret_cast<const char*>(addr::cSC4NeighborsSimulator_Name));
        Virtual<uint32_t>(simulators, 0x4C, target, 6u, &name, 2u);
        Virtual<uint32_t>(simulators, 0x4C, target, 4u, &name, 2u);
        Virtual<uint32_t>(simulators, 0x4C, target, 10u, &name, 2u);
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (name.begin)
            reinterpret_cast<FreeFn>(addr::SC4_Free)(name.begin);
    }
    if (!At<void*>(self, field::kMessages)) {
        void* const messages = Global<void*>(addr::SC4_MessageServer);
        At<void*>(self, field::kMessages) = messages;
        Virtual<uint32_t>(messages, 0x4);
        for (const uint32_t type : {0xAA7A014Du, 0x278E6003u, 0xEA418D8Fu, 0x6AC284F3u, 0x99EF1143u, 0x26D31EC5u,
                                    0x26D31EC4u})
            Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x14, target, type);
    }
    if (!At<void*>(self, field::kCity)) {
        void* const city = Global<void*>(addr::SC4_City);
        At<void*>(self, field::kCity) = city;
        Virtual<uint32_t>(city, 0x4);
    }
    void* const ours = Region();
    for (ListNode* node = Head(self, field::kConnections)->next; node != Head(self, field::kConnections);
         node = node->next)
        Virtual<void>(node->value, 0x28, ours);

    void* adjacent = NewList(addr::SC4List_NodeAllocator4);
    Virtual<void>(self, 0x50, &adjacent, 0u);
    auto* const cities = static_cast<ListNode*>(adjacent);
    while (cities->next != cities) {
        void* const city = cities->prev->value;
        if (city)
            Virtual<uint32_t>(city, 0x4);
        int32_t x = 0, z = 0;
        Virtual<void>(city, 0x14, &x, &z);
        void** slot = Virtual<void**>(Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x8C), 0x2C, x, z);
        if (slot && *slot)
            Virtual<void>(Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x8C), 0x3C, &slot);
        PopBack(cities, addr::SC4List_NodeAllocator4);
        Virtual<uint32_t>(city, 0x8);
    }
    reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing4)(&adjacent);

    Virtual<void>(self, 0x50, &adjacent, 0u);
    void* const region = Region();
    Virtual<void>(region, 0x110);
    reinterpret_cast<ListFn>(addr::SC4List_Clear)(static_cast<char*>(self) + field::kDeals);
    while (cities->next != cities) {
        void* const city = cities->prev->value;
        if (city && city != region) {
            void* copies = NewList(addr::SC4List_NodeAllocator);
            Virtual<void>(city, 0x110);
            Virtual<void>(city, 0x118, &copies);
            auto* const deals = static_cast<ListNode*>(copies);
            while (deals->next != deals) {
                void* clone = nullptr;
                if (Virtual<uint8_t>(deals->prev->value, 0xA0, &clone, 0u) && clone && Virtual<void*>(clone, 0x1C)) {
                    Virtual<void>(clone, 0x94, 0x80u, 0u);
                    ListNode* const head = Head(self, field::kDeals);
                    ListNode* const node = reinterpret_cast<AllocateNodesFn>(addr::SC4List_AllocateNodes1)(
                        static_cast<char*>(self) + field::kDeals, 1, 0);
                    node->value = clone;
                    if (clone)
                        Virtual<uint32_t>(clone, 0x4);
                    ListNode* const last = head->prev;
                    node->next = head;
                    node->prev = last;
                    last->next = node;
                    head->prev = node;
                    Date start{}, current{}, expiration{}, suspension{}, today{};
                    const auto Default = reinterpret_cast<DateDefaultFn>(addr::cRZDate_Default);
                    Default(&start);
                    Default(&current);
                    Default(&expiration);
                    Default(&suspension);
                    reinterpret_cast<DateFn>(addr::cRZDate_FromDay)(
                        &today, Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x1C));
                    Virtual<void>(clone, 0x70, &current.face);
                    Virtual<void>(clone, 0x60, &start.face);
                    Virtual<void>(clone, 0x68, &expiration.face);
                    Virtual<void>(clone, 0x78, &suspension.face);
                    const uint32_t delta = Virtual<uint32_t>(&today.face, 0x88, &current.face);
                    Shift(clone, 0x74, &current, delta);
                    Shift(clone, 0x64, &start, delta);
                    Shift(clone, 0x6C, &expiration, delta);
                    Shift(clone, 0x7C, &suspension, delta);
                    const auto Drop = reinterpret_cast<DateDtorFn>(addr::cRZDate_Destroy);
                    Drop(&today);
                    Drop(&suspension);
                    Drop(&expiration);
                    Drop(&current);
                    Drop(&start);
                }
                PopBack(deals, addr::SC4List_NodeAllocator);
                if (clone)
                    Virtual<uint32_t>(clone, 0x8);
            }
            reinterpret_cast<ListFn>(addr::SC4List_Clear)(&copies);
            Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator), 0x10, copies);
        }
        PopBack(cities, addr::SC4List_NodeAllocator4);
    }

    void* sharing = NewList(addr::SC4List_NodeAllocator4);
    Virtual<void>(self, 0x50, &sharing, 0u);
    void* const ours2 = Region();
    auto* const share = static_cast<ListNode*>(sharing);
    while (share->next != share) {
        if (void* const city = share->prev->value) {
            Virtual<void>(city, 0x114, static_cast<char*>(self) + field::kDeals);
            Virtual<void>(city, 0x11C);
        }
        PopBack(share, addr::SC4List_NodeAllocator4);
    }
    Virtual<void>(ours2, 0x114, static_cast<char*>(self) + field::kDeals);
    Virtual<void>(ours2, 0x11C);
    At<uint8_t>(self, field::kDealsDirty) = 0;
    At<uint8_t>(self, 0x3D) = 0;
    reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing4)(&sharing);
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, sharing);
    reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing4)(&adjacent);
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, adjacent);
    return 1;
}

using InitFn = uint8_t(__fastcall*)(void*, void*);
InitFn g_originalInit = nullptr;

bool InstallInit(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4NeighborsSimulator_Init);
    return game::IsBuild641() && entry && hook::Install(*entry, &Init, &g_originalInit);
}

const bool g_registeredInit = registry::Register({
    "simulators.neighbors.init",
    "simulators/neighbors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4NeighborsSimulator_Init,
    "Our neighbors Init: tuning, registration, messages, connections repaired, deals rebuilt from the region "
    "(docs/neighbors.md)",
    false,
    InstallInit,
});

// --- Our object -------------------------------------------------------------

constexpr uint32_t kClassId = 0xC990C0A4;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidNeighbors = 0x0773FEF0;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0x40;

constexpr int kSlots = 39, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x006CDF00).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidNeighbors:
    case kIidUnknown: offset = 0; break;
    case kIidMessageTarget: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self ? self + offset : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's Release (0x006CE770): at the last reference, Shutdown and
// the deleting destructor (slot 38).
uint32_t __fastcall Release(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, field::kRefCount);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x98, 1u);
    return 0;
}

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
}

// As the game's destructor (0x006D09D0).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("neighbors object: destroying %p", static_cast<void*>(self));
    SetVtables(self);
    if (void* const points = At<void*>(self, field::kCurve))
        reinterpret_cast<FreeFn>(addr::SC4_Free)(points);
    if (void* const tuning = At<void*>(self, field::kTuning))
        Virtual<uint32_t>(tuning, 0x8);
    reinterpret_cast<ListFn>(addr::SC4PairList_Destroy)(self + field::kPairs);
    reinterpret_cast<ListFn>(addr::SC4List_Destroy)(self + field::kDeals);
    reinterpret_cast<ListFn>(addr::SC4List_Destroy3)(self + field::kConnections);
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

// As the game's constructor (0x006D0930).
void Construct(char* self)
{
    SetVtables(self);
    At<uint8_t>(self, field::kLoaded) = 0;
    At<uint8_t>(self, field::kInitialized) = 0;
    At<uint32_t>(self, field::kRefCount) = 0;
    At<void*>(self, field::kCity) = nullptr;
    At<void*>(self, field::kMessages) = nullptr;
    At<void*>(self, field::kConnections) = nullptr;
    ListNode* node = reinterpret_cast<AllocateNodesFn>(addr::SC4List_AllocateNodes3)(self + field::kConnections, 1, 0);
    node->next = node;
    node->prev = node;
    At<ListNode*>(self, field::kConnections) = node;
    At<void*>(self, field::kDeals) = nullptr;
    node = reinterpret_cast<AllocateNodesFn>(addr::SC4List_AllocateNodes1)(self + field::kDeals, 1, 0);
    node->next = node;
    node->prev = node;
    At<ListNode*>(self, field::kDeals) = node;
    At<void*>(self, field::kPairs) = nullptr;
    node = static_cast<ListNode*>(reinterpret_cast<NewFn>(addr::SC4_stl_allocate)(0xC));
    node->next = node;
    node->prev = node;
    At<ListNode*>(self, field::kPairs) = node;
    At<void*>(self, field::kTuning) = nullptr;
    reinterpret_cast<CurveInitFn>(addr::SC4Curve_Init)(self + field::kCurve);
    At<uint8_t>(self, 0x3D) = 0;
    At<uint8_t>(self, field::kUIDisabled) = 0;
    At<uint8_t>(self, 0x3F) = 0;
    At<uint8_t>(self, field::kDealsDirty) = 1;
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4NeighborsSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4NeighborsSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4NeighborsSimulator_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[38] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
}

// The factory, as the game's (0x006D38D0).
cIGZUnknown* Create()
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    auto* const self = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(kSize));
    if (!self)
        return nullptr;
    Construct(self);
    static int created = 0;
    if (created++ < 3)
        log::Write("neighbors object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool ApplyObject(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registeredObject = registry::Register({
    "simulators.neighbors.object",
    "simulators/neighbors",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4NeighborsSimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/neighbors.md)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::neighbors_sim
