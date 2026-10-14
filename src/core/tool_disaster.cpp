// The common disaster tool of the city view (0x00580220 to 0x005804F4).
// Replacement, off by default:
//
//   ui.tools.disaster: the view input control of 0xB4 bytes (vtable
//     0x00A9EAA0, 31 slots, ID 0x6A40222A, cursor 0x4A08C57E) the view makes
//     for a disaster whose exemplar is in group 0x29B6C670 (StartDisaster,
//     0x007E9DB0, for every ID but the fire's 0xA9DE8784).
//
// The tool is the effect trigger tool (0x004BAF60, src/core/tool_effect.cpp)
// with a disaster manager. The view gives it its effects by name (the
// exemplar's property 0x4A08C700) or by number (0x4A08C702) and, when the
// exemplar's property 0x4A08C701 is not 0, a disaster type (0x00580460).
// The constructor reads 0x4A08C702 once more, as a bool, into the base's
// pick flag (+0x8F).
//
// Without a type the tool's disaster is the effect disaster (0x6A7B4397):
// slot 30 with 1 (a release after a press that found the control effect not
// running) hands the control effect, the place and the type to the effect
// manager's part (interface 0xCA7B3F3C, slot 3), which makes an instance
// around the effect, starts it and tells the layer a disaster began; the
// instance is kept at +0xB0. Slot 30 with 0 ends that instance.
// With a type the release of a press asks that type's manager for a
// disaster (its slot 6) at the place: the place's x and z in metres,
// truncated, are what the manager is given.
//
// No clock and no random number is read here; what a manager does with a
// start is the manager's (docs/disasters.md).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_disaster {
namespace {

// The base classes' fields.
namespace base {
constexpr std::size_t kInited = 0x04;    // byte
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kCursor = 0x10;    // cursor ID
constexpr std::size_t kPlace = 0x2C;     // float[3]: the pointer's place on the ground
constexpr std::size_t kTransform = 0x38; // 0x38 bytes: what the effects are told
constexpr std::size_t kPickFlag = 0x8F;  // byte: the pick's last argument
constexpr std::size_t kControl = 0x94;   // the control effect (referenced)
} // namespace base

// The tool's fields. +0xA4 and +0xA8 are not set by the constructor.
namespace field {
constexpr std::size_t kHasType = 0xA4;  // byte: the view gave a disaster type with a manager
constexpr std::size_t kType = 0xA8;     // the disaster type
constexpr std::size_t kManager = 0xAC;  // the type's manager (referenced)
constexpr std::size_t kInstance = 0xB0; // the effect disaster's instance (referenced)
} // namespace field

constexpr uint32_t kToolId = 0x6A40222A;
constexpr uint32_t kToolCursor = 0x4A08C57E;
constexpr uint32_t kEffectDisaster = 0x6A7B4397;
constexpr uint32_t kEffectPart = 0xCA7B3F3C;   // the effect manager's part
constexpr uint32_t kPropertyPickFlag = 0x4A08C702;

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword
// of the int64.
int32_t Ftol(float value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// A reference the tool holds, cleared and let go.
void Drop(char* self, std::size_t offset)
{
    if (void* const held = At<void*>(self, offset)) {
        At<void*>(self, offset) = nullptr;
        Release(held);
    }
}

// The game's 0x00580220 (ret 4, returns this): the constructor. The effect
// trigger tool's; no manager and no instance; the ID and the cursor; with
// an exemplar, its property 0x4A08C702 read as a bool is the pick flag when
// the read succeeds.
char* __fastcall Construct(char* self, void*, void* exemplar)
{
    This<void*>(addr::SC4EffectTool_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::SC4DisasterTool_Vtable;
    At<void*>(self, field::kManager) = nullptr;
    At<void*>(self, field::kInstance) = nullptr;
    At<uint32_t>(self, base::kId) = kToolId;
    At<uint32_t>(self, base::kCursor) = kToolCursor;
    if (exemplar) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        uint8_t flag = 0;
        if (Cdecl<uint8_t>(addr::SC4_GetPropertyBool, holder, kPropertyPickFlag, &flag))
            At<uint8_t>(self, base::kPickFlag) = flag;
    }
    return self;
}

// The game's 0x00580290 (slot 3): Init. False when inited. Else the effect
// trigger tool's Init; the effect disaster's manager from the city's
// disaster layer (city slot 72, the layer's slot 7), kept (referenced; what
// was kept before let go); no type given; the type is the effect
// disaster's. True. The city and the layer are not checked.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 0;
    This<uint8_t>(addr::SC4EffectTool_Init, self);
    void* const layer = Virtual<void*>(Global<void*>(addr::SC4_City), 0x120);
    void* const manager = Virtual<void*>(layer, 0x1C, kEffectDisaster);
    void* const old = At<void*>(self, field::kManager);
    if (manager != old) {
        if (manager)
            AddRef(manager);
        At<void*>(self, field::kManager) = manager;
        if (old)
            Release(old);
    }
    At<uint8_t>(self, field::kHasType) = 0;
    At<uint32_t>(self, field::kType) = kEffectDisaster;
    return 1;
}

// The game's 0x00580300 (slot 4): Shutdown. False unless inited. Else the
// manager, the instance and the control effect are let go (the effect is
// not stopped: the disaster it belongs to goes on), then the effect trigger
// tool's Shutdown. True.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 0;
    Drop(self, field::kManager);
    Drop(self, field::kInstance);
    Drop(self, base::kControl);
    This<uint8_t>(addr::SC4EffectTool_Shutdown, self);
    return 1;
}

// The game's 0x00580370 (slot 18, OnMouseUpL, ret 0xC): the effect trigger
// tool's; false when that is. With a manager and a type the view gave, the
// manager is asked for a disaster (its slot 6) at the place's x and z,
// truncated (z is converted first). True.
uint8_t __fastcall OnMouseUpL(char* self, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    if (!This<uint8_t>(addr::SC4EffectTool_OnMouseUpL, self, x, z, modifiers))
        return 0;
    void* const manager = At<void*>(self, field::kManager);
    if (manager && At<uint8_t>(self, field::kHasType)) {
        const int32_t placeZ = Ftol(At<float>(self, base::kPlace + 8));
        const int32_t placeX = Ftol(At<float>(self, base::kPlace));
        Virtual<void*>(manager, 0x18, placeX, placeZ);
    }
    return 1;
}

// The game's 0x005803D0 (slot 30, ret 4). With the argument set: the
// control effect (not checked) is told the block; unless the view gave a
// type, the manager (not checked) is asked for its effect part (its slot 0
// with 0xCA7B3F3C), the part's slot 3 makes a disaster instance of the
// effect at the place with the type, the instance is kept at +0xB0 and the
// part let go. Else the kept instance, when there is one, is ended (its
// slot 8).
void __fastcall Switch(char* self, void*, uint8_t on)
{
    if (!on) {
        if (void* const instance = At<void*>(self, field::kInstance))
            Virtual<uint8_t>(instance, 0x20);
        return;
    }
    Virtual<void>(At<void*>(self, base::kControl), 0x20, static_cast<void*>(self + base::kTransform));
    if (At<uint8_t>(self, field::kHasType))
        return;
    void* part = nullptr;
    if (!Virtual<uint8_t>(At<void*>(self, field::kManager), 0x0, kEffectPart, &part))
        return;
    void* const instance = Virtual<void*>(part, 0xC, At<void*>(self, base::kControl),
                                          &At<float>(self, base::kPlace), At<uint32_t>(self, field::kType));
    This<void*>(addr::SC4RefPtr_Assign, self + field::kInstance, instance);
    Release(part);
}

// The game's 0x00580460 (ret 4): the view's disaster type. Nothing for 0.
// When the city's disaster layer has a manager of the type, it is kept
// (0x005447B0) and +0xA4 set; the type is kept either way. The city is not
// checked.
void __fastcall SetDisasterType(char* self, void*, uint32_t type)
{
    if (type == 0)
        return;
    if (void* const layer = Virtual<void*>(Global<void*>(addr::SC4_City), 0x120)) {
        if (void* const manager = Virtual<void*>(layer, 0x1C, type)) {
            This<void*>(addr::SC4RefPtr_Assign, self + field::kManager, manager);
            At<uint8_t>(self, field::kHasType) = 1;
        }
    }
    At<uint32_t>(self, field::kType) = type;
}

// The game's 0x005804B0 (slot 26, ret 4): the deleting destructor. The
// instance and the manager let go (the fields are left), then the effect
// trigger tool's destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::SC4DisasterTool_Vtable;
    if (void* const instance = At<void*>(self, field::kInstance))
        Release(instance);
    if (void* const manager = At<void*>(self, field::kManager))
        Release(manager);
    This<void>(addr::SC4EffectTool_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// --- Registration ----------------------------------------------------------------------

bool InstallDisasterTool(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4ViewInputControls_F580220, reinterpret_cast<void*>(&Construct)},
        {addr::SC4DisasterTool_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4DisasterTool_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4DisasterTool_OnMouseUpL, reinterpret_cast<void*>(&OnMouseUpL)},
        {addr::SC4DisasterTool_Switch, reinterpret_cast<void*>(&Switch)},
        {addr::SC4ViewInputControls_F580460, reinterpret_cast<void*>(&SetDisasterType)},
        {addr::SC4DisasterTool_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.disaster",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4DisasterTool_Init,
    "Our common disaster tool of the city view (docs/tools.md)",
    false,
    InstallDisasterTool,
});

} // namespace
} // namespace opensc4::tool_disaster
