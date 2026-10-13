// The view input control base class (vtable 0x00AA6658, 0x24 bytes, code
// 0x005FAFC0 to 0x005FB339 and Release at 0x004B9E20) that every tool of the
// city view is built on, the model holder next to it (0x005FB290 to
// 0x005FB357), and the small functions several tool classes share.
// Replacement, off by default:
//
//   ui.tools.base: the base constructor, destructor and deleting destructor,
//     QueryInterface, Release, Init, Shutdown, both SetCursor, SetWindow,
//     the capture functions, IsCurrent, PutAway (slot 27), PlaySound (slot
//     28), Destroy (slot 29); the model holder's Construct, Reset, Show and
//     Make; the shared deleting destructors, the three QueryInterface
//     variants of the tools with a message target at +0x28, the key slot
//     that answers false, and SetGrabOffset.
//
// Everything installs as a detour at the game's entry: our tools and the
// game's remaining ones call these by address or have them in their vtables.
//
// The base object: +4 the inited byte, +8 the reference count, +0xC the ID,
// +0x10 the cursor's ID, +0x14 the cursor (referenced), +0x18 the window
// (not referenced), +0x1C the city view (global 0x00B43DC4, referenced by
// Init), +0x20 the window manager (referenced by Init). The constructor
// leaves +0xC, +0x10 and +0x20 alone.
//
// Left the game's: the stubs in the base vtable that unrelated classes share
// (AddRef 0x004BDF00, GetID 0x00517F70, SetID 0x00989960, GetCursor
// 0x0040CE70 and the handlers that only answer), the adjustor thunks of the
// message target at +0x28 (0x00565570, 0x00565580, 0x00565590, 0x004BA870,
// 0x004BD2A0, 0x00661DB0) and the jumps 0x004BD280, 0x004BD290 and
// 0x00580500 (one jump instruction each; what they jump to is ours), and the
// holder's 0x005FB360, which builds the model's transform (drawing). Also
// left: the overlay helper's 0x004BC3B0 and 0x004BC4A0 (appending cells),
// which the fenced map drawing calls.
//
// Nothing here reads a clock or draws a random number.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace opensc4::tool_base {
namespace {

namespace field { // the base class
constexpr std::size_t kInited = 0x04;   // byte, set by Init
constexpr std::size_t kCount = 0x08;    // the reference count
constexpr std::size_t kCursorId = 0x10; // the cursor's ID
constexpr std::size_t kCursor = 0x14;   // the cursor (referenced)
constexpr std::size_t kWindow = 0x18;   // the window (not referenced)
constexpr std::size_t kView = 0x1C;     // the city view (referenced)
constexpr std::size_t kManager = 0x20;  // the window manager (referenced)
constexpr std::size_t kTarget = 0x28;   // the derived tools' message target part
constexpr std::size_t kGrabX = 0x34;    // the dispatch tools' grab offset
constexpr std::size_t kGrabZ = 0x38;
} // namespace field

constexpr uint32_t kIdUnknown = 1, kIdControl = 0xC6BB8345, kIdTarget = 0x452294AA;

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

// x87 fistp as _ftol does: truncation, the "integer indefinite" for what does
// not fit.
int32_t Truncate(double value)
{
    if (!(value > -2147483649.0 && value < 2147483648.0))
        return INT32_MIN;
    return static_cast<int32_t>(value);
}

// --- The base class ---------------------------------------------------------------------

// The game's 0x005FB320: the constructor. Returns this.
char* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4ViewInputControl_Vtable;
    At<uint8_t>(self, field::kInited) = 0;
    At<uint32_t>(self, field::kCount) = 0;
    At<void*>(self, field::kCursor) = nullptr;
    At<void*>(self, field::kWindow) = nullptr;
    At<void*>(self, field::kView) = nullptr;
    return self;
}

// The game's 0x005FAFC0: the destructor. Only the vtable.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4ViewInputControl_Vtable;
}

// The game's 0x005FB1B0 (ret 4; slot 26 of the base vtable): the deleting
// destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint8_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::SC4ViewInputControl_Vtable;
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x005FB100 (ret 8; slot 0): the IDs 1 and 0xC6BB8345 give the
// object itself, referenced.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t id, void** out)
{
    if (id != kIdUnknown && id != kIdControl)
        return 0;
    *out = self;
    AddRef(self);
    return 1;
}

// The game's 0x004B9E20 (slot 2): the count goes down while it is above 1;
// the last reference (and a count of 0) destroys the object through slot 29.
uint32_t __fastcall ReleaseSelf(char* self, void*)
{
    const uint32_t count = At<uint32_t>(self, field::kCount);
    if (count > 1) {
        At<uint32_t>(self, field::kCount) = count - 1;
        return count - 1;
    }
    Virtual<void>(self, 0x74);
    return 0;
}

// The game's 0x005FB1D0 (slot 3): Init. Once: the cursor of the ID at +0x10
// from the window manager's cursor set (when no cursor is set yet and the ID
// is not zero; the set is not checked for null here), the cursor to the
// window, the city view and the window manager kept. True always.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, field::kInited))
        return 1;
    if (!At<void*>(self, field::kCursor) && At<uint32_t>(self, field::kCursorId)) {
        void* const cursors = Virtual<void*>(Cdecl<void*>(addr::SC4_GetWinManager), 0x80);
        void* const cursor = Virtual<void*>(cursors, 0x24, At<uint32_t>(self, field::kCursorId));
        At<void*>(self, field::kCursor) = cursor;
        if (cursor)
            AddRef(cursor);
    }
    if (void* const cursor = At<void*>(self, field::kCursor)) {
        if (void* const window = At<void*>(self, field::kWindow))
            Virtual<void>(window, 0x154, cursor, uint32_t{1});
    }
    void* const view = Global<void*>(addr::SC4_Service43DC4);
    At<void*>(self, field::kView) = view;
    if (view)
        AddRef(view);
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    At<void*>(self, field::kManager) = manager;
    AddRef(manager);
    At<uint8_t>(self, field::kInited) = 1;
    return 1;
}

// The game's 0x005FAFD0 (slot 4): Shutdown. When inited, lets go of the
// window manager, the view and the cursor. True always.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (At<uint8_t>(self, field::kInited)) {
        for (const std::size_t offset : {field::kManager, field::kView, field::kCursor}) {
            if (void* const held = At<void*>(self, offset)) {
                Release(held);
                At<void*>(self, offset) = nullptr;
            }
        }
        At<uint8_t>(self, field::kInited) = 0;
    }
    return 1;
}

// The game's 0x005FB020 (ret 4; slot 9): SetCursor. A different cursor is
// referenced and kept, the old one released; when the control is inited and
// has a window, the window gets it.
void __fastcall SetCursor(char* self, void*, void* cursor)
{
    if (cursor == At<void*>(self, field::kCursor))
        return;
    if (cursor)
        AddRef(cursor);
    if (void* const old = At<void*>(self, field::kCursor))
        Release(old);
    At<void*>(self, field::kCursor) = cursor;
    if (!cursor || !At<uint8_t>(self, field::kInited))
        return;
    if (void* const window = At<void*>(self, field::kWindow))
        Virtual<void>(window, 0x154, cursor, uint32_t{1});
}

// The game's 0x005FB070 (ret 4; slot 8): SetCursor by ID. Only an ID the
// window manager's cursor set knows changes anything: then the ID is kept
// and the cursor goes to slot 9.
void __fastcall SetCursorId(char* self, void*, uint32_t id)
{
    void* const cursors = Virtual<void*>(Cdecl<void*>(addr::SC4_GetWinManager), 0x80);
    if (!cursors)
        return;
    void* const cursor = Virtual<void*>(cursors, 0x24, id);
    if (!cursor)
        return;
    At<uint32_t>(self, field::kCursorId) = id;
    Virtual<void>(self, 0x24, cursor);
}

// The game's 0x005FB0B0 (ret 4; slot 10): SetWindow. The original leaves
// the window in EAX.
void* __fastcall SetWindow(char* self, void*, void* window)
{
    At<void*>(self, field::kWindow) = window;
    return window;
}

// The game's 0x005FB0C0 (slot 27): asks the view to put its current tool
// away (slot 0x68 with 0); the view's answer is what comes back, 0 without a
// view.
uint32_t __fastcall PutAway(char* self, void*)
{
    void* const view = At<void*>(self, field::kView);
    if (!view)
        return 0;
    return Virtual<uint32_t>(view, 0x68, uint32_t{0});
}

// The game's 0x005FB0E0 (slot 29): Shutdown, then the deleting destructor
// with 1, whose answer comes back.
void* __fastcall Destroy(char* self, void*)
{
    Virtual<uint8_t>(self, 0x10);
    return Virtual<void*>(self, 0x68, uint32_t{1});
}

// The game's 0x005FB130: asks the window manager for the mouse (slot 0x74
// with the window). 0 without a window.
uint32_t __fastcall BeginCapture(char* self, void*)
{
    void* const window = At<void*>(self, field::kWindow);
    if (!window)
        return 0;
    return Virtual<uint32_t>(At<void*>(self, field::kManager), 0x74, window);
}

// The game's 0x005FB150: gives the mouse back (slot 0x78 with the window).
// 0 without a window.
uint32_t __fastcall EndCapture(char* self, void*)
{
    void* const window = At<void*>(self, field::kWindow);
    if (!window)
        return 0;
    return Virtual<uint32_t>(At<void*>(self, field::kManager), 0x78, window);
}

// The game's 0x005FB170 (slot 25): 1 when the window manager says (slot
// 0x70) that the control's window has the mouse.
uint32_t __fastcall AmCapturing(char* self, void*)
{
    if (!At<void*>(self, field::kWindow))
        return 0;
    return Virtual<void*>(At<void*>(self, field::kManager), 0x70) == At<void*>(self, field::kWindow) ? 1 : 0;
}

// The game's 0x005FB190: true when the view's current control (slot 0x60)
// is this one. The view is not checked for null.
uint8_t __fastcall IsCurrent(char* self, void*)
{
    return Virtual<void*>(At<void*>(self, field::kView), 0x60) == self;
}

// The game's 0x005FB260 (ret 4; slot 28): PlaySound. An ID that is not zero
// goes to the sound service (global 0x00B43CB8, slot 0x2C) with 1 before it
// and three zeros after.
void __fastcall PlaySound(char*, void*, uint32_t id)
{
    if (!id)
        return;
    if (void* const sound = Global<void*>(addr::SC4_Sound43CB8))
        Virtual<void>(sound, 0x2C, uint32_t{1}, id, uint32_t{0}, uint32_t{0}, uint32_t{0});
}

// --- The model holder ---------------------------------------------------------------------

// The game's 0x005FB290: the holder (one pointer) starts empty. Returns it.
void** __fastcall ModelConstruct(void** holder, void*)
{
    *holder = nullptr;
    return holder;
}

// The game's 0x005FB2A0: hides the model when it is shown (slot 5 asked,
// slot 4 with 0), empties the holder and releases the model.
void __fastcall ModelReset(void** holder, void*)
{
    if (!*holder)
        return;
    if (Virtual<uint8_t>(*holder, 0x14))
        Virtual<void>(*holder, 0x10, uint32_t{0});
    if (void* const model = *holder) {
        *holder = nullptr;
        Release(model);
    }
}

// The game's 0x005FB2D0 (ret 4): shows the model (slot 3 with 0) unless it
// is shown, or hides it (slot 4 with 0) when it is. Nothing without a model.
void __fastcall ModelShow(void** holder, void*, uint8_t show)
{
    if (!*holder)
        return;
    if (show) {
        if (!Virtual<uint8_t>(*holder, 0x14))
            Virtual<void>(*holder, 0xC, uint32_t{0});
    } else if (Virtual<uint8_t>(*holder, 0x14)) {
        Virtual<void>(*holder, 0x10, uint32_t{0});
    }
}

// The game's 0x005FB340: an empty holder gets the model named local_grid
// from the service 0x00B43D1C (slot 7).
void __fastcall ModelMake(void** holder, void*)
{
    if (!*holder)
        Virtual<void>(Global<void*>(addr::SC4_Service43D1C), 0x1C,
                      reinterpret_cast<const char*>(addr::SC4ViewModel_GridName), holder);
}

// --- Shared by several tools -----------------------------------------------------------------

// The game's 0x004B7ED0 (ret 4): the deleting destructor of the tools that
// have nothing of their own to destroy (scroll drag, flora, terrain command).
char* __fastcall PlainDeletingDestruct(char* self, void*, uint8_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x004D7AA0 (ret 4): the deleting destructor of the cheat tool
// and of the sign pick tool, whose destructor body (0x004DA180: the models
// at +0x2C and +0x28, then the base's) is the same function.
char* __fastcall CheatDeletingDestruct(char* self, void*, uint8_t flags)
{
    This<void>(addr::SC4CheatTool_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x004B8480 (ret 8): QueryInterface of the view's default
// control, the cell paint tool and the U-Drive control. The message target's
// ID gives the object itself (not its part at +0x28) and counts the
// reference at +8 up without the vtable (the original calls the shared
// AddRef 0x004BDF00 directly); the rest is the base's.
uint8_t __fastcall QueryInterfaceSelfTarget(char* self, void*, uint32_t id, void** out)
{
    if (id != kIdTarget)
        return QueryInterface(self, nullptr, id, out);
    *out = self;
    ++At<uint32_t>(self, field::kCount);
    return 1;
}

// The game's 0x004B9DE0 (ret 8): QueryInterface of the dispatch tool and
// the MySim dispatch tool. The IDs 1 and 0x452294AA give the message target
// part; nothing else is known, the control's own ID included. A null `this`
// stores null and then reads its vtable, as the original does.
uint8_t __fastcall QueryInterfaceTargetOnly(char* self, void*, uint32_t id, void** out)
{
    if (id != kIdUnknown && id != kIdTarget)
        return 0;
    *out = self ? self + field::kTarget : nullptr;
    AddRef(self);
    return 1;
}

// The game's 0x00661D70 (ret 8): QueryInterface of the obliterate tool, the
// terrain brush, the zone tool and the network drawing tool. The message
// target's ID gives the part at +0x28; the rest is the base's.
uint8_t __fastcall QueryInterfaceTarget(char* self, void*, uint32_t id, void** out)
{
    if (id != kIdTarget)
        return QueryInterface(self, nullptr, id, out);
    *out = self ? self + field::kTarget : nullptr;
    AddRef(self);
    return 1;
}

// The game's 0x004BDBE0 (ret 8): a key slot of six tools. The opposite of
// what 0x00505BE0 answers, which is true: so false.
uint8_t __fastcall OnKeyUnhandled(char* self, void*, uint32_t key, uint32_t modifiers)
{
    return This<uint8_t>(addr::SC4ViewInputControl_OnKeyDefault, self, key, modifiers) ? 0 : 1;
}

// The game's 0x004BA070 (ret 0xC): where on the screen the figure's
// occupant is (its position through the camera view at 0x00B43DD8, read
// before the calls), less the cursor, truncated, into +0x34 and +0x38. The
// position's three floats are not set before the occupant fills them.
void __fastcall SetGrabOffset(char* self, void*, void* figure, int32_t x, int32_t z)
{
    void* const camera = Global<void*>(addr::SC4_CameraView);
    float position[3];
    Virtual<void>(Virtual<void*>(figure, 0xC), 0x20, position);
    This<void>(addr::SC4CameraView_WorldToView, camera, position);
    At<int32_t>(self, field::kGrabX) = Truncate(static_cast<double>(position[0]) - static_cast<double>(x));
    At<int32_t>(self, field::kGrabZ) = Truncate(static_cast<double>(position[1]) - static_cast<double>(z));
}

// --- Registration -------------------------------------------------------------------------

bool InstallToolBase(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4ViewInputControl_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4ViewInputControl_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4ViewInputControl_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4ViewInputControl_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::SC4ViewInputControl_Release, reinterpret_cast<void*>(&ReleaseSelf)},
        {addr::SC4ViewInputControl_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4ViewInputControl_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4ViewInputControl_SetCursor, reinterpret_cast<void*>(&SetCursor)},
        {addr::SC4ViewInputControl_SetCursorID, reinterpret_cast<void*>(&SetCursorId)},
        {addr::SC4ViewInputControl_SetWindow, reinterpret_cast<void*>(&SetWindow)},
        {addr::SC4ViewInputControl_PutAway, reinterpret_cast<void*>(&PutAway)},
        {addr::SC4ViewInputControl_Destroy, reinterpret_cast<void*>(&Destroy)},
        {addr::SC4ViewInputControl_BeginCapture, reinterpret_cast<void*>(&BeginCapture)},
        {addr::SC4ViewInputControl_EndCapture, reinterpret_cast<void*>(&EndCapture)},
        {addr::SC4ViewInputControl_AmCapturing, reinterpret_cast<void*>(&AmCapturing)},
        {addr::SC4ViewInputControl_IsCurrent, reinterpret_cast<void*>(&IsCurrent)},
        {addr::SC4ViewInputControl_PlaySound, reinterpret_cast<void*>(&PlaySound)},
        {addr::SC4ViewModel_Construct, reinterpret_cast<void*>(&ModelConstruct)},
        {addr::SC4ViewModel_Reset, reinterpret_cast<void*>(&ModelReset)},
        {addr::SC4ViewModel_Show, reinterpret_cast<void*>(&ModelShow)},
        {addr::SC4ViewModel_Make, reinterpret_cast<void*>(&ModelMake)},
        {addr::SC4ViewInputControl_PlainDeletingDestructor, reinterpret_cast<void*>(&PlainDeletingDestruct)},
        {addr::SC4CheatTool_DeletingDestructor, reinterpret_cast<void*>(&CheatDeletingDestruct)},
        {addr::SC4ViewInputControl_QueryInterfaceSelfTarget, reinterpret_cast<void*>(&QueryInterfaceSelfTarget)},
        {addr::SC4ViewInputControl_QueryInterfaceTargetOnly, reinterpret_cast<void*>(&QueryInterfaceTargetOnly)},
        {addr::SC4ViewInputControl_QueryInterfaceTarget, reinterpret_cast<void*>(&QueryInterfaceTarget)},
        {addr::SC4ViewInputControl_OnKeyUnhandled, reinterpret_cast<void*>(&OnKeyUnhandled)},
        {addr::SC4ViewInputControl_SetGrabOffset, reinterpret_cast<void*>(&SetGrabOffset)},
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
    "ui.tools.base",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PreAppInit, // as the tools on it (tool_bulldoze.cpp)
    addr::SC4ViewInputControl_Init,
    "Our view input control base class, model holder and the tools' shared small functions (docs/tools.md)",
    false,
    InstallToolBase,
});

} // namespace
} // namespace opensc4::tool_base
