// Our cSC4AdvisorSystem object (docs/advisors.md, "Our object"): our
// factory registered under the game's class ID, our constructor,
// destructor, AddRef and Release, and our own copies of the six vtables.
// The object has the game's layout (0xA8 bytes, the reference count in the
// base at +0x14), so every method, the game's or ours, works on it; the
// vtables are copied from the game's when the first object is made, after
// the other replacements have patched them. Off, the game's own object is
// used.
#include "core/director.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <cstdint>
#include <cstring>

namespace opensc4::advisors {
namespace {

constexpr uint32_t kClassId = 0x4A0DD616;
constexpr std::size_t kSize = 0xA8;
constexpr std::size_t kBase = 0x14; // reference-counted base, count at +0x18
constexpr std::size_t kRefs = 0x18;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

constexpr int kSlots = 29, kSlots4 = 4, kSlots8 = 4, kSlotsC = 1, kSlots10 = 6, kSlots14 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable10[kSlots10];
void* g_vtable14[kSlots14];

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    At<void**>(self, 0xC) = g_vtableC;
    At<void**>(self, 0x10) = g_vtable10;
    At<void**>(self, kBase) = g_vtable14;
}

// A map emptied with its subtree eraser, then its header freed.
void ClearMap(char* self, std::size_t map, std::uintptr_t eraseSubtree)
{
    char* const header = At<char*>(self, map);
    if (At<uint32_t>(self, map + 4)) {
        reinterpret_cast<void(__thiscall*)(void*, char*)>(eraseSubtree)(self + map, At<char*>(header, 0x4));
        At<char*>(header, 0x8) = header;
        At<char*>(header, 0x4) = nullptr;
        At<char*>(header, 0xC) = header;
        At<uint32_t>(self, map + 4) = 0;
    }
    if (header)
        Free(header);
}

// As the game's (0x0041A0D0).
void Destroy(char* self)
{
    SetVtables(self);
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Destruct)(self + 0x90);
    if (void* const tags = At<void*>(self, 0x80))
        Free(tags);
    reinterpret_cast<ThisFn>(addr::SC4RefVector_Destroy)(self + 0x74);
    ClearMap(self, 0x68, addr::SC4AdvisorEventMap_EraseSubtree);
    for (std::size_t at : {std::size_t{0x64}, std::size_t{0x5C}, std::size_t{0x58}})
        if (void* const object = At<void*>(self, at))
            Virtual<uint32_t>(object, 0x8);
    ClearMap(self, 0x4C, addr::SC4AdvisorMap_EraseSubtree);
    At<std::uintptr_t>(self, kBase) = addr::cRZBaseUnknown_Vtable;
    At<std::uintptr_t>(self, 0xC) = addr::cSC4AdvisorSystem_vftable_C_base;
    Global<void*>(addr::cSC4AdvisorSystem_Instance) = nullptr;
}

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, kRefs);
}

// As the game's (0x0090D997 on the base): the deleting destructor when the
// last reference goes.
uint32_t __fastcall Release(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, kRefs);
    if (refs > 1)
        return --refs;
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("advisor system object: destroying %p", static_cast<void*>(self));
    Virtual<void*>(self + kBase, 0xC, 1u);
    return 0;
}

template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return Virtual<uint8_t>(part - Offset, 0x0, iid, object);
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
void* __fastcall DeletingDestructorAt(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - Offset, nullptr, flags);
}

// A map header from the game's allocator, linked to itself.
char* NewHeader(std::size_t size)
{
    auto* const header =
        static_cast<char*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(size));
    At<uint8_t>(header, 0x0) = 0;
    At<char*>(header, 0x4) = nullptr;
    At<char*>(header, 0x8) = header;
    At<char*>(header, 0xC) = header;
    return header;
}

// As the game's constructor (0x0041A990): only the fields it sets.
void Construct(char* self)
{
    Global<void*>(addr::cSC4AdvisorSystem_Instance) = self;
    At<uint32_t>(self, kRefs) = 0;
    SetVtables(self);
    At<uint8_t>(self, 0x28) = 0;
    At<uint8_t>(self, 0x29) = 0;
    {
        // Clock seeds count under the original's call site.
        const fixed_seed::SiteOverride site(0x0041AA02);
        reinterpret_cast<void*(__thiscall*)(void*, int32_t)>(addr::SC4Random_Construct)(self + 0x30, -1);
    }
    At<int32_t>(self, 0x48) = 0;
    At<char*>(self, 0x4C) = nullptr;
    At<char*>(self, 0x4C) = NewHeader(0x18);
    At<uint32_t>(self, 0x50) = 0;
    At<void*>(self, 0x58) = nullptr;
    At<void*>(self, 0x5C) = nullptr;
    At<uint8_t>(self, 0x60) = 0;
    At<void*>(self, 0x64) = nullptr;
    At<char*>(self, 0x68) = nullptr;
    At<char*>(self, 0x68) = NewHeader(0x20);
    At<uint32_t>(self, 0x6C) = 0;
    std::memset(self + 0x74, 0, 0x18); // context advice and data tags
    reinterpret_cast<void*(__thiscall*)(void*, int32_t)>(addr::SC4Stopwatch_Construct)(self + 0x90, 4);
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Start)(self + 0x90);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4AdvisorSystem_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4AdvisorSystem_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4AdvisorSystem_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4AdvisorSystem_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable10, reinterpret_cast<const void*>(addr::cSC4AdvisorSystem_vftable_10), sizeof(g_vtable10));
    std::memcpy(g_vtable14, reinterpret_cast<const void*>(addr::cSC4AdvisorSystem_vftable_14), sizeof(g_vtable14));
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtableC[0] = reinterpret_cast<void*>(&DeletingDestructorAt<0xC>);
    g_vtable10[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x10>);
    g_vtable10[1] = reinterpret_cast<void*>(&AddRefAt<0x10>);
    g_vtable10[2] = reinterpret_cast<void*>(&ReleaseAt<0x10>);
    g_vtable14[0] = reinterpret_cast<void*>(&QueryInterfaceAt<kBase>);
    g_vtable14[1] = reinterpret_cast<void*>(&AddRefAt<kBase>);
    g_vtable14[2] = reinterpret_cast<void*>(&ReleaseAt<kBase>);
    g_vtable14[3] = reinterpret_cast<void*>(&DeletingDestructorAt<kBase>);
}

// The factory, as the game's (0x0041B220): operator new(0xA8), then the
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
        log::Write("advisor system object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool Apply(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registered = registry::Register({
    "simulators.advisors.object",
    "simulators/advisors",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4AdvisorSystem object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/advisors.md)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::advisors
