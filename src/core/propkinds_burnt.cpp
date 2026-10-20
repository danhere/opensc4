// Our cSC4PropOccupantBurntBuilding's logic (build 641; the class map, the
// vtables and the fields are in propkinds.h): the prop a fire leaves where
// a building stood (the lot developer's ExplodeLot makes it, 0x006CC310).
// It keeps the burnt building's box, loads five texture bindings and a
// render state for itself, and adds its model to the renderer while it is
// shown. The prop underneath (cSC4PropOccupant) is called at its addresses.
//
// The drawing is in propkinds_burnt_draw.cpp, a group of its own: the quad
// builder 0x005EDC60 (called here at its address, so ours or the game's as
// that group says) and the functions behind the model interface (the
// virtual base at +0xD4). The model part's constructor and destructor
// (0x00801CF0, 0x00801810) stay the game's. The texture bindings, the
// render state and the renderer are told through their interfaces and
// helpers.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the occupant part 0x00AA4A98: 0x14 IsInitialized none (0x00458A40);
//   - the occupant handed to SetOccupant: 0x28 GetBoundingBox two
//     (0x005DED60 in the building's vtable);
//   - the renderer 0x00ABAB60 (the global at 0x00B43DD0): 0x6C one
//     (0x007CA180), 0x70 one (0x007CA200), 0x74 one (0x007CA3D0);
//   - the framework: 0x14 GetSystemService three;
//   - the texture binding factory 0x00ABD8C8: 0x18 two (0x0080A380);
//   - the streams 0x00AA2310 (out) and 0x00AA2280 (in): 0x1C one, 0x48
//     none.
// Direct callees: 0x005EC960 (ret 8), 0x005ECE90 and 0x005EF360 (ret 4),
// 0x005EE050 (ret), 0x00801CF0 (ret 4), 0x00801810 (ret), 0x005E8C90 and
// 0x005E8980 (ret), 0x005E8B90 (ret 4), 0x005E8C40 (ret), 0x0049FC70 (ret
// 8), 0x004495C0 (ret), 0x007D2B50 (ret 0x38), 0x008090E0 (ret), 0x005EDC60
// (ret), 0x0090CF9A (ret), 0x0090D031 and 0x0090CFF9 (ret 4); cdecl:
// 0x008793EC (none), 0x005DA600, 0x005DA5B0 (two), 0x0090CF63, 0x005E5620
// (one).
//
// Callers from outside the class: the class registration (0x004607C6,
// which calls GetGZCLSID and keeps the factory's address, before any
// replacement is installed) and the lot developer's 0x006CC310 / 0x006CC320
// / 0x006CC33C (allocator, constructor with 1, SetOccupant: it reads only
// EAX after each call and keeps nothing in ECX or EDX across them).
// Everything else is reached through the vtables and their thunks. The
// detours are plain functions.
#include "core/propkinds.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::propkinds {
namespace {

constexpr std::uintptr_t kPool = addr::cSC4PropOccupantBurntBuilding_Pool;
constexpr uint32_t kPoolReopenAt = 0x40000038;
constexpr uint32_t kTextureFactoryClass = 0x1AC0E11A;     // cIS3DTextureBindingFactory
constexpr uint32_t kTextureFactoryInterface = 0xFAC0E219;

// The virtual base (the model interface) of an object whose vbptr is set:
// the vbptr + its table's second entry.
char* VirtualBase(char* self)
{
    return self + burnt::kVbptr + At<int32_t>(At<char*>(self, burnt::kVbptr), 0x4);
}

// ====================================================================================
// The prop part (`self` is the object)
// ====================================================================================

// Slot 0 (0x005ECC60, a jump): the prop's QueryInterface with the same
// `this`. The class has no interface of its own; its class ID is not
// answered.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("burnt QueryInterface", logged);
    return This<uint8_t, uint32_t, void**>(addr::cSC4PropOccupant_QueryInterface, self, iid, object);
}

// Not virtual (0x005EEB70): the occupant's box (its slot 10) into the prop's
// box, copied into the burnt box (+0xA0, low corner then high), then the
// quad builder. No return value.
void __fastcall SetOccupant(char* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("burnt SetOccupant", logged);
    Virtual<void>(occupant, 0x28, static_cast<void*>(self + burnt::kMin), static_cast<void*>(self + burnt::kMax));
    for (std::size_t i = 0; i < 12; i += 4) {
        At<uint32_t>(self, burnt::kBox + i) = At<uint32_t>(self, burnt::kMin + i);
        At<uint32_t>(self, burnt::kBox + 12 + i) = At<uint32_t>(self, burnt::kMax + i);
    }
    This<void>(addr::cSC4PropOccupantBurntBuilding_BuildQuad, self);
}

// ====================================================================================
// The occupant part (`occupant` is the object + 8)
// ====================================================================================

// Slot 3 (0x005F0600): nothing when initialized. The base's Init (not
// tested again). With no texture bindings yet: five null ones, the
// framework's texture binding factory (asked once; not tested for null
// before use, as in the game) asked five times for the texture 0x7AB50E44,
// 0x1ABE787D, 0xA9F0E710, and the factory let go. The render state set up
// (1, 1, 1, 0, 1, 1, 0, 3, 4, 5, -1, -1.0, 1, -1). A burnt building read
// from a save (base state bit 2) has its quad built. True.
uint8_t __fastcall Init(char* occupant, void*)
{
    static bool logged = false;
    FirstCall("burnt Init", logged);
    char* const self = occupant - burnt::kOccupant;
    if (Virtual<uint8_t>(occupant, 0x14))
        return 1;
    This<uint8_t>(addr::cSC4Occupant_Init, occupant);
    char* const textures = self + burnt::kTextures;
    if (At<char*>(textures, 0x0) == At<char*>(textures, 0x4)) {
        void* none = nullptr;
        This<void, uint32_t, void**>(addr::SC4PtrVector_Fill, textures, burnt::kTextureCount, &none);
        void* service = nullptr;
        if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
            Virtual<uint8_t>(framework, 0x14, kTextureFactoryClass, kTextureFactoryInterface, &service);
        for (uint32_t i = 0; i < burnt::kTextureCount * 4; i += 4) {
            void** const slot = reinterpret_cast<void**>(At<char*>(textures, 0x0) + i);
            uint32_t key[3] = {burnt::kTexture[0], burnt::kTexture[1], burnt::kTexture[2]};
            Virtual<uint8_t>(service, 0x18, static_cast<const uint32_t*>(key), slot);
        }
        This<void>(addr::SC4RefPtr_Destruct, &service);
    }
    This<void, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int32_t,
         uint32_t, uint32_t, int32_t>(addr::SC4RenderState_Init, self + burnt::kRenderState, 1u, 1u, 1u, 0u, 1u, 1u, 0u, 3u, 4u,
                                      5u, -1, 0xBF800000u, 1u, -1);
    if (At<uint8_t>(self, burnt::kBaseState) & 2)
        This<void>(addr::cSC4PropOccupantBurntBuilding_BuildQuad, self);
    return 1;
}

// Slot 4 (0x005EFD20): nothing unless initialized. The base's Shutdown
// first (the prop's own is not called: the date range stays). With texture
// bindings, all five are let go (those that are not null; the vector is
// taken to hold five) and the vector emptied, its buffer kept. The quad
// freed. The model is not taken off the renderer here. True.
uint8_t __fastcall Shutdown(char* occupant, void*)
{
    static bool logged = false;
    FirstCall("burnt Shutdown", logged);
    char* const self = occupant - burnt::kOccupant;
    if (!Virtual<uint8_t>(occupant, 0x14))
        return 1;
    This<uint8_t>(addr::cSC4Occupant_Shutdown, occupant);
    char* const textures = self + burnt::kTextures;
    if (At<char*>(textures, 0x0) != At<char*>(textures, 0x4)) {
        for (uint32_t i = 0; i < burnt::kTextureCount * 4; i += 4)
            if (void* const binding = At<void*>(At<char*>(textures, 0x0), i))
                This<void>(addr::SC4TextureBinding_Release, binding);
        // The game's erase of the whole range: a move of nothing, then the
        // end set to the beginning.
        At<char*>(textures, 0x4) = At<char*>(textures, 0x0);
    }
    if (void* const quad = At<void*>(self, burnt::kQuad)) {
        Cdecl<void, void*>(addr::SC4_operator_delete, quad);
        At<void*>(self, burnt::kQuad) = nullptr;
    }
    return 1;
}

// Slot 30 (0x005EDB50): when base flag 2 changes, the model (the virtual
// base, +0xD4; null for a null object) goes to the renderer: shown, it is
// added unless the renderer has it (slots 27 and 28); hidden, it is taken
// off when the renderer has it (slots 27 and 29). The renderer is not
// tested for null. Then the base's SetAllFlags. AL is the flags' byte, from
// the base.
uint32_t __fastcall SetAllFlags(char* occupant, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("burnt SetAllFlags", logged);
    if ((At<uint8_t>(occupant, burnt::kFlags - burnt::kOccupant) ^ flags) & 2) {
        void* const renderer = Global<void*>(addr::SC4_View43DD0);
        char* const self = occupant - burnt::kOccupant;
        char* const model = self ? VirtualBase(self) : nullptr;
        if (flags & 2) {
            if (!Virtual<uint8_t>(renderer, 0x6C, static_cast<void*>(model)))
                Virtual<void>(renderer, 0x70, static_cast<void*>(model));
        } else if (Virtual<uint8_t>(renderer, 0x6C, static_cast<void*>(model))) {
            Virtual<void>(renderer, 0x74, static_cast<void*>(model));
        }
    }
    return This<uint32_t>(addr::cSC4Occupant_SetAllFlags, occupant, flags);
}

// ====================================================================================
// Save (`part` is the object + 0xC)
// ====================================================================================

// Slot 3 (0x005EDB10): version 3, the prop's record (its Write, with the
// same `this`), then the burnt box. When the prop's Write fails nothing
// more is written; the answer is in both cases whether the stream is free
// of errors.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("burnt Write", logged);
    char* const self = part - burnt::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 3u);
    if (This<uint8_t>(addr::cSC4PropOccupant_Write, part, stream))
        Cdecl<void*>(addr::SC4Stream_WriteBox, stream, static_cast<void*>(self + burnt::kBox));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 4 (0x005EFBA0): false at once for a version other than 3 (read into
// the argument's stack slot, the stream pointer until then). When the
// prop's Read fails nothing more is read; else the burnt box. The answer is
// whether the stream is free of errors.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("burnt Read", logged);
    char* const self = part - burnt::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 3)
        return 0;
    if (This<uint8_t>(addr::cSC4PropOccupant_Read, part, stream))
        Cdecl<void*>(addr::SC4Stream_ReadBox, stream, static_cast<void*>(self + burnt::kBox));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 5 (0x005EF950).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("burnt GetGZCLSID", logged);
    return burnt::kClassId;
}

// ====================================================================================
// Installation
// ====================================================================================

// QueryInterface's code is one five-byte jump and GetGZCLSID's six bytes
// (mov eax, the ID; ret): the detours' jumps cover their first instruction
// exactly.
const Detour kBurntDetours[] = {
    {addr::cSC4PropOccupantBurntBuilding_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
    {addr::cSC4PropOccupantBurntBuilding_SetOccupant, reinterpret_cast<void*>(&SetOccupant)},
    {addr::cSC4PropOccupantBurntBuilding_Init, reinterpret_cast<void*>(&Init)},
    {addr::cSC4PropOccupantBurntBuilding_Shutdown, reinterpret_cast<void*>(&Shutdown)},
    {addr::cSC4PropOccupantBurntBuilding_SetAllFlags, reinterpret_cast<void*>(&SetAllFlags)},
    {addr::cSC4PropOccupantBurntBuilding_Write, reinterpret_cast<void*>(&Write)},
    {addr::cSC4PropOccupantBurntBuilding_Read, reinterpret_cast<void*>(&Read)},
    {addr::cSC4PropOccupantBurntBuilding_GetGZCLSID, reinterpret_cast<void*>(&GetGZCLSID)},
};

bool InstallBurnt(registry::Mode) { return InstallDetours(kBurntDetours, "burnt"); }

const bool g_registered = registry::Register({
    "simulators.propkinds.burnt",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantBurntBuilding_SetOccupant,
    "Our cSC4PropOccupantBurntBuilding's logic: QueryInterface, SetOccupant, Init, Shutdown, SetAllFlags, Write, Read "
    "and GetGZCLSID (8 detours; the drawing is simulators.propkinds.burnt_draw; src/core/propkinds.h)",
    false,
    InstallBurnt,
});

// ====================================================================================
// Our object
// ====================================================================================

// Copies of the game's eight vtables, taken at the first construction,
// after every PostAppInit slot swap of the other groups (the prop's
// GetRandomChance in 0x00AA4B30). The virtual base table 0x00AA4B90 is data
// and stays the game's.
constexpr int kSlots0 = 24, kSlots4 = 4, kSlots8 = 34, kSlotsC = 6, kSlots10 = 25, kSlots24 = 5, kSlots68 = 1, kSlotsD4 = 19;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable10[kSlots10];
void* g_vtable24[kSlots24];
void* g_vtable68[kSlots68];
void* g_vtableD4[kSlotsD4];

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags);

// The holder part's slot 22 (0x005EF9A0).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - burnt::kHolder, nullptr, flags);
}

// The model part's slot 0 (0x005EF9C0).
void* __fastcall DeletingDestructorAtModel(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - burnt::kModelPart, nullptr, flags);
}

void BuildVtables()
{
    static bool built = false;
    if (built)
        return;
    built = true;
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4PropOccupant_DerivedVftable_AA4B30), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4PropOccupantBurntBuilding_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4PropOccupantBurntBuilding_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4PropOccupantBurntBuilding_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable10, reinterpret_cast<const void*>(addr::cSC4PropOccupantBurntBuilding_vftable_10), sizeof(g_vtable10));
    std::memcpy(g_vtable24, reinterpret_cast<const void*>(addr::cSC4PropOccupantBurntBuilding_vftable_24), sizeof(g_vtable24));
    std::memcpy(g_vtable68, reinterpret_cast<const void*>(addr::cSC4PropOccupantBurntBuilding_vftable_68), sizeof(g_vtable68));
    std::memcpy(g_vtableD4, reinterpret_cast<const void*>(addr::cSC4PropOccupantBurntBuilding_vftable_D4), sizeof(g_vtableD4));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtableC[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtable10[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    g_vtable68[0] = reinterpret_cast<void*>(&DeletingDestructorAtModel);
    log::Write("propkinds burnt object: vtables copied (+8 slots 3/4/30 %p %p %p, +C slots 3/4 %p %p)", g_vtable8[3], g_vtable8[4],
               g_vtable8[30], g_vtableC[3], g_vtableC[4]);
}

// The eight vtables; the virtual base's through the vbptr, and its
// displacement (the vbptr's second entry less 0x68) before it.
void SetVtables(char* self)
{
    BuildVtables();
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, burnt::kMessageTarget) = g_vtable4;
    At<void**>(self, burnt::kOccupant) = g_vtable8;
    At<void**>(self, burnt::kSerializable) = g_vtableC;
    At<void**>(self, burnt::kHolder) = g_vtable10;
    At<void**>(self, burnt::kExemplarPart) = g_vtable24;
    At<void**>(self, burnt::kModelPart) = g_vtable68;
    const int32_t offset = At<int32_t>(At<char*>(self, burnt::kVbptr), 0x4);
    At<void**>(self, burnt::kVbptr + offset) = g_vtableD4;
    At<int32_t>(self, burnt::kModelPart + offset) = offset - static_cast<int32_t>(burnt::kModelPart);
}

// The constructor (detour of 0x005EF810): as the most derived object (the
// argument not 0) the vbptr and the virtual base's first vtable; then the
// prop constructed at its address on the object, the model part's
// constructor (as a base, 0), our vtables, no texture bindings, the burnt
// box from 1, 1, 1 to 0, 0, 0 (after the game's zero float in all six), no
// quad, the render state zeroed, the scaled corner the zero float.
void* __fastcall Construct(char* self, void*, uint32_t mostDerived)
{
    static bool logged = false;
    FirstCall("burnt Construct", logged);
    if (mostDerived) {
        At<std::uintptr_t>(self, burnt::kVbptr) = addr::cSC4PropOccupantBurntBuilding_vbtable;
        At<std::uintptr_t>(self, burnt::kVirtualBase) = addr::cSC4NetworkOccupant_vbase_abstract_vftable;
    }
    This<void*>(addr::cSC4PropOccupant_cSC4PropOccupant, self);
    This<void*, uint32_t>(addr::SC4Model_ConstructModelPart, self + burnt::kModelPart, 0u);
    SetVtables(self);
    At<uint32_t>(self, burnt::kTextures) = 0;
    At<uint32_t>(self, burnt::kTextures + 4) = 0;
    At<uint32_t>(self, burnt::kTextures + 8) = 0;
    const uint32_t zero = Global<uint32_t>(addr::cSC4PropOccupant_Zero);
    for (std::size_t i = 0; i < 24; i += 4)
        At<uint32_t>(self, burnt::kBox + i) = zero;
    for (std::size_t i = 0; i < 12; i += 4) {
        At<uint32_t>(self, burnt::kBox + i) = 0x3F800000;
        At<uint32_t>(self, burnt::kBox + 12 + i) = 0;
    }
    At<uint32_t>(self, burnt::kQuad) = 0;
    At<uint32_t>(self, burnt::kRenderState) = 0;
    At<uint32_t>(self, burnt::kRenderState + 4) = 0;
    for (std::size_t i = 0; i < 12; i += 4)
        At<uint32_t>(self, burnt::kScaled + i) = zero;
    static int created = 0;
    if (created++ < 3)
        log::Write("propkinds burnt object: constructed %p", static_cast<void*>(self));
    return self;
}

// The destructor's body (detour of 0x005EFB20; the deleting destructor is
// its one caller): the class's vtables, the texture vector's buffer freed
// (the bindings are not let go here: Shutdown does that; nor is the quad
// freed), the model part destroyed, then the occupant base.
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("burnt Destroy", logged);
    SetVtables(self);
    if (void* const buffer = At<void*>(self, burnt::kTextures))
        Cdecl<void>(addr::SC4_Free, buffer);
    This<void>(addr::SC4Model_DestroyModelPart, self + burnt::kModelPart);
    This<void>(addr::cSC4Occupant_Destroy, self + burnt::kOccupant);
}

// The deleting destructor (detour of 0x005EFCC0): the body, and with flag 1
// the memory back to the pool. A null object is still destroyed first, as
// in the game.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("burnt DeletingDestructor", logged);
    Destroy(self, nullptr);
    if ((flags & 1) && self)
        PoolFree(kPool, self, kPoolReopenAt);
    return self;
}

// The pool allocator (detour of 0x005EDAE0; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("burnt Allocate", logged);
    return PoolAllocate(kPool);
}

// The factory (detour of 0x005EFC80): an item of the pool, constructed as
// the most derived object; the object itself (the prop interface is at +0)
// is what it hands out, not tested for null.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("burnt Create", logged);
    return Construct(static_cast<char*>(PoolAllocate(kPool)), nullptr, 1u);
}

const Detour kBurntObjectDetours[] = {
    {addr::cSC4PropOccupantBurntBuilding_Construct, reinterpret_cast<void*>(&Construct)},
    {addr::cSC4PropOccupantBurntBuilding_Allocate, reinterpret_cast<void*>(&Allocate)},
    {addr::cSC4PropOccupantBurntBuilding_Create, reinterpret_cast<void*>(&Create)},
    {addr::cSC4PropOccupantBurntBuilding_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
    {addr::cSC4PropOccupantBurntBuilding_Destroy, reinterpret_cast<void*>(&Destroy)},
};

// Every construction path: the factory (through the class registration's
// pointer when a save is read) and the lot developer's allocator and
// constructor pair. The deleting destructor is reached through the holder
// part's slot 22 and the model part's slot 0: ours in the copies, and by the
// detour for the game's thunks.
bool InstallBurntObject(registry::Mode) { return InstallDetours(kBurntObjectDetours, "burnt object"); }

const bool g_registeredObject = registry::Register({
    "simulators.propkinds.burnt_object",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantBurntBuilding_Construct,
    "Our cSC4PropOccupantBurntBuilding object: the constructor, the pool allocation, the factory, the destructor and "
    "the deleting destructor (5 detours), identity; methods from copies of the game vtables taken once "
    "(src/core/propkinds.h)",
    false,
    InstallBurntObject,
});

} // namespace
} // namespace opensc4::propkinds
