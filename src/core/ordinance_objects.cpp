// Our ordinance objects (docs/ordinance.md, "Our objects"): factories for the
// 21 ordinance class IDs (cSC4OrdinanceSimple and the 20 classes derived from
// it), our constructor, destructors, QueryInterface, AddRef and Release, and
// our own copies of each class's four vtables. The objects have the game's
// layout (0xA0 bytes, 0xA8 for the classes that reference game objects), so
// every method, the game's or ours, works on them; the vtables are copied
// from the game's when the first object is made, after the other
// replacements have patched them. The property holder at +0x8 stays the
// game's generic cSCPropertyHolder code (constructor 0x005FF1C0, destructor
// 0x005FF220, QueryInterface 0x005FD310).
#include "core/director.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::ordinance_class {
namespace {

constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidOrdinance = 0xAA5E2247;
constexpr uint32_t kIidSimple = 0x8BD4FC74;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kRefs = 0x24;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// What a class's destructor releases besides the base class's.
enum class Extra { None, Service, Services };

struct Class {
    uint32_t id;
    std::uintptr_t vtable, vtable4, vtable8, vtable1c;
    Extra extra;
};

const Class kClasses[] = {
    {0x8BD4FC74, 0x00ABEA10, 0x00ABE9F4, 0x00ABE990, 0x00ABE978, Extra::None}, // cSC4OrdinanceSimple
    {0xA2BF1DDC, 0x00ABD9D8, 0x00ABD9BC, 0x00ABD958, 0x00ABD940, Extra::None},
    {0xA2BF1E43, 0x00ABDAF8, 0x00ABDADC, 0x00ABDA78, 0x00ABDA60, Extra::None},
    {0x00D0723D, 0x00ABDC18, 0x00ABDBFC, 0x00ABDB98, 0x00ABDB80, Extra::None},
    {0x40D07236, 0x00ABDD38, 0x00ABDD1C, 0x00ABDCB8, 0x00ABDCA0, Extra::Service},
    {0x815B4CEF, 0x00ABDE58, 0x00ABDE3C, 0x00ABDDD8, 0x00ABDDC0, Extra::Service},
    {0x22F6E80C, 0x00ABDF78, 0x00ABDF5C, 0x00ABDEF8, 0x00ABDEE0, Extra::Services},
    {0xA0D07129, 0x00ABE098, 0x00ABE07C, 0x00ABE018, 0x00ABE000, Extra::None},
    {0x62BF1DAA, 0x00ABE1B8, 0x00ABE19C, 0x00ABE138, 0x00ABE120, Extra::None},
    {0x62BF1DB9, 0x00ABE2D8, 0x00ABE2BC, 0x00ABE258, 0x00ABE240, Extra::Service},
    {0xE0D0722E, 0x00ABE3F8, 0x00ABE3DC, 0x00ABE378, 0x00ABE360, Extra::None},
    {0x22F6E81B, 0x00ABE518, 0x00ABE4FC, 0x00ABE498, 0x00ABE480, Extra::None},
    {0x82B9999B, 0x00ABE638, 0x00ABE61C, 0x00ABE5B8, 0x00ABE5A0, Extra::None},
    {0xE0D07233, 0x00ABE758, 0x00ABE73C, 0x00ABE6D8, 0x00ABE6C0, Extra::Service},
    {0xA2BF1DE5, 0x00ABE878, 0x00ABE85C, 0x00ABE7F8, 0x00ABE7E0, Extra::None},
    {0xC2F6E81F, 0x00ABEBC8, 0x00ABEBAC, 0x00ABEB48, 0x00ABEB34, Extra::None},
    {0x62F6E7CF, 0x00ABECE8, 0x00ABECCC, 0x00ABEC68, 0x00ABEC50, Extra::Service},
    {0x42BF1E18, 0x00ABEE08, 0x00ABEDEC, 0x00ABED88, 0x00ABED70, Extra::Services},
    {0xC2BF1E04, 0x00ABEF28, 0x00ABEF0C, 0x00ABEEA8, 0x00ABEE90, Extra::None},
    {0x02BF1DFA, 0x00ABF048, 0x00ABF02C, 0x00ABEFC8, 0x00ABEFB0, Extra::None},
    {0xC2BF1DC5, 0x00ABF168, 0x00ABF14C, 0x00ABF0E8, 0x00ABF0D0, Extra::None},
};

constexpr int kClassCount = sizeof(kClasses) / sizeof(kClasses[0]);

// Our copies of each class's vtables: cISC4Ordinance (32 slots),
// cIGZSerializable at +0x4 (6), cSCPropertyHolder at +0x8 (25, the deleting
// destructor in slot 22) and its second interface at +0x1C (5).
constexpr int kSlots = 32, kSlots4 = 6, kSlots8 = 25, kSlots1c = 5;
struct Vtables {
    void* primary[kSlots];
    void* at4[kSlots4];
    void* at8[kSlots8];
    void* at1c[kSlots1c];
};
Vtables g_vtables[kClassCount];

void SetVtables(char* self, int index)
{
    Vtables& v = g_vtables[index];
    At<void**>(self, 0x0) = v.primary;
    At<void**>(self, 0x4) = v.at4;
    At<void**>(self, 0x8) = v.at8;
    At<void**>(self, 0x1C) = v.at1c;
}

// --- Identity --------------------------------------------------------------

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, kRefs);
}

// As the game's (0x0080C920): the ordinance interfaces, else the property
// holder's QueryInterface (0x005FD310).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    switch (iid) {
    case kIidOrdinance:
    case kIidUnknown:
    case kIidSimple:
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    case kIidSerializable:
        *object = self + 0x4;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    default: {
        using HolderQueryFn = uint8_t(__thiscall*)(void* holder, uint32_t iid, void** object);
        return reinterpret_cast<HolderQueryFn>(addr::cSCPropertyHolder_QueryInterface)(self + 0x8, iid, object);
    }
    }
}

// As the game's (0x0080C990): the count stays at 1 when the last reference
// goes, then the deleting destructor (+0x8 slot 22).
uint32_t __fastcall Release(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, kRefs);
    if (refs > 1)
        return --refs;
    Virtual<void*>(self + 0x8, 0x58, 1u);
    return 0;
}

// As the game's destructor (0x0080CD50): the base class's vtables, the
// strings, then the property holder's destructor.
void DestroyBase(char* self)
{
    SetVtables(self, 0);
    if (At<uint8_t>(self, 0x28)) {
        At<uint8_t>(self, 0x28) = 0;
        At<uint8_t>(self, 0x93) = 0;
    }
    const auto stringVtable = reinterpret_cast<void*>(addr::SC4String_Vtable);
    At<void*>(self, 0x50) = stringVtable;
    if (void* const buffer = At<void*>(self, 0x54))
        Free(buffer);
    At<void*>(self, 0x3C) = stringVtable;
    if (void* const buffer = At<void*>(self, 0x40))
        Free(buffer);
    reinterpret_cast<ThisFn>(addr::cSCPropertyHolder_Destroy)(self + 0x8);
}

// The deleting destructors (+0x8 slot 22): 0x0080BE50 (the base),
// 0x0080C8A0 (+0xA0 released first) and 0x0080C320 (+0xA4, then +0xA0).
template <Extra Kind>
void* __fastcall DeletingDestructor(char* holder, void*, uint32_t flags)
{
    char* const self = holder - 0x8;
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("ordinance objects: first destructor (%d)", static_cast<int>(Kind));
    }
    if (Kind == Extra::Services)
        if (void* const object = At<void*>(self, 0xA4))
            Virtual<uint32_t>(object, 0x8);
    if (Kind != Extra::None)
        if (void* const object = At<void*>(self, 0xA0))
            Virtual<uint32_t>(object, 0x8);
    DestroyBase(self);
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

template <std::size_t Offset>
void Identity(void** vtable)
{
    vtable[0] = reinterpret_cast<void*>(&QueryInterfaceAt<Offset>);
    vtable[1] = reinterpret_cast<void*>(&AddRefAt<Offset>);
    vtable[2] = reinterpret_cast<void*>(&ReleaseAt<Offset>);
}

// The vtables: the game's as they are now (with whatever the other
// replacements swapped in), with our identity and destructor.
void BuildVtables()
{
    for (int i = 0; i < kClassCount; ++i) {
        const Class& c = kClasses[i];
        Vtables& v = g_vtables[i];
        std::memcpy(v.primary, reinterpret_cast<const void*>(c.vtable), sizeof(v.primary));
        std::memcpy(v.at4, reinterpret_cast<const void*>(c.vtable4), sizeof(v.at4));
        std::memcpy(v.at8, reinterpret_cast<const void*>(c.vtable8), sizeof(v.at8));
        std::memcpy(v.at1c, reinterpret_cast<const void*>(c.vtable1c), sizeof(v.at1c));
        v.primary[0] = reinterpret_cast<void*>(&QueryInterface);
        v.primary[1] = reinterpret_cast<void*>(&AddRef);
        v.primary[2] = reinterpret_cast<void*>(&Release);
        Identity<0x4>(v.at4);
        Identity<0x8>(v.at8);
        Identity<0x1C>(v.at1c);
        switch (c.extra) {
        case Extra::None: v.at8[22] = reinterpret_cast<void*>(&DeletingDestructor<Extra::None>); break;
        case Extra::Service: v.at8[22] = reinterpret_cast<void*>(&DeletingDestructor<Extra::Service>); break;
        case Extra::Services: v.at8[22] = reinterpret_cast<void*>(&DeletingDestructor<Extra::Services>); break;
        }
    }
}

// As the game's constructor (0x0080CC50): the property holder's constructor
// between the base and the final vtables, the name and description strings
// with 8-byte buffers from the STL allocator, the chance from 0x00B4E734.
void Construct(char* self)
{
    At<uint32_t>(self, 0x0) = 0x00ABE900;
    At<uint32_t>(self, 0x4) = 0x00A80784;
    reinterpret_cast<ThisFn>(addr::cSCPropertyHolder_Construct)(self + 0x8);
    SetVtables(self, 0);
    At<uint32_t>(self, kRefs) = 0;
    At<uint8_t>(self, 0x28) = 0;
    std::memset(self + 0x2C, 0, 0x10);
    const auto stringVtable = reinterpret_cast<void*>(addr::SC4String_Vtable);
    for (const std::size_t string : {std::size_t{0x3C}, std::size_t{0x50}}) {
        std::memset(self + string + 4, 0, 0xC);
        auto* const buffer = static_cast<char*>(Allocate(8));
        At<char*>(self, string + 0x4) = buffer;
        At<char*>(self, string + 0x8) = buffer;
        At<char*>(self, string + 0xC) = buffer + 8;
        buffer[0] = 0;
        At<uint32_t>(self, string + 0x10) = 0;
        At<void*>(self, string) = stringVtable;
    }
    At<uint32_t>(self, 0x64) = 0;
    At<uint32_t>(self, 0x68) = Global<uint32_t>(addr::Ordinance_DefaultChance);
    std::memset(self + 0x70, 0, 0x20);
    std::memset(self + 0x90, 0, 4);
    std::memset(self + 0x98, 0, 8);
}

// The factories, as the game's (0x0080CDC0 and one per derived class):
// operator new, the constructor, the class's vtables, the referenced objects
// cleared. GZCOM factories return objects with no references.
template <int Index>
cIGZUnknown* Create()
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    const Class& c = kClasses[Index];
    auto* const self = static_cast<char*>(OperatorNew(c.extra == Extra::None ? 0xA0 : 0xA8));
    if (!self)
        return nullptr;
    Construct(self);
    if (Index != 0) {
        SetVtables(self, Index);
        At<void*>(self, 0xA0) = nullptr;
        if (c.extra == Extra::Services)
            At<void*>(self, 0xA4) = nullptr;
    }
    static int created = 0;
    if (created++ < 3)
        log::Write("ordinance objects: created %08X at %p", static_cast<unsigned>(c.id), static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

template <int... I>
bool AddAll(std::integer_sequence<int, I...>)
{
    return (director::AddClass(kClasses[I].id, &Create<I>) && ...);
}

bool Apply(registry::Mode)
{
    return game::IsBuild641() && AddAll(std::make_integer_sequence<int, kClassCount>{});
}

const bool g_registered = registry::Register({
    "simulators.ordinance.objects",
    "simulators/ordinance",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    0x8BD4FC74,
    "Our ordinance objects under the 21 ordinance class IDs: factories, constructor, destructors, identity; "
    "methods from the (patched) vtables (docs/ordinance.md)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::ordinance_class
