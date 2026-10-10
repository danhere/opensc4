// Our drawing of the water effects (build 641; docs/renderer.md, "Water
// effects"): the shore strips the terrain draws after the models. Compared
// in the same frame as the terrain's passes are (terrain_draw.cpp): ours
// runs first with the D3D11 driver recording a checksum per draw and keeping
// them off the screen, the state it changed is put back, then the game's
// runs and draws for real, and the two lists of draws are compared.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include "core/gd/gd.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace opensc4::water_effects {
namespace {

constexpr std::size_t kShoreSize = 0xB8;    // from the interface the 3D render holds
constexpr std::size_t kContextSize = 0x200; // cSC43DRender + 0x11C
constexpr std::size_t kCameraSize = 0x1A8;
constexpr std::uintptr_t kShoreStateReady = 0x00B4D525;
constexpr std::uintptr_t kShoreState = 0x00B4D528;

int g_depth = 0;
uint64_t g_passesCompared = 0, g_drawsCompared = 0;

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

struct Region {
    const char* name;
    char* data;
    std::size_t size;
};

std::string Hex(const void* data, std::size_t size)
{
    std::string text;
    char hex[4];
    for (std::size_t i = 0; i < size; ++i) {
        std::snprintf(hex, sizeof hex, "%02X", static_cast<const uint8_t*>(data)[i]);
        text += hex;
    }
    return text;
}

// The memory the draw may change: the object, the render context (its state
// cache and its texture stage cache) and the S3D camera.
std::vector<Region> ShoreRegions(char* self, char* context)
{
    char* const render = At<char*>(context, 0x34);
    char* const camera = render ? At<char*>(render, 0xD8) : nullptr;
    std::vector<Region> regions{{"shore", self, kShoreSize}, {"context", context, kContextSize}};
    if (camera)
        regions.push_back({"camera", camera, kCameraSize});
    if (char* const stages = At<char*>(context, 0xE0))
        regions.push_back({"stages", stages, static_cast<std::size_t>(At<char*>(context, 0xE4) - stages)});
    regions.push_back({"stateready", reinterpret_cast<char*>(kShoreStateReady), 1});
    regions.push_back({"state", reinterpret_cast<char*>(kShoreState), 8});
    return regions;
}

template <typename Ours, typename Original>
void DrawCompared(const char* id, const std::vector<Region>& regions, Ours&& ours, Original&& original)
{
    std::vector<std::vector<char>> before, mine;
    for (const Region& r : regions)
        before.emplace_back(r.data, r.data + r.size);
    const std::vector<char> pipeline = gd::SaveState();
    ++g_depth;
    gd::BeginCapture(false);
    ours();
    const std::vector<uint32_t> ourDraws = gd::EndCapture();
    const std::vector<char> ourPipeline = gd::SaveState();
    for (const Region& r : regions)
        mine.emplace_back(r.data, r.data + r.size);
    for (std::size_t i = 0; i < regions.size(); ++i)
        std::memcpy(regions[i].data, before[i].data(), regions[i].size);
    gd::RestoreState(pipeline);
    gd::BeginCapture(true);
    original();
    const std::vector<uint32_t> gameDraws = gd::EndCapture();
    --g_depth;
    g_drawsCompared += gameDraws.size();
    ++g_passesCompared;
    if ((g_passesCompared & (g_passesCompared - 1)) == 0)
        log::Write("water effects: %s: %llu passes, %llu draws compared so far", id,
                   static_cast<unsigned long long>(g_passesCompared), static_cast<unsigned long long>(g_drawsCompared));
    char text[96];
    if (ourDraws != gameDraws) {
        std::size_t at = 0;
        while (at < ourDraws.size() && at < gameDraws.size() && ourDraws[at] == gameDraws[at])
            ++at;
        std::snprintf(text, sizeof text, "draw %zu of %zu: %08X", at, gameDraws.size(),
                      at < gameDraws.size() ? gameDraws[at] : 0u);
        const std::string game = text;
        std::snprintf(text, sizeof text, "draw %zu of %zu: %08X", at, ourDraws.size(),
                      at < ourDraws.size() ? ourDraws[at] : 0u);
        compare::Record(id, false, game, text);
        return;
    }
    const std::vector<char> gamePipeline = gd::SaveState();
    if (gamePipeline != ourPipeline) {
        std::size_t at = 0;
        while (at < gamePipeline.size() && at < ourPipeline.size() && gamePipeline[at] == ourPipeline[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "driver state+%zX ", at);
        const std::size_t n = gamePipeline.size() - at < 8 ? gamePipeline.size() - at : 8;
        compare::Record(id, false, text + Hex(gamePipeline.data() + at, n), text + Hex(ourPipeline.data() + at, n));
        return;
    }
    for (std::size_t i = 0; i < regions.size(); ++i) {
        const Region& r = regions[i];
        if (std::memcmp(r.data, mine[i].data(), r.size) == 0)
            continue;
        std::size_t at = 0;
        while (r.data[at] == mine[i][at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "%s+%zX ", r.name, at);
        const std::size_t n = r.size - at < 8 ? r.size - at : 8;
        compare::Record(id, false, text + Hex(r.data + at, n), text + Hex(mine[i].data() + at, n));
        return;
    }
    compare::Record(id, true, std::string(), std::string());
}

// OPENSC4_WATER_SELFCHECK=1: compare mode runs the game's draw against
// itself, to check the comparison.
const bool g_selfCheck = [] {
    const char* v = std::getenv("OPENSC4_WATER_SELFCHECK");
    return v && *v == '1';
}();

// --- The shore ---------------------------------------------------------------------------

// A strip (0x0075FE70): its 24-byte vertices at +0 to +4, the index of its
// texture at +0xC, its bounding box at +0x34 and +0x40.
void DrawShoreStrip(char* self, char* strip, char* context)
{
    char* const textures = At<char*>(self, 0xB4);
    if (!textures)
        return;
    if (!CallThis<bool>(addr::SC4CameraView_BoxInView, At<void*>(context, 0x24), strip + 0x34, strip + 0x40))
        return;
    CallThis<void>(addr::SC4RenderContext_BindTexture, context,
                   At<uint32_t*>(textures, 0)[At<uint32_t>(strip, 0xC)], 0u);
    char* const vertices = At<char*>(strip, 0);
    CallThis<void>(addr::SC4RenderContext_DrawArrays, context, 7u, 0xAu,
                   static_cast<int32_t>((At<char*>(strip, 4) - vertices) / 24), vertices);
}

// The draw (0x00761FF0): the strips are built on the first draw and redone
// for a new zoom (both still the game's); then every group (0x38 bytes, from
// +0x34 to +0x38) with strips (a list at +0xC) and its box (+0x20, +0x2C) in
// view has its strips drawn.
bool DrawShore(char* self, char* context)
{
    if (!At<uint8_t>(self, 0x18)) {
        At<uint8_t>(self, 0x18) = 1;
        CallThis<void>(addr::STEShore_Build, self);
        CallThis<void>(addr::STEShore_SetZoom, self);
    }
    if (At<char*>(self, 0x34) == At<char*>(self, 0x38))
        return true;
    if (!At<uint8_t>(At<char*>(At<char*>(context, 0x38), 0xC), 0x7C))
        return true;
    char* const state = reinterpret_cast<char*>(kShoreState);
    if (!Global<uint8_t>(kShoreStateReady)) {
        CallThis<void>(addr::SC4RenderState_Init, state, 1u, 0u, 1u, 0u, 1u, 1u, 1u, 3u, 4u, 5u, 0xFFFFFFFFu, -1.0f, 1u,
                       0xFFFFFFFFu);
        Global<uint8_t>(kShoreStateReady) = 1;
    }
    CallThis<void>(addr::SC4RenderContext_SetRenderState, context, state);
    CallThis<void>(addr::SC4RenderContext_UpdateViewMatrix, context);
    if (At<int32_t>(context, 0x14) != At<int32_t>(self, 0x40)) {
        At<int32_t>(self, 0x40) = At<int32_t>(context, 0x14);
        CallThis<void>(addr::STEShore_SetZoom, self);
    }
    char* const end = At<char*>(self, 0x38);
    for (char* group = At<char*>(self, 0x34); group != end; group += 0x38) {
        char* const head = At<char*>(group, 0xC);
        if (At<char*>(head, 0) == head)
            continue;
        if (!CallThis<bool>(addr::SC4CameraView_BoxInView, At<void*>(context, 0x24), group + 0x20, group + 0x2C))
            continue;
        for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0))
            DrawShoreStrip(self, node + 8, context);
    }
    return true;
}

using ShoreFn = bool(__fastcall*)(char*, void*, char*);
ShoreFn g_shore = nullptr;
bool g_compareShore = false;

bool __fastcall DShore(char* self, void*, char* context)
{
    if (g_depth > 0)
        return g_shore(self, nullptr, context);
    if (!g_compareShore)
        return DrawShore(self, context);
    // The build and the zoom change free and allocate the strips, which a
    // copy cannot undo: those frames run the game's only.
    if (!At<uint8_t>(self, 0x18) || At<uint8_t>(self, 0x78) || At<int32_t>(context, 0x14) != At<int32_t>(self, 0x40))
        return g_shore(self, nullptr, context);
    DrawCompared(g_selfCheck ? "water.shore.selfcheck" : "water.shore", ShoreRegions(self, context),
                 [&] {
                     if (g_selfCheck)
                         g_shore(self, nullptr, context);
                     else
                         DrawShore(self, context);
                 },
                 [&] { g_shore(self, nullptr, context); });
    return true;
}

OPENSC4_THUNK(TShore, DShore, 4)

template <typename Fn>
bool Detour(std::uintptr_t address, void (*thunk)(), Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, reinterpret_cast<Fn>(thunk), original);
}

bool InstallShore(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareShore = mode == registry::Mode::Compare;
    if (!Detour(addr::STEShore_Draw, &TShore, &g_shore))
        return false;
    log::Write("water effects: shore %s", g_compareShore ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredShore = registry::Register({
    "renderer.water.shore",
    "renderer/water",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::STEShore_Draw,
    "The shore strips' draw after the models (building the strips still the game's); compare mode runs ours "
    "quietly, then the game's, and compares the draws (docs/renderer.md, \"Water effects\")",
    true,
    InstallShore,
});

} // namespace
} // namespace opensc4::water_effects
