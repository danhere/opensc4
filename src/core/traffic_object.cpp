// Our cSC4TrafficSimulator object (docs/traffic.md, "Our object"): our
// factory registered under the game's class ID, our constructor,
// destructor, QueryInterface, AddRef and Release, and our own copies of the
// three vtables. The object has the game's layout (0xD58 bytes, the same
// fields), so every method, the game's or ours, works on it; the vtables
// are copied from the game's when the first object is made, after the
// other replacements have patched them. The members are built and torn
// down through the game's own container code, in the game's order, so the
// heap sees the same allocations. Off, the game's own object is used.
#include "core/director.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <cstdint>
#include <cstring>

namespace opensc4::traffic_sim {
namespace {

constexpr uint32_t kClassId = 0x6990C1AA;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidTraffic = 0x68027F37;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0xD58;
constexpr std::size_t kRefs = 0x10;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
using ThisArgFn = void(__thiscall*)(void* self, uint32_t arg);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// The three vtables (the game's: 0x00AB33D0 with the deleting destructor in
// slot 43, 0x00AB33BC, 0x00AB33A4).
constexpr int kSlots = 44, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

// --- Identity --------------------------------------------------------------

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, kRefs);
}

// As the game's (0x0070D7E0).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidTraffic:
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

// As the game's Release (0x0070E2F0): at 1, Shutdown and the deleting
// destructor.
uint32_t __fastcall Release(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, kRefs);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10); // Shutdown
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("traffic object: destroying %p", static_cast<void*>(self));
    Virtual<void*>(self, 0xAC, 1u); // the deleting destructor
    return 0;
}

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

void Zero(char* self, std::size_t first, std::size_t end)
{
    std::memset(self + first, 0, end - first);
}

// An empty tree map: a header node of `bytes` (colour 0, no root, left and
// right itself) and a size of 0.
void EmptyTree(char* self, std::size_t at, std::size_t bytes)
{
    At<void*>(self, at) = nullptr;
    char* const header = static_cast<char*>(Allocate(bytes));
    At<char*>(self, at) = header;
    At<uint32_t>(self, at + 4) = 0;
    header[0] = 0;
    At<char*>(header, 4) = nullptr;
    At<char*>(header, 8) = header;
    At<char*>(header, 0xC) = header;
}

// A chained hash set of 100 buckets (its +0 is left as it was).
void HashSet(char* self, std::size_t at)
{
    Zero(self, at + 4, at + 0x14);
    reinterpret_cast<ThisArgFn>(addr::SC4Uint32HashSet_Construct)(self + at, 100);
}

// The statistics block (0x1E4 and 0x440): 13 records of 0x14 bytes, the
// counters, and 12 records keyed by the default at 0x00B4B2E8.
void Statistics(char* block)
{
    const uint32_t empty = Global<uint32_t>(addr::cSC4TrafficSimulator_RecordDefault);
    for (std::size_t i = 0; i < 13; ++i)
        Zero(block, 0x14 * i, 0x14 * i + 0x14);
    Zero(block, 0x104, 0x130);
    At<uint16_t>(block, 0x130) = 0;
    At<uint16_t>(block, 0x132) = 0;
    At<uint16_t>(block, 0x134) = 0;
    block[0x238] = 0;
    for (std::size_t i = 0; i < 12; ++i) {
        char* const record = block + 0x138 + 0x14 * i;
        At<uint16_t>(record, 0) = 0;
        At<uint32_t>(record, 4) = empty;
        At<uint32_t>(record, 8) = 0;
        At<uint32_t>(record, 0xC) = 0;
        At<uint32_t>(record, 0x10) = empty;
    }
    At<uint32_t>(block, 0x228) = empty;
    At<uint32_t>(block, 0x22C) = empty;
    block[0x230] = 0;
    At<uint32_t>(block, 0x234) = empty;
}

// As the game's constructor (0x0071F830): only the fields it sets, in its
// order of allocations.
void Construct(char* self)
{
    using AssignFn = void(__thiscall*)(void* vector, uint32_t count, const uint32_t* value);
    At<std::uintptr_t>(self, 0x4) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0x8) = addr::SC4SimGrid_VtableBase;
    SetVtables(self);
    self[0xC] = 0;
    Zero(self, 0x10, 0x20);
    Zero(self, 0x30, 0x5C);
    At<uint16_t>(self, 0x60) = 1;
    Zero(self, 0x64, 0x88);
    Zero(self, 0x88, 0xB8); // four vectors
    EmptyTree(self, 0xB8, 0x18);
    At<uint32_t>(self, 0xC4) = 0;
    HashSet(self, 0xC8);
    HashSet(self, 0xDC);
    HashSet(self, 0xF0);
    At<void*>(self, 0x104) = nullptr;
    char* const connections = static_cast<char*>(Allocate(0x28));
    At<char*>(connections, 0) = connections;
    At<char*>(connections, 4) = connections;
    At<char*>(self, 0x104) = connections;
    At<uint32_t>(self, 0x108) = 0;
    Zero(self, 0x114, 0x120);
    Zero(self, 0x130, 0x13C);
    Zero(self, 0x13C, 0x148);
    reinterpret_cast<ThisArgFn>(addr::SC4Stopwatch_Construct)(self + 0x148, 4);
    Zero(self, 0x164, 0x178);
    HashSet(self, 0x178);
    self[0x19C] = 0;
    self[0x19D] = 0;
    EmptyTree(self, 0x1A0, 0x18);
    At<int32_t>(self, 0x1AC) = -1;
    for (std::size_t i = 0; i < 12; ++i) {
        At<uint32_t>(self, 0x320 + 0x14 * i) = 0;
        At<uint32_t>(self, 0x32C + 0x14 * i) = 0;
    }
    At<uint32_t>(self, 0x40C) = 0;
    At<uint32_t>(self, 0x410) = 0;
    At<uint32_t>(self, 0x418) = 0;
    Zero(self, 0x424, 0x440);
    for (std::size_t i = 0; i < 12; ++i) {
        At<uint32_t>(self, 0x57C + 0x14 * i) = 0;
        At<uint32_t>(self, 0x588 + 0x14 * i) = 0;
    }
    At<uint32_t>(self, 0x668) = 0;
    At<uint32_t>(self, 0x66C) = 0;
    At<uint32_t>(self, 0x674) = 0;
    At<uint32_t>(self, 0x6FC) = 0;
    At<uint32_t>(self, 0x700) = 0;
    At<uint32_t>(self, 0x704) = 0;
    At<uint32_t>(self, 0x708) = 1;
    HashSet(self, 0x70C);
    At<float>(self, 0x720) = 1.0f;
    At<uint32_t>(self, 0x724) = 0;
    At<uint32_t>(self, 0x728) = 0;
    // An empty string (cIGZString over a buffer of 8).
    Zero(self, 0x730, 0x73C);
    char* const text = static_cast<char*>(Allocate(8));
    At<char*>(self, 0x730) = text;
    At<char*>(self, 0x734) = text;
    At<char*>(self, 0x738) = text + 8;
    text[0] = 0;
    At<std::uintptr_t>(self, 0x72C) = addr::SC4String_Vtable;
    At<uint32_t>(self, 0x73C) = 0;
    At<double>(self, 0x798) = 0.0;
    Zero(self, 0x778, 0x78C);
    At<uint32_t>(self, 0x7A4) = 2;
    Zero(self, 0x7AC, 0x7B8);
    HashSet(self, 0x7B8);
    HashSet(self, 0x7CC);
    Zero(self, 0x7E0, 0x7EC);
    HashSet(self, 0x7EC);
    HashSet(self, 0x800);
    At<uint32_t>(self, 0x84C) = 0;
    self[0x920] = 0;
    At<uint32_t>(self, 0x924) = 0;
    At<uint32_t>(self, 0x928) = 0;
    At<uint32_t>(self, 0x934) = 0;
    At<uint32_t>(self, 0x938) = 0;
    At<uint32_t>(self, 0x950) = 0;
    At<uint32_t>(self, 0x970) = 1;
    At<float>(self, 0x978) = 0.03125f;
    At<float>(self, 0xB88) = 1.2f;
    At<float>(self, 0xB8C) = 1.2f;
    At<float>(self, 0xB90) = 32.0f;
    At<uint32_t>(self, 0xB94) = Global<uint32_t>(addr::SC4_FloatOne);
    At<float>(self, 0xB98) = 0.7f;
    At<uint32_t>(self, 0xB9C) = 0x18;
    At<uint32_t>(self, 0xC50) = 1;
    At<uint32_t>(self, 0xBA0) = Global<uint32_t>(addr::SC4_FloatOne);
    self[0xBC8] = 0x3C;
    At<uint32_t>(self, 0xC48) = 0x16D;
    At<uint32_t>(self, 0xC4C) = 0x80;
    At<float>(self, 0xC54) = 90.0f;
    At<float>(self, 0xC58) = 64.0f;
    self[0xC68] = 0;
    Zero(self, 0xC6C, 0xC78);
    At<float>(self, 0xCE4) = 1.0f;
    At<float>(self, 0xCF8) = 0.75f;
    At<float>(self, 0xCFC) = 0.5f;
    At<float>(self, 0xD00) = 4.0f;
    At<uint32_t>(self, 0xD04) = 0;
    At<uint32_t>(self, 0xD08) = 0x3A83126F; // 0.001
    At<uint32_t>(self, 0xD0C) = 0x3E03126F; // 0.128
    Zero(self, 0xD10, 0xD50); // the four curves
    At<uint32_t>(self, 0x818) = 0;
    At<uint32_t>(self, 0x830) = 0;
    At<uint32_t>(self, 0x834) = 0;
    At<uint32_t>(self, 0x904) = 0;
    At<uint32_t>(self, 0x910) = 0;
    self[0x838] = 1;
    At<uint32_t>(self, 0x908) = 0;
    Zero(self, 0xBA4, 0xBC8);
    for (std::size_t i = 0; i < 13; ++i) {
        At<uint32_t>(self, 0xBCC + 4 * i) = 0;
        At<uint32_t>(self, 0xB54 + 4 * i) = 0;
    }
    Statistics(self + 0x1E4);
    Statistics(self + 0x440);
    Zero(self, 0x740, 0x774);
    Zero(self, 0x1B0, 0x1C0);
    const uint32_t zero = 0;
    reinterpret_cast<AssignFn>(addr::SC4Uint32Vector_Assign)(self + 0x424, 9, &zero);
    reinterpret_cast<AssignFn>(addr::SC4Uint32Vector_Assign)(self + 0x430, 9, &zero);
    reinterpret_cast<ThisArgFn>(addr::SC4Vector12_Reserve)(self + 0x13C, 100);
    reinterpret_cast<ThisArgFn>(addr::SC4Vector12_Reserve)(self + 0x130, 100);
    At<uint32_t>(self, 0x128) = 0;
    At<uint32_t>(self, 0x12C) = 0;
}

// A hash set's buckets cleared and freed.
void DestroyHashSet(char* self, std::size_t at)
{
    reinterpret_cast<ThisFn>(addr::SC4Uint32HashSet_Clear)(self + at);
    if (void* const buckets = At<void*>(self, at + 4))
        Free(buckets);
}

// A tree map emptied (EraseSubtree from the root) and its header freed.
void DestroyTree(char* self, std::size_t at, std::uintptr_t eraseSubtree)
{
    using EraseFn = void(__thiscall*)(void* tree, void* node);
    if (At<uint32_t>(self, at + 4)) {
        char* const header = At<char*>(self, at);
        reinterpret_cast<EraseFn>(eraseSubtree)(self + at, At<char*>(header, 4));
        At<char*>(At<char*>(self, at), 8) = At<char*>(self, at);
        At<char*>(At<char*>(self, at), 4) = nullptr;
        At<char*>(At<char*>(self, at), 0xC) = At<char*>(self, at);
        At<uint32_t>(self, at + 4) = 0;
    }
    if (void* const header = At<void*>(self, at))
        Free(header);
}

void FreeIf(char* self, std::size_t at)
{
    if (void* const storage = At<void*>(self, at))
        Free(storage);
}

// A vector of 0x1C-byte zones destroyed and freed.
void DestroyZones(char* self, std::size_t at)
{
    using RangeFn = void(__cdecl*)(void* first, void* last, void* allocator);
    uint8_t allocator = 0;
    reinterpret_cast<RangeFn>(addr::SC4TrafficZones_DestroyRange)(At<void*>(self, at), At<void*>(self, at + 4), &allocator);
    FreeIf(self, at);
}

// As the game's destructor (0x007200C0).
void Destroy(char* self)
{
    SetVtables(self);
    for (const std::size_t curve : {0xD40, 0xD30, 0xD20, 0xD10})
        FreeIf(self, curve);
    FreeIf(self, 0xC6C);
    if (void* const held = At<void*>(self, 0x84C))
        Virtual<uint32_t>(held, 0x8);
    for (const std::size_t set : {0x800, 0x7EC, 0x7CC, 0x7B8})
        DestroyHashSet(self, set);
    At<std::uintptr_t>(self, 0x72C) = addr::SC4String_Vtable;
    FreeIf(self, 0x730);
    reinterpret_cast<ThisFn>(addr::SC4CostTable_Clear)(self + 0x70C);
    FreeIf(self, 0x710);
    FreeIf(self, 0x430);
    FreeIf(self, 0x424);
    DestroyTree(self, 0x1A0, addr::SC4LotMultimap_EraseSubtree);
    DestroyHashSet(self, 0x178);
    FreeIf(self, 0x164);
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Destruct)(self + 0x148);
    FreeIf(self, 0x13C);
    FreeIf(self, 0x130);
    FreeIf(self, 0x114);
    if (void* const finder = At<void*>(self, 0x108))
        Virtual<uint32_t>(finder, 0x8);
    reinterpret_cast<ThisFn>(addr::SC4TrafficConnectionList_Clear)(self + 0x104);
    FreeIf(self, 0x104);
    for (const std::size_t set : {0xF0, 0xDC, 0xC8})
        DestroyHashSet(self, set);
    if (void* const held = At<void*>(self, 0xC4))
        Virtual<uint32_t>(held, 0x8);
    DestroyTree(self, 0xB8, addr::SC4ObjectMap_EraseSubtree);
    for (std::size_t at = 0xAC; at >= 0x88; at -= 0xC)
        FreeIf(self, at);
    reinterpret_cast<ThisFn>(addr::SC4ShortVectorVector_Destroy)(self + 0x7C);
    DestroyZones(self, 0x70);
    DestroyZones(self, 0x64);
    reinterpret_cast<ThisFn>(addr::SC4ShortVectorVector_Destroy)(self + 0x4C);
}

// The vtables: the game's as they are now (with whatever the other
// replacements swapped in), with our identity functions.
void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4TrafficSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4TrafficSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4TrafficSimulator_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[43] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
}

// The factory, as the game's (0x00721580): operator new(0xD58), then the
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
        log::Write("traffic object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool Apply(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registered = registry::Register({
    "simulators.traffic.object",
    "simulators/traffic",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4TrafficSimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/traffic.md)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::traffic_sim
