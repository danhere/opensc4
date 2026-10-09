// Our fire and police simulator objects and their dispatch sources
// (docs/fire-police.md, "Our objects"), as for water (water_object.cpp):
// our factories registered under the game's class IDs, our constructors,
// destructors and identity functions, and our own copies of the vtables,
// copied from the game's when the first object is made, after the other
// replacements have patched them. The objects keep the game's layouts, so
// every method, the game's or ours, works on them.
//
// simulators.fire.object: cSC4FireProtectionSimulator (0x2990C05A) and the
// fire plane source (0x4C0AE114); simulators.police.object:
// cSC4PoliceSimulator (0x6990C04F) and the police helicopter source
// (0x0C0B06E8). The simulators' Init makes their source with the game's
// constructor (0x006ACE00, 0x006DFF70), so those are detoured to ours too.
#include "core/address_check.h"
#include "core/director.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <cstdint>
#include <cstring>

namespace opensc4::protection_object {
namespace {

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
using NodesFn = char*(__thiscall*)(void* list, uint32_t count, uint32_t unused);
using RandomFn = void*(__thiscall*)(void* random, int32_t seed);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// A list's sentinel from the list helpers (0x00413DD0), linked to itself.
void NewList(char* at)
{
    At<char*>(at, 0) = nullptr;
    char* const sentinel = reinterpret_cast<NodesFn>(addr::SC4List_AllocateNodes)(at, 1, 0);
    At<char*>(sentinel, 0) = sentinel;
    At<char*>(sentinel, 4) = sentinel;
    At<char*>(at, 0) = sentinel;
}

void DestroyList(char* at)
{
    reinterpret_cast<ThisFn>(addr::SC4List_Destroy2)(at);
}

void FreeCurve(char* self, std::size_t curve)
{
    if (void* const points = At<void*>(self, curve))
        Free(points);
}

void ReleaseAt(char* self, std::size_t at)
{
    if (void* const held = At<void*>(self, at))
        Virtual<uint32_t>(held, 0x8);
}

// Our vtables for one class: copies of the game's, with our identity
// functions.
struct Vtables {
    void* table[4][48];
};

template <std::size_t N>
void Copy(void* (&to)[48], std::uintptr_t from)
{
    std::memcpy(to, reinterpret_cast<const void*>(from), N * sizeof(void*));
}

// --- The simulators ------------------------------------------------------------------

// Both have the base at +8, the dispatch interface at +0xC, cIGZSerializable
// at +0x10, and the reference count at +0xC4.
constexpr std::size_t kRefs = 0xC4;

struct Simulator {
    const char* name;
    uint32_t classId;
    uint32_t iid;      // the primary interface
    std::size_t size;
    Vtables vtables;
    bool built;
};

Simulator g_fire{"fire", 0x2990C05A, 0x88F54C7B, 0x108, {}, false};
Simulator g_police{"police", 0x6990C04F, 0x67659895, 0x168, {}, false};

void SetVtables(Simulator& sim, char* self)
{
    At<void**>(self, 0x0) = sim.vtables.table[0];
    At<void**>(self, 0x8) = sim.vtables.table[1];
    At<void**>(self, 0xC) = sim.vtables.table[2];
    At<void**>(self, 0x10) = sim.vtables.table[3];
}

// As the base constructor (0x006F6900), at +8.
void ConstructBase(char* base)
{
    reinterpret_cast<RandomFn>(addr::SC4Random_Construct)(base + 0x10, -1);
    At<uint32_t>(base, 0x28) = 0;
    At<uint32_t>(base, 0x2C) = 0;
    At<uint32_t>(base, 0x30) = 0;
    base[0x34] = 1;
    base[0x35] = 1;
    base[0x36] = 1;
    std::memset(base + 0x38, 0, 0x60 - 0x38);
    At<uint32_t>(base, 0x60) = 1;
    std::memset(base + 0x64, 0, 0x80 - 0x64);
    NewList(base + 0x80);
    std::memset(base + 0x84, 0, 0x94 - 0x84);
    At<uint32_t>(base, 0x94) = Global<uint32_t>(addr::cSC4ProtectionSimulator_DefaultShare);
    At<float>(base, 0x98) = 1.0f;
    base[0x9C] = 0;
    At<uint32_t>(base, 0xA0) = 0;
    At<uint32_t>(base, 0xA4) = 1;
    At<uint32_t>(base, 0xAC) = 0;
    At<uint32_t>(base, 0xB0) = 0;
}

// As the base destructor (0x006F65D0).
void DestroyBase(char* base)
{
    At<std::uintptr_t>(base, 0x0) = addr::cSC4ProtectionSimulator_vftable;
    At<std::uintptr_t>(base, 0x4) = addr::cSC4ProtectionSimulator_vftable_4;
    At<std::uintptr_t>(base, 0x8) = addr::cSC4ProtectionSimulator_vftable_8;
    DestroyList(base + 0x80);
    if (void* const buffer = At<void*>(base, 0x4C))
        Free(buffer);
}

// As the fire constructor (0x006ADA50).
void ConstructFire(char* self)
{
    ConstructBase(self + 8);
    SetVtables(g_fire, self);
    self[0xC0] = 0;
    At<uint32_t>(self, 0xC4) = 0;
    self[0xC8] = 0;
    At<uint32_t>(self, 0xCC) = 0;
    At<uint32_t>(self, 0xD0) = 0;
    std::memset(self + 0xD4, 0, 0x10); // curve
    At<uint32_t>(self, 0xE4) = 0;
    At<float>(self, 0xE8) = 1.0f;
    At<uint32_t>(self, 0xEC) = 0;
    std::memset(self + 0xF0, 0, 0x10); // curve
    At<float>(self, 0x100) = 1.0f;
    At<uint32_t>(self, 0x104) = 0;
}

// As the fire destructor (0x006ADB20).
void DestroyFire(char* self)
{
    SetVtables(g_fire, self);
    FreeCurve(self, 0xF0);
    FreeCurve(self, 0xD4);
    ReleaseAt(self, 0xD0);
    DestroyBase(self + 8);
}

// As the police constructor (0x006E1F80).
void ConstructPolice(char* self)
{
    ConstructBase(self + 8);
    SetVtables(g_police, self);
    const uint32_t zero = Global<uint32_t>(addr::SC4_Zero2);
    self[0xC0] = 0;
    self[0xC1] = 0;
    At<uint32_t>(self, 0xC4) = 0;
    At<uint32_t>(self, 0xC8) = 0;
    At<uint32_t>(self, 0xCC) = 0;
    At<uint32_t>(self, 0xD0) = 0;
    NewList(self + 0xD4);
    std::memset(self + 0xD8, 0, 0xF8 - 0xD8);
    NewList(self + 0xF8);
    At<uint32_t>(self, 0xFC) = zero;
    std::memset(self + 0x100, 0, 0x20); // two curves
    At<uint32_t>(self, 0x120) = zero;
    At<uint32_t>(self, 0x124) = zero;
    At<uint32_t>(self, 0x128) = zero;
    At<uint32_t>(self, 0x12C) = 0;
    At<uint32_t>(self, 0x130) = zero;
    std::memset(self + 0x134, 0, 0x10); // curve
    At<uint32_t>(self, 0x144) = 0;
    std::memset(self + 0x148, 0, 0x10); // curve
    At<uint32_t>(self, 0x158) = zero;
    At<uint32_t>(self, 0x15C) = zero;
    At<uint32_t>(self, 0x160) = zero;
    NewList(self + 0x164);
}

// As the police destructor (0x006E16E0).
void DestroyPolice(char* self)
{
    SetVtables(g_police, self);
    DestroyList(self + 0x164);
    for (const std::size_t curve : {0x148, 0x134, 0x110, 0x100})
        FreeCurve(self, curve);
    DestroyList(self + 0xF8);
    DestroyList(self + 0xD4);
    ReleaseAt(self, 0xC8);
    DestroyBase(self + 8);
}

void Destroy(char* self)
{
    if (At<void**>(self, 0) == g_police.vtables.table[0])
        DestroyPolice(self);
    else
        DestroyFire(self);
}

Simulator& Of(char* self)
{
    return At<void**>(self, 0) == g_police.vtables.table[0] ? g_police : g_fire;
}

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, kRefs);
}

// As the game's (0x006ACED0, 0x006E0060).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    if (iid == 1 || iid == Of(self).iid)
        offset = 0;
    else if (iid == 0x452294AA)
        offset = 0x8;
    else if (iid == 0xC9C00435)
        offset = 0xC;
    else if (iid == 0xE4FDA3D4)
        offset = 0x10;
    else
        return 0;
    *object = self + offset;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x006AD1B0, both): at the last reference, Shutdown, the
// destructor and operator delete.
uint32_t __fastcall Release(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, kRefs);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10); // Shutdown
    static int destroyed = 0;
    if (destroyed++ < 4)
        log::Write("%s object: destroying %p", Of(self).name, static_cast<void*>(self));
    Destroy(self);
    OperatorDelete(self);
    return 0;
}

// The base's slot 4 (this = +8): the deleting destructor.
void* __fastcall DeletingDestructor(char* base, void*, uint32_t flags)
{
    char* const self = base - 8;
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

template <std::size_t Offset>
void SetIdentity(void* (&table)[48])
{
    table[0] = reinterpret_cast<void*>(&QueryInterfaceAt<Offset>);
    table[1] = reinterpret_cast<void*>(&AddRefAt<Offset>);
    table[2] = reinterpret_cast<void*>(&ReleaseAt<Offset>);
}

void BuildFire()
{
    auto& t = g_fire.vtables.table;
    Copy<26>(t[0], addr::cSC4FireProtectionSimulator_vftable);
    Copy<18>(t[1], addr::cSC4FireProtectionSimulator_vftable_8);
    Copy<12>(t[2], addr::cSC4FireProtectionSimulator_vftable_C);
    Copy<6>(t[3], addr::cSC4FireProtectionSimulator_vftable_10);
}

void BuildPolice()
{
    auto& t = g_police.vtables.table;
    Copy<45>(t[0], addr::cSC4PoliceSimulator_vftable);
    Copy<18>(t[1], addr::cSC4PoliceSimulator_vftable_8);
    Copy<12>(t[2], addr::cSC4PoliceSimulator_vftable_C);
    Copy<6>(t[3], addr::cSC4PoliceSimulator_vftable_10);
}

void Build(Simulator& sim, void (*copy)())
{
    if (sim.built)
        return;
    sim.built = true;
    copy();
    auto& t = sim.vtables.table;
    t[0][0] = reinterpret_cast<void*>(&QueryInterface);
    t[0][1] = reinterpret_cast<void*>(&AddRef);
    t[0][2] = reinterpret_cast<void*>(&Release);
    SetIdentity<0x8>(t[1]);
    t[1][4] = reinterpret_cast<void*>(&DeletingDestructor);
    SetIdentity<0xC>(t[2]);
    SetIdentity<0x10>(t[3]);
}

// The factories, as the game's (0x006ADB80, 0x006E2720).
cIGZUnknown* CreateFire()
{
    Build(g_fire, BuildFire);
    auto* const self = static_cast<char*>(OperatorNew(g_fire.size));
    if (!self)
        return nullptr;
    ConstructFire(self);
    log::Write("fire object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

cIGZUnknown* CreatePolice()
{
    Build(g_police, BuildPolice);
    auto* const self = static_cast<char*>(OperatorNew(g_police.size));
    if (!self)
        return nullptr;
    ConstructPolice(self);
    log::Write("police object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

// --- The dispatch sources ------------------------------------------------------------

// 0x28 bytes: +0 dispatch interface, +4 message target, +8 serializable,
// +0x10 references (docs/fire-police.md).
struct Source {
    uint32_t classId;
    uint32_t kind;
    Vtables vtables;
    bool built;
};

Source g_planes{0x4C0AE114, 5, {}, false};
Source g_helicopters{0x0C0B06E8, 6, {}, false};

Source& SourceOf(char* self)
{
    return At<void**>(self, 0) == g_helicopters.vtables.table[0] ? g_helicopters : g_planes;
}

void SetSourceVtables(Source& source, char* self)
{
    At<void**>(self, 0x0) = source.vtables.table[0];
    At<void**>(self, 0x4) = source.vtables.table[1];
    At<void**>(self, 0x8) = source.vtables.table[2];
}

uint32_t __fastcall SourceAddRef(char* self, void*)
{
    return ++At<uint32_t>(self, 0x10);
}

// As the game's (0x006AC550): no answer for cIGZUnknown.
uint8_t __fastcall SourceQueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    if (iid == 0xC9C00435)
        offset = 0;
    else if (iid == 0x452294AA)
        offset = 4;
    else if (iid == 0xE4FDA3D4)
        offset = 8;
    else
        return 0;
    *object = self + offset;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's destructors (0x006ACE60, 0x006DFFD0).
void* __fastcall SourceDeletingDestructor(char* self, void*, uint32_t flags)
{
    SetSourceVtables(SourceOf(self), self);
    DestroyList(self + 0x1C);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// As the game's (0x006DFAF0): Shutdown, then the deleting destructor.
uint32_t __fastcall SourceRelease(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, 0x10);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    reinterpret_cast<ThisFn>(addr::SC4DispatchSource_Shutdown)(self);
    Virtual<void*>(self, 0x30, 1u);
    return 0;
}

template <std::size_t Offset>
uint8_t __fastcall SourceQueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return SourceQueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall SourceAddRefAt(char* part, void*)
{
    return SourceAddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall SourceReleaseAt(char* part, void*)
{
    return SourceRelease(part - Offset, nullptr);
}

void BuildSource(Source& source, std::uintptr_t primary, std::uintptr_t target, std::uintptr_t serializable)
{
    if (source.built)
        return;
    source.built = true;
    auto& t = source.vtables.table;
    Copy<18>(t[0], primary);
    Copy<4>(t[1], target);
    Copy<6>(t[2], serializable);
    t[0][0] = reinterpret_cast<void*>(&SourceQueryInterface);
    t[0][1] = reinterpret_cast<void*>(&SourceAddRef);
    t[0][2] = reinterpret_cast<void*>(&SourceRelease);
    t[0][12] = reinterpret_cast<void*>(&SourceDeletingDestructor);
    t[1][0] = reinterpret_cast<void*>(&SourceQueryInterfaceAt<4>);
    t[1][1] = reinterpret_cast<void*>(&SourceAddRefAt<4>);
    t[1][2] = reinterpret_cast<void*>(&SourceReleaseAt<4>);
    t[2][0] = reinterpret_cast<void*>(&SourceQueryInterfaceAt<8>);
    t[2][1] = reinterpret_cast<void*>(&SourceAddRefAt<8>);
    t[2][2] = reinterpret_cast<void*>(&SourceReleaseAt<8>);
}

void BuildPlanes()
{
    BuildSource(g_planes, addr::SC4FirePlaneSource_vftable, addr::SC4FirePlaneSource_vftable_4,
                addr::SC4FirePlaneSource_vftable_8);
}

void BuildHelicopters()
{
    BuildSource(g_helicopters, addr::SC4PoliceHelicopterSource_vftable, addr::SC4PoliceHelicopterSource_vftable_4,
                addr::SC4PoliceHelicopterSource_vftable_8);
}

// As the game's constructors (0x006ACE00, 0x006DFF70).
void ConstructSource(Source& source, char* self)
{
    SetSourceVtables(source, self);
    self[0xC] = 0;
    At<uint32_t>(self, 0x10) = 0;
    At<uint32_t>(self, 0x14) = source.kind;
    At<uint32_t>(self, 0x18) = 0;
    NewList(self + 0x1C);
    At<uint32_t>(self, 0x20) = 0;
    At<uint32_t>(self, 0x24) = 1;
}

void* __fastcall ConstructPlanes(char* self, void*)
{
    BuildPlanes();
    ConstructSource(g_planes, self);
    return self;
}

void* __fastcall ConstructHelicopters(char* self, void*)
{
    BuildHelicopters();
    ConstructSource(g_helicopters, self);
    return self;
}

cIGZUnknown* CreatePlanes()
{
    auto* const self = static_cast<char*>(OperatorNew(0x28));
    return self ? static_cast<cIGZUnknown*>(ConstructPlanes(self, nullptr)) : nullptr;
}

cIGZUnknown* CreateHelicopters()
{
    auto* const self = static_cast<char*>(OperatorNew(0x28));
    return self ? static_cast<cIGZUnknown*>(ConstructHelicopters(self, nullptr)) : nullptr;
}

// --- Registration ----------------------------------------------------------------------

bool Detour(std::uintptr_t address, void* ours)
{
    static void* originals[2] = {};
    static int used = 0;
    const addr::Entry* found = addr_check::Find(address);
    return found && used < 2 && hook::Install(*found, ours, &originals[used++]);
}

bool ApplyFire(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(g_fire.classId, CreateFire) &&
           director::AddClass(g_planes.classId, CreatePlanes) &&
           Detour(addr::SC4FirePlaneSource_Construct, reinterpret_cast<void*>(&ConstructPlanes));
}

bool ApplyPolice(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(g_police.classId, CreatePolice) &&
           director::AddClass(g_helicopters.classId, CreateHelicopters) &&
           Detour(addr::SC4PoliceHelicopterSource_Construct, reinterpret_cast<void*>(&ConstructHelicopters));
}

const bool g_fireRegistered = registry::Register({
    "simulators.fire.object",
    "simulators/fire",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    0x2990C05A,
    "Our cSC4FireProtectionSimulator and fire plane source objects under the game's class IDs: factories, "
    "constructors, destructors, identity; methods from the (patched) vtables (docs/fire-police.md)",
    false,
    ApplyFire,
});

const bool g_policeRegistered = registry::Register({
    "simulators.police.object",
    "simulators/police",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    0x6990C04F,
    "Our cSC4PoliceSimulator and police helicopter source objects under the game's class IDs: factories, "
    "constructors, destructors, identity; methods from the (patched) vtables (docs/fire-police.md)",
    false,
    ApplyPolice,
});

} // namespace
} // namespace opensc4::protection_object
