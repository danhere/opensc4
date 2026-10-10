// The disaster classes' objects (docs/disasters.md, "The objects"): the
// factories, constructors and destructors of the layer, the seven managers
// and the seven instances. Replacement, off by default:
//
//   simulators.disasters.objects: every factory, constructor, destructor
//     and deleting destructor of the 15 classes and of the managers' base.
//
// The objects keep the game's layouts and the game's vtables (data); with
// the other disaster replacements on, every function those vtables point
// at is ours too, but for shared base code (reference counting, the base
// QueryInterface) and the fire occupant.
#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <initializer_list>
#include <utility>

namespace opensc4::disasters {
namespace {

using ThisFn = void(__thiscall*)(void* self);
using Vtables = std::initializer_list<std::pair<std::size_t, std::uintptr_t>>;

void* OperatorNew(std::size_t size)
{
    return reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(size);
}

void OperatorDelete(void* memory)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(memory);
}

void* Allocate(std::size_t size)
{
    return reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(size);
}

void Free(void* memory)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory);
}

void FreeAt(char* self, std::size_t at)
{
    if (void* const memory = At<void*>(self, at))
        Free(memory);
}

void SetVtables(char* self, Vtables vtables)
{
    for (const auto& [at, vtable] : vtables)
        At<std::uintptr_t>(self, at) = vtable;
}

// The reference-counted base (0x0090DA1E, 0x0090D990).
void BaseConstruct(char* self)
{
    At<uint32_t>(self, 0x4) = 0;
    At<std::uintptr_t>(self, 0x0) = addr::cRZBaseUnknown_Vtable;
}

void BaseDestruct(char* self)
{
    At<std::uintptr_t>(self, 0x0) = addr::cRZBaseUnknown_Vtable;
}

// A list's sentinel node, linked to itself.
char* NewList(std::size_t nodeSize)
{
    char* const node = static_cast<char*>(Allocate(nodeSize));
    At<char*>(node, 0x0) = node;
    At<char*>(node, 0x4) = node;
    return node;
}

// A set or map with its header node: the header at +0, the count at +4.
void NewTree(char* self, std::size_t at, std::size_t nodeSize)
{
    At<char*>(self, at) = nullptr;
    char* const header = static_cast<char*>(Allocate(nodeSize));
    At<char*>(self, at) = header;
    At<uint32_t>(self, at + 4) = 0;
    At<uint8_t>(header, 0x0) = 0;
    At<char*>(At<char*>(self, at), 0x4) = nullptr;
    At<char*>(At<char*>(self, at), 0x8) = At<char*>(self, at);
    At<char*>(At<char*>(self, at), 0xC) = At<char*>(self, at);
}

void DestroyTree(char* self, std::size_t at, std::uintptr_t eraseSubtree)
{
    char* const tree = self + at;
    if (At<uint32_t>(tree, 0x4)) {
        reinterpret_cast<void(__thiscall*)(void*, void*)>(eraseSubtree)(tree, At<void*>(At<char*>(tree, 0x0), 0x4));
        At<char*>(At<char*>(tree, 0x0), 0x8) = At<char*>(tree, 0x0);
        At<void*>(At<char*>(tree, 0x0), 0x4) = nullptr;
        At<char*>(At<char*>(tree, 0x0), 0xC) = At<char*>(tree, 0x0);
        At<uint32_t>(tree, 0x4) = 0;
    }
    FreeAt(tree, 0x0);
}

void StopwatchConstruct(char* stopwatch)
{
    reinterpret_cast<void*(__thiscall*)(void*, int32_t)>(addr::SC4Stopwatch_Construct)(stopwatch, 4);
}

void StopwatchDestruct(char* stopwatch)
{
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Destruct)(stopwatch);
}

void* Deleting(char* self, uint32_t flags)
{
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// The managers' base.

const Vtables kManager = {{0x0, addr::cSC4DisasterManager_vftable},
                          {0x8, addr::cSC4DisasterManager_vftable_8},
                          {0xC, addr::cSC4DisasterManager_vftable_C}};

// The game's 0x00578160.
char* __fastcall ManagerConstruct(char* self, void*)
{
    BaseConstruct(self);
    SetVtables(self, kManager);
    At<uint8_t>(self, 0x10) = 0;
    At<char*>(self, 0x14) = NewList(0xC);
    At<uint8_t>(self, 0x18) = 0;
    At<char*>(self, 0x1C) = NewList(0xC);
    At<char*>(self, 0x20) = NewList(0xC);
    return self;
}

// The game's 0x005781E0.
void __fastcall ManagerDestruct(char* self, void*)
{
    SetVtables(self, kManager);
    if (At<uint8_t>(self, 0x10))
        reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterManager_Shutdown)(self + 0x8);
    for (std::size_t list : {std::size_t{0x20}, std::size_t{0x1C}, std::size_t{0x14}})
        reinterpret_cast<ThisFn>(addr::SC4PairList_Destroy)(self + list);
    BaseDestruct(self);
}

// A manager of `size` bytes: the base constructed, then its own vtables.
char* NewManager(std::size_t size, Vtables vtables)
{
    char* const self = static_cast<char*>(OperatorNew(size));
    if (!self)
        return nullptr;
    ManagerConstruct(self, nullptr);
    SetVtables(self, vtables);
    return self;
}

// The layer.

const Vtables kLayer = {{0x0, addr::cSC4DisasterLayer_vftable},
                        {0x8, addr::cSC4DisasterLayer_vftable_8},
                        {0xC, addr::cSC4DisasterLayer_vftable_C}};

// The game's 0x00578A10.
char* __fastcall LayerConstruct(char* self, void*)
{
    BaseConstruct(self);
    SetVtables(self, kLayer);
    NewTree(self, 0x10, 0x18);
    NewTree(self, 0x1C, 0x18);
    StopwatchConstruct(self + 0x28);
    At<uint32_t>(self, 0x40) = 0;
    At<uint8_t>(self, 0x44) = 0;
    At<uint32_t>(self, 0x48) = 0;
    for (std::size_t at : {std::size_t{0xA0}, std::size_t{0xA4}, std::size_t{0xA8}})
        At<uint32_t>(self, at) = 0;
    NewTree(self, 0xB8, 0x18);
    for (std::size_t at : {std::size_t{0xC8}, std::size_t{0xCC}, std::size_t{0xD0}, std::size_t{0xD4}})
        At<uint32_t>(self, at) = 0;
    return self;
}

// The game's 0x005787D0.
void __fastcall LayerDestruct(char* self, void*)
{
    SetVtables(self, kLayer);
    if (At<uint8_t>(self, 0x44))
        reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterLayer_Shutdown)(self + 0x8);
    FreeAt(self, 0xCC);
    DestroyTree(self, 0xB8, addr::cSC4DisasterLayer_EraseManagerSubtree);
    uint8_t unused = 0;
    reinterpret_cast<void(__cdecl*)(void*, void*, void*)>(addr::SC4CostRecords_Destroy)(At<void*>(self, 0xA0),
                                                                                       At<void*>(self, 0xA4), &unused);
    FreeAt(self, 0xA0);
    StopwatchDestruct(self + 0x28);
    DestroyTree(self, 0x1C, addr::cSC4DisasterLayer_EraseTimerSubtree);
    DestroyTree(self, 0x10, addr::cSC4DisasterLayer_EraseTimerSubtree);
    BaseDestruct(self);
}

void* __fastcall LayerDeletingDestructor(char* self, void*, uint32_t flags)
{
    LayerDestruct(self, nullptr);
    return Deleting(self, flags);
}

char* __cdecl LayerCreate()
{
    char* const self = static_cast<char*>(OperatorNew(0xD8));
    return self ? LayerConstruct(self, nullptr) + 0x8 : nullptr;
}

// The fire manager and instance.

const Vtables kFireManager = {{0x0, addr::cSC4DisasterManagerFire_vftable},
                              {0x8, addr::cSC4DisasterManagerFire_vftable_8},
                              {0xC, addr::cSC4DisasterManagerFire_vftable_C},
                              {0x24, addr::cSC4DisasterManagerFire_vftable_24},
                              {0x28, addr::cSC4DisasterManagerFire_vftable_28}};

// The +0x28 part's vtable while it is destroyed (0x00A9E4D4).
constexpr std::uintptr_t kFirePartBase = addr::cSC4DisasterManagerFire_vftable_28 - 0x4;

char* __cdecl FireManagerCreate()
{
    char* const self = static_cast<char*>(OperatorNew(0x2C));
    if (!self)
        return nullptr;
    ManagerConstruct(self, nullptr);
    Global<char*>(addr::cSC4DisasterManagerFire_Instance) = self;
    SetVtables(self, kFireManager);
    return self + 0x24;
}

void* __fastcall FireManagerDeletingDestructor(char* self, void*, uint32_t flags)
{
    SetVtables(self, kFireManager);
    At<std::uintptr_t>(self, 0x28) = kFirePartBase;
    Global<char*>(addr::cSC4DisasterManagerFire_Instance) = nullptr;
    ManagerDestruct(self, nullptr);
    return Deleting(self, flags);
}

void* __fastcall FirePartDeletingDestructor(char* part, void*, uint32_t flags)
{
    At<std::uintptr_t>(part, 0x0) = kFirePartBase;
    Global<char*>(addr::cSC4DisasterManagerFire_Instance) = nullptr;
    return Deleting(part, flags);
}

const Vtables kFireInstance = {{0x0, addr::cSC4DisasterInstanceFire_vftable},
                               {0x8, addr::cSC4DisasterInstanceFire_vftable_8},
                               {0xC, addr::cSC4DisasterInstanceFire_vftable_C},
                               {0x10, addr::cSC4DisasterInstanceFire_vftable_10}};

// The game's 0x0057AD90.
char* __fastcall FireConstruct(char* self, void*)
{
    BaseConstruct(self);
    SetVtables(self, kFireInstance);
    At<uint32_t>(self, 0x14) = 0;
    {
        // The clock seed counts under the original's call site.
        const fixed_seed::SiteOverride site(0x0057ADD9);
        reinterpret_cast<void*(__thiscall*)(void*, int32_t)>(addr::SC4Random_Construct)(self + 0x18, -1);
    }
    StopwatchConstruct(self + 0x30);
    for (std::size_t at = 0x48; at <= 0x50; at += 4)
        At<uint32_t>(self, at) = 0;
    At<uint8_t>(self, 0x54) = 0;
    for (std::size_t at = 0x58; at <= 0x78; at += 4)
        At<uint32_t>(self, at) = 0;
    At<uint32_t>(self, 0x80) = 0;
    for (std::size_t curve = 0x84; curve <= 0xB4; curve += 0x10)
        reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4Curve_Init)(self + curve);
    At<uint8_t>(self, 0xC5) = 0;
    for (std::size_t at = 0xC8; at <= 0xDC; at += 4)
        At<uint32_t>(self, at) = 0;
    At<char*>(self, 0xE0) = NewList(0xC);
    At<uint32_t>(self, 0xE4) = 0;
    At<uint32_t>(self, 0xE8) = 0;
    At<char*>(self, 0xEC) = nullptr;
    char* const burned = reinterpret_cast<char*(__thiscall*)(void*, uint32_t, uint32_t)>(addr::SC4List_AllocateNodes)(
        self + 0xEC, 1, 0);
    At<char*>(burned, 0x0) = burned;
    At<char*>(burned, 0x4) = burned;
    At<char*>(self, 0xEC) = burned;
    NewTree(self, 0xF0, 0x14);
    return self;
}

// The game's 0x0057ABF0.
void __fastcall FireDestruct(char* self, void*)
{
    SetVtables(self, kFireInstance);
    DestroyTree(self, 0xF0, addr::SC4Uint32Set_EraseNodes);
    reinterpret_cast<ThisFn>(addr::SC4List_Destroy2)(self + 0xEC);
    reinterpret_cast<ThisFn>(addr::SC4RefList_Clear)(self + 0xE0);
    FreeAt(self, 0xE0);
    for (std::size_t at : {std::size_t{0xB4}, std::size_t{0xA4}, std::size_t{0x94}, std::size_t{0x84}, std::size_t{0x70},
                           std::size_t{0x64}, std::size_t{0x58}})
        FreeAt(self, at);
    if (void* const filter = At<void*>(self, 0x48))
        Virtual<uint32_t>(filter, 0x8);
    StopwatchDestruct(self + 0x30);
    BaseDestruct(self);
}

void* __fastcall FireDeletingDestructor(char* self, void*, uint32_t flags)
{
    FireDestruct(self, nullptr);
    return Deleting(self, flags);
}

char* __cdecl FireCreate()
{
    char* const self = static_cast<char*>(OperatorNew(0x100));
    return self ? FireConstruct(self, nullptr) + 0x8 : nullptr;
}

// The earthquake manager and instance.

const Vtables kQuakeManager = {{0x0, addr::cSC4DisasterManagerEarthquake_vftable},
                               {0x8, addr::cSC4DisasterManagerEarthquake_vftable_8},
                               {0xC, addr::cSC4DisasterManagerEarthquake_vftable_C}};

char* __cdecl QuakeManagerCreate()
{
    char* const self = NewManager(0x24, kQuakeManager);
    return self ? self + 0x8 : nullptr;
}

void* __fastcall QuakeManagerDeletingDestructor(char* self, void*, uint32_t flags)
{
    SetVtables(self, kQuakeManager);
    ManagerDestruct(self, nullptr);
    return Deleting(self, flags);
}

const Vtables kQuakeInstance = {{0x0, addr::cSC4DisasterInstanceEarthquake_vftable},
                                {0x8, addr::cSC4DisasterInstanceEarthquake_vftable_8},
                                {0xC, addr::cSC4DisasterInstanceEarthquake_vftable_C}};

// The game's 0x00576150.
char* __fastcall QuakeConstruct(char* self, void*)
{
    BaseConstruct(self);
    SetVtables(self, kQuakeInstance);
    At<float>(self, 0x24) = 0.1f;
    At<uint8_t>(self, 0x10) = 0;
    for (std::size_t at : {std::size_t{0x14}, std::size_t{0x18}, std::size_t{0x1C}, std::size_t{0x20}, std::size_t{0x38},
                           std::size_t{0x40}, std::size_t{0x44}, std::size_t{0x48}, std::size_t{0x4C}, std::size_t{0x50},
                           std::size_t{0x54}, std::size_t{0x58}, std::size_t{0x5C}, std::size_t{0x78}, std::size_t{0x7C},
                           std::size_t{0x84}, std::size_t{0x88}})
        At<uint32_t>(self, at) = 0;
    At<uint32_t>(self, 0x28) = 0xFF7FFFFF; // the lowest float
    At<uint32_t>(self, 0x2C) = 0xFF7FFFFF;
    for (std::size_t request : {std::size_t{0x9C}, std::size_t{0xD4}}) {
        At<int32_t>(self, request + 0x10) = -1;
        At<uint8_t>(self, request + 0x14) = 1;
        At<float>(self, request + 0x18) = 1.0f;
        At<float>(self, request + 0x1C) = 1.0f;
        At<uint32_t>(self, request + 0x20) = 3;
        At<float>(self, request + 0x34) = 100.0f;
    }
    for (std::size_t at = 0x10C; at <= 0x11C; at += 4)
        At<uint32_t>(self, at) = 0;
    return self;
}

// The game's 0x00576420.
void __fastcall QuakeDestruct(char* self, void*)
{
    SetVtables(self, kQuakeInstance);
    if (At<uint8_t>(self, 0x10))
        reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterInstanceEarthquake_Shutdown)(self + 0x8);
    FreeAt(self, 0x114);
    FreeAt(self, 0x54);
    FreeAt(self, 0x48);
    BaseDestruct(self);
}

void* __fastcall QuakeDeletingDestructor(char* self, void*, uint32_t flags)
{
    QuakeDestruct(self, nullptr);
    return Deleting(self, flags);
}

char* __cdecl QuakeCreate()
{
    char* const self = static_cast<char*>(OperatorNew(0x120));
    return self ? QuakeConstruct(self, nullptr) + 0x8 : nullptr;
}

// The effect manager and instance.

const Vtables kEffectManager = {{0x0, addr::cSC4DisasterManagerEffect_vftable},
                                {0x8, addr::cSC4DisasterManagerEffect_vftable_8},
                                {0xC, addr::cSC4DisasterManagerEffect_vftable_C},
                                {0x24, addr::cSC4DisasterManagerEffect_vftable_24}};

char* __cdecl EffectManagerCreate()
{
    char* const self = NewManager(0x28, kEffectManager);
    return self ? self + 0x24 : nullptr;
}

void* __fastcall EffectManagerDeletingDestructor(char* self, void*, uint32_t flags)
{
    SetVtables(self, kEffectManager);
    ManagerDestruct(self, nullptr);
    return Deleting(self, flags);
}

const Vtables kEffectInstance = {{0x0, addr::cSC4DisasterInstanceEffect_vftable},
                                 {0x8, addr::cSC4DisasterInstanceEffect_vftable_8},
                                 {0xC, addr::cSC4DisasterInstanceEffect_vftable_C}};

// The game's 0x005772B0.
char* __fastcall EffectConstruct(char* self, void*, void* effect, uint32_t type)
{
    BaseConstruct(self);
    At<uint32_t>(self, 0x1C) = type;
    SetVtables(self, kEffectInstance);
    At<uint32_t>(self, 0x10) = 0;
    At<uint32_t>(self, 0x14) = 0;
    At<uint8_t>(self, 0x18) = 0;
    At<uint32_t>(self, 0x20) = 0;
    At<uint32_t>(self, 0x24) = 0;
    At<void*>(self, 0x28) = effect;
    Virtual<uint32_t>(effect, 0x4);
    return self;
}

void* __fastcall EffectDeletingDestructor(char* self, void*, uint32_t flags)
{
    SetVtables(self, kEffectInstance);
    if (At<uint8_t>(self, 0x18))
        reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterInstanceEffect_Shutdown)(self + 0x8);
    Virtual<uint32_t>(At<void*>(self, 0x28), 0x8);
    BaseDestruct(self);
    return Deleting(self, flags);
}

// The pipe burst manager and instance.

const Vtables kPipeManager = {{0x0, addr::cSC4DisasterManagerPipeBurst_vftable},
                              {0x8, addr::cSC4DisasterManagerPipeBurst_vftable_8},
                              {0xC, addr::cSC4DisasterManagerPipeBurst_vftable_C}};

char* __cdecl PipeManagerCreate()
{
    char* const self = static_cast<char*>(OperatorNew(0x28));
    if (!self)
        return nullptr;
    ManagerConstruct(self, nullptr);
    At<std::uintptr_t>(self, 0x0) = addr::cSC4DisasterManagerPipeBurst_vftable;
    At<std::uintptr_t>(self, 0xC) = addr::cSC4DisasterManagerPipeBurst_vftable_C;
    At<uint32_t>(self, 0x24) = 0;
    At<std::uintptr_t>(self, 0x8) = addr::cSC4DisasterManagerPipeBurst_vftable_8;
    return self + 0x8;
}

void* __fastcall PipeManagerDeletingDestructor(char* self, void*, uint32_t flags)
{
    SetVtables(self, kPipeManager);
    ManagerDestruct(self, nullptr);
    return Deleting(self, flags);
}

const Vtables kPipeInstance = {{0x0, addr::cSC4DisasterInstancePipeBurst_vftable},
                               {0x8, addr::cSC4DisasterInstancePipeBurst_vftable_8},
                               {0xC, addr::cSC4DisasterInstancePipeBurst_vftable_C}};

// The game's 0x0057CED0.
char* __fastcall PipeConstruct(char* self, void*)
{
    BaseConstruct(self);
    SetVtables(self, kPipeInstance);
    At<uint8_t>(self, 0x10) = 0;
    for (std::size_t at = 0x14; at <= 0x20; at += 4)
        At<uint32_t>(self, at) = 0;
    At<char*>(self, 0x28) = NewList(0x28);
    At<char*>(self, 0x34) = NewList(0x18);
    return self;
}

// The game's 0x0057CF40.
void __fastcall PipeDestruct(char* self, void*)
{
    SetVtables(self, kPipeInstance);
    if (At<uint8_t>(self, 0x10))
        reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterInstancePipeBurst_Shutdown)(self + 0x8);
    reinterpret_cast<ThisFn>(addr::cSC4PipeBurstSpreads_Clear)(self + 0x34);
    FreeAt(self, 0x34);
    reinterpret_cast<ThisFn>(addr::SC4PairList_Destroy)(self + 0x28);
    BaseDestruct(self);
}

void* __fastcall PipeDeletingDestructor(char* self, void*, uint32_t flags)
{
    PipeDestruct(self, nullptr);
    return Deleting(self, flags);
}

char* __cdecl PipeCreate()
{
    char* const self = static_cast<char*>(OperatorNew(0x38));
    return self ? PipeConstruct(self, nullptr) + 0x8 : nullptr;
}

// The volcano manager and instance.

const Vtables kVolcanoManager = {{0x0, addr::cSC4DisasterManagerVolcano_vftable},
                                 {0x8, addr::cSC4DisasterManagerVolcano_vftable_8},
                                 {0xC, addr::cSC4DisasterManagerVolcano_vftable_C},
                                 {0x24, addr::cSC4DisasterManagerVolcano_vftable_24}};

char* __cdecl VolcanoManagerCreate()
{
    char* const self = NewManager(0x28, kVolcanoManager);
    return self ? self + 0x8 : nullptr;
}

void* __fastcall VolcanoManagerDeletingDestructor(char* self, void*, uint32_t flags)
{
    SetVtables(self, kVolcanoManager);
    ManagerDestruct(self, nullptr);
    return Deleting(self, flags);
}

const Vtables kVolcanoInstance = {{0x0, addr::cSC4DisasterInstanceVolcano_vftable},
                                  {0x4, addr::cSC4DisasterInstanceVolcano_vftable_4},
                                  {0x8, addr::cSC4DisasterInstanceVolcano_vftable_8}};

// The game's 0x0057FAF0 (+0x1C, +0x48 and +0x50 are left as they come).
char* __fastcall VolcanoConstruct(char* self, void*)
{
    SetVtables(self, kVolcanoInstance);
    At<uint32_t>(self, 0xC) = 0;
    At<int32_t>(self, 0x10) = -1;
    At<int32_t>(self, 0x14) = -1;
    At<uint32_t>(self, 0x18) = 0;
    At<uint8_t>(self, 0x20) = 0;
    for (std::size_t at = 0x24; at <= 0x3C; at += 4)
        At<uint32_t>(self, at) = 0;
    for (std::size_t at = 0x40; at <= 0x44; ++at)
        At<uint8_t>(self, at) = 0;
    At<uint32_t>(self, 0x4C) = 0;
    return self;
}

void* __fastcall VolcanoDeletingDestructor(char* self, void*, uint32_t flags)
{
    SetVtables(self, kVolcanoInstance);
    if (At<uint8_t>(self, 0x20))
        reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterInstanceVolcano_Shutdown)(self);
    return Deleting(self, flags);
}

char* __cdecl VolcanoCreate()
{
    char* const self = static_cast<char*>(OperatorNew(0x54));
    return self ? VolcanoConstruct(self, nullptr) : nullptr;
}

// The riot manager and instance.

const Vtables kRiotManager = {{0x0, addr::cSC4DisasterManagerRiot_vftable},
                              {0x8, addr::cSC4DisasterManagerRiot_vftable_8},
                              {0xC, addr::cSC4DisasterManagerRiot_vftable_C}};

// The game's 0x0057D9A0.
char* __fastcall RiotManagerConstruct(char* self, void*)
{
    ManagerConstruct(self, nullptr);
    SetVtables(self, kRiotManager);
    NewTree(self, 0x24, 0x14);
    return self;
}

// The game's 0x0057D6B0.
void __fastcall RiotManagerDestruct(char* self, void*)
{
    SetVtables(self, kRiotManager);
    DestroyTree(self, 0x24, addr::SC4OccupantSet_EraseTree);
    ManagerDestruct(self, nullptr);
}

void* __fastcall RiotManagerDeletingDestructor(char* self, void*, uint32_t flags)
{
    RiotManagerDestruct(self, nullptr);
    return Deleting(self, flags);
}

char* __cdecl RiotManagerCreate()
{
    char* const self = static_cast<char*>(OperatorNew(0x30));
    return self ? RiotManagerConstruct(self, nullptr) + 0x8 : nullptr;
}

const Vtables kRiotInstance = {{0x0, addr::cSC4DisasterInstanceRiot_vftable},
                               {0x8, addr::cSC4DisasterInstanceRiot_vftable_8},
                               {0xC, addr::cSC4DisasterInstanceRiot_vftable_C},
                               {0x10, addr::cSC4DisasterInstanceRiot_vftable_10}};

// The game's 0x0057DAC0.
char* __fastcall RiotConstruct(char* self, void*)
{
    BaseConstruct(self);
    SetVtables(self, kRiotInstance);
    At<uint32_t>(self, 0x14) = 0;
    At<uint32_t>(self, 0x18) = 0;
    At<char*>(self, 0x1C) = NewList(0x1C);
    At<uint8_t>(self, 0x20) = 0;
    At<uint32_t>(self, 0x24) = 0;
    At<uint32_t>(self, 0x28) = 0;
    StopwatchConstruct(self + 0x30);
    for (std::size_t at = 0x48; at <= 0x60; at += 4)
        At<uint32_t>(self, at) = 0;
    reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4Curve_Init)(self + 0x64);
    return self;
}

// The game's 0x0057DBA0.
void __fastcall RiotDestruct(char* self, void*)
{
    SetVtables(self, kRiotInstance);
    if (At<uint8_t>(self, 0x20))
        reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterInstanceRiot_Shutdown)(self + 0x8);
    FreeAt(self, 0x64);
    FreeAt(self, 0x58);
    StopwatchDestruct(self + 0x30);
    reinterpret_cast<ThisFn>(addr::cSC4RiotList_Clear)(self + 0x1C);
    FreeAt(self, 0x1C);
    BaseDestruct(self);
}

void* __fastcall RiotDeletingDestructor(char* self, void*, uint32_t flags)
{
    RiotDestruct(self, nullptr);
    return Deleting(self, flags);
}

char* __cdecl RiotCreate()
{
    char* const self = static_cast<char*>(OperatorNew(0x78));
    return self ? RiotConstruct(self, nullptr) + 0x8 : nullptr;
}

// The toxic spill manager and instance.

const Vtables kToxicManager = {{0x0, addr::cSC4DisasterManagerToxic_vftable},
                               {0x8, addr::cSC4DisasterManagerToxic_vftable_8},
                               {0xC, addr::cSC4DisasterManagerToxic_vftable_C},
                               {0x24, addr::cSC4DisasterManagerToxic_vftable_24}};

char* __cdecl ToxicManagerCreate()
{
    char* const self = NewManager(0x28, kToxicManager);
    return self ? self + 0x8 : nullptr;
}

void* __fastcall ToxicManagerDeletingDestructor(char* self, void*, uint32_t flags)
{
    SetVtables(self, kToxicManager);
    ManagerDestruct(self, nullptr);
    return Deleting(self, flags);
}

const Vtables kToxicInstance = {{0x0, addr::cSC4DisasterInstanceToxic_vftable},
                                {0x8, addr::cSC4DisasterInstanceToxic_vftable_8},
                                {0xC, addr::cSC4DisasterInstanceToxic_vftable_C}};

// The game's 0x0057EF20.
char* __fastcall ToxicConstruct(char* self, void*)
{
    BaseConstruct(self);
    StopwatchConstruct(self + 0x20);
    SetVtables(self, kToxicInstance);
    At<uint8_t>(self, 0x10) = 0;
    At<uint32_t>(self, 0x14) = 0;
    At<uint32_t>(self, 0x18) = 0;
    At<uint32_t>(self, 0x38) = 0;
    At<char*>(self, 0x3C) = NewList(0xC);
    return self;
}

void* __fastcall ToxicDeletingDestructor(char* self, void*, uint32_t flags)
{
    SetVtables(self, kToxicInstance);
    if (At<uint8_t>(self, 0x10))
        reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterInstanceToxic_Shutdown)(self + 0x8);
    reinterpret_cast<ThisFn>(addr::SC4PairList_Destroy)(self + 0x3C);
    StopwatchDestruct(self + 0x20);
    BaseDestruct(self);
    return Deleting(self, flags);
}

bool InstallObjects(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[56] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4DisasterManager_Construct, reinterpret_cast<void*>(&ManagerConstruct)},
        {addr::cSC4DisasterManager_Destruct, reinterpret_cast<void*>(&ManagerDestruct)},
        {addr::cSC4DisasterLayer_Create, reinterpret_cast<void*>(&LayerCreate)},
        {addr::cSC4DisasterLayer_Construct, reinterpret_cast<void*>(&LayerConstruct)},
        {addr::cSC4DisasterLayer_Destruct, reinterpret_cast<void*>(&LayerDestruct)},
        {addr::cSC4DisasterLayer_DeletingDestructor, reinterpret_cast<void*>(&LayerDeletingDestructor)},
        {addr::cSC4DisasterManagerFire_Create, reinterpret_cast<void*>(&FireManagerCreate)},
        {addr::cSC4DisasterManagerFire_DeletingDestructor, reinterpret_cast<void*>(&FireManagerDeletingDestructor)},
        {addr::cSC4DisasterManagerFire_PartDeletingDestructor, reinterpret_cast<void*>(&FirePartDeletingDestructor)},
        {addr::cSC4DisasterInstanceFire_Create, reinterpret_cast<void*>(&FireCreate)},
        {addr::cSC4DisasterInstanceFire_Construct, reinterpret_cast<void*>(&FireConstruct)},
        {addr::cSC4DisasterInstanceFire_Destruct, reinterpret_cast<void*>(&FireDestruct)},
        {addr::cSC4DisasterInstanceFire_DeletingDestructor, reinterpret_cast<void*>(&FireDeletingDestructor)},
        {addr::cSC4DisasterManagerEarthquake_Create, reinterpret_cast<void*>(&QuakeManagerCreate)},
        {addr::cSC4DisasterManagerEarthquake_DeletingDestructor, reinterpret_cast<void*>(&QuakeManagerDeletingDestructor)},
        {addr::cSC4DisasterInstanceEarthquake_Create, reinterpret_cast<void*>(&QuakeCreate)},
        {addr::cSC4DisasterInstanceEarthquake_Construct, reinterpret_cast<void*>(&QuakeConstruct)},
        {addr::cSC4DisasterInstanceEarthquake_Destruct, reinterpret_cast<void*>(&QuakeDestruct)},
        {addr::cSC4DisasterInstanceEarthquake_DeletingDestructor, reinterpret_cast<void*>(&QuakeDeletingDestructor)},
        {addr::cSC4DisasterManagerEffect_Create, reinterpret_cast<void*>(&EffectManagerCreate)},
        {addr::cSC4DisasterManagerEffect_DeletingDestructor, reinterpret_cast<void*>(&EffectManagerDeletingDestructor)},
        {addr::cSC4DisasterInstanceEffect_Construct, reinterpret_cast<void*>(&EffectConstruct)},
        {addr::cSC4DisasterInstanceEffect_DeletingDestructor, reinterpret_cast<void*>(&EffectDeletingDestructor)},
        {addr::cSC4DisasterManagerPipeBurst_Create, reinterpret_cast<void*>(&PipeManagerCreate)},
        {addr::cSC4DisasterManagerPipeBurst_DeletingDestructor, reinterpret_cast<void*>(&PipeManagerDeletingDestructor)},
        {addr::cSC4DisasterInstancePipeBurst_Create, reinterpret_cast<void*>(&PipeCreate)},
        {addr::cSC4DisasterInstancePipeBurst_Construct, reinterpret_cast<void*>(&PipeConstruct)},
        {addr::cSC4DisasterInstancePipeBurst_Destruct, reinterpret_cast<void*>(&PipeDestruct)},
        {addr::cSC4DisasterInstancePipeBurst_DeletingDestructor, reinterpret_cast<void*>(&PipeDeletingDestructor)},
        {addr::cSC4DisasterManagerVolcano_Create, reinterpret_cast<void*>(&VolcanoManagerCreate)},
        {addr::cSC4DisasterManagerVolcano_DeletingDestructor, reinterpret_cast<void*>(&VolcanoManagerDeletingDestructor)},
        {addr::cSC4DisasterInstanceVolcano_Create, reinterpret_cast<void*>(&VolcanoCreate)},
        {addr::cSC4DisasterInstanceVolcano_Construct, reinterpret_cast<void*>(&VolcanoConstruct)},
        {addr::cSC4DisasterInstanceVolcano_DeletingDestructor, reinterpret_cast<void*>(&VolcanoDeletingDestructor)},
        {addr::cSC4DisasterManagerRiot_Create, reinterpret_cast<void*>(&RiotManagerCreate)},
        {addr::cSC4DisasterManagerRiot_Construct, reinterpret_cast<void*>(&RiotManagerConstruct)},
        {addr::cSC4DisasterManagerRiot_Destruct, reinterpret_cast<void*>(&RiotManagerDestruct)},
        {addr::cSC4DisasterManagerRiot_DeletingDestructor, reinterpret_cast<void*>(&RiotManagerDeletingDestructor)},
        {addr::cSC4DisasterInstanceRiot_Create, reinterpret_cast<void*>(&RiotCreate)},
        {addr::cSC4DisasterInstanceRiot_Construct, reinterpret_cast<void*>(&RiotConstruct)},
        {addr::cSC4DisasterInstanceRiot_Destruct, reinterpret_cast<void*>(&RiotDestruct)},
        {addr::cSC4DisasterInstanceRiot_DeletingDestructor, reinterpret_cast<void*>(&RiotDeletingDestructor)},
        {addr::cSC4DisasterManagerToxic_Create, reinterpret_cast<void*>(&ToxicManagerCreate)},
        {addr::cSC4DisasterManagerToxic_DeletingDestructor, reinterpret_cast<void*>(&ToxicManagerDeletingDestructor)},
        {addr::cSC4DisasterInstanceToxic_Construct, reinterpret_cast<void*>(&ToxicConstruct)},
        {addr::cSC4DisasterInstanceToxic_DeletingDestructor, reinterpret_cast<void*>(&ToxicDeletingDestructor)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_objectsRegistered = registry::Register({
    "simulators.disasters.objects",
    "simulators/disasters",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DisasterLayer_Construct,
    "Our constructors, destructors and factories of the disaster classes (docs/disasters.md)",
    false,
    InstallObjects,
});

} // namespace
} // namespace opensc4::disasters
