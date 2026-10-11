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

#include <windows.h>

#include "core/gd/gd.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>
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

// --- An effect -----------------------------------------------------------------------------

// The effect's draw changes the effect and frees memory, so it cannot run
// twice. Its comparison runs the game's for real with the functions it calls
// recorded (which, on what, with what, and what the two that steer it
// answered), puts the effect back as it was, and runs ours with every call
// only recorded and answered from the game's record; then the two records
// and the two effects are compared, and the effect is put back as the game
// left it.
struct EffectCall {
    uint32_t target, self, args[2];
    bool operator==(const EffectCall& other) const
    {
        return target == other.target && self == other.self && args[0] == other.args[0] && args[1] == other.args[1];
    }
};
bool g_effectRecord = false, g_effectReplay = false;
int g_effectNested = 0;
std::vector<EffectCall> g_effectGameCalls, g_effectOurCalls;
std::vector<uint32_t> g_effectAnswers;
std::size_t g_effectAnswer = 0;
float g_effectClock = 0;

// What a recorded call changed in the effect and its list nodes (the bytes
// that differ after it, by their place in the saved copy), which the replay
// applies at the same call: the effect's draw goes by what its steps leave
// in the effect.
constexpr std::size_t kEffectSize = 0x220;
char* g_effectSelf = nullptr;
std::vector<char*> g_effectNodes;
std::vector<std::vector<std::pair<uint32_t, char>>> g_effectChanges;

// The saved copy: the effect, then 13 bytes per list node (the head first):
// the node and the first byte of its particle, which says whether the
// particle is still running (the step clears it when the particle ends).
char g_noParticle = 0;
std::vector<char*> g_effectParticles;
std::unordered_set<char*> g_effectNodeSet;

char* EffectByte(std::size_t at)
{
    if (at < kEffectSize)
        return g_effectSelf + at;
    const std::size_t node = (at - kEffectSize) / 13, offset = (at - kEffectSize) % 13;
    if (offset < 12)
        return g_effectNodes[node] + offset;
    return g_effectParticles[node];
}

std::vector<char> SaveEffect()
{
    std::vector<char> bytes(kEffectSize + g_effectNodes.size() * 13);
    for (std::size_t at = 0; at < bytes.size(); ++at)
        bytes[at] = *EffectByte(at);
    return bytes;
}

void RestoreEffect(const std::vector<char>& bytes)
{
    for (std::size_t at = 0; at < bytes.size(); ++at)
        *EffectByte(at) = bytes[at];
}

std::vector<char> g_effectTaken;
std::vector<std::pair<char*, char>> g_effectPoked;

bool IsTake(uint32_t target)
{
    return target == addr::SC4EffectBase_TakeParticle || target == addr::SC4EffectBaseB_TakeParticle;
}

void UnpokeParticles()
{
    for (auto poked = g_effectPoked.rbegin(); poked != g_effectPoked.rend(); ++poked)
        *poked->first = poked->second;
    g_effectPoked.clear();
}

// The recorded functions whose answer the caller goes by: the particle's
// step and the end check (a byte), and taking a particle (a pointer).
bool g_poolHooked = false;

bool Steers(uint32_t target)
{
    return target == addr::SC4Effect_StepParticle || target == addr::SC4EffectBase_Finish ||
           (g_poolHooked &&
            (target == addr::SC4EffectBase_TakeParticle || target == addr::SC4EffectBaseB_TakeParticle));
}

// The functions of the effect the comparison records in the game's run (the
// particle pool's only once their detours are in).
bool Recorded(uint32_t target)
{
    for (const uint32_t address :
         {addr::SC4Effect_StepParticle, addr::SC4Effect_Emit, addr::SC4Effect_BeforeParticles,
          addr::SC4Effect_DrawParticlesTextured, addr::SC4Effect_DrawParticles, addr::SC4EffectB_DrawParticles,
          addr::SC4Effect_NotDrawn, addr::SC4EffectB_NotDrawn, addr::SC4Effect_StepFlag2000,
          addr::SC4Effect_PathVelocity, addr::SC4Effect_StepFlag80000000, addr::SC4EffectBase_Finish,
          addr::SC4Effect_StepFlag800000}) {
        if (target == address)
            return true;
    }
    return g_poolHooked &&
           (target == addr::SC4EffectBase_TakeParticle || target == addr::SC4EffectBaseB_TakeParticle ||
            target == addr::SC4EffectBase_AddParticle || target == addr::SC4EffectBase_GiveBackParticle ||
            target == addr::SC4EffectBaseB_GiveBackParticle || target == addr::SC4Effect_SpawnFromParticle ||
            target == addr::SC4Effect_SetStopped || target == addr::SC4EffectB_SetStopped);
}

template <typename... Args>
uint32_t CallThisTraced(uint32_t target, void* self, Args... args)
{
    if (!g_effectReplay)
        return reinterpret_cast<uint32_t(__thiscall*)(void*, Args...)>(target)(self, args...);
    if (Recorded(target)) {
        EffectCall call{target, reinterpret_cast<uint32_t>(self), {0, 0}};
        std::size_t n = 0;
        ((call.args[n++] = args), ...);
        if (g_effectOurCalls.size() < g_effectChanges.size()) {
            for (const auto& [at, value] : g_effectChanges[g_effectOurCalls.size()])
                *EffectByte(at) = value;
        }
        g_effectOurCalls.push_back(call);
    }
    if (!Steers(target))
        return 0;
    const uint32_t answer = g_effectAnswer < g_effectAnswers.size() ? g_effectAnswers[g_effectAnswer] : 0u;
    // A particle just taken is read by its first byte, which is no longer
    // what the game's run saw (given back, the pool keeps a link there): the
    // byte of the record is put there until the comparison ends.
    if (answer && IsTake(target) && g_effectAnswer < g_effectTaken.size()) {
        char* const first = reinterpret_cast<char*>(answer);
        g_effectPoked.push_back({first, *first});
        *first = g_effectTaken[g_effectAnswer];
    }
    ++g_effectAnswer;
    return answer;
}

// A random number (SC4Random::Uniform, 0x00913761: the generator, two
// doubles as four words): in the replay the one the game's run drew.
std::vector<double> g_effectRandoms;
std::size_t g_effectRandom = 0;

double CallRandomTraced(uint32_t target, void* self, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    if (!g_effectReplay)
        return reinterpret_cast<double(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t)>(target)(self, a, b,
                                                                                                           c, d);
    return g_effectRandom < g_effectRandoms.size() ? g_effectRandoms[g_effectRandom++] : 0.0;
}

double CallFloatTraced(uint32_t target, void* self)
{
    if (!g_effectReplay)
        return reinterpret_cast<double(__thiscall*)(void*)>(target)(self);
    return g_effectClock;
}

uint32_t CallPlainTraced(uint32_t target, uint32_t argument)
{
    if (!g_effectReplay)
        return reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(target)(argument);
    return 0;
}

// OPENSC4_EFFECT_CENSUS=1: counts the calls of every hooked effect function
// and logs the totals now and then, to see which of them a city reaches.
void CountEffectCall(uint32_t target)
{
    static const bool on = [] {
        const char* v = std::getenv("OPENSC4_EFFECT_CENSUS");
        return v && *v == '1';
    }();
    if (!on)
        return;
    static std::vector<std::pair<uint32_t, uint64_t>> counts;
    static uint64_t total = 0;
    auto it = counts.begin();
    while (it != counts.end() && it->first != target)
        ++it;
    if (it == counts.end())
        it = counts.insert(counts.end(), {target, 0});
    ++it->second;
    ++total;
    if ((total & (total - 1)) != 0 || total < 0x10000)
        return;
    std::string text;
    char entry[40];
    for (const auto& [address, count] : counts) {
        std::snprintf(entry, sizeof entry, " %08X:%llu", address, static_cast<unsigned long long>(count));
        text += entry;
    }
    log::Write("water effects: census after %llu calls:%s", static_cast<unsigned long long>(total), text.c_str());
}

template <typename Fn, typename... Args>
uint32_t EffectRecorded(uint32_t target, Fn original, char* self, Args... args)
{
    CountEffectCall(target);
    const bool record = g_effectRecord && g_effectNested == 0;
    if (record) {
        EffectCall call{target, reinterpret_cast<uint32_t>(self), {0, 0}};
        std::size_t n = 0;
        ((call.args[n++] = args), ...);
        g_effectGameCalls.push_back(call);
    }
    const std::vector<char> before = record ? SaveEffect() : std::vector<char>();
    ++g_effectNested;
    const uint32_t result = original(self, nullptr, args...);
    --g_effectNested;
    if (record) {
        // Nodes the call added to the list join the saved copy, whole: the
        // game's run may free them again before it ends.
        char* const head = g_effectNodes[0];
        for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
            if (g_effectNodeSet.insert(node).second) {
                g_effectNodes.push_back(node);
                g_effectParticles.push_back(At<char*>(node, 8));
            }
        }
        const std::vector<char> after = SaveEffect();
        std::vector<std::pair<uint32_t, char>> changes;
        for (std::size_t at = 0; at < after.size(); ++at) {
            if (at >= before.size() || after[at] != before[at])
                changes.push_back({static_cast<uint32_t>(at), after[at]});
        }
        g_effectChanges.push_back(std::move(changes));
        if (Steers(target)) {
            g_effectAnswers.push_back(result);
            g_effectTaken.push_back(IsTake(target) && result ? *reinterpret_cast<char*>(result) : 0);
        }
    }
    return result;
}

#define OPENSC4_EFFECT_HOOK0(Name)                                                                                     \
    Call0Fn g_##Name = nullptr;                                                                                        \
    uint32_t __fastcall D##Name(char* self, void*) { return EffectRecorded(addr::Name, g_##Name, self); }              \
    OPENSC4_THUNK(T##Name, D##Name, 0)
#define OPENSC4_EFFECT_HOOK1(Name)                                                                                     \
    Call1Fn g_##Name = nullptr;                                                                                        \
    uint32_t __fastcall D##Name(char* self, void*, uint32_t a) { return EffectRecorded(addr::Name, g_##Name, self, a); } \
    OPENSC4_THUNK(T##Name, D##Name, 4)
#define OPENSC4_EFFECT_HOOK2(Name)                                                                                     \
    Call2Fn g_##Name = nullptr;                                                                                        \
    uint32_t __fastcall D##Name(char* self, void*, uint32_t a, uint32_t b)                                             \
    {                                                                                                                  \
        return EffectRecorded(addr::Name, g_##Name, self, a, b);                                                       \
    }                                                                                                                  \
    OPENSC4_THUNK(T##Name, D##Name, 8)

// A particle's step (0x005B9BA0, slot 0x50 of both effect classes),
// translated instruction by instruction: the particle's age (+4) grows by
// the step, and past its life (+8) the particle ends (its first byte
// cleared, the answer false); else its velocity (+0x1C) takes the effect's
// (+0x13C) times the step and is scaled down by the description's +0x100,
// and its position (+0xC) moves by the old and the new velocity.
// Descriptions with a flag of the mask at 0x00B22E14 go to the game's
// 0x005B9790 instead.
uint32_t StepParticle(char* self, uint32_t particle, uint32_t step)
{
    uint32_t stackmem[160] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = particle;
    M<uint32_t>(esp + 8) = step;
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
#include "core/effect_step.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

constexpr std::size_t kParticleSize = 0x28;
constexpr std::uintptr_t kStepOtherMask = 0x00B22E14;
enum class StepMode { Original, Ours, Compare };
StepMode g_stepMode = StepMode::Original;
Call2Fn g_SC4Effect_StepParticle = nullptr;
uint64_t g_stepsCompared = 0;

// The step by its mode. It only reads the effect and writes the particle,
// so the comparison runs ours on the particle, keeps what it left, puts the
// particle back and runs the game's. The other step (0x005B9790) is not
// compared: it is still the game's on both sides.
uint32_t __fastcall StepByMode(char* self, void*, uint32_t particle, uint32_t step)
{
    if (g_stepMode == StepMode::Original)
        return g_SC4Effect_StepParticle(self, nullptr, particle, step);
    if (g_stepMode == StepMode::Ours)
        return StepParticle(self, particle, step);
    if (At<uint32_t>(At<char*>(self, 0x24), 0) & Global<uint32_t>(kStepOtherMask))
        return g_SC4Effect_StepParticle(self, nullptr, particle, step);
    char* const bytes = reinterpret_cast<char*>(particle);
    char before[kParticleSize], mine[kParticleSize];
    std::memcpy(before, bytes, kParticleSize);
    const uint32_t ourResult = StepParticle(self, particle, step);
    std::memcpy(mine, bytes, kParticleSize);
    std::memcpy(bytes, before, kParticleSize);
    const uint32_t result = g_SC4Effect_StepParticle(self, nullptr, particle, step);
    ++g_stepsCompared;
    if ((g_stepsCompared & (g_stepsCompared - 1)) == 0)
        log::Write("water effects: effects.step: %llu steps compared so far",
                   static_cast<unsigned long long>(g_stepsCompared));
    const bool same = (result & 0xFF) == (ourResult & 0xFF) && std::memcmp(bytes, mine, kParticleSize) == 0;
    if (same)
        compare::Record("effects.step", true, std::string(), std::string());
    else
        compare::Record("effects.step", false, std::to_string(result & 0xFF) + " " + Hex(bytes, kParticleSize),
                        std::to_string(ourResult & 0xFF) + " " + Hex(mine, kParticleSize));
    return result;
}

uint32_t __fastcall DSC4Effect_StepParticle(char* self, void*, uint32_t particle, uint32_t step)
{
    return EffectRecorded(addr::SC4Effect_StepParticle, &StepByMode, self, particle, step);
}
OPENSC4_THUNK(TSC4Effect_StepParticle, DSC4Effect_StepParticle, 8)

// An effect's emit (0x005B96E0, slot 0x3C of both effect classes),
// translated instruction by instruction: nothing when +0x10 is set; a
// countdown at +0x190 is lowered by the step (+0x108), and while it is above
// zero nothing is emitted; then the step (what the countdown went below zero
// by, at most a value from the description's +0x1C) goes to one of three
// emitters by the description's flags: 4, 2, or neither.
bool g_emitReplay = false;
uint32_t g_emitTarget = 0, g_emitStep = 0;

uint32_t CallEmitter(uint32_t target, void* self, uint32_t step)
{
    if (!g_emitReplay)
        return reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t)>(target)(self, step);
    g_emitTarget = target;
    g_emitStep = step;
    return 0;
}

uint32_t EmitParticles(char* self)
{
    uint32_t stackmem[160] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
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
#include "core/effect_emit.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

enum class EmitMode { Original, Ours, Compare };
EmitMode g_emitMode = EmitMode::Original;
Call0Fn g_SC4Effect_Emit = nullptr;
Call1Fn g_emitToCount = nullptr, g_emitBurst = nullptr, g_emitStream = nullptr;
uint64_t g_emitsCompared = 0;

// The comparison: everything the emit does itself comes before its one call
// to an emitter. Ours runs with that call only noted; the effect it left is
// kept and the effect put back; then the game's runs for real, and a detour
// on each emitter notes which was called, with what, and the effect as it
// was on entry (or, when none was called, as the emit left it).
bool g_emitWatch = false;
uint32_t g_emitGameTarget = 0, g_emitGameStep = 0;
char g_emitSeen[kEffectSize];
char* g_emitSelf = nullptr;

template <typename Fn>
uint32_t EmitterWatched(uint32_t target, Fn original, char* self, uint32_t step)
{
    if (g_emitWatch && self == g_emitSelf && !g_emitGameTarget) {
        g_emitGameTarget = target;
        g_emitGameStep = step;
        std::memcpy(g_emitSeen, self, kEffectSize);
    }
    return original(self, nullptr, step);
}

void TEmitToCount();
void TEmitBurst();
void TEmitStream();

uint32_t __fastcall EmitByMode(char* self, void*)
{
    if (g_emitMode == EmitMode::Original || g_emitWatch)
        return g_SC4Effect_Emit(self, nullptr);
    if (g_emitMode == EmitMode::Ours)
        return EmitParticles(self);
    char before[kEffectSize], mine[kEffectSize];
    std::memcpy(before, self, kEffectSize);
    g_emitTarget = g_emitStep = 0;
    g_emitReplay = true;
    EmitParticles(self);
    g_emitReplay = false;
    std::memcpy(mine, self, kEffectSize);
    std::memcpy(self, before, kEffectSize);
    g_emitGameTarget = g_emitGameStep = 0;
    g_emitSelf = self;
    g_emitWatch = true;
    const uint32_t result = g_SC4Effect_Emit(self, nullptr);
    g_emitWatch = false;
    if (!g_emitGameTarget)
        std::memcpy(g_emitSeen, self, kEffectSize);
    ++g_emitsCompared;
    if ((g_emitsCompared & (g_emitsCompared - 1)) == 0)
        log::Write("water effects: effects.emit: %llu emits compared so far",
                   static_cast<unsigned long long>(g_emitsCompared));
    char text[96];
    if (g_emitTarget != g_emitGameTarget || g_emitStep != g_emitGameStep) {
        std::snprintf(text, sizeof text, "emitter %08X, step %08X", g_emitGameTarget, g_emitGameStep);
        const std::string game = text;
        std::snprintf(text, sizeof text, "emitter %08X, step %08X", g_emitTarget, g_emitStep);
        compare::Record("effects.emit", false, game, text);
    } else if (std::memcmp(g_emitSeen, mine, kEffectSize) != 0) {
        std::size_t at = 0;
        while (g_emitSeen[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "effect+%zX ", at);
        compare::Record("effects.emit", false, text + Hex(g_emitSeen + at, 8), text + Hex(mine + at, 8));
    } else {
        compare::Record("effects.emit", true, std::string(), std::string());
    }
    return result;
}

uint32_t __fastcall DSC4Effect_Emit(char* self, void*)
{
    return EffectRecorded(addr::SC4Effect_Emit, &EmitByMode, self);
}
OPENSC4_THUNK(TSC4Effect_Emit, DSC4Effect_Emit, 0)

// What an effect does before it draws its particles (0x005B3B90, slot 0x38
// of both effect classes), translated instruction by instruction: when the
// rotation of the view (the context's +0x18) is not the one kept at +0x88,
// the two axes the particles are drawn along (+0x70 and +0x7C) are taken
// again: the camera view's two (its +0x60 and +0x6C), or for descriptions
// whose byte at +0xD5 is 1 two worked out from the description's own
// direction (+0x30).
uint32_t SetEffectAxes(char* self, uint32_t context)
{
    uint32_t stackmem[160] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = context;
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
#include "core/effect_axes.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

StepMode g_axesMode = StepMode::Original;
Call1Fn g_SC4Effect_BeforeParticles = nullptr;
uint64_t g_axesCompared = 0;

// It reads the context and the description and writes only the effect, so
// the comparison runs ours and the game's on the same effect.
uint32_t __fastcall AxesByMode(char* self, void*, uint32_t context)
{
    if (g_axesMode == StepMode::Original || g_effectReplay)
        return g_SC4Effect_BeforeParticles(self, nullptr, context);
    if (g_axesMode == StepMode::Ours)
        return SetEffectAxes(self, context);
    char before[kEffectSize], mine[kEffectSize];
    std::memcpy(before, self, kEffectSize);
    SetEffectAxes(self, context);
    std::memcpy(mine, self, kEffectSize);
    std::memcpy(self, before, kEffectSize);
    const uint32_t result = g_SC4Effect_BeforeParticles(self, nullptr, context);
    ++g_axesCompared;
    if ((g_axesCompared & (g_axesCompared - 1)) == 0)
        log::Write("water effects: effects.axes: %llu calls compared so far",
                   static_cast<unsigned long long>(g_axesCompared));
    if (std::memcmp(self, mine, kEffectSize) == 0) {
        compare::Record("effects.axes", true, std::string(), std::string());
    } else {
        std::size_t at = 0;
        while (self[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        char text[32];
        std::snprintf(text, sizeof text, "effect+%zX ", at);
        compare::Record("effects.axes", false, text + Hex(self + at, 8), text + Hex(mine + at, 8));
    }
    return result;
}

uint32_t __fastcall DSC4Effect_BeforeParticles(char* self, void*, uint32_t context)
{
    return EffectRecorded(addr::SC4Effect_BeforeParticles, &AxesByMode, self, context);
}
OPENSC4_THUNK(TSC4Effect_BeforeParticles, DSC4Effect_BeforeParticles, 4)
// The x87 sine and cosine, as the game executes them.
double X87Sin(double x)
{
    double result;
    __asm {
        fld x
        fsin
        fstp result
    }
    return result;
}

double X87Cos(double x)
{
    double result;
    __asm {
        fld x
        fcos
        fstp result
    }
    return result;
}

// The base effect class's particle draw (0x005B3DA0, slot 0x44; used when
// +0x1EC is clear), translated instruction by instruction: nothing without a
// texture (+0x68); else the effect's batch (+0x28: vertices and indices) is
// emptied and given room for the particles (+0x18), each particle of the
// list becomes a quad in it (its size, colour, turn and place worked out
// from the description's curves and the two axes at +0x70 and +0x7C), a box
// around them is kept at +0x1A0 to +0x1B4, and the batch is drawn with the
// texture.
uint32_t DrawParticles(char* self, uint32_t context)
{
    uint32_t stackmem[320] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[256]);
    M<uint32_t>(esp + 4) = context;
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
#include "core/effect_particles.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// What a particle draw may change, for a comparison that runs both: the
// effect but for its batch (+0x28 to +0x68: the batch is emptied at the
// start of every draw, and its storage may be moved by the first run), the
// render context with its texture stage cache, and the S3D camera.
constexpr std::size_t kBatchBegin = 0x28, kBatchEnd = 0x68;

std::vector<Region> ParticleRegions(char* self, char* context)
{
    std::vector<Region> regions{{"effect", self, kBatchBegin},
                                {"effect+68", self + kBatchEnd, kEffectSize - kBatchEnd},
                                {"context", context, kContextSize}};
    char* const render = At<char*>(context, 0x34);
    if (char* const camera = render ? At<char*>(render, 0xD8) : nullptr)
        regions.push_back({"camera", camera, kCameraSize});
    if (char* const stages = At<char*>(context, 0xE0))
        regions.push_back({"stages", stages, static_cast<std::size_t>(At<char*>(context, 0xE4) - stages)});
    return regions;
}

// An effect's particle draw into the driver's dynamic vertex buffer
// (0x005B4B90, slot 0x48 of both effect classes; used when the effect has
// one, +0x1EC), translated instruction by instruction: nothing without a
// texture (+0x68); else the texture is bound and the particles are drawn in
// runs of at most the buffer's capacity: the batch (+0x28) gets the run's
// indices, the buffer is locked for its vertices, each particle becomes a
// quad of 0x20-byte vertices worked out as in the other draw, the buffer is
// unlocked and drawn with the indices. The box around the particles is kept
// at +0x1A0 to +0x1B4.
uint32_t DrawParticlesBuffered(char* self, uint32_t context)
{
    static uint32_t stackmem[1024];
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[896]);
    M<uint32_t>(esp + 4) = context;
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
#include "core/effect_particles_buffered.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

StepMode g_bufferedMode = StepMode::Original;
Call1Fn g_SC4Effect_DrawParticlesTextured = nullptr;

uint32_t __fastcall BufferedByMode(char* self, void*, uint32_t context)
{
    if (g_bufferedMode == StepMode::Original || g_depth > 0 || g_effectReplay)
        return g_SC4Effect_DrawParticlesTextured(self, nullptr, context);
    if (g_bufferedMode == StepMode::Ours)
        return DrawParticlesBuffered(self, context);
    uint32_t result = 0;
    DrawCompared(g_selfCheck ? "effects.particles_buffered.selfcheck" : "effects.particles_buffered",
                 ParticleRegions(self, reinterpret_cast<char*>(context)),
                 [&] {
                     if (g_selfCheck)
                         g_SC4Effect_DrawParticlesTextured(self, nullptr, context);
                     else
                         DrawParticlesBuffered(self, context);
                 },
                 [&] { result = g_SC4Effect_DrawParticlesTextured(self, nullptr, context); });
    return result;
}

uint32_t __fastcall DSC4Effect_DrawParticlesTextured(char* self, void*, uint32_t context)
{
    return EffectRecorded(addr::SC4Effect_DrawParticlesTextured, &BufferedByMode, self, context);
}
OPENSC4_THUNK(TSC4Effect_DrawParticlesTextured, DSC4Effect_DrawParticlesTextured, 4)

StepMode g_particlesMode = StepMode::Original;
Call1Fn g_SC4Effect_DrawParticles = nullptr;

uint32_t __fastcall ParticlesByMode(char* self, void*, uint32_t context)
{
    if (g_particlesMode == StepMode::Original || g_depth > 0 || g_effectReplay)
        return g_SC4Effect_DrawParticles(self, nullptr, context);
    if (g_particlesMode == StepMode::Ours)
        return DrawParticles(self, context);
    uint32_t result = 0;
    DrawCompared(g_selfCheck ? "effects.particles.selfcheck" : "effects.particles",
                 ParticleRegions(self, reinterpret_cast<char*>(context)),
                 [&] {
                     if (g_selfCheck)
                         g_SC4Effect_DrawParticles(self, nullptr, context);
                     else
                         DrawParticles(self, context);
                 },
                 [&] { result = g_SC4Effect_DrawParticles(self, nullptr, context); });
    return result;
}

uint32_t __fastcall DSC4Effect_DrawParticles(char* self, void*, uint32_t context)
{
    return EffectRecorded(addr::SC4Effect_DrawParticles, &ParticlesByMode, self, context);
}
OPENSC4_THUNK(TSC4Effect_DrawParticles, DSC4Effect_DrawParticles, 4)
OPENSC4_EFFECT_HOOK1(SC4EffectB_DrawParticles)
// What the base effect class does instead of drawing, when it has no render
// context or no particle (0x005B6390, slot 0x4C), translated instruction by
// instruction: the box around its particles (+0x1A0 to +0x1B4) is worked
// out anew from each particle's position, taken through the effect's matrix
// (+0xC8), and widened as after a draw.
uint32_t UpdateEffectBox(char* self)
{
    uint32_t stackmem[160] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
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
#include "core/effect_box.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

StepMode g_boxMode = StepMode::Original;
Call0Fn g_SC4Effect_NotDrawn = nullptr;
uint64_t g_boxesCompared = 0;

// It reads the particles and writes only the effect's box, so the
// comparison runs ours and the game's on the same effect.
uint32_t __fastcall BoxByMode(char* self, void*)
{
    if (g_boxMode == StepMode::Original || g_effectReplay)
        return g_SC4Effect_NotDrawn(self, nullptr);
    if (g_boxMode == StepMode::Ours)
        return UpdateEffectBox(self);
    char before[kEffectSize], mine[kEffectSize];
    std::memcpy(before, self, kEffectSize);
    UpdateEffectBox(self);
    std::memcpy(mine, self, kEffectSize);
    std::memcpy(self, before, kEffectSize);
    const uint32_t result = g_SC4Effect_NotDrawn(self, nullptr);
    ++g_boxesCompared;
    if ((g_boxesCompared & (g_boxesCompared - 1)) == 0)
        log::Write("water effects: effects.box: %llu calls compared so far",
                   static_cast<unsigned long long>(g_boxesCompared));
    if (std::memcmp(self, mine, kEffectSize) == 0) {
        compare::Record("effects.box", true, std::string(), std::string());
    } else {
        std::size_t at = 0;
        while (self[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        char text[32];
        std::snprintf(text, sizeof text, "effect+%zX ", at);
        compare::Record("effects.box", false, text + Hex(self + at, 8), text + Hex(mine + at, 8));
    }
    return result;
}

uint32_t __fastcall DSC4Effect_NotDrawn(char* self, void*)
{
    return EffectRecorded(addr::SC4Effect_NotDrawn, &BoxByMode, self);
}
OPENSC4_THUNK(TSC4Effect_NotDrawn, DSC4Effect_NotDrawn, 0)
OPENSC4_EFFECT_HOOK0(SC4EffectB_NotDrawn)
OPENSC4_EFFECT_HOOK1(SC4Effect_StepFlag2000)
// The pull on an effect's particles from outside (0x005B6C40; called by the
// effect's draw when its description lists any, +0x1A4), translated
// instruction by instruction: for descriptions with flag 0x08000000 three
// floats of the effect (+0xB4) are copied to +0x178; else a velocity is
// worked out into +0x16C from the middle of
// the effect's box and what a service of the game (0x00B43CEC, its slots
// 0xBC and 0x58) answers for that place.
uint32_t UpdateEffectPull(char* self)
{
    uint32_t stackmem[192] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
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
#include "core/effect_pull.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

StepMode g_pullMode = StepMode::Original;
Call0Fn g_SC4Effect_PathVelocity = nullptr;
uint64_t g_pullsCompared = 0;

// It asks the game's service and writes only the effect, so the comparison
// runs ours and the game's on the same effect (the service is asked twice).
uint32_t __fastcall PullByMode(char* self, void*)
{
    if (g_pullMode == StepMode::Original || g_effectReplay)
        return g_SC4Effect_PathVelocity(self, nullptr);
    if (g_pullMode == StepMode::Ours)
        return UpdateEffectPull(self);
    char before[kEffectSize], mine[kEffectSize];
    std::memcpy(before, self, kEffectSize);
    if (g_selfCheck)
        g_SC4Effect_PathVelocity(self, nullptr);
    else
        UpdateEffectPull(self);
    std::memcpy(mine, self, kEffectSize);
    std::memcpy(self, before, kEffectSize);
    const uint32_t result = g_SC4Effect_PathVelocity(self, nullptr);
    const char* const id = g_selfCheck ? "effects.pull.selfcheck" : "effects.pull";
    ++g_pullsCompared;
    if ((g_pullsCompared & (g_pullsCompared - 1)) == 0)
        log::Write("water effects: %s: %llu calls compared so far", id,
                   static_cast<unsigned long long>(g_pullsCompared));
    if (std::memcmp(self, mine, kEffectSize) == 0) {
        compare::Record(id, true, std::string(), std::string());
    } else {
        std::size_t at = 0;
        while (self[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        char text[32];
        std::snprintf(text, sizeof text, "effect+%zX ", at);
        compare::Record(id, false, text + Hex(self + at, 8), text + Hex(mine + at, 8));
    }
    return result;
}

uint32_t __fastcall DSC4Effect_PathVelocity(char* self, void*)
{
    return EffectRecorded(addr::SC4Effect_PathVelocity, &PullByMode, self);
}
OPENSC4_THUNK(TSC4Effect_PathVelocity, DSC4Effect_PathVelocity, 0)
OPENSC4_EFFECT_HOOK0(SC4Effect_StepFlag80000000)
OPENSC4_EFFECT_HOOK0(SC4EffectBase_Finish)
OPENSC4_EFFECT_HOOK1(SC4Effect_StepFlag800000)
OPENSC4_EFFECT_HOOK0(SC4EffectBase_TakeParticle)
OPENSC4_EFFECT_HOOK0(SC4EffectBaseB_TakeParticle)
OPENSC4_EFFECT_HOOK1(SC4EffectBase_AddParticle)
OPENSC4_EFFECT_HOOK1(SC4EffectBase_GiveBackParticle)
OPENSC4_EFFECT_HOOK1(SC4EffectBaseB_GiveBackParticle)
OPENSC4_EFFECT_HOOK1(SC4Effect_SpawnFromParticle)
OPENSC4_EFFECT_HOOK1(SC4Effect_SetStopped)
OPENSC4_EFFECT_HOOK1(SC4EffectB_SetStopped)

// A particle effect's draw (0x005B8540, slot 0x24 of both effect classes),
// translated instruction by instruction: the step added to the effect's
// clock (+0x110), its velocity worked out (+0x13C), each particle of the
// list at +0x14 stepped (the class's slot 0x50; one that ends is unlinked
// and released), new ones emitted (slot 0x3C), then with a render context
// and particles the drawing (slot 0x38, the render state at +0x1C, slot 0x48
// or 0x44), else slot 0x4C. Everything it calls is still the game's.
uint32_t DrawEffect(char* self, uint32_t step, uint32_t context)
{
    uint32_t stackmem[160] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = step;
    M<uint32_t>(esp + 8) = context;
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
#include "core/effect_draw.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// While the game's run is recorded, what it frees is only noted and freed
// once the comparison is over: until then the replay reads and writes the
// nodes the game let go of, and nothing else may be given their memory.
using FreeFn = void(__cdecl*)(void*);
FreeFn g_free = nullptr, g_delete = nullptr;
std::vector<std::pair<FreeFn*, void*>> g_effectFreed;
DWORD g_effectThread = 0;

// Other threads free memory too while the draw runs; only the drawing
// thread's frees are held back.
void __cdecl DFree(void* p)
{
    if (g_effectRecord && GetCurrentThreadId() == g_effectThread)
        g_effectFreed.push_back({&g_free, p});
    else
        g_free(p);
}

void __cdecl DDelete(void* p)
{
    if (g_effectRecord && GetCurrentThreadId() == g_effectThread)
        g_effectFreed.push_back({&g_delete, p});
    else
        g_delete(p);
}

void FreeNoted()
{
    for (const auto& [release, p] : g_effectFreed)
        (*release)(p);
    g_effectFreed.clear();
}

// The replay behind a guard: a fault in it (ours going another way than the
// game's record) is reported as a divergence instead of ending the game.
__declspec(noinline) bool TryReplay(uint32_t (*run)(void*), void* argument, uint32_t* result)
{
    __try {
        *result = run(argument);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

enum class EffectMode { Original, Ours, Compare };
EffectMode g_effectMode = EffectMode::Original;

// The effect's particle list: the head node (+0x14) and each node (next,
// previous, the particle).
std::vector<char*> EffectNodes(char* self)
{
    char* const head = At<char*>(self, 0x14);
    std::vector<char*> nodes{head};
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        // A broken list (a null or endless chain) is given back empty.
        if (!node || nodes.size() > 100000)
            return {};
        nodes.push_back(node);
    }
    return nodes;
}

// The comparison by record and replay, for one function of an effect:
// `game` runs the game's for real, `replay` ours with every call answered
// from the record. `checkResult` when the function's answer counts.
template <typename Game>
uint32_t RecordAndReplay(const char* id, char* self, bool checkResult, Game&& game, uint32_t (*replay)(void*),
                         void* replayArgument)
{
    g_effectSelf = self;
    g_effectNodes = EffectNodes(self);
    if (g_effectNodes.empty())
        return game();
    g_effectParticles.assign(1, &g_noParticle);
    for (std::size_t i = 1; i < g_effectNodes.size(); ++i)
        g_effectParticles.push_back(At<char*>(g_effectNodes[i], 8));
    g_effectNodeSet.clear();
    g_effectNodeSet.insert(g_effectNodes.begin(), g_effectNodes.end());
    const std::vector<char> before = SaveEffect();
    g_effectGameCalls.clear();
    g_effectAnswers.clear();
    g_effectRandoms.clear();
    g_effectRandom = 0;
    g_effectTaken.clear();
    g_effectChanges.clear();
    g_effectThread = GetCurrentThreadId();
    g_effectRecord = true;
    const uint32_t result = game();
    g_effectRecord = false;
    const std::vector<char> after = SaveEffect();
    const std::vector<char*> gameNodes = EffectNodes(self);
    g_effectClock = At<float>(self, 0x1FC);
    RestoreEffect(before);
    g_effectOurCalls.clear();
    g_effectAnswer = 0;
    g_effectReplay = true;
    uint32_t ourResult = 0;
    const bool finished = TryReplay(replay, replayArgument, &ourResult);
    g_effectReplay = false;
    if (!finished) {
        UnpokeParticles();
        RestoreEffect(after);
        FreeNoted();
        char fault[160];
        const std::size_t at = g_effectOurCalls.size();
        const EffectCall last = at ? g_effectOurCalls[at - 1] : EffectCall{};
        const EffectCall same = at && at <= g_effectGameCalls.size() ? g_effectGameCalls[at - 1] : EffectCall{};
        std::snprintf(fault, sizeof fault, "fault after call %zu of %zu: %08X on %08X (%08X, %08X)", at,
                      g_effectGameCalls.size(), last.target, last.self, last.args[0], last.args[1]);
        const std::string ours = fault;
        std::snprintf(fault, sizeof fault, "call %zu: %08X on %08X (%08X, %08X)", at, same.target, same.self,
                      same.args[0], same.args[1]);
        compare::Record(id, false, fault, ours);
        return result;
    }
    const std::vector<char> mine(self, self + kEffectSize);
    const std::vector<char*> ourNodes = EffectNodes(self);
    UnpokeParticles();
    RestoreEffect(after);
    FreeNoted();
    ++g_passesCompared;
    g_drawsCompared += g_effectGameCalls.size();
    if ((g_passesCompared & (g_passesCompared - 1)) == 0)
        log::Write("water effects: %s: %llu draws, %llu calls compared so far", id,
                   static_cast<unsigned long long>(g_passesCompared), static_cast<unsigned long long>(g_drawsCompared));
    char text[96];
    if (g_effectOurCalls != g_effectGameCalls) {
        std::size_t at = 0;
        while (at < g_effectOurCalls.size() && at < g_effectGameCalls.size() &&
               g_effectOurCalls[at] == g_effectGameCalls[at])
            ++at;
        const auto describe = [&](const std::vector<EffectCall>& calls) {
            if (at < calls.size())
                std::snprintf(text, sizeof text, "call %zu of %zu: %08X on %08X (%08X, %08X)", at, calls.size(),
                              calls[at].target, calls[at].self, calls[at].args[0], calls[at].args[1]);
            else
                std::snprintf(text, sizeof text, "%zu calls", calls.size());
            return std::string(text);
        };
        const std::string game = describe(g_effectGameCalls);
        compare::Record(id, false, game, describe(g_effectOurCalls));
        return result;
    }
    if (gameNodes.empty() || ourNodes.empty()) {
        std::string calls;
        for (const EffectCall& call : g_effectGameCalls) {
            std::snprintf(text, sizeof text, " %X", call.target);
            if (calls.size() < 400)
                calls += text;
        }
        compare::Record(id, false, (gameNodes.empty() ? "list broken;" : "list whole;") + calls,
                        ourNodes.empty() ? "list broken" : "list whole");
        return result;
    }
    if ((checkResult && (result & 0xFF) != (ourResult & 0xFF)) || gameNodes != ourNodes) {
        std::snprintf(text, sizeof text, "result %u, %zu particles", result & 0xFF, gameNodes.size() - 1);
        const std::string game = text;
        std::snprintf(text, sizeof text, "result %u, %zu particles", ourResult & 0xFF, ourNodes.size() - 1);
        compare::Record(id, false, game, text);
        return result;
    }
    if (std::memcmp(after.data(), mine.data(), kEffectSize) != 0) {
        std::size_t at = 0;
        while (after[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "effect+%zX ", at);
        compare::Record(id, false, text + Hex(after.data() + at, 8), text + Hex(mine.data() + at, 8));
        return result;
    }
    compare::Record(id, true, std::string(), std::string());
    return result;
}

uint32_t EffectDraw(char* self, uint32_t step, uint32_t context)
{
    if (g_effectMode == EffectMode::Original || g_effectRecord || g_effectReplay)
        return g_effectDraw(self, nullptr, step, context);
    if (g_effectMode == EffectMode::Ours)
        return DrawEffect(self, step, context);
    struct Arguments {
        char* self;
        uint32_t step, context;
    } arguments{self, step, context};
    return RecordAndReplay(
        "effects.effect", self, true, [&] { return g_effectDraw(self, nullptr, step, context); },
        [](void* a) {
            const Arguments& args = *static_cast<Arguments*>(a);
            return DrawEffect(args.self, args.step, args.context);
        },
        &arguments);
}

// The emitter that keeps a count of particles (0x005B3CF0, for descriptions
// with flag 4), translated instruction by instruction: the step times a rate
// (+0x130) is added to a double at +0x120, and once that has passed +0x128
// the number of particles (+0x18) is brought up to a count worked out from
// the description's +0x88: each one taken from the sub-object (slot 0x3C)
// and added to the list when its first byte is set (slot 0x2C), else given
// back (slot 0x40).
uint32_t EmitToCount(char* self, uint32_t step)
{
    uint32_t stackmem[160] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = step;
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
#include "core/effect_emit_count.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// The random numbers the recorded function itself draws (not those its
// callees draw) are noted for the replay.
using UniformFn = double(__fastcall*)(void*, void*, double, double);
UniformFn g_uniform = nullptr;

double __fastcall DUniform(void* self, void*, double low, double high)
{
    const double value = g_uniform(self, nullptr, low, high);
    if (g_effectRecord && g_effectNested == 0 && GetCurrentThreadId() == g_effectThread)
        g_effectRandoms.push_back(value);
    return value;
}

// The emitter that emits a steady stream (0x005B87B0, for descriptions with
// neither flag 4 nor flag 2), translated instruction by instruction: for
// each particle due in the time it is given, one is taken from the
// sub-object; when its first byte is set it is stepped by the time since it
// was due (the class's slot 0x50) and, when it is still running, added to
// the list (after 0x005B84C0 for descriptions with flag 0x80000000), else it
// is given back. What is left of the time, less a random delay for some
// descriptions, becomes the countdown at +0x190.
uint32_t EmitStream(char* self, uint32_t step)
{
    uint32_t stackmem[192] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = step;
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
#include "core/effect_emit_stream.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// The emitter that emits in bursts (0x005B8B50, for descriptions with flag
// 2 and not 4), translated instruction by instruction. It calls what the
// stream emitter calls: a particle taken, stepped, added or given back, a
// random number drawn, the effect's slot 4.
uint32_t EmitBurst(char* self, uint32_t step)
{
    uint32_t stackmem[192] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = step;
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
#include "core/effect_emit_burst.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

EmitMode g_emitBurstMode = EmitMode::Original;

uint32_t __fastcall EmitBurstByMode(char* self, void*, uint32_t step)
{
    if (g_emitBurstMode == EmitMode::Original || g_effectRecord || g_effectReplay)
        return g_emitBurst(self, nullptr, step);
    if (g_emitBurstMode == EmitMode::Ours)
        return EmitBurst(self, step);
    struct Arguments {
        char* self;
        uint32_t step;
    } arguments{self, step};
    return RecordAndReplay(
        "effects.emit_burst", self, false, [&] { return g_emitBurst(self, nullptr, step); },
        [](void* a) {
            const Arguments& args = *static_cast<Arguments*>(a);
            return EmitBurst(args.self, args.step);
        },
        &arguments);
}

uint32_t __fastcall DEmitBurst(char* self, void*, uint32_t step)
{
    return EmitterWatched(addr::SC4Effect_EmitBurst, &EmitBurstByMode, self, step);
}
OPENSC4_THUNK(TEmitBurst, DEmitBurst, 4)

EmitMode g_emitStreamMode = EmitMode::Original;

uint32_t __fastcall EmitStreamByMode(char* self, void*, uint32_t step)
{
    if (g_emitStreamMode == EmitMode::Original || g_effectRecord || g_effectReplay)
        return g_emitStream(self, nullptr, step);
    if (g_emitStreamMode == EmitMode::Ours)
        return EmitStream(self, step);
    struct Arguments {
        char* self;
        uint32_t step;
    } arguments{self, step};
    return RecordAndReplay(
        "effects.emit_stream", self, false, [&] { return g_emitStream(self, nullptr, step); },
        [](void* a) {
            const Arguments& args = *static_cast<Arguments*>(a);
            return EmitStream(args.self, args.step);
        },
        &arguments);
}

uint32_t __fastcall DEmitStream(char* self, void*, uint32_t step)
{
    return EmitterWatched(addr::SC4Effect_EmitStream, &EmitStreamByMode, self, step);
}
OPENSC4_THUNK(TEmitStream, DEmitStream, 4)

EmitMode g_emitCountMode = EmitMode::Original;

uint32_t __fastcall EmitToCountByMode(char* self, void*, uint32_t step)
{
    if (g_emitCountMode == EmitMode::Original || g_effectRecord || g_effectReplay)
        return g_emitToCount(self, nullptr, step);
    if (g_emitCountMode == EmitMode::Ours)
        return EmitToCount(self, step);
    struct Arguments {
        char* self;
        uint32_t step;
    } arguments{self, step};
    return RecordAndReplay(
        "effects.emit_count", self, false, [&] { return g_emitToCount(self, nullptr, step); },
        [](void* a) {
            const Arguments& args = *static_cast<Arguments*>(a);
            return EmitToCount(args.self, args.step);
        },
        &arguments);
}

uint32_t __fastcall DEmitToCount(char* self, void*, uint32_t step)
{
    return EmitterWatched(addr::SC4Effect_EmitToCount, &EmitToCountByMode, self, step);
}
OPENSC4_THUNK(TEmitToCount, DEmitToCount, 4)

uint32_t __fastcall DEffectDraw(char* self, void*, uint32_t step, uint32_t context)
{
    if (g_trace != Trace::Off) {
        g_calls.insert(g_calls.end(), {5u, reinterpret_cast<uint32_t>(self), step, context});
        if (g_trace == Trace::Record)
            return 0;
    }
    return EffectDraw(self, step, context);
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

// The effect's draw has one detour, which the manager's comparison and the
// effect's replacement share.
bool HookEffectDraw()
{
    return g_effectDraw || Detour(addr::SC4Effect_Draw, &TEffectDraw, &g_effectDraw);
}

// The particle's step has one detour too, shared by the effect's comparison
// (which records it) and its own replacement.
bool HookStepParticle()
{
    return g_SC4Effect_StepParticle ||
           Detour(addr::SC4Effect_StepParticle, &TSC4Effect_StepParticle, &g_SC4Effect_StepParticle);
}

bool InstallStep(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookStepParticle())
        return false;
    g_stepMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: particle step %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredStep = registry::Register({
    "renderer.effects.step",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StepParticle,
    "A particle's step: its age, its end, its velocity and position (the other step, for flagged descriptions, "
    "still the game's); compare mode runs ours and the game's on the same particle (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallStep,
});

// What every record-and-replay comparison needs: the frees held back, and
// the particle pool's functions recorded.
bool HookFrees()
{
    if (g_free)
        return true;
    const addr::Entry* const release = addr_check::Find(addr::SC4_Free);
    const addr::Entry* const destroy = addr_check::Find(addr::SC4_operator_delete);
    return release && destroy && hook::Install(*release, &DFree, &g_free) &&
           hook::Install(*destroy, &DDelete, &g_delete);
}

bool HookPool()
{
    if (g_poolHooked)
        return true;
    g_poolHooked =
        Detour(addr::SC4EffectBase_TakeParticle, &TSC4EffectBase_TakeParticle, &g_SC4EffectBase_TakeParticle) &&
        Detour(addr::SC4EffectBaseB_TakeParticle, &TSC4EffectBaseB_TakeParticle, &g_SC4EffectBaseB_TakeParticle) &&
        Detour(addr::SC4EffectBase_AddParticle, &TSC4EffectBase_AddParticle, &g_SC4EffectBase_AddParticle) &&
        Detour(addr::SC4EffectBase_GiveBackParticle, &TSC4EffectBase_GiveBackParticle,
               &g_SC4EffectBase_GiveBackParticle) &&
        Detour(addr::SC4EffectBaseB_GiveBackParticle, &TSC4EffectBaseB_GiveBackParticle,
               &g_SC4EffectBaseB_GiveBackParticle) &&
        Detour(addr::SC4Effect_SpawnFromParticle, &TSC4Effect_SpawnFromParticle, &g_SC4Effect_SpawnFromParticle) &&
        Detour(addr::SC4Effect_SetStopped, &TSC4Effect_SetStopped, &g_SC4Effect_SetStopped) &&
        Detour(addr::SC4EffectB_SetStopped, &TSC4EffectB_SetStopped, &g_SC4EffectB_SetStopped);
    if (g_poolHooked) {
        const addr::Entry* const uniform = addr_check::Find(addr::SC4Random_Uniform);
        g_poolHooked = uniform && hook::Install(*uniform, &DUniform, &g_uniform);
    }
    return g_poolHooked;
}

bool HookPull()
{
    return g_SC4Effect_PathVelocity ||
           Detour(addr::SC4Effect_PathVelocity, &TSC4Effect_PathVelocity, &g_SC4Effect_PathVelocity);
}

bool InstallPull(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookPull())
        return false;
    g_pullMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effect pull %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredPull = registry::Register({
    "renderer.effects.pull",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_PathVelocity,
    "The pull on an effect's particles from outside, worked out each frame for descriptions that list any; "
    "compare mode runs ours and the game's on the same effect (docs/renderer.md, \"Water effects, sky and "
    "clouds\")",
    true,
    InstallPull,
});

bool HookBox()
{
    return g_SC4Effect_NotDrawn || Detour(addr::SC4Effect_NotDrawn, &TSC4Effect_NotDrawn, &g_SC4Effect_NotDrawn);
}

bool InstallBox(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookBox())
        return false;
    g_boxMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effect box %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredBox = registry::Register({
    "renderer.effects.box",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_NotDrawn,
    "What the base effect class does when it is not drawn: the box around its particles; compare mode runs ours "
    "and the game's on the same effect (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallBox,
});

bool HookBuffered()
{
    return g_SC4Effect_DrawParticlesTextured ||
           Detour(addr::SC4Effect_DrawParticlesTextured, &TSC4Effect_DrawParticlesTextured,
                  &g_SC4Effect_DrawParticlesTextured);
}

bool InstallBuffered(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookBuffered())
        return false;
    g_bufferedMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: buffered particle draw %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredBuffered = registry::Register({
    "renderer.effects.particles_buffered",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_DrawParticlesTextured,
    "An effect's particle draw into the driver's dynamic vertex buffer: each particle a quad, drawn in runs of the "
    "buffer's capacity; compare mode runs ours quietly, then the game's, and compares the draws (docs/renderer.md, "
    "\"Water effects, sky and clouds\")",
    true,
    InstallBuffered,
});

bool HookParticles()
{
    return g_SC4Effect_DrawParticles ||
           Detour(addr::SC4Effect_DrawParticles, &TSC4Effect_DrawParticles, &g_SC4Effect_DrawParticles);
}

bool InstallParticles(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookParticles())
        return false;
    g_particlesMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: particle draw %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredParticles = registry::Register({
    "renderer.effects.particles",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_DrawParticles,
    "The base effect class's particle draw: each particle a quad in the effect's batch, the batch drawn with the "
    "texture; compare mode runs ours quietly, then the game's, and compares the draws (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallParticles,
});

bool HookAxes()
{
    return g_SC4Effect_BeforeParticles ||
           Detour(addr::SC4Effect_BeforeParticles, &TSC4Effect_BeforeParticles, &g_SC4Effect_BeforeParticles);
}

bool InstallAxes(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookAxes())
        return false;
    g_axesMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effect axes %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredAxes = registry::Register({
    "renderer.effects.axes",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_BeforeParticles,
    "What an effect does before it draws: the two axes its particles are drawn along, taken again when the view's "
    "rotation changed; compare mode runs ours and the game's on the same effect (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallAxes,
});

bool HookEmitBurst()
{
    return g_emitBurst || Detour(addr::SC4Effect_EmitBurst, &TEmitBurst, &g_emitBurst);
}

bool InstallEmitBurst(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookEmitBurst())
        return false;
    if (mode == registry::Mode::Compare && !(HookFrees() && HookPool() && HookStepParticle()))
        return false;
    g_emitBurstMode = mode == registry::Mode::Compare ? EmitMode::Compare : EmitMode::Ours;
    log::Write("water effects: burst emitter %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredEmitBurst = registry::Register({
    "renderer.effects.emit_burst",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_EmitBurst,
    "The emitter that emits particles in bursts (taking, stepping, adding and giving back particles still the "
    "game's); compare mode records the game's run and replays ours, as renderer.effects.effect "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallEmitBurst,
});

bool HookEmitStream()
{
    return g_emitStream || Detour(addr::SC4Effect_EmitStream, &TEmitStream, &g_emitStream);
}

bool InstallEmitStream(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookEmitStream())
        return false;
    // The stream emitter steps each new particle: that call is recorded too.
    if (mode == registry::Mode::Compare && !(HookFrees() && HookPool() && HookStepParticle()))
        return false;
    g_emitStreamMode = mode == registry::Mode::Compare ? EmitMode::Compare : EmitMode::Ours;
    log::Write("water effects: stream emitter %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredEmitStream = registry::Register({
    "renderer.effects.emit_stream",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_EmitStream,
    "The emitter that emits a steady stream of particles (taking, stepping, adding and giving back particles "
    "still the game's); compare mode records the game's run and replays ours, as renderer.effects.effect "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallEmitStream,
});

bool HookEmitToCount()
{
    return g_emitToCount || Detour(addr::SC4Effect_EmitToCount, &TEmitToCount, &g_emitToCount);
}

bool InstallEmitCount(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookEmitToCount())
        return false;
    if (mode == registry::Mode::Compare && !(HookFrees() && HookPool()))
        return false;
    g_emitCountMode = mode == registry::Mode::Compare ? EmitMode::Compare : EmitMode::Ours;
    log::Write("water effects: count emitter %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredEmitCount = registry::Register({
    "renderer.effects.emit_count",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_EmitToCount,
    "The emitter that keeps a count of particles (taking, adding and giving back particles still the game's); "
    "compare mode records the game's run and replays ours, as renderer.effects.effect (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallEmitCount,
});

bool HookEmit()
{
    return g_SC4Effect_Emit || Detour(addr::SC4Effect_Emit, &TSC4Effect_Emit, &g_SC4Effect_Emit);
}

bool InstallEmit(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookEmit())
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookEmitToCount() &&
          HookEmitBurst() &&
          HookEmitStream()))
        return false;
    g_emitMode = mode == registry::Mode::Compare ? EmitMode::Compare : EmitMode::Ours;
    log::Write("water effects: emit %s", mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredEmit = registry::Register({
    "renderer.effects.emit",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_Emit,
    "An effect's emit: the countdown to the next emission and the choice of emitter (the three emitters still the "
    "game's); compare mode runs ours with the emitter only noted, then the game's (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallEmit,
});

bool InstallEffect(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    if (mode == registry::Mode::Compare) {
        // OPENSC4_EFFECT_HOOKS=<bits, hex> leaves recording hooks out (to
        // find one that disturbs the game); OPENSC4_EFFECT_NOCOMPARE=1
        // installs them without comparing.
        const char* const pick = std::getenv("OPENSC4_EFFECT_HOOKS");
        const uint32_t bits = pick ? std::strtoul(pick, nullptr, 16) : 0xFFFFFFFFu;
        uint32_t bit = 1;
        bool ok = true;
        const auto hook = [&](std::uintptr_t address, void (*thunk)(), auto* original) {
            if (bits & bit)
                ok = Detour(address, thunk, original) && ok;
            bit <<= 1;
        };
        if (bits & bit)
            ok = HookStepParticle() && ok;
        bit <<= 1;
        if (bits & bit)
            ok = HookEmit() && ok;
        bit <<= 1;
        if (bits & bit)
            ok = HookAxes() && ok;
        bit <<= 1;
        if (bits & bit)
            ok = HookBuffered() && ok;
        bit <<= 1;
        if (bits & bit)
            ok = HookParticles() && ok;
        bit <<= 1;
        hook(addr::SC4EffectB_DrawParticles, &TSC4EffectB_DrawParticles, &g_SC4EffectB_DrawParticles);
        if (bits & bit)
            ok = HookBox() && ok;
        bit <<= 1;
        hook(addr::SC4EffectB_NotDrawn, &TSC4EffectB_NotDrawn, &g_SC4EffectB_NotDrawn);
        hook(addr::SC4Effect_StepFlag2000, &TSC4Effect_StepFlag2000, &g_SC4Effect_StepFlag2000);
        if (bits & bit)
            ok = HookPull() && ok;
        bit <<= 1;
        hook(addr::SC4Effect_StepFlag80000000, &TSC4Effect_StepFlag80000000, &g_SC4Effect_StepFlag80000000);
        hook(addr::SC4EffectBase_Finish, &TSC4EffectBase_Finish, &g_SC4EffectBase_Finish);
        hook(addr::SC4Effect_StepFlag800000, &TSC4Effect_StepFlag800000, &g_SC4Effect_StepFlag800000);
        if (!ok || !HookFrees() || !HookPool())
            return false;
        const char* const off = std::getenv("OPENSC4_EFFECT_NOCOMPARE");
        if (off && *off == '1') {
            log::Write("water effects: effect hooks %08X installed, no comparison", bits);
            return true;
        }
    }
    if (!HookEffectDraw())
        return false;
    g_effectMode = mode == registry::Mode::Compare ? EffectMode::Compare : EffectMode::Ours;
    log::Write("water effects: effect draw %s", mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredEffect = registry::Register({
    "renderer.effects.effect",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_Draw,
    "A particle effect's draw: its clock, its particles stepped and ended, new ones emitted, then the drawing (the "
    "steps themselves still the game's); compare mode runs the game's with its calls recorded, puts the effect "
    "back, then ours answered from the record (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallEffect,
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
          HookEffectDraw() &&
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
