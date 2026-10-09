// Our cSC4OrdinanceSimulator object (docs/ordinance.md, "Our objects"): our
// factory registered under the game's class ID, our constructor,
// destructor, QueryInterface, AddRef and Release, and our own copies of the
// three vtables. The object has the game's layout (0x40 bytes, the same
// fields), so every method, the game's or ours, works on it; the vtables are
// copied from the game's when the first object is made, after the other
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

namespace opensc4::ordinance_sim {
namespace {

constexpr uint32_t kClassId = 0xA990C08E;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidOrdinanceSimulator = 0x41193C3A;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0x40;
constexpr std::size_t kRefs = 0x10;
constexpr std::size_t kMap = 0x30;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// The three vtables (the game's: 0x00ABEAC0 with the deleting destructor in
// slot 26, 0x00ABEAB0, 0x00ABEA98; packed, no RTTI slots).
constexpr int kSlots = 27, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

// --- Identity --------------------------------------------------------------

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, kRefs);
}

// As the game's (0x0080D340): the reference through the primary vtable.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidOrdinanceSimulator:
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
// destructor and operator delete, as the game's Release (0x0080D520) and
// deleting destructor (slot 26).
uint32_t __fastcall Release(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, kRefs);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10); // Shutdown
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("ordinance object: destroying %p", static_cast<void*>(self));
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

// As the game's constructor (0x0080E6E0): only the fields it sets; the rest
// stay as operator new returned them. The ordinance map's header comes from
// the STL allocator.
void Construct(char* self)
{
    SetVtables(self);
    At<uint8_t>(self, 0xC) = 0;
    At<uint8_t>(self, 0xD) = 0;
    At<uint32_t>(self, kRefs) = 0;
    At<char*>(self, kMap) = nullptr;
    auto* const header = static_cast<char*>(Allocate(0x18));
    At<char*>(self, kMap) = header;
    At<uint32_t>(self, kMap + 4) = 0;
    header[0] = 0;
    At<void*>(header, 0x4) = nullptr;
    At<void*>(header, 0x8) = header;
    At<void*>(header, 0xC) = header;
}

// The map's nodes below a node (0x0080DFC0): each ordinance released.
void EraseNodes(char* node)
{
    while (node) {
        EraseNodes(At<char*>(node, 0xC));
        char* const left = At<char*>(node, 0x8);
        if (void* const ordinance = At<void*>(node, 0x14))
            Virtual<uint32_t>(ordinance, 0x8);
        Free(node);
        node = left;
    }
}

// As the game's destructor (0x0080E1D0).
void Destroy(char* self)
{
    SetVtables(self);
    char* const header = At<char*>(self, kMap);
    if (At<uint32_t>(self, kMap + 4)) {
        EraseNodes(At<char*>(header, 0x4));
        At<void*>(header, 0x8) = header;
        At<void*>(header, 0x4) = nullptr;
        At<void*>(header, 0xC) = header;
        At<uint32_t>(self, kMap + 4) = 0;
    }
    if (header)
        Free(header);
}

// The vtables: the game's as they are now (with whatever the other
// replacements swapped in), with our identity functions.
void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4OrdinanceSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4OrdinanceSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4OrdinanceSimulator_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[26] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
}

// The factory, as the game's (0x0080E890): operator new(0x40), then the
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
        log::Write("ordinance object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool Apply(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registered = registry::Register({
    "simulators.ordinance.object",
    "simulators/ordinance",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4OrdinanceSimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/ordinance.md)",
    false,
    Apply,
});

} // namespace
} // namespace opensc4::ordinance_sim
