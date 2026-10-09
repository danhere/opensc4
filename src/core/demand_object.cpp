// Our own cSC4DemandSimulator object under the game's class ID (build 641;
// docs/demand.md, "Our object"): the factory, constructor and destructor,
// the identity slots, and copies of the game's three vtables (holding ours
// where those replacements are on).
#include "core/demand_sim.h"

#include "core/director.h"
#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <cstdint>
#include <cstring>

namespace opensc4::demand_sim {
namespace {

constexpr uint32_t kClassId = 0x2990BFF2;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidDemand = 0x47967E16;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0xDC;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using EraseFn = void(__thiscall*)(void* tree, TreeNode* root);
using ListFn = void(__thiscall*)(void* list);
using ResizeFn = void(__thiscall*)(void* vector, uint32_t count, const float* fill);

const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto StlAllocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// The game's three vtables: 0x00AB0E48 (14 slots: the deleting destructor in
// 12, AddDemand in 13), 0x00AB0E38, 0x00AB0E20.
constexpr int kSlots = 14, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, field::kRefCount);
}

// As the game's (0x006A2310): a null result is still a success, as there.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidDemand:
    case kIidUnknown: offset = 0; break;
    case kIidMessageTarget: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self ? self + offset : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

void Destroy(char* self);

// As the game's Release (0x006BD8A0, shared): at the last reference,
// Shutdown and the deleting destructor; the count stays at 1.
uint32_t __fastcall Release(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, field::kRefCount);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x30, 1u);
    return 0;
}

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("demand object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
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

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
}

// An empty map: a header of `size` bytes from the STL allocator.
void NewMap(char* self, std::size_t offset, std::size_t size)
{
    At<void*>(self, offset) = nullptr;
    auto* const header = static_cast<TreeNode*>(StlAllocate(size));
    At<TreeNode*>(self, offset) = header;
    At<uint32_t>(self, offset + 4) = 0;
    *reinterpret_cast<uint8_t*>(&header->color) = 0;
    header->parent = nullptr;
    header->left = header;
    header->right = header;
}

void* NewList()
{
    void** const head = Virtual<void**>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0xC, 0xCu);
    head[0] = head;
    head[1] = head;
    return head;
}

// As the game's constructor (0x006A35E0): only the fields it sets; the rest
// stay as operator new returned them.
void Construct(char* self)
{
    SetVtables(self);
    At<uint8_t>(self, 0xC) = 0;
    At<uint8_t>(self, 0xD) = 0;
    At<uint32_t>(self, 0x10) = 0;
    NewMap(self, field::kPending, 0x18);
    NewMap(self, field::kCounted, 0x18);
    for (std::size_t offset = 0x34; offset <= 0x4C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    NewMap(self, field::kDemands, 0x18);
    NewMap(self, field::kEffects, 0x28);
    At<void*>(self, 0x98) = nullptr;
    At<void*>(self, 0x98) = NewList();
    At<void*>(self, 0x9C) = nullptr;
    At<void*>(self, 0x9C) = NewList();
    for (std::size_t offset = 0xA0; offset <= 0xB4; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, 0xB8) = 0x5A;
    At<uint32_t>(self, 0xBC) = 0;
    At<uint32_t>(self, 0xC0) = 0;
    At<uint32_t>(self, 0xC4) = 0;
    At<uint32_t>(self, 0xC8) = 0x3F800000;
    At<uint32_t>(self, 0xCC) = 0x40000000;
    At<uint32_t>(self, 0xD0) = 0x41A00000;
    At<uint32_t>(self, 0xD4) = 0x3DCCCCCD;
    At<uint32_t>(self, 0xD8) = 0x41A00000;
    At<uint32_t>(self, field::kCellIterator) = 0;
    At<uint32_t>(self, field::kState) = 3;
    const float zero = 0.0f;
    reinterpret_cast<ResizeFn>(addr::SC4FloatVector_Resize2)(self + 0xBC, 13, &zero);
}

void DestroyMap(char* self, std::size_t offset, std::uintptr_t erase)
{
    if (At<uint32_t>(self, offset + 4)) {
        TreeNode* const header = At<TreeNode*>(self, offset);
        reinterpret_cast<EraseFn>(erase)(self + offset, header->parent);
        header->left = header;
        header->parent = nullptr;
        header->right = header;
        At<uint32_t>(self, offset + 4) = 0;
    }
    if (void* const header = At<void*>(self, offset))
        Free(header);
}

// As the game's destructor (0x006A3020): the weights' buffer, the two lists,
// then the four maps.
void Destroy(char* self)
{
    SetVtables(self);
    if (void* const weights = At<void*>(self, 0xBC))
        Free(weights);
    const auto destroy4 = reinterpret_cast<ListFn>(addr::SC4List_Destroy4);
    destroy4(self + 0x9C);
    destroy4(self + 0x98);
    DestroyMap(self, field::kEffects, addr::SC4EffectMap_EraseTree);
    DestroyMap(self, field::kDemands, addr::SC4DemandMap_EraseTree);
    DestroyMap(self, field::kCounted, addr::SC4FloatMap_EraseTree2);
    DestroyMap(self, field::kPending, addr::SC4FloatMap_EraseTree2);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4DemandSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4DemandSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4DemandSimulator_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[12] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
}

// The factory, as the game's (0x006A6F30): operator new(0xDC), then the
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
        log::Write("demand object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool ApplyObject(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registeredObject = registry::Register({
    "simulators.demand.object",
    "simulators/demand",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4DemandSimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/demand.md)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::demand_sim
