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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

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

// diagnostics.trace.effects: after each draw of the effects manager, counts
// what it drew by class (the vtable, and slot 0x24, the effect's own draw):
// the depth-sorted effects in view (+0xDA0) and the fourth list (+0xBC0).
using DrawFn = bool(__fastcall*)(char*, void*, char*);
DrawFn g_effectsDraw = nullptr;

bool __fastcall DEffectsDraw(char* manager, void*, char* context)
{
    const bool result = g_effectsDraw(manager, nullptr, context);
    static std::vector<std::pair<uint32_t, uint64_t>> counts;
    static uint64_t frames = 0;
    const auto count = [](uint32_t object, uint32_t tag) {
        const uint32_t vtable = *reinterpret_cast<const uint32_t*>(object) | tag;
        for (auto& c : counts) {
            if (c.first == vtable) {
                ++c.second;
                return;
            }
        }
        counts.push_back({vtable, 1});
    };
    const auto at = [&](std::size_t offset) { return *reinterpret_cast<uint32_t*>(manager + offset); };
    for (uint32_t pair = at(0xDA0); pair != at(0xDA4); pair += 8)
        count(reinterpret_cast<const uint32_t*>(pair)[1], 0);
    for (uint32_t index = at(0xBCC); index != at(0xBD0); index += 4)
        count(reinterpret_cast<const uint32_t*>(at(0xBC0))[*reinterpret_cast<const uint32_t*>(index)], 1);
    if (++frames % 2000 != 0)
        return result;
    std::string text;
    char entry[64];
    for (const auto& [vtable, n] : counts) {
        std::snprintf(entry, sizeof entry, " %08X%s>%08X:%llu", vtable & ~1u, vtable & 1 ? "(list 4)" : "",
                      reinterpret_cast<const uint32_t*>(vtable & ~1u)[9], static_cast<unsigned long long>(n));
        text += entry;
    }
    log::Write("effects: census after %llu draws, vtable>slot 0x24:count:%s", static_cast<unsigned long long>(frames),
               text.c_str());
    return result;
}

bool InstallEffects(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* entry = addr_check::Find(addr::SC4EffectsManager_Draw);
    return entry && hook::Install(*entry, &DEffectsDraw, &g_effectsDraw);
}

const bool g_registeredEffects = registry::Register({
    "diagnostics.trace.effects",
    "diagnostics",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectsManager_Draw,
    "Counts what the effects manager draws by class (docs/renderer.md)",
    false,
    InstallEffects,
});

} // namespace
} // namespace opensc4::view_objects
