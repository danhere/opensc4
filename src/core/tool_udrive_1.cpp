// The U-Drive-It driving control, first piece (0x00562C90 to 0x00566D55).
// Replacement, off by default:
//
//   ui.tools.udrive1: the constructor, the destructor and the deleting
//     destructor, the right button (OnMouseDownR, OnMouseUpR), the left
//     button's release (OnMouseUpL) and the check whether the driven object
//     takes input (CanDrive).
//
// The control (vtable 0x00A9D9B0, message target 0x00A9D9A0 at +0x28, a
// third interface 0x00A9D98C at +0x2C, 0xC0 bytes, ID 0x2B709676) is made by
// the park automaton 0x00506850 when the player takes over a vehicle, not by
// the city view. The driven object is at +0x30. The right button tells it
// command 8 (1 down, 0 up); letting go of the left one, when it can be
// driven, tells it command 7 with 0 and the service of 0x00602640 its slot
// 0x18 with 4. Init, Shutdown, the keys, the left press, the mouse move, the
// message handler and the drawing stay the game's.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_udrive {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kCursor = 0x10; // cursor ID
} // namespace base

// The control's fields.
namespace field {
constexpr std::size_t kTarget = 0x28;    // the message target part (vtable 0x00A9D9A0)
constexpr std::size_t kThird = 0x2C;     // the third interface (vtable 0x00A9D98C)
constexpr std::size_t kDriven = 0x30;    // the driven object (referenced)
constexpr std::size_t kRef34 = 0x34;     // referenced
constexpr std::size_t kWord38 = 0x38;
constexpr std::size_t kByte3C = 0x3C;    // 1
constexpr std::size_t kFloat40 = 0x40;   // 0.042f
constexpr std::size_t kByte44 = 0x44;
constexpr std::size_t kByte45 = 0x45;
constexpr std::size_t kStopwatch = 0x48; // 0x18 bytes
constexpr std::size_t kRef60 = 0x60;     // referenced
constexpr std::size_t kList = 0x64;      // a list head (12-byte node) and its count at +0x68
constexpr std::size_t kCount68 = 0x68;
constexpr std::size_t kFloat6C = 0x6C;   // 1.0f
constexpr std::size_t kFloat70 = 0x70;   // 1.0f
constexpr std::size_t kWord74 = 0x74;
constexpr std::size_t kFirstRef = 0x8C;  // ten referenced objects, +0x8C to +0xB0
constexpr std::size_t kLastRef = 0xB0;
constexpr std::size_t kPointHolder = 0x9C; // one of them: its +0x24/+0x28 is a point
constexpr std::size_t kWordB4 = 0xB4;
constexpr std::size_t kWordB8 = 0xB8;
} // namespace field

constexpr uint32_t kId = 0x2B709676;
constexpr uint32_t kCursor = 0xC16C4E1B;
constexpr uint32_t kFloat0042 = 0x3D2C0831;
constexpr uint32_t kFlagStopped = 0x8000000; // the driven object's flags asked through slot 0x7C
constexpr uint32_t kFlagOne = 1;

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

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// The two flag checks on the driven object and the city's check (city slot
// 0xC4, that object's slot 0x80) the three button functions share.
bool TakesInput(char* self)
{
    void* const driven = At<void*>(self, field::kDriven);
    if (!driven)
        return false;
    if (Virtual<uint8_t>(driven, 0x7C, kFlagStopped))
        return false;
    if (Virtual<uint8_t>(At<void*>(self, field::kDriven), 0x7C, kFlagOne))
        return false;
    void* const part = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC4);
    return !Virtual<uint8_t>(part, 0x80);
}

// The game's 0x00562ED0: true when there is a driven object and both its
// flags are clear.
uint8_t __fastcall CanDrive(char* self, void*)
{
    void* const driven = At<void*>(self, field::kDriven);
    if (!driven)
        return 0;
    if (Virtual<uint8_t>(driven, 0x7C, kFlagStopped))
        return 0;
    return Virtual<uint8_t>(At<void*>(self, field::kDriven), 0x7C, kFlagOne) ? 0 : 1;
}

// The game's 0x00564300 (slot 18, OnMouseUpL, ret 0xC).
uint8_t __fastcall OnMouseUpL(char* self, void*, int32_t, int32_t, uint32_t)
{
    if (char* const holder = At<char*>(self, field::kPointHolder)) {
        if (At<int32_t>(holder, 0x24) != -1 || At<int32_t>(holder, 0x28) != -1) {
            At<int32_t>(holder, 0x24) = -1;
            At<int32_t>(holder, 0x28) = -1;
        }
    }
    if (TakesInput(self) && CanDrive(self, nullptr)) {
        Virtual<void>(At<void*>(self, field::kDriven), 0xF0, uint32_t{7}, uint32_t{0});
        void* const service = Cdecl<void*>(addr::SC4UDrive_GetService602640);
        Virtual<void>(service, 0x18, uint32_t{4});
    }
    return 1;
}

// The game's 0x00564390 (slot 17, OnMouseDownR, ret 0xC).
uint8_t __fastcall OnMouseDownR(char* self, void*, int32_t, int32_t, uint32_t)
{
    if (TakesInput(self))
        Virtual<void>(At<void*>(self, field::kDriven), 0xF0, uint32_t{8}, uint32_t{1});
    return 1;
}

// The game's 0x005643F0 (slot 19, OnMouseUpR, ret 0xC).
uint8_t __fastcall OnMouseUpR(char* self, void*, int32_t, int32_t, uint32_t)
{
    if (TakesInput(self))
        Virtual<void>(At<void*>(self, field::kDriven), 0xF0, uint32_t{8}, uint32_t{0});
    return 1;
}

// The game's 0x00565480: the constructor; the caller allocated 0xC0 bytes.
// +0x78 is left as it was.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::cSC4UDriveControl_Vtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::cSC4UDriveControl_TargetVtable;
    At<std::uintptr_t>(self, field::kThird) = addr::cSC4UDriveControl_ThirdVtable;
    At<void*>(self, field::kDriven) = nullptr;
    At<void*>(self, field::kRef34) = nullptr;
    At<uint32_t>(self, field::kWord38) = 0;
    At<uint8_t>(self, field::kByte3C) = 1;
    At<uint32_t>(self, field::kFloat40) = kFloat0042;
    At<uint8_t>(self, field::kByte44) = 0;
    At<uint8_t>(self, field::kByte45) = 0;
    This<void*>(addr::SC4Stopwatch_Construct, self + field::kStopwatch, uint32_t{4});
    At<void*>(self, field::kRef60) = nullptr;
    At<void*>(self, field::kList) = nullptr;
    void** const head = static_cast<void**>(Cdecl<void*>(addr::SC4_stl_allocate, std::size_t{0xC}));
    head[0] = head;
    head[1] = head;
    At<void*>(self, field::kList) = head;
    At<uint32_t>(self, field::kCount68) = 0;
    At<uint32_t>(self, field::kWord74) = 0;
    for (std::size_t offset = 0x7C; offset <= 0x88; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, field::kFloat6C) = 0x3F800000;
    At<uint32_t>(self, field::kFloat70) = 0x3F800000;
    for (std::size_t offset = field::kFirstRef; offset <= field::kLastRef; offset += 4)
        At<void*>(self, offset) = nullptr;
    At<uint32_t>(self, field::kWordB4) = 0;
    At<uint32_t>(self, field::kWordB8) = 0;
    At<uint32_t>(self, base::kId) = kId;
    At<uint32_t>(self, base::kCursor) = kCursor;
    return self;
}

// The game's 0x00565930: the destructor's body.
void __fastcall Destruct(char* self, void*)
{
    for (std::size_t offset = field::kLastRef; offset >= field::kFirstRef; offset -= 4) {
        if (void* const object = At<void*>(self, offset))
            Release(object);
    }
    This<void>(addr::SC4PairList_Destroy, self + field::kList);
    if (void* const object = At<void*>(self, field::kRef60))
        Release(object);
    This<void>(addr::SC4Stopwatch_Destruct, self + field::kStopwatch);
    if (void* const object = At<void*>(self, field::kRef34))
        Release(object);
    if (void* const object = At<void*>(self, field::kDriven))
        Release(object);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// The game's 0x00565910 (slot 26, ret 4): the deleting destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// --- Registration ----------------------------------------------------------------------

bool InstallUDrive1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4UDriveControl_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::cSC4UDriveControl_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::cSC4UDriveControl_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::cSC4UDriveControl_CanDrive, reinterpret_cast<void*>(&CanDrive)},
        {addr::cSC4UDriveControl_OnMouseUpL, reinterpret_cast<void*>(&OnMouseUpL)},
        {addr::cSC4UDriveControl_OnMouseDownR, reinterpret_cast<void*>(&OnMouseDownR)},
        {addr::cSC4UDriveControl_OnMouseUpR, reinterpret_cast<void*>(&OnMouseUpR)},
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
    "ui.tools.udrive1",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4UDriveControl_Construct,
    "Our U-Drive-It control's lifetime and mouse buttons (docs/tools.md)",
    false,
    InstallUDrive1,
});

} // namespace
} // namespace opensc4::tool_udrive
