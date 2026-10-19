// The plop tool's message target thunks (T12): the three adjustor thunks of
// the message target at +0x28 (vtable 0x00A90728). Replacement, off by
// default:
//
//   ui.tools.plop5: 0x004BFB00 (QueryInterface), 0x004BFB10 (AddRef) and
//     0x004BFB20 (Release). Each is two instructions in the game: subtract
//     0x28 from `this` and jump to the tool's own method.
//
// Four message vtables hold them, so the hooks serve all four tools:
//
//   0x00A90728  the plop tool's       0x004BFB00, 0x004BFB10, 0x004BFB20
//   0x00A90A78  the query tool's      0x004BFB00, 0x004BFB10, 0x004BFB20
//   0x00A90258  the dispatch tool's               0x004BFB10, 0x004BFB20
//   0x00A85988  the MySim dispatch tool's         0x004BFB10, 0x004BFB20
//
// (the dispatch tools' slot 0 is the base's thunk 0x004BA870). The jump
// targets are called by address: the plop and query tools' QueryInterface
// 0x004BDF10 (the game's, a shared helper), the base's AddRef 0x004BDF00 and
// Release 0x004B9E20 (ours in ui.tools.base when that group is on).
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_plop_5 {
namespace {

// The message target sits at +0x28 in every tool that uses these thunks.
constexpr std::ptrdiff_t kTarget = 0x28;

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

// The game's 0x004BFB00: slot 0 of the message target. (id, out), ret 8.
uint8_t __fastcall MessageQueryInterface(char* self, void*, uint32_t id, void** object)
{
    return This<uint8_t>(addr::cSC4ViewInputControlPlaceLot_QueryInterface, self - kTarget, id, object);
}

// The game's 0x004BFB10: slot 1. No arguments.
uint32_t __fastcall MessageAddRef(char* self, void*)
{
    return This<uint32_t>(addr::SC4ViewInputControl_AddRef, self - kTarget);
}

// The game's 0x004BFB20: slot 2. No arguments.
uint32_t __fastcall MessageRelease(char* self, void*)
{
    return This<uint32_t>(addr::SC4ViewInputControl_Release, self - kTarget);
}

// --- Registration ------------------------------------------------------------------------

bool InstallPlop5(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[3] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4ViewInputControlPlaceLot_MessageQueryInterface, reinterpret_cast<void*>(&MessageQueryInterface)},
        {addr::cSC4ViewInputControlPlaceLot_MessageAddRef, reinterpret_cast<void*>(&MessageAddRef)},
        {addr::cSC4ViewInputControlPlaceLot_MessageRelease, reinterpret_cast<void*>(&MessageRelease)},
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
    "ui.tools.plop5",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4ViewInputControlPlaceLot_MessageQueryInterface,
    "Our plop tool's message target thunks, shared with the query and dispatch tools (docs/tools.md)",
    false,
    InstallPlop5,
});

} // namespace
} // namespace opensc4::tool_plop_5
