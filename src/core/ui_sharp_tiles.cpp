// renderer.interface.sharp_tiles: the CPU-drawn interface pictures (text
// windows, the region view) as textures at the physical size when the D3D11
// driver draws at a render scale (docs/ui-scaling.md, "Sharp interface
// tiles"). The first slice of crisp text: the tiles' textures are made scale
// times larger (gd::PhysicalTextures around UIPictureTextures::Create,
// 0x00991440) and the driver fills them from the game-sized pixels the game
// uploads. Text drawn at the physical size into them comes next.
//
// The game draws each tile with texture coordinates over the size it asked
// for (0x0098F110 divides by the tile's size from the textures object), so a
// larger texture fills the same place. With no render scale nothing changes.
#include "core/address_check.h"
#include "core/game.h"
#include "core/gd/gd.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstdint>

namespace opensc4::sharptiles {
namespace {

using CreateFn = uint32_t(__fastcall*)(void*, void*, uint32_t);
CreateFn g_create = nullptr;

uint32_t __fastcall DCreate(void* self, void*, uint32_t argument)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("sharptiles: first UIPictureTextures::Create 0x00991440");
    }
    gd::PhysicalTextures physical;
    return g_create(self, nullptr, argument);
}

OPENSC4_THUNK(TCreate, DCreate, 4)

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* entry = addr_check::Find(addr::UIPictureTextures_Create);
    if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(&TCreate), reinterpret_cast<void**>(&g_create)))
        return false;
    log::Write("sharptiles: interface picture tiles made at the physical size under a render scale");
    return true;
}

const bool g_registered = registry::Register({"renderer.interface.sharp_tiles", "renderer/interface",
                                              registry::Kind::Detour, registry::Phase::Attach,
                                              addr::UIPictureTextures_Create,
                                              "The interface pictures' tile textures (text windows, the region view) "
                                              "at the physical size under a render scale (docs/ui-scaling.md)",
                                              false, Install});

} // namespace
} // namespace opensc4::sharptiles
