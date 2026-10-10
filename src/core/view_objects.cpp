// diagnostics.trace.viewobjects: logs every object that registers with the
// 3D render to draw in one of its passes (cSC43DRender::AddViewObject), with
// its vtable and the function it draws with (slot 0xC). Used to find what
// draws the water effects, the sky and the clouds (docs/renderer.md).
// OPENSC4_VIEWOBJECT_SKIP=<draw function, hex>[,...] leaves the objects that
// draw with one of those functions out, to see in a screenshot what they
// draw.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace opensc4::view_objects {
namespace {

using AddFn = bool(__fastcall*)(void*, void*, void*, int32_t, uint32_t);
AddFn g_add = nullptr;

bool __fastcall DAdd(void* render, void*, void* object, int32_t pass, uint32_t order)
{
    const uint32_t* const vtable = *static_cast<const uint32_t* const*>(object);
    log::Write("viewobjects: object %p vtable %08X draw %08X pass %d order %u", object,
               static_cast<unsigned>(reinterpret_cast<uintptr_t>(vtable)), static_cast<unsigned>(vtable[3]),
               static_cast<int>(pass), static_cast<unsigned>(order));
    if (const char* skip = std::getenv("OPENSC4_VIEWOBJECT_SKIP")) {
        while (*skip) {
            char* end = nullptr;
            if (std::strtoul(skip, &end, 16) == vtable[3]) {
                log::Write("viewobjects: left out");
                return true;
            }
            skip = *end ? end + 1 : end;
        }
    }
    return g_add(render, nullptr, object, pass, order);
}

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* entry = addr_check::Find(addr::cSC43DRender_AddViewObject);
    return entry && hook::Install(*entry, &DAdd, &g_add);
}

const bool g_registered = registry::Register({
    "diagnostics.trace.viewobjects",
    "diagnostics",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC43DRender_AddViewObject,
    "Logs the objects that register to draw in the 3D render's passes (docs/renderer.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::view_objects
