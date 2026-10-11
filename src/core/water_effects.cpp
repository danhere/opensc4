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
#include <utility>
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

// --- The effects manager ---------------------------------------------------------------------

// The machine the translated function runs on (effects_draw.inc): memory by
// address, the x87 stack as doubles, its compare flags as the status word's
// high byte holds them (C0 bit 0, C2 bit 2, C3 bit 6).
template <typename T>
T& M(uint32_t address)
{
    return *reinterpret_cast<T*>(static_cast<std::uintptr_t>(address));
}

[[maybe_unused]] bool Even(uint32_t value)
{
    value &= 0xFF;
    value ^= value >> 4;
    value ^= value >> 2;
    value ^= value >> 1;
    return (value & 1) == 0;
}

uint32_t CompareStatus(double a, double b)
{
    if (a != a || b != b)
        return 0x45;
    return a < b ? 0x01 : a == b ? 0x40 : 0x00;
}

// The effects manager's draw (0x00593120), translated instruction by
// instruction. `self` is the interface the 3D render holds, the object + 4.
// The effects in view from three lists go into a vector of {depth, effect}
// (+0xDA0), those out of view into another (+0xDAC); the particle budget
// (+0xC9C) follows the count of the last frame (+0xC8C); the vector is
// sorted and each effect drawn (its slot 0x24), then the fourth list
// (+0xBC0). The effects' own drawing is still the game's.
uint32_t DrawEffects(char* self, char* context)
{
    uint32_t stackmem[160] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = reinterpret_cast<uint32_t>(context);
    double st[8] = {};
    int top = -1;
    [[maybe_unused]] uint32_t status = 0;
#define ST(i) st[top - (i)]
#define PUSH(value)                                                                                                      \
    do {                                                                                                                 \
        const double pushed = (value);                                                                                   \
        st[++top] = pushed;                                                                                              \
    } while (0)
#define POP() --top
#define FCOM(value) status = CompareStatus(ST(0), (value))
#include "core/effects_draw.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

constexpr std::size_t kManagerSize = 0xDE4; // from the object, 4 below the interface

using EffectsFn = uint32_t(__fastcall*)(char*, void*, char*);
EffectsFn g_effects = nullptr;
bool g_compareEffects = false;

// The draw cannot be run twice in a frame: the effects move on when drawn.
// So the comparison runs ours with the calls that draw or step something
// only recorded, then the game's with the same calls recorded and made, and
// compares the two records and what both left in the manager.
enum class Trace { Off, Record, RecordAndCall };
Trace g_trace = Trace::Off;
std::vector<uint32_t> g_calls;

using Call0Fn = uint32_t(__fastcall*)(char*, void*);
using Call1Fn = uint32_t(__fastcall*)(char*, void*, uint32_t);
using Call2Fn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t);
Call1Fn g_beforeDraw = nullptr, g_stepShake = nullptr, g_decalDraw = nullptr;
Call2Fn g_drawFlash = nullptr, g_effectDraw = nullptr;
Call0Fn g_afterDraw = nullptr;

template <typename Fn, typename... Args>
uint32_t Traced(uint32_t id, Fn original, char* self, Args... args)
{
    if (g_trace != Trace::Off) {
        g_calls.push_back(id);
        g_calls.push_back(reinterpret_cast<uint32_t>(self));
        (g_calls.push_back(args), ...);
        if (g_trace == Trace::Record)
            return 0;
    }
    return original(self, nullptr, args...);
}

uint32_t __fastcall DBeforeDraw(char* self, void*, uint32_t context)
{
    return Traced(1, g_beforeDraw, self, context);
}

uint32_t __fastcall DDrawFlash(char* self, void*, uint32_t context, uint32_t step)
{
    return Traced(2, g_drawFlash, self, context, step);
}

uint32_t __fastcall DStepShake(char* self, void*, uint32_t step)
{
    return Traced(3, g_stepShake, self, step);
}

uint32_t __fastcall DAfterDraw(char* self, void*)
{
    return Traced(4, g_afterDraw, self);
}

uint32_t __fastcall DEffectDraw(char* self, void*, uint32_t step, uint32_t context)
{
    return Traced(5, g_effectDraw, self, step, context);
}

uint32_t __fastcall DDecalDraw(char* self, void*, uint32_t step)
{
    return Traced(6, g_decalDraw, self, step);
}


OPENSC4_THUNK(TBeforeDraw, DBeforeDraw, 4)
OPENSC4_THUNK(TDrawFlash, DDrawFlash, 8)
OPENSC4_THUNK(TStepShake, DStepShake, 4)
OPENSC4_THUNK(TAfterDraw, DAfterDraw, 0)
OPENSC4_THUNK(TEffectDraw, DEffectDraw, 8)
OPENSC4_THUNK(TDecalDraw, DDecalDraw, 4)

// Gives the two vectors the draw fills room for every effect (from the
// game's allocator, as their own growth would), so that neither compared
// run reallocates them: a copy cannot undo that.
void ReserveEffects(char* self)
{
    std::size_t count = 0;
    for (const std::size_t list : {std::size_t{0xBF0}, std::size_t{0xC38}, std::size_t{0xC5C}})
        count += static_cast<std::size_t>(At<char*>(self, list + 4) - At<char*>(self, list)) / 4;
    for (const auto& [vector, size] :
         {std::pair{std::size_t{0xDA0}, std::size_t{8}}, std::pair{std::size_t{0xDAC}, std::size_t{4}}}) {
        char* const begin = At<char*>(self, vector);
        const std::size_t used = static_cast<std::size_t>(At<char*>(self, vector + 4) - begin);
        if (static_cast<std::size_t>(At<char*>(self, vector + 8) - begin) >= count * size)
            continue;
        char* const grown = reinterpret_cast<char*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(count * size);
        if (used)
            std::memcpy(grown, begin, used);
        if (begin)
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(begin);
        At<char*>(self, vector) = grown;
        At<char*>(self, vector + 4) = grown + used;
        At<char*>(self, vector + 8) = grown + count * size;
    }
}

uint32_t __fastcall DEffects(char* self, void*, char* context)
{
    if (g_depth > 0 || g_trace != Trace::Off)
        return g_effects(self, nullptr, context);
    if (!g_compareEffects)
        return DrawEffects(self, context);
    const char* const id = g_selfCheck ? "effects.draw.selfcheck" : "effects.draw";
    ReserveEffects(self);
    char* const manager = self - 4;
    const std::vector<char> before(manager, manager + kManagerSize);
    g_calls.clear();
    g_trace = Trace::Record;
    if (g_selfCheck)
        g_effects(self, nullptr, context);
    else
        DrawEffects(self, context);
    const std::vector<char> mine(manager, manager + kManagerSize);
    const std::vector<char> ourSorted(At<char*>(self, 0xDA0), At<char*>(self, 0xDA4));
    const std::vector<char> ourHidden(At<char*>(self, 0xDAC), At<char*>(self, 0xDB0));
    const std::vector<uint32_t> ourCalls = std::move(g_calls);
    std::memcpy(manager, before.data(), kManagerSize);
    g_calls.clear();
    g_trace = Trace::RecordAndCall;
    const uint32_t result = g_effects(self, nullptr, context);
    g_trace = Trace::Off;
    const std::vector<uint32_t> gameCalls = std::move(g_calls);
    g_calls.clear();
    ++g_passesCompared;
    g_drawsCompared += gameCalls.size();
    if ((g_passesCompared & (g_passesCompared - 1)) == 0)
        log::Write("water effects: %s: %llu passes, %llu call words compared so far", id,
                   static_cast<unsigned long long>(g_passesCompared), static_cast<unsigned long long>(g_drawsCompared));
    // Drawing an effect changes the fourth list (an effect leaves a mark on
    // the ground, or takes one away) and counts particles, which only the
    // game's run does. So the fourth list's draws are left out, every other
    // call is compared exactly, and of the manager what the draw itself
    // works out: the two vectors and their contents, the budget and the
    // number of effects drawn.
    const auto split = [](const std::vector<uint32_t>& calls, std::vector<uint32_t>& decals) {
        static const uint8_t kWords[7] = {0, 3, 4, 3, 2, 4, 3};
        std::vector<uint32_t> rest;
        for (std::size_t i = 0; i < calls.size() && calls[i] < 7; i += kWords[calls[i]])
            (calls[i] == 6 ? decals : rest).insert((calls[i] == 6 ? decals : rest).end(), calls.begin() + i,
                                                   calls.begin() + i + kWords[calls[i]]);
        return rest;
    };
    std::vector<uint32_t> ourDecals, gameDecals;
    const std::vector<uint32_t> ourRest = split(ourCalls, ourDecals), gameRest = split(gameCalls, gameDecals);
    char text[96];
    if (ourRest != gameRest) {
        std::size_t at = 0;
        while (at < ourRest.size() && at < gameRest.size() && ourRest[at] == gameRest[at])
            ++at;
        std::snprintf(text, sizeof text, "call word %zu of %zu: %08X", at, gameRest.size(),
                      at < gameRest.size() ? gameRest[at] : 0u);
        const std::string gameText = text;
        std::snprintf(text, sizeof text, "call word %zu of %zu: %08X", at, ourRest.size(),
                      at < ourRest.size() ? ourRest[at] : 0u);
        compare::Record(id, false, gameText, text);
        return result;
    }
    for (const auto& [from, to] : {std::pair{std::size_t{0xC94}, std::size_t{0xC98}},
                                   std::pair{std::size_t{0xC9C}, std::size_t{0xCA0}},
                                   std::pair{std::size_t{0xDA0}, std::size_t{0xDB8}}}) {
        if (std::memcmp(self + from, mine.data() + 4 + from, to - from) == 0)
            continue;
        std::snprintf(text, sizeof text, "manager+%zX ", from);
        compare::Record(id, false, text + Hex(self + from, to - from), text + Hex(mine.data() + 4 + from, to - from));
        return result;
    }
    if (ourSorted != std::vector<char>(At<char*>(self, 0xDA0), At<char*>(self, 0xDA4)) ||
        ourHidden != std::vector<char>(At<char*>(self, 0xDAC), At<char*>(self, 0xDB0))) {
        compare::Record(id, false, "the effects in view, sorted, and those out of view", "another order or set");
        return result;
    }
    compare::Record(id, true, std::string(), std::string());
    return result;
}

OPENSC4_THUNK(TEffects, DEffects, 4)

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

bool InstallEffects(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareEffects = mode == registry::Mode::Compare;
    if (g_compareEffects &&
        !(Detour(addr::SC4EffectsManager_BeforeDraw, &TBeforeDraw, &g_beforeDraw) &&
          Detour(addr::SC4EffectsManager_DrawFlash, &TDrawFlash, &g_drawFlash) &&
          Detour(addr::SC4EffectsManager_StepShake, &TStepShake, &g_stepShake) &&
          Detour(addr::SC4EffectsManager_AfterDraw, &TAfterDraw, &g_afterDraw) &&
          Detour(addr::SC4Effect_Draw, &TEffectDraw, &g_effectDraw) &&
          Detour(addr::SC4EffectDecal_Draw, &TDecalDraw, &g_decalDraw)))
        return false;
    if (!Detour(addr::SC4EffectsManager_Draw, &TEffects, &g_effects))
        return false;
    log::Write("water effects: effects draw %s", g_compareEffects ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredEffects = registry::Register({
    "renderer.effects.draw",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectsManager_Draw,
    "The effects manager's draw: the effects in view collected, sorted by depth and drawn (each effect's own "
    "drawing still the game's); compare mode runs ours with the calls that draw only recorded, then the game's, and "
    "compares the calls and the manager (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallEffects,
});

} // namespace
} // namespace opensc4::water_effects
