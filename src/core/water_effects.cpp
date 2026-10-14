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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <functional>
#include <initializer_list>
#include <map>
#include <unordered_map>
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

// The comparison of an effect taking a description notes some calls of
// functions that have detours of their own further up; defined with it.
bool DescNoting();
uint32_t DescCallee(uint32_t target, uint32_t a, const std::function<uint32_t()>& real);
// The same for the second class's version, which notes the base class's as one call.
bool BNoting();
bool BBusy();
uint32_t PartDescriptionByMode(char* part, uint32_t description);
uint32_t BCallee(uint32_t target, uint32_t a, uint32_t data, std::size_t size, const std::function<uint32_t()>& real,
                 uint32_t out = 0);
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

// The clean-up by the mode of its own replacement, defined with it.
uint32_t __fastcall AfterDrawByMode(char* self, void*);

uint32_t __fastcall DAfterDraw(char* self, void*)
{
    return Traced(4, &AfterDrawByMode, self);
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
// Letting go of what an effect holds records two more, once their detours are in.
bool g_letGoHooked = false;

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
            target == addr::SC4Effect_SetStopped || target == addr::SC4EffectB_SetStopped ||
            target == addr::SC4EffectBase_SetEnding || target == addr::SC4EffectBase_LetGo ||
            target == addr::SC4EffectBaseB_LetGo ||
            (g_letGoHooked &&
             (target == addr::SC4EffectDescription_Release || target == addr::SC4EffectBase_Stop)));
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

double X87Atan2(double y, double x)
{
    double result;
    __asm {
        fld y
        fld x
        fpatan
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

// The block of a particle of the second effect class (0x005ACA30; the
// class's draw and its step out of view call it with the particle, a
// word, a scale, a pointer to three floats and the block to fill),
// translated instruction by instruction. The block is two bytes and
// thirteen floats: a matrix of the particle's turning and size and its
// place. How the particle is turned goes by the description's kind
// (+0xD5): kinds 2 to 4 face the camera in one of three ways (the game's
// 0x0080B910, 0x0080B530, 0x0080B830), the others turn about an angle
// kept in the particle (+0x50), which follows the particle's velocity for
// some descriptions. The effect's box (+0x1A0) is widened to hold the
// particle.
uint32_t ParticleBlockB(char* self, uint32_t particle, uint32_t a, uint32_t scale, uint32_t vector, uint32_t block)
{
    uint32_t stackmem[320] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[224]);
    M<uint32_t>(esp + 4) = particle;
    M<uint32_t>(esp + 8) = a;
    M<uint32_t>(esp + 12) = scale;
    M<uint32_t>(esp + 16) = vector;
    M<uint32_t>(esp + 20) = block;
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
#include "core/effect_block_b.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// It writes the particle, the effect's box, the block and perhaps the
// three floats, and only does arithmetic, so the comparison runs ours,
// keeps what it left, puts all four back and runs the game's.
using Call5Fn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
StepMode g_blockModeB = StepMode::Original;
Call5Fn g_SC4EffectB_ParticleBlock = nullptr;
uint64_t g_blocksBCompared = 0;

uint32_t __fastcall BlockBByMode(char* self, void*, uint32_t particle, uint32_t a, uint32_t scale, uint32_t vector,
                                 uint32_t block)
{
    if (g_blockModeB == StepMode::Original)
        return g_SC4EffectB_ParticleBlock(self, nullptr, particle, a, scale, vector, block);
    if (g_blockModeB == StepMode::Ours)
        return ParticleBlockB(self, particle, a, scale, vector, block);
    constexpr std::size_t kParticleSize = 0x54, kEffectSize2 = 0x27C, kBlockSize = 0x38, kVectorSize = 12;
    constexpr std::size_t kEffectAt = kParticleSize, kBlockAt = kEffectAt + kEffectSize2,
                          kVectorAt = kBlockAt + kBlockSize, kTotal = kVectorAt + kVectorSize;
    char before[kTotal], mine[kTotal], theirs[kTotal];
    const auto save = [&](char* to) {
        std::memcpy(to, reinterpret_cast<const void*>(particle), kParticleSize);
        std::memcpy(to + kEffectAt, self, kEffectSize2);
        std::memcpy(to + kBlockAt, reinterpret_cast<const void*>(block), kBlockSize);
        std::memcpy(to + kVectorAt, reinterpret_cast<const void*>(vector), kVectorSize);
    };
    save(before);
    ParticleBlockB(self, particle, a, scale, vector, block);
    save(mine);
    std::memcpy(reinterpret_cast<void*>(particle), before, kParticleSize);
    std::memcpy(self, before + kEffectAt, kEffectSize2);
    std::memcpy(reinterpret_cast<void*>(block), before + kBlockAt, kBlockSize);
    std::memcpy(reinterpret_cast<void*>(vector), before + kVectorAt, kVectorSize);
    const uint32_t result = g_SC4EffectB_ParticleBlock(self, nullptr, particle, a, scale, vector, block);
    save(theirs);
    ++g_blocksBCompared;
    if ((g_blocksBCompared & (g_blocksBCompared - 1)) == 0)
        log::Write("water effects: effects.block_b: %llu blocks compared so far",
                   static_cast<unsigned long long>(g_blocksBCompared));
    if (std::memcmp(theirs, mine, kTotal) == 0) {
        compare::Record("effects.block_b", true, std::string(), std::string());
    } else {
        std::size_t at = 0;
        while (theirs[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        const char* const part =
            at < kEffectAt ? "particle" : at < kBlockAt ? "effect" : at < kVectorAt ? "block" : "vector";
        const std::size_t offset =
            at < kEffectAt ? at : at < kBlockAt ? at - kEffectAt : at < kVectorAt ? at - kBlockAt : at - kVectorAt;
        char text[40];
        std::snprintf(text, sizeof text, "%s+%zX ", part, offset);
        compare::Record("effects.block_b", false, text + Hex(theirs + at, 8), text + Hex(mine + at, 8));
    }
    return result;
}
#define OPENSC4_REPUSH_20 OPENSC4_PUSH(20) OPENSC4_PUSH(20) OPENSC4_PUSH(20) OPENSC4_PUSH(20) OPENSC4_PUSH(20)
OPENSC4_THUNK(TSC4EffectB_ParticleBlock, BlockBByMode, 20)

// The second effect class's step when it is not drawn (0x005AE6E0, slot
// 0x4C), translated instruction by instruction. The effect's box (+0x1A0)
// is started again, and every particle of the list takes part in it. A
// particle whose own effect (+0x44) is there and says so (its slot 0x14)
// gives that effect where it is: a block of two bytes and thirteen floats
// made from the particle and the effect's matrix (0x005ACA30, still the
// game's), one value of the description's curve at the particle's age
// (slot 0x30 with 3), and the block (slot 0x20), or for descriptions with
// flag 8 of their third word a second block with its turning left out
// (slot 0x1C). Last the box is widened by the effect's +0x274.
uint32_t NotDrawnB(char* self)
{
    uint32_t stackmem[256] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[160]);
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
#include "core/effect_notdrawn_b.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// Its comparison: what it does is the effect's box and the calls to the
// particles' own effects. For the length of the comparison every such
// effect is given a copy of its table of functions with the four slots
// replaced, which note each call (the slot, the effect, and the values
// behind the pointer, without the block's two unset bytes). Slot 0x14 only
// asks, and is always passed on. Ours runs first with the other calls only
// noted; the effect is put back and the game's runs with them passed on.
// The calls and the effect must be the same.
struct ChildCall {
    uint32_t slot, child, a, b;
    char data[0x38];
    bool operator==(const ChildCall& other) const
    {
        return slot == other.slot && child == other.child && a == other.a && b == other.b &&
               std::memcmp(data, other.data, sizeof data) == 0;
    }
};
constexpr std::size_t kChildSlots = 64, kEffectSizeB = 0x27C;
constexpr std::size_t kParticleBytesB = 0x54, kParticleChild = 0x44;
std::map<uint32_t, std::vector<uint32_t>> g_childTables;
std::unordered_map<char*, uint32_t> g_childReal;
std::vector<ChildCall> g_childOurCalls, g_childGameCalls;
bool g_childForward = false;
StepMode g_notDrawnModeB = StepMode::Original;
Call0Fn g_SC4EffectB_NotDrawn = nullptr;
uint64_t g_notDrawnBCompared = 0, g_childCallsCompared = 0;

uint32_t ChildSlot(char* child, uint32_t slot)
{
    return M<uint32_t>(g_childReal[child] + slot);
}

void NoteChildCall(const ChildCall& call)
{
    (g_childForward ? g_childGameCalls : g_childOurCalls).push_back(call);
}

void CopyBlock(char* to, uint32_t block)
{
    const char* const from = reinterpret_cast<const char*>(block);
    std::memcpy(to, from, 2);
    std::memcpy(to + 4, from + 4, 0x34);
}

uint32_t __fastcall ChildRunning(char* child, void*)
{
    const uint32_t result = CallThis<uint32_t>(ChildSlot(child, 0x14), child) & 0xFF;
    NoteChildCall({0x14, reinterpret_cast<uint32_t>(child), result, 0, {}});
    return result;
}

uint32_t __fastcall ChildValue(char* child, void*, uint32_t which, uint32_t values, uint32_t count)
{
    ChildCall call{0x30, reinterpret_cast<uint32_t>(child), which, count, {}};
    std::memcpy(call.data, reinterpret_cast<const void*>(values), count <= 14 ? count * 4 : 0);
    NoteChildCall(call);
    return g_childForward ? CallThis<uint32_t>(ChildSlot(child, 0x30), child, which, values, count) : 0;
}

uint32_t __fastcall ChildBlockPlain(char* child, void*, uint32_t block)
{
    ChildCall call{0x1C, reinterpret_cast<uint32_t>(child), 0, 0, {}};
    CopyBlock(call.data, block);
    NoteChildCall(call);
    return g_childForward ? CallThis<uint32_t>(ChildSlot(child, 0x1C), child, block) : 0;
}

uint32_t __fastcall ChildBlock(char* child, void*, uint32_t block)
{
    ChildCall call{0x20, reinterpret_cast<uint32_t>(child), 0, 0, {}};
    CopyBlock(call.data, block);
    NoteChildCall(call);
    return g_childForward ? CallThis<uint32_t>(ChildSlot(child, 0x20), child, block) : 0;
}

// Every particle's own effect gets the noting table, or its own back.
void SwapChildren(char* self, bool in)
{
    if (!in) {
        for (const auto& [child, table] : g_childReal)
            At<uint32_t>(child, 0) = table;
        g_childReal.clear();
        return;
    }
    char* const head = At<char*>(self, 0x14);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        char* const child = At<char*>(At<char*>(node, 8), kParticleChild);
        if (!child || g_childReal.count(child))
            continue;
        const uint32_t real = At<uint32_t>(child, 0);
        std::vector<uint32_t>& table = g_childTables[real];
        if (table.empty()) {
            table.assign(reinterpret_cast<const uint32_t*>(real), reinterpret_cast<const uint32_t*>(real) + kChildSlots);
            table[0x14 / 4] = reinterpret_cast<uint32_t>(&ChildRunning);
            table[0x30 / 4] = reinterpret_cast<uint32_t>(&ChildValue);
            table[0x1C / 4] = reinterpret_cast<uint32_t>(&ChildBlockPlain);
            table[0x20 / 4] = reinterpret_cast<uint32_t>(&ChildBlock);
        }
        g_childReal[child] = real;
        At<uint32_t*>(child, 0) = table.data();
    }
}

uint32_t __fastcall NotDrawnBByMode(char* self, void*)
{
    if (g_notDrawnModeB == StepMode::Original || g_effectRecord || g_effectReplay || !g_childReal.empty())
        return g_SC4EffectB_NotDrawn(self, nullptr);
    if (g_notDrawnModeB == StepMode::Ours)
        return NotDrawnB(self);
    char before[kEffectSizeB], mine[kEffectSizeB];
    std::memcpy(before, self, kEffectSizeB);
    // The game's 0x005ACA30 turns the particles as it goes (+0x48 to
    // +0x50), so they are kept and put back as well.
    std::vector<char*> particles;
    char* const head = At<char*>(self, 0x14);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0))
        particles.push_back(At<char*>(node, 8));
    const auto saveParticles = [&] {
        std::vector<char> bytes(particles.size() * kParticleBytesB);
        for (std::size_t i = 0; i < particles.size(); ++i)
            std::memcpy(bytes.data() + i * kParticleBytesB, particles[i], kParticleBytesB);
        return bytes;
    };
    const std::vector<char> particlesBefore = saveParticles();
    SwapChildren(self, true);
    g_childOurCalls.clear();
    g_childGameCalls.clear();
    g_childForward = false;
    NotDrawnB(self);
    std::memcpy(mine, self, kEffectSizeB);
    const std::vector<char> particlesMine = saveParticles();
    std::memcpy(self, before, kEffectSizeB);
    for (std::size_t i = 0; i < particles.size(); ++i)
        std::memcpy(particles[i], particlesBefore.data() + i * kParticleBytesB, kParticleBytesB);
    g_childForward = true;
    const uint32_t result = g_SC4EffectB_NotDrawn(self, nullptr);
    SwapChildren(self, false);
    const bool sameParticles = saveParticles() == particlesMine;
    ++g_notDrawnBCompared;
    g_childCallsCompared += g_childGameCalls.size();
    if ((g_notDrawnBCompared & (g_notDrawnBCompared - 1)) == 0)
        log::Write("water effects: effects.notdrawn_b: %llu steps, %llu calls compared so far",
                   static_cast<unsigned long long>(g_notDrawnBCompared),
                   static_cast<unsigned long long>(g_childCallsCompared));
    char text[80];
    if (!(g_childGameCalls == g_childOurCalls)) {
        std::size_t at = 0;
        while (at < g_childGameCalls.size() && at < g_childOurCalls.size() &&
               g_childGameCalls[at] == g_childOurCalls[at])
            ++at;
        const auto describe = [&](const std::vector<ChildCall>& calls) {
            if (at >= calls.size()) {
                std::snprintf(text, sizeof text, "%zu calls", calls.size());
                return std::string(text);
            }
            std::snprintf(text, sizeof text, "call %zu of %zu: slot %X on %08X (%X, %X) ", at, calls.size(),
                          calls[at].slot, calls[at].child, calls[at].a, calls[at].b);
            return text + Hex(calls[at].data, sizeof calls[at].data);
        };
        const std::string game = describe(g_childGameCalls);
        compare::Record("effects.notdrawn_b", false, game, describe(g_childOurCalls));
    } else if (!sameParticles) {
        compare::Record("effects.notdrawn_b", false, "particles", "particles differ");
    } else if (std::memcmp(self, mine, kEffectSizeB) != 0) {
        std::size_t at = 0;
        while (self[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "effect+%zX ", at);
        compare::Record("effects.notdrawn_b", false, text + Hex(self + at, 4), text + Hex(mine + at, 4));
    } else {
        compare::Record("effects.notdrawn_b", true, std::string(), std::string());
    }
    return result;
}

// The second effect class's draw (0x005ADD50, slot 0x44), translated
// instruction by instruction. Its particles are drawn as one model each:
// the effect keeps a model instance (+0x220) and the model (+0x224). The
// box is kept as in the step out of view. Each particle in view (the
// game's 0x005AD890 says, for descriptions with flag 2 of their third
// word) gets its block from 0x005ACA30, scaled by a curve of the
// description; the instance is given the block (slot 0x28) and the
// particle's colour and opacity in the block the instance itself gave at
// the start (slots 0x3C and 0x40), the model is set to the frame the
// particle's own clock (+0x48) has reached (slot 0x28; the clock runs by
// the effect's step or by the particle's speed), or the effect's timer
// (+0x228) is used, and the instance is drawn (slot 0x30 with the
// context). A particle's own effect is then told where it is, as out of
// view. Last the context's colour is reset (0x007D3290).
uint32_t DrawParticlesB(char* self, uint32_t context)
{
    uint32_t stackmem[320] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[224]);
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
#include "core/effect_draw_b.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// Its comparison is the step's out of view, with the model instance and
// the model given noting tables as well: what only asks (the instance's
// slot 0x3C, the model's 0x30 and 0x44) is always passed on, and the
// rest, the drawing of the instance among it, only when the game's runs.
std::map<uint32_t, std::vector<uint32_t>> g_instanceTables, g_modelTables;
StepMode g_drawModeB = StepMode::Original;
Call1Fn g_SC4EffectB_DrawParticles = nullptr;
uint64_t g_drawsBCompared = 0, g_drawBCallsCompared = 0;

uint32_t __fastcall InstanceInfo(char* object, void*, uint32_t out)
{
    const uint32_t result = CallThis<uint32_t>(ChildSlot(object, 0x3C), object, out);
    ChildCall call{0x3C, reinterpret_cast<uint32_t>(object), 0, 0, {}};
    std::memcpy(call.data, reinterpret_cast<const void*>(result), 0x14);
    NoteChildCall(call);
    return result;
}

uint32_t __fastcall InstanceBlock(char* object, void*, uint32_t block)
{
    ChildCall call{0x28, reinterpret_cast<uint32_t>(object), 0, 0, {}};
    CopyBlock(call.data, block);
    NoteChildCall(call);
    return g_childForward ? CallThis<uint32_t>(ChildSlot(object, 0x28), object, block) : 0;
}

uint32_t __fastcall InstanceSetInfo(char* object, void*, uint32_t info)
{
    ChildCall call{0x40, reinterpret_cast<uint32_t>(object), 0, 0, {}};
    std::memcpy(call.data, reinterpret_cast<const void*>(info), 0x14);
    NoteChildCall(call);
    return g_childForward ? CallThis<uint32_t>(ChildSlot(object, 0x40), object, info) : 0;
}

uint32_t __fastcall InstanceDraw(char* object, void*, uint32_t context, uint32_t flag)
{
    NoteChildCall({0x30, reinterpret_cast<uint32_t>(object), context, flag, {}});
    return g_childForward ? CallThis<uint32_t>(ChildSlot(object, 0x30), object, context, flag) : 0;
}

uint32_t __fastcall ModelFrames(char* object, void*)
{
    const uint32_t result = CallThis<uint32_t>(ChildSlot(object, 0x30), object);
    NoteChildCall({0x130, reinterpret_cast<uint32_t>(object), result, 0, {}});
    return result;
}

uint32_t __fastcall ModelInfo(char* object, void*)
{
    const uint32_t result = CallThis<uint32_t>(ChildSlot(object, 0x44), object);
    NoteChildCall({0x144, reinterpret_cast<uint32_t>(object), M<uint16_t>(result + 2), 0, {}});
    return result;
}

uint32_t __fastcall ModelSetFrame(char* object, void*, uint32_t frame)
{
    NoteChildCall({0x128, reinterpret_cast<uint32_t>(object), frame, 0, {}});
    return g_childForward ? CallThis<uint32_t>(ChildSlot(object, 0x28), object, frame) : 0;
}

// The effect's part at +0x228 keeps memory of its own, so its two
// functions the draw calls cannot run twice from the same bytes: during a
// comparison they are noted as the calls above, and run only for the
// game's.
constexpr std::size_t kEffectPartB = 0x228, kEffectPartEndB = 0x26C;
char* g_drawBPart = nullptr;
Call2Fn g_SC4EffectPartB_Start = nullptr;
Call1Fn g_SC4EffectPartB_Step = nullptr;

uint32_t __fastcall DSC4EffectPartB_Start(char* part, void*, uint32_t a, uint32_t b)
{
    if (part != g_drawBPart)
        return g_SC4EffectPartB_Start(part, nullptr, a, b);
    NoteChildCall({0x803E10, 0, a, b, {}});
    return g_childForward ? g_SC4EffectPartB_Start(part, nullptr, a, b) : 0;
}
OPENSC4_THUNK(TSC4EffectPartB_Start, DSC4EffectPartB_Start, 8)

uint32_t __fastcall DSC4EffectPartB_Step(char* part, void*, uint32_t step)
{
    if (part != g_drawBPart)
        return g_SC4EffectPartB_Step(part, nullptr, step);
    NoteChildCall({0x5AC200, 0, step, 0, {}});
    return g_childForward ? g_SC4EffectPartB_Step(part, nullptr, step) : 0;
}
OPENSC4_THUNK(TSC4EffectPartB_Step, DSC4EffectPartB_Step, 4)

void SwapOne(char* object, std::map<uint32_t, std::vector<uint32_t>>& tables,
             std::initializer_list<std::pair<uint32_t, uint32_t>> slots)
{
    if (!object || g_childReal.count(object))
        return;
    const uint32_t real = At<uint32_t>(object, 0);
    std::vector<uint32_t>& table = tables[real];
    if (table.empty()) {
        table.assign(reinterpret_cast<const uint32_t*>(real), reinterpret_cast<const uint32_t*>(real) + kChildSlots);
        for (const auto& [slot, function] : slots)
            table[slot / 4] = function;
    }
    g_childReal[object] = real;
    At<uint32_t*>(object, 0) = table.data();
}

uint32_t __fastcall DrawBByMode(char* self, void*, uint32_t context)
{
    if (g_drawModeB == StepMode::Original || g_effectRecord || g_effectReplay || !g_childReal.empty())
        return g_SC4EffectB_DrawParticles(self, nullptr, context);
    if (g_drawModeB == StepMode::Ours)
        return DrawParticlesB(self, context);
    char before[kEffectSizeB], mine[kEffectSizeB];
    std::memcpy(before, self, kEffectSizeB);
    std::vector<char*> particles;
    char* const head = At<char*>(self, 0x14);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0))
        particles.push_back(At<char*>(node, 8));
    const auto saveParticles = [&] {
        std::vector<char> bytes(particles.size() * kParticleBytesB);
        for (std::size_t i = 0; i < particles.size(); ++i)
            std::memcpy(bytes.data() + i * kParticleBytesB, particles[i], kParticleBytesB);
        return bytes;
    };
    const std::vector<char> particlesBefore = saveParticles();
    SwapChildren(self, true);
    SwapOne(At<char*>(self, 0x220), g_instanceTables,
            {{0x3C, reinterpret_cast<uint32_t>(&InstanceInfo)},
             {0x28, reinterpret_cast<uint32_t>(&InstanceBlock)},
             {0x40, reinterpret_cast<uint32_t>(&InstanceSetInfo)},
             {0x30, reinterpret_cast<uint32_t>(&InstanceDraw)}});
    SwapOne(At<char*>(self, 0x224), g_modelTables,
            {{0x30, reinterpret_cast<uint32_t>(&ModelFrames)},
             {0x44, reinterpret_cast<uint32_t>(&ModelInfo)},
             {0x28, reinterpret_cast<uint32_t>(&ModelSetFrame)}});
    g_drawBPart = self + kEffectPartB;
    g_childOurCalls.clear();
    g_childGameCalls.clear();
    g_childForward = false;
    DrawParticlesB(self, context);
    std::memcpy(mine, self, kEffectSizeB);
    const std::vector<char> particlesMine = saveParticles();
    std::memcpy(self, before, kEffectSizeB);
    for (std::size_t i = 0; i < particles.size(); ++i)
        std::memcpy(particles[i], particlesBefore.data() + i * kParticleBytesB, kParticleBytesB);
    g_childForward = true;
    const uint32_t result = g_SC4EffectB_DrawParticles(self, nullptr, context);
    SwapChildren(self, false);
    g_drawBPart = nullptr;
    const bool sameParticles = saveParticles() == particlesMine;
    // The part is the game's alone: ours is given what the game's left.
    std::memcpy(mine + kEffectPartB, self + kEffectPartB, kEffectPartEndB - kEffectPartB);
    ++g_drawsBCompared;
    g_drawBCallsCompared += g_childGameCalls.size();
    if ((g_drawsBCompared & (g_drawsBCompared - 1)) == 0)
        log::Write("water effects: effects.draw_b: %llu draws, %llu calls compared so far",
                   static_cast<unsigned long long>(g_drawsBCompared),
                   static_cast<unsigned long long>(g_drawBCallsCompared));
    char text[80];
    if (!(g_childGameCalls == g_childOurCalls)) {
        std::size_t at = 0;
        while (at < g_childGameCalls.size() && at < g_childOurCalls.size() &&
               g_childGameCalls[at] == g_childOurCalls[at])
            ++at;
        const auto describe = [&](const std::vector<ChildCall>& calls) {
            if (at >= calls.size()) {
                std::snprintf(text, sizeof text, "%zu calls", calls.size());
                return std::string(text);
            }
            std::snprintf(text, sizeof text, "call %zu of %zu: slot %X on %08X (%X, %X) ", at, calls.size(),
                          calls[at].slot, calls[at].child, calls[at].a, calls[at].b);
            return text + Hex(calls[at].data, sizeof calls[at].data);
        };
        const std::string game = describe(g_childGameCalls);
        compare::Record("effects.draw_b", false, game, describe(g_childOurCalls));
    } else if (!sameParticles) {
        compare::Record("effects.draw_b", false, "particles", "particles differ");
    } else if (std::memcmp(self, mine, kEffectSizeB) != 0) {
        std::size_t at = 0;
        while (self[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "effect+%zX ", at);
        compare::Record("effects.draw_b", false, text + Hex(self + at, 4), text + Hex(mine + at, 4));
    } else {
        compare::Record("effects.draw_b", true, std::string(), std::string());
    }
    return result;
}

uint32_t __fastcall DSC4EffectB_DrawParticles(char* self, void*, uint32_t context)
{
    return EffectRecorded(addr::SC4EffectB_DrawParticles, &DrawBByMode, self, context);
}
OPENSC4_THUNK(TSC4EffectB_DrawParticles, DSC4EffectB_DrawParticles, 4)

uint32_t __fastcall DSC4EffectB_NotDrawn(char* self, void*)
{
    return EffectRecorded(addr::SC4EffectB_NotDrawn, &NotDrawnBByMode, self);
}
OPENSC4_THUNK(TSC4EffectB_NotDrawn, DSC4EffectB_NotDrawn, 0)
Call1Fn g_SC4Effect_StepFlag2000 = nullptr;
uint32_t __fastcall DSC4Effect_StepFlag2000(char* self, void*, uint32_t a)
{
    if (DescNoting())
        return DescCallee(addr::SC4Effect_StepFlag2000, a, [&] { return g_SC4Effect_StepFlag2000(self, nullptr, a); });
    return EffectRecorded(addr::SC4Effect_StepFlag2000, g_SC4Effect_StepFlag2000, self, a);
}
OPENSC4_THUNK(TSC4Effect_StepFlag2000, DSC4Effect_StepFlag2000, 4)
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
Call0Fn g_SC4EffectBase_Finish = nullptr;
void TSC4EffectBase_Finish();
// An effect's wandering (0x005B2730; the effect's draw calls it last for
// descriptions with flag 0x800000, unless the effect has just ended),
// translated instruction by instruction: a timer (+0x148) is lowered by the
// step, and each time it runs out it is set again to a random time between
// two values of the description (+0x174) and the effect's own velocity
// (+0x150) is turned by a random amount; the random numbers come from the
// generator at 0x00B22DF8.
uint32_t WanderEffect(char* self, uint32_t step)
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
#include "core/effect_wander.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

StepMode g_wanderMode = StepMode::Original;
Call1Fn g_SC4Effect_StepFlag800000 = nullptr;
uint64_t g_wandersCompared = 0;
constexpr std::uintptr_t kEffectGenerator = 0x00B22DF8;
constexpr std::size_t kGeneratorSize = 0x10;

// It writes the effect and draws random numbers, so the comparison runs
// ours, keeps the effect and the generator it left, puts both back and runs
// the game's: the two then draw the same numbers.
uint32_t __fastcall WanderByMode(char* self, void*, uint32_t step)
{
    if (g_wanderMode == StepMode::Original || g_effectReplay)
        return g_SC4Effect_StepFlag800000(self, nullptr, step);
    if (g_wanderMode == StepMode::Ours)
        return WanderEffect(self, step);
    char* const generator = reinterpret_cast<char*>(kEffectGenerator);
    char before[kEffectSize + kGeneratorSize], mine[kEffectSize + kGeneratorSize];
    const auto save = [&](char* to) {
        std::memcpy(to, self, kEffectSize);
        std::memcpy(to + kEffectSize, generator, kGeneratorSize);
    };
    save(before);
    if (g_selfCheck)
        g_SC4Effect_StepFlag800000(self, nullptr, step);
    else
        WanderEffect(self, step);
    save(mine);
    std::memcpy(self, before, kEffectSize);
    std::memcpy(generator, before + kEffectSize, kGeneratorSize);
    const uint32_t result = g_SC4Effect_StepFlag800000(self, nullptr, step);
    char game[kEffectSize + kGeneratorSize];
    save(game);
    const char* const id = g_selfCheck ? "effects.wander.selfcheck" : "effects.wander";
    ++g_wandersCompared;
    if ((g_wandersCompared & (g_wandersCompared - 1)) == 0)
        log::Write("water effects: %s: %llu calls compared so far", id,
                   static_cast<unsigned long long>(g_wandersCompared));
    if (std::memcmp(game, mine, sizeof game) == 0) {
        compare::Record(id, true, std::string(), std::string());
    } else {
        std::size_t at = 0;
        while (game[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        char text[40];
        std::snprintf(text, sizeof text, "%s+%zX ", at < kEffectSize ? "effect" : "generator",
                      at < kEffectSize ? at : at - kEffectSize);
        compare::Record(id, false, text + Hex(game + at, 8), text + Hex(mine + at, 8));
    }
    return result;
}

uint32_t __fastcall DSC4Effect_StepFlag800000(char* self, void*, uint32_t step)
{
    return EffectRecorded(addr::SC4Effect_StepFlag800000, &WanderByMode, self, step);
}
OPENSC4_THUNK(TSC4Effect_StepFlag800000, DSC4Effect_StepFlag800000, 4)

// A particle's start (0x005B7580; taking a particle from the pool calls it
// with the effect), translated instruction by instruction: the particle is
// marked as running (its first byte), its age (+4) is 0 and its life (+8) a
// random time between two values of the description, and its position,
// velocity and the rest up to +0x40 come from the effect, its description
// and random numbers of the generator at 0x00B22DF8. For descriptions with
// a flag of the mask at 0x00B22E18 the game's 0x005B71E0 then places the
// particle on the ground, and may end it at once.
uint32_t StartParticle(char* self, uint32_t particle)
{
    uint32_t stackmem[192] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = particle;
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
#include "core/effect_start.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

StepMode g_startMode = StepMode::Original, g_groundMode = StepMode::Original;
Call1Fn g_startParticle = nullptr, g_SC4Effect_PlaceOnGround = nullptr;
uint64_t g_startsCompared = 0;
constexpr std::size_t kStartParticleBytes = 0x44;

// The comparison of a function that writes a particle and draws random
// numbers: ours runs, the particle, the effect and the generator it left
// are kept, all three are put back and the game's runs: the two then draw
// the same numbers. The answers are compared by their low byte.
uint32_t ParticleCompared(const char* name, Call1Fn game, uint32_t (*ours)(char*, uint32_t), char* self,
                          uint32_t particle)
{
    char* const generator = reinterpret_cast<char*>(kEffectGenerator);
    char* const bytes = reinterpret_cast<char*>(particle);
    constexpr std::size_t kEffectAt = kStartParticleBytes, kGeneratorAt = kEffectAt + kEffectSize,
                          kResultAt = kGeneratorAt + kGeneratorSize, kTotal = kResultAt + 4;
    char before[kTotal] = {}, mine[kTotal] = {}, theirs[kTotal] = {};
    const auto save = [&](char* to) {
        std::memcpy(to, bytes, kStartParticleBytes);
        std::memcpy(to + kEffectAt, self, kEffectSize);
        std::memcpy(to + kGeneratorAt, generator, kGeneratorSize);
    };
    save(before);
    mine[kResultAt] = static_cast<char>(g_selfCheck ? game(self, nullptr, particle) : ours(self, particle));
    save(mine);
    std::memcpy(bytes, before, kStartParticleBytes);
    std::memcpy(self, before + kEffectAt, kEffectSize);
    std::memcpy(generator, before + kGeneratorAt, kGeneratorSize);
    const uint32_t result = game(self, nullptr, particle);
    theirs[kResultAt] = static_cast<char>(result);
    save(theirs);
    const std::string id = std::string(name) + (g_selfCheck ? ".selfcheck" : "");
    ++g_startsCompared;
    if ((g_startsCompared & (g_startsCompared - 1)) == 0)
        log::Write("water effects: %s: %llu particle calls compared so far", id.c_str(),
                   static_cast<unsigned long long>(g_startsCompared));
    if (std::memcmp(theirs, mine, kTotal) == 0) {
        compare::Record(id.c_str(), true, std::string(), std::string());
    } else {
        std::size_t at = 0;
        while (theirs[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        char text[40];
        std::snprintf(text, sizeof text, "%s+%zX ",
                      at < kEffectAt ? "particle" : at < kGeneratorAt ? "effect" : at < kResultAt ? "generator" : "result",
                      at < kEffectAt ? at : at < kGeneratorAt ? at - kEffectAt : at < kResultAt ? at - kGeneratorAt : 0);
        compare::Record(id.c_str(), false, text + Hex(theirs + at, 4), text + Hex(mine + at, 4));
    }
    return result;
}

// A new particle put on the ground (0x005B71E0; the start calls it for
// descriptions with a flag of the mask at 0x00B22E18), translated
// instruction by instruction: the particle's position is taken to the
// terrain's grid, and the particle is ended (its first byte cleared, the
// answer false) when that is off the map, when the height the terrain
// answers (the object at +0x1F0, slot 0x34) is outside two values of the
// description (flag 0x10000) or when the cell is of the wrong kind (flags
// of the mask at 0x00B22E1C and 0x20000, asked of the object at
// 0x00B43CEC). Else, for flags 0x4000 and 0x8000, the particle is put at
// that height or the effect's own (+0x1FC) plus a random amount, and the
// answer is true.
uint32_t PlaceOnGround(char* self, uint32_t particle)
{
    uint32_t stackmem[192] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = particle;
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
#include "core/effect_ground.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

uint32_t __fastcall StartByMode(char* self, void*, uint32_t particle)
{
    if (g_startMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_startParticle(self, nullptr, particle);
    if (g_startMode == StepMode::Ours)
        return StartParticle(self, particle);
    return ParticleCompared("effects.start", g_startParticle, &StartParticle, self, particle);
}

uint32_t __fastcall GroundByMode(char* self, void*, uint32_t particle)
{
    if (g_groundMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4Effect_PlaceOnGround(self, nullptr, particle);
    if (g_groundMode == StepMode::Ours)
        return PlaceOnGround(self, particle);
    return ParticleCompared("effects.ground", g_SC4Effect_PlaceOnGround, &PlaceOnGround, self, particle);
}
OPENSC4_THUNK(TSC4Effect_PlaceOnGround, GroundByMode, 4)

// A particle's step for descriptions with a flag of the mask at 0x00B22E14
// (0x005B9790; the plain step jumps to it), translated instruction by
// instruction: the particle's age (+4) grows by the step and past its life
// (+8) the particle ends; else the step is done in parts of at most the
// float at 0x00AA0CF4, each moving the particle (flag 0x10000000 through
// the game's 0x005B6600), changing its velocity by the effect's and the
// description's values, and ending it when it leaves the map (flag 0x800)
// or when the game's 0x005B8DC0 says so (flags 0x100 and 0x400, on the
// map); last, descriptions with bit 0 of their second word of flags go to
// the game's 0x005B8020.
uint32_t StepParticleFull(char* self, uint32_t particle, uint32_t step)
{
    uint32_t stackmem[192] = {};
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
#include "core/effect_step_full.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// Its comparison: two of the functions it calls do more than write the
// particle (0x005B8DC0 may start another effect, 0x005B8020 makes objects),
// so the game's runs for real with each call of the two noted (what it was
// given, what it answered and which bytes of the particle and the effect
// it changed); then the particle and the effect are put back and ours runs
// with those calls answered from the notes and only those bytes set. The calls, the answers and what is
// left of the particle and the effect must be the same.
struct FullStepCall {
    uint32_t target, particle;
    char point[12];
    uint32_t step, result;
    std::vector<std::pair<uint32_t, char>> changes;
    bool operator==(const FullStepCall& other) const
    {
        return target == other.target && particle == other.particle && step == other.step &&
               std::memcmp(point, other.point, sizeof point) == 0;
    }
};
using Call3Fn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t);
StepMode g_fullStepMode = StepMode::Original;
Call2Fn g_SC4Effect_StepParticleFull = nullptr;
Call3Fn g_SC4Effect_StepFlag100 = nullptr;
Call1Fn g_SC4Effect_StepFlagB1 = nullptr;
bool g_fullRecord = false, g_fullReplay = false;
int g_fullNested = 0;
char* g_fullSelf = nullptr;
char* g_fullParticle = nullptr;
std::vector<FullStepCall> g_fullGameCalls, g_fullOurCalls;
uint64_t g_fullStepsCompared = 0, g_fullCallsCompared = 0;

std::vector<char> SaveFullStep()
{
    std::vector<char> bytes(kStartParticleBytes + kEffectSize);
    std::memcpy(bytes.data(), g_fullParticle, kStartParticleBytes);
    std::memcpy(bytes.data() + kStartParticleBytes, g_fullSelf, kEffectSize);
    return bytes;
}

void RestoreFullStep(const std::vector<char>& bytes)
{
    std::memcpy(g_fullParticle, bytes.data(), kStartParticleBytes);
    std::memcpy(g_fullSelf, bytes.data() + kStartParticleBytes, kEffectSize);
}

// A particle meeting the water or the ground (0x005B8DC0; the other step
// calls it for descriptions with flag 0x100 or 0x400, with the particle,
// its place on the terrain's grid and the part of the step), translated
// instruction by instruction. A particle at or below the water's height
// (the effect's +0x1FC) may end there, by a random number against the
// description's +0x154, and an effect with its +0x219 set starts another
// effect there ("_water", through 0x005B8290). Otherwise the terrain is
// asked (the service 0x00B43D1C's interface 0x0A5EAA1A and the object at
// +0x1F0) for the height and the slope under the particle: a particle that
// reached the ground bounces (its velocity turned about the ground's
// normal and scaled by values of the description), slides, stays, or ends
// (an effect with +0x218 set starts "_terrain" there), and some
// descriptions scale the effect's own velocity (+0x150). False ends the
// particle.
uint32_t HitGround(char* self, uint32_t particle, uint32_t point, uint32_t step)
{
    uint32_t stackmem[192] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = particle;
    M<uint32_t>(esp + 8) = point;
    M<uint32_t>(esp + 12) = step;
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
#include "core/effect_hit.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// Its comparison: it writes the particle and the effect, draws random
// numbers and asks the terrain, all of which can be done twice, but the
// effect it may start cannot. So the game's runs for real with each start
// noted (which, and where); the particle, the effect and the generator are
// put back, and ours runs with the starts only noted. The starts, the
// answer and what is left of the three must be the same.
struct HitStart {
    uint32_t which;
    char place[12];
    bool operator==(const HitStart& other) const
    {
        return which == other.which && std::memcmp(place, other.place, sizeof place) == 0;
    }
};
StepMode g_hitMode = StepMode::Original;
Call2Fn g_SC4Effect_StartNamed = nullptr;
bool g_hitRecord = false, g_hitReplay = false;
std::vector<HitStart> g_hitGameStarts, g_hitOurStarts;
uint64_t g_hitsCompared = 0, g_hitStartsCompared = 0;

uint32_t __fastcall DSC4Effect_StartNamed(char* self, void*, uint32_t which, uint32_t place)
{
    if (!g_hitRecord && !g_hitReplay)
        return g_SC4Effect_StartNamed(self, nullptr, which, place);
    HitStart start{which, {}};
    std::memcpy(start.place, reinterpret_cast<const void*>(place), sizeof start.place);
    if (g_hitReplay) {
        g_hitOurStarts.push_back(start);
        return 0;
    }
    g_hitGameStarts.push_back(start);
    g_hitRecord = false;
    const uint32_t result = g_SC4Effect_StartNamed(self, nullptr, which, place);
    g_hitRecord = true;
    return result;
}
OPENSC4_THUNK(TSC4Effect_StartNamed, DSC4Effect_StartNamed, 8)

uint32_t HitByMode(char* self, uint32_t particle, uint32_t point, uint32_t step)
{
    if (g_hitMode == StepMode::Original || g_effectRecord || g_effectReplay || g_hitRecord || g_hitReplay)
        return g_SC4Effect_StepFlag100(self, nullptr, particle, point, step);
    if (g_hitMode == StepMode::Ours)
        return HitGround(self, particle, point, step);
    char* const generator = reinterpret_cast<char*>(kEffectGenerator);
    char* const bytes = reinterpret_cast<char*>(particle);
    constexpr std::size_t kEffectAt = kStartParticleBytes, kGeneratorAt = kEffectAt + kEffectSize,
                          kTotal = kGeneratorAt + kGeneratorSize;
    char before[kTotal], mine[kTotal], theirs[kTotal];
    const auto save = [&](char* to) {
        std::memcpy(to, bytes, kStartParticleBytes);
        std::memcpy(to + kEffectAt, self, kEffectSize);
        std::memcpy(to + kGeneratorAt, generator, kGeneratorSize);
    };
    const auto restore = [&](const char* from) {
        std::memcpy(bytes, from, kStartParticleBytes);
        std::memcpy(self, from + kEffectAt, kEffectSize);
        std::memcpy(generator, from + kGeneratorAt, kGeneratorSize);
    };
    // The place is the caller's, and the same for both runs.
    char place[12];
    std::memcpy(place, reinterpret_cast<const void*>(point), sizeof place);
    save(before);
    g_hitGameStarts.clear();
    g_hitRecord = true;
    const uint32_t result = g_SC4Effect_StepFlag100(self, nullptr, particle, point, step);
    g_hitRecord = false;
    save(theirs);
    char placeAfter[12];
    std::memcpy(placeAfter, reinterpret_cast<const void*>(point), sizeof placeAfter);
    restore(before);
    std::memcpy(reinterpret_cast<void*>(point), place, sizeof place);
    g_hitOurStarts.clear();
    g_hitReplay = true;
    const uint32_t ourResult = HitGround(self, particle, point, step);
    g_hitReplay = false;
    save(mine);
    const bool samePlace = std::memcmp(reinterpret_cast<const void*>(point), placeAfter, sizeof placeAfter) == 0;
    restore(theirs);
    std::memcpy(reinterpret_cast<void*>(point), placeAfter, sizeof placeAfter);
    ++g_hitsCompared;
    g_hitStartsCompared += g_hitGameStarts.size();
    if ((g_hitsCompared & (g_hitsCompared - 1)) == 0)
        log::Write("water effects: effects.hit: %llu calls, %llu starts compared so far",
                   static_cast<unsigned long long>(g_hitsCompared),
                   static_cast<unsigned long long>(g_hitStartsCompared));
    char text[64];
    if (!(g_hitGameStarts == g_hitOurStarts)) {
        std::snprintf(text, sizeof text, "%zu starts", g_hitGameStarts.size());
        const std::string game = text;
        std::snprintf(text, sizeof text, "%zu starts", g_hitOurStarts.size());
        compare::Record("effects.hit", false, game, text);
    } else if ((result & 0xFF) != (ourResult & 0xFF) || !samePlace || std::memcmp(theirs, mine, kTotal) != 0) {
        std::size_t at = 0;
        while (at < kTotal && theirs[at] == mine[at])
            ++at;
        at = at < kTotal ? at & ~std::size_t{3} : 0;
        const char* const part = at < kEffectAt ? "particle" : at < kGeneratorAt ? "effect" : "generator";
        const std::size_t offset = at < kEffectAt ? at : at < kGeneratorAt ? at - kEffectAt : at - kGeneratorAt;
        std::snprintf(text, sizeof text, "%u%s %s+%zX ", result & 0xFF, samePlace ? "" : " place", part, offset);
        const std::string game = text + Hex(theirs + at, 8);
        std::snprintf(text, sizeof text, "%u%s %s+%zX ", ourResult & 0xFF, samePlace ? "" : " place", part, offset);
        compare::Record("effects.hit", false, game, text + Hex(mine + at, 8));
    } else {
        compare::Record("effects.hit", true, std::string(), std::string());
    }
    return result;
}

template <typename Real>
uint32_t FullStepCallee(uint32_t target, uint32_t particle, const void* point, uint32_t step, Real&& real)
{
    if ((!g_fullRecord && !g_fullReplay) || g_fullNested)
        return real();
    FullStepCall call{target, particle, {}, step, 0, {}};
    if (point)
        std::memcpy(call.point, point, sizeof call.point);
    if (g_fullReplay) {
        const std::size_t at = g_fullOurCalls.size();
        g_fullOurCalls.push_back(std::move(call));
        if (at >= g_fullGameCalls.size())
            return 0;
        // Only the bytes the game's call changed are given to ours: what
        // ours has worked out so far stays, and is compared at the end.
        for (const auto& [offset, value] : g_fullGameCalls[at].changes)
            (offset < kStartParticleBytes ? g_fullParticle + offset : g_fullSelf + (offset - kStartParticleBytes))[0] =
                value;
        return g_fullGameCalls[at].result;
    }
    // What the call itself steps is not part of this record.
    const std::vector<char> before = SaveFullStep();
    ++g_fullNested;
    call.result = real();
    --g_fullNested;
    const std::vector<char> after = SaveFullStep();
    for (std::size_t offset = 0; offset < after.size(); ++offset) {
        if (after[offset] != before[offset])
            call.changes.push_back({static_cast<uint32_t>(offset), after[offset]});
    }
    g_fullGameCalls.push_back(std::move(call));
    return g_fullGameCalls.back().result;
}

uint32_t __fastcall DSC4Effect_StepFlag100(char* self, void*, uint32_t particle, uint32_t point, uint32_t step)
{
    return FullStepCallee(addr::SC4Effect_StepFlag100, particle, reinterpret_cast<const void*>(point), step,
                          [&] { return HitByMode(self, particle, point, step); });
}
OPENSC4_THUNK(TSC4Effect_StepFlag100, DSC4Effect_StepFlag100, 12)

uint32_t __fastcall DSC4Effect_StepFlagB1(char* self, void*, uint32_t particle)
{
    return FullStepCallee(addr::SC4Effect_StepFlagB1, particle, nullptr, 0,
                          [&] { return g_SC4Effect_StepFlagB1(self, nullptr, particle); });
}
OPENSC4_THUNK(TSC4Effect_StepFlagB1, DSC4Effect_StepFlagB1, 4)

uint32_t __fastcall FullStepByMode(char* self, void*, uint32_t particle, uint32_t step)
{
    if (g_fullStepMode == StepMode::Original || g_effectRecord || g_effectReplay || g_fullRecord || g_fullReplay)
        return g_SC4Effect_StepParticleFull(self, nullptr, particle, step);
    if (g_fullStepMode == StepMode::Ours)
        return StepParticleFull(self, particle, step);
    g_fullSelf = self;
    g_fullParticle = reinterpret_cast<char*>(particle);
    const std::vector<char> before = SaveFullStep();
    g_fullGameCalls.clear();
    g_fullRecord = true;
    const uint32_t result = g_SC4Effect_StepParticleFull(self, nullptr, particle, step);
    g_fullRecord = false;
    const std::vector<char> after = SaveFullStep();
    RestoreFullStep(before);
    g_fullOurCalls.clear();
    g_fullReplay = true;
    const uint32_t ourResult = StepParticleFull(self, particle, step);
    g_fullReplay = false;
    const std::vector<char> mine = SaveFullStep();
    RestoreFullStep(after);
    ++g_fullStepsCompared;
    g_fullCallsCompared += g_fullGameCalls.size();
    if ((g_fullStepsCompared & (g_fullStepsCompared - 1)) == 0)
        log::Write("water effects: effects.step_full: %llu steps, %llu calls compared so far",
                   static_cast<unsigned long long>(g_fullStepsCompared),
                   static_cast<unsigned long long>(g_fullCallsCompared));
    char text[96];
    if (!(g_fullOurCalls == g_fullGameCalls)) {
        std::size_t at = 0;
        while (at < g_fullOurCalls.size() && at < g_fullGameCalls.size() && g_fullOurCalls[at] == g_fullGameCalls[at])
            ++at;
        const auto describe = [&](const std::vector<FullStepCall>& calls) {
            if (at < calls.size())
                std::snprintf(text, sizeof text, "call %zu of %zu: %08X (%08X, step %08X) ", at, calls.size(),
                              calls[at].target, calls[at].particle, calls[at].step);
            else
                std::snprintf(text, sizeof text, "%zu calls", calls.size());
            return at < calls.size() ? text + Hex(calls[at].point, sizeof calls[at].point) : std::string(text);
        };
        const std::string game = describe(g_fullGameCalls);
        compare::Record("effects.step_full", false, game, describe(g_fullOurCalls));
    } else if ((result & 0xFF) != (ourResult & 0xFF) || after != mine) {
        std::size_t at = 0;
        while (at < after.size() && after[at] == mine[at])
            ++at;
        at = at < after.size() ? at & ~std::size_t{3} : 0;
        std::snprintf(text, sizeof text, "%u %s+%zX ", result & 0xFF, at < kStartParticleBytes ? "particle" : "effect",
                      at < kStartParticleBytes ? at : at - kStartParticleBytes);
        const std::string game = text + Hex(after.data() + at, 8);
        std::snprintf(text, sizeof text, "%u %s+%zX ", ourResult & 0xFF,
                      at < kStartParticleBytes ? "particle" : "effect",
                      at < kStartParticleBytes ? at : at - kStartParticleBytes);
        compare::Record("effects.step_full", false, game, text + Hex(mine.data() + at, 8));
    } else {
        compare::Record("effects.step_full", true, std::string(), std::string());
    }
    return result;
}
OPENSC4_THUNK(TSC4Effect_StepParticleFull, FullStepByMode, 8)
// --- The effect's small functions ------------------------------------------------------------

// Five of the effect's functions that only read or write a few of its
// fields, written by hand. The box asked for (0x005B26F0: six floats from
// +0x1A0) and three floats from +0x34 (0x005B2B00) are copied to where the
// caller says; the answer is that place, as the game's leaves it.
uint32_t EffectBox(char* self, uint32_t out)
{
    std::memcpy(reinterpret_cast<void*>(out), self + 0x1A0, 24);
    return out;
}

uint32_t EffectPlace(char* self, uint32_t out)
{
    std::memcpy(reinterpret_cast<void*>(out), self + 0x34, 12);
    return out;
}

// The word at +0x1E8 set (0x005B2220, slot 0x34 of both classes).
uint32_t SetEffectField1E8(char* self, uint32_t value)
{
    At<uint32_t>(self, 0x1E8) = value;
    return value;
}

// One more holder of the effect (0x005AF940): the count at +0x204.
uint32_t HoldEffect(char* self)
{
    return ++At<uint32_t>(self, 0x204);
}

// The sub-object set going again (0x005B9CF0, slot 0x18 of both classes'
// sub-objects): unless it is going and not told to go on (+5 set, +0xC
// clear) or is marked to end when empty (+0xD), the two marks are cleared
// and +5 set; true when it did.
uint32_t RestartEffect(char* base)
{
    if ((At<uint8_t>(base, 5) && !At<uint8_t>(base, 0xC)) || At<uint8_t>(base, 0xD))
        return 0;
    At<uint8_t>(base, 0xC) = 0;
    At<uint8_t>(base, 0xD) = 0;
    At<uint8_t>(base, 5) = 1;
    return 1;
}

StepMode g_smallMode = StepMode::Original;
Call1Fn g_SC4Effect_GetBox = nullptr, g_SC4Effect_GetPlace = nullptr, g_SC4Effect_SetField1E8 = nullptr;
Call0Fn g_SC4Effect_Hold = nullptr, g_SC4EffectBase_Restart = nullptr;
uint64_t g_smallCompared = 0;
// What is held is not an effect: only the bytes up to its count are known to be its own.
constexpr std::size_t kHeldSize = 0x208;

// Their comparison: ours on the effect, what it left and what it wrote for
// the caller kept, the effect put back, the game's; the two must have left
// the same bytes and given the same answer (as a whole word, or as a byte
// for the one that answers true or false).
template <typename Ours, typename Game>
uint32_t SmallCompared(const char* id, char* self, std::size_t size, uint32_t out, std::size_t outSize,
                       bool byteAnswer, Ours&& ours, Game&& game)
{
    char before[kEffectSize], mine[kEffectSize], outMine[24] = {}, outBefore[24] = {};
    std::memcpy(before, self, size);
    if (outSize)
        std::memcpy(outBefore, reinterpret_cast<const void*>(out), outSize);
    const uint32_t ourResult = ours();
    std::memcpy(mine, self, size);
    if (outSize) {
        std::memcpy(outMine, reinterpret_cast<const void*>(out), outSize);
        std::memcpy(reinterpret_cast<void*>(out), outBefore, outSize);
    }
    std::memcpy(self, before, size);
    const uint32_t result = game();
    const uint32_t mask = byteAnswer ? 0xFFu : 0xFFFFFFFFu;
    const bool same = (result & mask) == (ourResult & mask) && std::memcmp(self, mine, size) == 0 &&
                      (!outSize || std::memcmp(reinterpret_cast<const void*>(out), outMine, outSize) == 0);
    ++g_smallCompared;
    if ((g_smallCompared & (g_smallCompared - 1)) == 0)
        log::Write("water effects: effects.small: %llu calls compared so far",
                   static_cast<unsigned long long>(g_smallCompared));
    char text[48];
    std::snprintf(text, sizeof text, "answer %08X", result & mask);
    const std::string theirs = text;
    std::snprintf(text, sizeof text, "answer %08X", ourResult & mask);
    compare::Record(id, same, same ? std::string() : theirs, same ? std::string() : std::string(text));
    return result;
}

uint32_t __fastcall GetBoxByMode(char* self, void*, uint32_t out)
{
    if (g_smallMode == StepMode::Original)
        return g_SC4Effect_GetBox(self, nullptr, out);
    if (g_smallMode == StepMode::Ours)
        return EffectBox(self, out);
    return SmallCompared(
        "effects.small.box", self, kEffectSize, out, 24, false, [&] { return EffectBox(self, out); },
        [&] { return g_SC4Effect_GetBox(self, nullptr, out); });
}
OPENSC4_THUNK(TSC4Effect_GetBox, GetBoxByMode, 4)

uint32_t __fastcall GetPlaceByMode(char* self, void*, uint32_t out)
{
    if (g_smallMode == StepMode::Original)
        return g_SC4Effect_GetPlace(self, nullptr, out);
    if (g_smallMode == StepMode::Ours)
        return EffectPlace(self, out);
    return SmallCompared(
        "effects.small.place", self, kEffectSize, out, 12, false, [&] { return EffectPlace(self, out); },
        [&] { return g_SC4Effect_GetPlace(self, nullptr, out); });
}
OPENSC4_THUNK(TSC4Effect_GetPlace, GetPlaceByMode, 4)

uint32_t __fastcall SetField1E8ByMode(char* self, void*, uint32_t value)
{
    if (g_smallMode == StepMode::Original)
        return g_SC4Effect_SetField1E8(self, nullptr, value);
    if (g_smallMode == StepMode::Ours)
        return SetEffectField1E8(self, value);
    return SmallCompared(
        "effects.small.field", self, kEffectSize, 0, 0, false, [&] { return SetEffectField1E8(self, value); },
        [&] { return g_SC4Effect_SetField1E8(self, nullptr, value); });
}
OPENSC4_THUNK(TSC4Effect_SetField1E8, SetField1E8ByMode, 4)

uint32_t __fastcall HoldByMode(char* self, void*)
{
    if (BNoting())
        return BCallee(addr::SC4Effect_Hold, reinterpret_cast<uint32_t>(self), 0, 0,
                       [&] { return g_SC4Effect_Hold(self, nullptr); });
    if (DescNoting())
        return DescCallee(addr::SC4Effect_Hold, reinterpret_cast<uint32_t>(self),
                          [&] { return g_SC4Effect_Hold(self, nullptr); });
    if (g_smallMode == StepMode::Original)
        return g_SC4Effect_Hold(self, nullptr);
    if (g_smallMode == StepMode::Ours)
        return HoldEffect(self);
    return SmallCompared(
        "effects.small.hold", self, kHeldSize, 0, 0, false, [&] { return HoldEffect(self); },
        [&] { return g_SC4Effect_Hold(self, nullptr); });
}
OPENSC4_THUNK(TSC4Effect_Hold, HoldByMode, 0)

// The sub-object is at the effect's +4.
uint32_t __fastcall RestartByMode(char* base, void*)
{
    if (g_smallMode == StepMode::Original)
        return g_SC4EffectBase_Restart(base, nullptr);
    if (g_smallMode == StepMode::Ours)
        return RestartEffect(base);
    return SmallCompared(
        "effects.small.restart", base - 4, kEffectSize, 0, 0, true, [&] { return RestartEffect(base); },
        [&] { return g_SC4EffectBase_Restart(base, nullptr); });
}
OPENSC4_THUNK(TSC4EffectBase_Restart, RestartByMode, 0)

// An effect's transform set (0x005B3430, slot 0xC of both classes; two
// blocks of two bytes and thirteen floats, as the second class makes for
// its particles), translated instruction by instruction. The first block
// is kept at +0x90 and the second at +0xC8, each with a count of the
// times it was set (+0x8C, +0xC4) and the block's second byte (+0x8D,
// +0xC5). An effect without +0x21A whose
// description has flag 0x1000 takes no place from the first block. Then
// the effect's own values that go by the transform are worked out again,
// up to its box (+0x194 to +0x1B4).
uint32_t SetEffectTransform(char* self, uint32_t block, uint32_t back)
{
    uint32_t stackmem[192] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = block;
    M<uint32_t>(esp + 8) = back;
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
#include "core/effect_transform.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

StepMode g_transformMode = StepMode::Original;
Call2Fn g_SC4Effect_SetTransform = nullptr;
uint64_t g_transformsCompared = 0;

// It reads the two blocks and writes only the effect: ours runs, what it
// left is kept, the effect is put back and the game's runs.
uint32_t __fastcall TransformByMode(char* self, void*, uint32_t block, uint32_t back)
{
    if (g_transformMode == StepMode::Original)
        return g_SC4Effect_SetTransform(self, nullptr, block, back);
    if (g_transformMode == StepMode::Ours)
        return SetEffectTransform(self, block, back);
    char before[kEffectSize], mine[kEffectSize];
    std::memcpy(before, self, kEffectSize);
    SetEffectTransform(self, block, back);
    std::memcpy(mine, self, kEffectSize);
    std::memcpy(self, before, kEffectSize);
    const uint32_t result = g_SC4Effect_SetTransform(self, nullptr, block, back);
    ++g_transformsCompared;
    if ((g_transformsCompared & (g_transformsCompared - 1)) == 0)
        log::Write("water effects: effects.transform: %llu calls compared so far",
                   static_cast<unsigned long long>(g_transformsCompared));
    if (std::memcmp(self, mine, kEffectSize) == 0) {
        compare::Record("effects.transform", true, std::string(), std::string());
    } else {
        std::size_t at = 0;
        while (self[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        char text[40];
        std::snprintf(text, sizeof text, "effect+%zX ", at);
        compare::Record("effects.transform", false, text + Hex(self + at, 8), text + Hex(mine + at, 8));
    }
    return result;
}
OPENSC4_THUNK(TSC4Effect_SetTransform, TransformByMode, 8)

// An effect started (0x005B3000, slot 0 of both classes, with one word),
// translated instruction by instruction. Nothing when the word is set and
// the description has a count at +0x18 without flag 8, or when the
// sub-object cannot be set going again (0x005B9CF0). Else the effect's
// clocks (+0x110, +0x120) and counts are set back, its pull (+0x13C) is
// the description's, and by the description's flags: a random delay and
// speed (flag 0x800000, with the wander run once), nothing at all on a
// cell of the wrong kind (the terrain's object at 0x00B43CEC, as for a
// new particle), and random values for the emitter's clocks. The random
// numbers come from the generator at 0x00B22DF8.
uint32_t BeginEffect(char* self, uint32_t again)
{
    uint32_t stackmem[192] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = again;
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
#include "core/effect_begin.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

StepMode g_beginMode = StepMode::Original;
Call1Fn g_SC4Effect_Begin = nullptr;
uint64_t g_beginsCompared = 0;

// It writes the effect and draws random numbers: ours runs, the effect and
// the generator it left are kept, both are put back and the game's runs.
uint32_t __fastcall BeginByMode(char* self, void*, uint32_t again)
{
    if (g_beginMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4Effect_Begin(self, nullptr, again);
    if (g_beginMode == StepMode::Ours)
        return BeginEffect(self, again);
    char* const generator = reinterpret_cast<char*>(kEffectGenerator);
    constexpr std::size_t kTotal = kEffectSize + kGeneratorSize;
    char before[kTotal], mine[kTotal], theirs[kTotal];
    const auto save = [&](char* to) {
        std::memcpy(to, self, kEffectSize);
        std::memcpy(to + kEffectSize, generator, kGeneratorSize);
    };
    save(before);
    BeginEffect(self, again);
    save(mine);
    std::memcpy(self, before, kEffectSize);
    std::memcpy(generator, before + kEffectSize, kGeneratorSize);
    const uint32_t result = g_SC4Effect_Begin(self, nullptr, again);
    save(theirs);
    ++g_beginsCompared;
    if ((g_beginsCompared & (g_beginsCompared - 1)) == 0)
        log::Write("water effects: effects.begin: %llu calls compared so far",
                   static_cast<unsigned long long>(g_beginsCompared));
    if (std::memcmp(theirs, mine, kTotal) == 0) {
        compare::Record("effects.begin", true, std::string(), std::string());
    } else {
        std::size_t at = 0;
        while (theirs[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        char text[40];
        std::snprintf(text, sizeof text, "%s+%zX ", at < kEffectSize ? "effect" : "generator",
                      at < kEffectSize ? at : at - kEffectSize);
        compare::Record("effects.begin", false, text + Hex(theirs + at, 8), text + Hex(mine + at, 8));
    }
    return result;
}
OPENSC4_THUNK(TSC4Effect_Begin, BeginByMode, 4)

// An effect's render state by its kind (0x005B2260; taking a description
// calls it with the description's kind, 0 to 7; other values do nothing):
// the packed state at +0x1C is filled by SC4RenderState::Init from one of
// eight rows of fourteen values, written here as the game pushes them (the
// twelfth is a float: 0, 0.9 or 0.5).
uint32_t SetEffectKindState(char* self, uint32_t kind)
{
    static const uint32_t kRows[8][14] = {
        {1, 0, 1, 1, 1, 1, 0, 3, 4, 5, 4, 0, 1, 0xFFFFFFFF},
        {1, 0, 1, 1, 1, 1, 0, 4, 4, 5, 4, 0, 1, 0xFFFFFFFF},
        {1, 0, 0, 1, 1, 1, 0, 0xFFFFFFFF, 4, 5, 4, 0, 1, 0xFFFFFFFF},
        {1, 1, 1, 1, 1, 1, 0, 3, 4, 5, 4, 0x3F666666, 1, 0xFFFFFFFF},
        {1, 1, 1, 1, 1, 1, 0, 3, 4, 5, 4, 0x3F000000, 1, 0xFFFFFFFF},
        {1, 0, 1, 1, 1, 1, 0, 3, 4, 1, 4, 0, 1, 0xFFFFFFFF},
        {1, 0, 0, 1, 1, 1, 0, 0xFFFFFFFF, 4, 1, 4, 0, 1, 0xFFFFFFFF},
        {1, 0, 1, 1, 1, 1, 0, 3, 0, 2, 4, 0, 1, 0xFFFFFFFF},
    };
    if (kind > 7)
        return kind;
    const uint32_t* const r = kRows[kind];
    return CallThis<uint32_t>(addr::SC4RenderState_Init, self + 0x1C, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7],
                              r[8], r[9], r[10], r[11], r[12], r[13]);
}

StepMode g_kindStateMode = StepMode::Original;
Call1Fn g_SC4Effect_SetKindState = nullptr;
uint64_t g_kindStatesCompared = 0;

// It writes only the effect: ours, what it left kept, the effect put back,
// the game's.
uint32_t __fastcall KindStateByMode(char* self, void*, uint32_t kind)
{
    if (g_kindStateMode == StepMode::Original)
        return g_SC4Effect_SetKindState(self, nullptr, kind);
    if (g_kindStateMode == StepMode::Ours)
        return SetEffectKindState(self, kind);
    char before[kEffectSize], mine[kEffectSize];
    std::memcpy(before, self, kEffectSize);
    SetEffectKindState(self, kind);
    std::memcpy(mine, self, kEffectSize);
    std::memcpy(self, before, kEffectSize);
    const uint32_t result = g_SC4Effect_SetKindState(self, nullptr, kind);
    ++g_kindStatesCompared;
    if ((g_kindStatesCompared & (g_kindStatesCompared - 1)) == 0)
        log::Write("water effects: effects.kind_state: %llu calls compared so far",
                   static_cast<unsigned long long>(g_kindStatesCompared));
    const bool same = std::memcmp(self, mine, kEffectSize) == 0;
    char text[32];
    std::snprintf(text, sizeof text, "kind %u ", kind);
    // One name a kind, so the totals show which kinds a city has.
    static const char* const kIds[9] = {
        "effects.kind_state.0", "effects.kind_state.1", "effects.kind_state.2", "effects.kind_state.3",
        "effects.kind_state.4", "effects.kind_state.5", "effects.kind_state.6", "effects.kind_state.7",
        "effects.kind_state.other",
    };
    compare::Record(kIds[kind < 8 ? kind : 8], same, same ? std::string() : text + Hex(self + 0x1C, 8),
                    same ? std::string() : text + Hex(mine + 0x1C, 8));
    return result;
}
OPENSC4_THUNK(TSC4Effect_SetKindState, KindStateByMode, 4)

// An effect taking a description (0x005B7C90, slot 0x28 of the base
// class), translated instruction by instruction. A new description is
// held and the old one let go (0x005AF940, 0x005B1C80). Then, whether the
// description is new or not: the byte +0x21A is cleared, the render state
// is set by the description's kind (+0xD4), its texture is loaded
// (0x005B74C0 with +0xD0), the batch at +0x28 is set up (slot 0x40), and
// the effect's values that come from the description are set: the rate
// of its clock (+0x130), colours and scales back to 1, the box cleared,
// the limits of where particles start (+0x200 to +0x214, with the
// terrain's height for descriptions with flag 0x1000, or 0x005B7030 for
// flag 0x2000), and whether the description has a pull of its own
// (+0x15C, +0x160). For flag 0x100 the effects manager is asked whether
// there are effects named after this one with "_terrain" and "_water"
// (+0x218, +0x219).
uint32_t SetEffectDescription(char* self, uint32_t description)
{
    uint32_t stackmem[192] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = description;
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
#include "core/effect_description.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// Its comparison. Five of the functions it calls cannot run twice (the
// old description may be destroyed, the texture is counted, the batch
// keeps memory), so the game's runs first, for real, with each of their
// calls noted: which, with what, what it answered and which bytes of the
// effect it changed. The effect is put back and ours runs with those
// calls not made: the answer and only the changed bytes are given to it.
// Everything else ours does for real (the render state, the terrain's
// height, the names asked of the manager). The calls and what is left of
// the effect must be the same.
struct DescCall {
    uint32_t target, a, result;
    std::vector<std::pair<uint32_t, char>> changes;
    bool operator==(const DescCall& other) const { return target == other.target && a == other.a; }
};
StepMode g_descMode = StepMode::Original;
Call1Fn g_SC4Effect_SetDescription = nullptr, g_SC4Effect_LoadTexture = nullptr, g_SC4Effect_SortBatch = nullptr;
Call0Fn g_SC4EffectDescription_Release = nullptr;
bool g_descRecord = false, g_descReplay = false;
int g_descNested = 0;
char* g_descSelf = nullptr;
DWORD g_descThread = 0;
std::vector<DescCall> g_descGameCalls, g_descOurCalls;
uint64_t g_descsCompared = 0, g_descCallsCompared = 0;

bool DescNoting()
{
    return (g_descRecord || g_descReplay) && g_descNested == 0 && GetCurrentThreadId() == g_descThread;
}

uint32_t DescCallee(uint32_t target, uint32_t a, const std::function<uint32_t()>& real)
{
    DescCall call{target, a, 0, {}};
    if (g_descReplay) {
        const std::size_t at = g_descOurCalls.size();
        g_descOurCalls.push_back(call);
        if (at >= g_descGameCalls.size())
            return 0;
        for (const auto& [offset, value] : g_descGameCalls[at].changes)
            g_descSelf[offset] = value;
        return g_descGameCalls[at].result;
    }
    char before[kEffectSize];
    std::memcpy(before, g_descSelf, kEffectSize);
    ++g_descNested;
    call.result = real();
    --g_descNested;
    for (std::size_t offset = 0; offset < kEffectSize; ++offset) {
        if (g_descSelf[offset] != before[offset])
            call.changes.push_back({static_cast<uint32_t>(offset), g_descSelf[offset]});
    }
    g_descGameCalls.push_back(std::move(call));
    return g_descGameCalls.back().result;
}

// A description let go (0x005B1C80), written by hand: its count of holders
// (+0x204) goes down by one, and at none it is destroyed (0x0059ECB0) and
// its memory freed (0x005E5620). The answer is the count left. Compared by
// running both on the count while a holder is left; the last letting go
// is the game's in compare mode.
StepMode g_releaseMode = StepMode::Original;

uint32_t ReleaseDescription(char* self)
{
    const int32_t left = --At<int32_t>(self, 0x204);
    if (left != 0)
        return static_cast<uint32_t>(left);
    CallThis<uint32_t>(0x0059ECB0, self);
    reinterpret_cast<void(__cdecl*)(void*)>(0x005E5620)(self);
    return 0;
}

uint32_t __fastcall ReleaseByMode(char* self, void*)
{
    if (g_releaseMode == StepMode::Original)
        return g_SC4EffectDescription_Release(self, nullptr);
    if (g_releaseMode == StepMode::Ours)
        return ReleaseDescription(self);
    const int32_t before = At<int32_t>(self, 0x204);
    if (before == 1) {
        compare::Record("effects.release.last", true, std::string(), std::string());
        return g_SC4EffectDescription_Release(self, nullptr);
    }
    const uint32_t mine = ReleaseDescription(self);
    const int32_t left = At<int32_t>(self, 0x204);
    At<int32_t>(self, 0x204) = before;
    const uint32_t result = g_SC4EffectDescription_Release(self, nullptr);
    const bool same = mine == result && left == At<int32_t>(self, 0x204);
    compare::Record("effects.release", same, same ? std::string() : std::to_string(result),
                    same ? std::string() : std::to_string(mine));
    return result;
}

uint32_t __fastcall DSC4EffectDescription_Release(char* self, void*)
{
    if (BNoting())
        return BCallee(addr::SC4EffectDescription_Release, reinterpret_cast<uint32_t>(self), 0, 0,
                       [&] { return g_SC4EffectDescription_Release(self, nullptr); });
    if (DescNoting())
        return DescCallee(addr::SC4EffectDescription_Release, reinterpret_cast<uint32_t>(self),
                          [&] { return g_SC4EffectDescription_Release(self, nullptr); });
    return EffectRecorded(addr::SC4EffectDescription_Release, &ReleaseByMode, self);
}
OPENSC4_THUNK(TSC4EffectDescription_Release, DSC4EffectDescription_Release, 0)

// An effect's texture loaded (0x005B74C0; taking a description calls it
// with the description's +0xD0), written by hand. The handle at +0x68 is
// cleared. The framework (0x008793EC) is asked for the service 0x1AC0E11A
// by its interface 0xFAC0E219; with it, what the effect held at +0x6C is
// let go (0x008090E0), the service is asked for the texture by its ID into
// +0x6C (its slot 0x14), and on success +0x68 is the word the holder's
// first word points at. The service is released again (0x004495C0, as the
// game's own holder of it does).
uint32_t LoadEffectTexture(char* self, uint32_t which)
{
    At<uint32_t>(self, 0x68) = 0;
    char* service = nullptr;
    char* const framework = reinterpret_cast<char*(__cdecl*)()>(0x008793EC)();
    if (framework)
        CallThis<uint32_t>(At<uint32_t>(At<char*>(framework, 0), 0x14), framework, 0x1AC0E11Au, 0xFAC0E219u,
                           &service);
    if (service) {
        if (char* const old = At<char*>(self, 0x6C)) {
            At<char*>(self, 0x6C) = nullptr;
            CallThis<uint32_t>(0x008090E0, old);
        }
        if (CallThis<uint32_t>(At<uint32_t>(At<char*>(service, 0), 0x14), service, which, self + 0x6C) & 0xFF)
            At<uint32_t>(self, 0x68) = M<uint32_t>(M<uint32_t>(At<uint32_t>(self, 0x6C)));
    }
    return CallThis<uint32_t>(0x004495C0, &service);
}

// Its comparison runs both for real. For an effect that holds no texture
// yet: ours, whose texture is let go again as the function itself lets an
// old one go, then the game's. For one that holds a texture: the game's,
// then ours on top of it. The handle, and whether a texture is held, must
// be the same.
StepMode g_textureMode = StepMode::Original;

uint32_t LoadTextureByMode(char* self, uint32_t which)
{
    if (g_textureMode == StepMode::Original)
        return g_SC4Effect_LoadTexture(self, nullptr, which);
    if (g_textureMode == StepMode::Ours)
        return LoadEffectTexture(self, which);
    if (At<char*>(self, 0x6C)) {
        // The game's lets the old texture go, which cannot be done twice:
        // ours then runs on what the game's left, letting that one go and
        // loading again, and must leave the same.
        const uint32_t result = g_SC4Effect_LoadTexture(self, nullptr, which);
        char theirs[kEffectSize], mine[kEffectSize];
        std::memcpy(theirs, self, kEffectSize);
        LoadEffectTexture(self, which);
        std::memcpy(mine, self, kEffectSize);
        const bool sameHeld = (At<char*>(theirs, 0x6C) != nullptr) == (At<char*>(mine, 0x6C) != nullptr);
        char text[64];
        std::snprintf(text, sizeof text, "handle %08X, held %d", At<uint32_t>(theirs, 0x68),
                      At<char*>(theirs, 0x6C) != nullptr);
        const std::string game = text;
        std::snprintf(text, sizeof text, "handle %08X, held %d", At<uint32_t>(mine, 0x68),
                      At<char*>(mine, 0x6C) != nullptr);
        std::memset(mine + 0x6C, 0, 4);
        std::memset(theirs + 0x6C, 0, 4);
        const bool same = sameHeld && std::memcmp(theirs, mine, kEffectSize) == 0;
        compare::Record("effects.texture.again", same, same ? std::string() : game,
                        same ? std::string() : std::string(text));
        return result;
    }
    char before[kEffectSize], mine[kEffectSize];
    std::memcpy(before, self, kEffectSize);
    LoadEffectTexture(self, which);
    std::memcpy(mine, self, kEffectSize);
    char* const held = At<char*>(self, 0x6C);
    if (held)
        CallThis<uint32_t>(0x008090E0, held);
    std::memcpy(self, before, kEffectSize);
    const uint32_t result = g_SC4Effect_LoadTexture(self, nullptr, which);
    const bool sameHeld = (held != nullptr) == (At<char*>(self, 0x6C) != nullptr);
    char theirs[kEffectSize];
    std::memcpy(theirs, self, kEffectSize);
    std::memset(mine + 0x6C, 0, 4);
    std::memset(theirs + 0x6C, 0, 4);
    const bool same = sameHeld && std::memcmp(theirs, mine, kEffectSize) == 0;
    char text[64];
    std::snprintf(text, sizeof text, "handle %08X, held %d", At<uint32_t>(theirs, 0x68),
                  At<char*>(self, 0x6C) != nullptr);
    const std::string game = text;
    std::snprintf(text, sizeof text, "handle %08X, held %d", At<uint32_t>(mine, 0x68), held != nullptr);
    compare::Record(At<uint32_t>(theirs, 0x68) ? "effects.texture" : "effects.texture.none", same,
                    same ? std::string() : game, same ? std::string() : std::string(text));
    return result;
}

uint32_t __fastcall DSC4Effect_LoadTexture(char* self, void*, uint32_t which)
{
    if (DescNoting())
        return DescCallee(addr::SC4Effect_LoadTexture, which,
                          [&] { return g_SC4Effect_LoadTexture(self, nullptr, which); });
    return LoadTextureByMode(self, which);
}
OPENSC4_THUNK(TSC4Effect_LoadTexture, DSC4Effect_LoadTexture, 4)

uint32_t __fastcall DSC4Effect_SortBatch(char* self, void*, uint32_t batch)
{
    if (DescNoting())
        return DescCallee(addr::SC4Effect_SortBatch, batch - reinterpret_cast<uint32_t>(self),
                          [&] { return g_SC4Effect_SortBatch(self, nullptr, batch); });
    return g_SC4Effect_SortBatch(self, nullptr, batch);
}
OPENSC4_THUNK(TSC4Effect_SortBatch, DSC4Effect_SortBatch, 4)

uint32_t __fastcall DescriptionByMode(char* self, void*, uint32_t description)
{
    if (BNoting())
        return BCallee(addr::SC4Effect_SetDescription, description, 0, 0,
                       [&] { return g_SC4Effect_SetDescription(self, nullptr, description); });
    if (g_descMode == StepMode::Original || g_effectRecord || g_effectReplay || g_descRecord || g_descReplay ||
        BBusy())
        return g_SC4Effect_SetDescription(self, nullptr, description);
    if (g_descMode == StepMode::Ours)
        return SetEffectDescription(self, description);
    char before[kEffectSize], after[kEffectSize], mine[kEffectSize];
    std::memcpy(before, self, kEffectSize);
    g_descSelf = self;
    g_descThread = GetCurrentThreadId();
    g_descGameCalls.clear();
    g_descRecord = true;
    const uint32_t result = g_SC4Effect_SetDescription(self, nullptr, description);
    g_descRecord = false;
    std::memcpy(after, self, kEffectSize);
    std::memcpy(self, before, kEffectSize);
    g_descOurCalls.clear();
    g_descReplay = true;
    SetEffectDescription(self, description);
    g_descReplay = false;
    std::memcpy(mine, self, kEffectSize);
    std::memcpy(self, after, kEffectSize);
    ++g_descsCompared;
    g_descCallsCompared += g_descGameCalls.size();
    if ((g_descsCompared & (g_descsCompared - 1)) == 0)
        log::Write("water effects: effects.description: %llu calls, %llu noted calls compared so far",
                   static_cast<unsigned long long>(g_descsCompared),
                   static_cast<unsigned long long>(g_descCallsCompared));
    char text[80];
    if (!(g_descGameCalls == g_descOurCalls)) {
        std::size_t at = 0;
        while (at < g_descGameCalls.size() && at < g_descOurCalls.size() && g_descGameCalls[at] == g_descOurCalls[at])
            ++at;
        const auto describe = [&](const std::vector<DescCall>& calls) {
            if (at < calls.size())
                std::snprintf(text, sizeof text, "call %zu of %zu: %08X (%08X)", at, calls.size(), calls[at].target,
                              calls[at].a);
            else
                std::snprintf(text, sizeof text, "%zu calls", calls.size());
            return std::string(text);
        };
        const std::string game = describe(g_descGameCalls);
        compare::Record("effects.description", false, game, describe(g_descOurCalls));
    } else if (std::memcmp(after, mine, kEffectSize) != 0) {
        std::size_t at = 0;
        while (after[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "effect+%zX ", at);
        compare::Record("effects.description", false, text + Hex(after + at, 8), text + Hex(mine + at, 8));
    } else {
        compare::Record("effects.description", true, std::string(), std::string());
    }
    return result;
}
OPENSC4_THUNK(TSC4Effect_SetDescription, DescriptionByMode, 4)

// The second class taking a description (0x005AEC90, its slot 0x28),
// translated instruction by instruction; nothing for a description without
// bit 0 of its third word of flags. After the base class's: the model
// instance (+0x220) and the model (+0x224) are released and the object at
// +0x278 ended (its slot 0x20), the part at +0x228 is cleared (0x008038D0
// with 0), and a model is loaded (slot 0x54, 0x005AC610) by the
// description's +0xD0 or one picked at random from its list at +0x1E0.
// With a model: the instance is told when the description has flag 1 (its
// slot 0x40), and for flags 0x100 or 4 of the third word the part is set
// up on the instance (0x005AC110, 0x00989A60 with 10, 0x008038D0,
// 0x00802DB0), the byte +0x26C saying whether that worked. Last the
// effects manager is asked for effects named after this one with two
// more suffixes (+0x26D, +0x26E).
// What a note keeps of the values behind a pointer: so many bytes, a
// string, or a block of two bytes and thirteen floats (its two unset bytes
// left out).
constexpr std::size_t kBData = 0x38, kBString = 0xFFFF, kBBlock = 0xFFFE;
struct BCall {
    uint32_t target, a, result, made;
    char data[kBData];
    std::vector<std::pair<uint32_t, char>> changes;
    bool operator==(const BCall& other) const
    {
        return target == other.target && a == other.a && std::memcmp(data, other.data, sizeof data) == 0;
    }
};
StepMode g_descModeB = StepMode::Original;
Call1Fn g_SC4EffectB_SetDescription = nullptr, g_SC4EffectB_LoadModel = nullptr, g_SC4EffectPartB_Attach = nullptr,
        g_SC4EffectPartB_Place = nullptr, g_SC4EffectPartB_SetDescription = nullptr;
bool g_bRecord = false, g_bReplay = false;
int g_bNested = 0;
// The memory whose bytes a noted call may change, one piece after another.
struct BRegion {
    char* at;
    std::size_t size;
};
std::vector<BRegion> g_bRegions;
char* g_bSelf = nullptr;
DWORD g_bThread = 0;
std::vector<BCall> g_bGameCalls, g_bOurCalls;
uint64_t g_bCompared = 0, g_bCallsCompared = 0;

bool BNoting()
{
    return g_bRecord && g_bNested == 0 && GetCurrentThreadId() == g_bThread;
}

bool BBusy()
{
    return g_bRecord || g_bReplay;
}

std::vector<char> BSaveRegions()
{
    std::vector<char> bytes;
    for (const BRegion& region : g_bRegions)
        bytes.insert(bytes.end(), region.at, region.at + region.size);
    return bytes;
}

char* BRegionByte(std::size_t offset)
{
    for (const BRegion& region : g_bRegions) {
        if (offset < region.size)
            return region.at + offset;
        offset -= region.size;
    }
    return nullptr;
}

// `out`, when given, is where the call leaves a pointer to what it made:
// noted in the game's run and given to ours in the replay.
uint32_t BCallee(uint32_t target, uint32_t a, uint32_t data, std::size_t size, const std::function<uint32_t()>& real,
                 uint32_t out)
{
    BCall call{target, a, 0, 0, {}, {}};
    if (size == kBString)
        std::strncpy(call.data, reinterpret_cast<const char*>(data), kBData - 1);
    else if (size == kBBlock)
        CopyBlock(call.data, data);
    else if (size)
        std::memcpy(call.data, reinterpret_cast<const void*>(data), size);
    if (g_bReplay) {
        const std::size_t at = g_bOurCalls.size();
        g_bOurCalls.push_back(call);
        if (at >= g_bGameCalls.size())
            return 0;
        for (const auto& [offset, value] : g_bGameCalls[at].changes) {
            if (char* const byte = BRegionByte(offset))
                *byte = value;
        }
        if (out)
            M<uint32_t>(out) = g_bGameCalls[at].made;
        return g_bGameCalls[at].result;
    }
    const std::vector<char> before = BSaveRegions();
    ++g_bNested;
    call.result = real();
    --g_bNested;
    const std::vector<char> after = BSaveRegions();
    for (std::size_t offset = 0; offset < after.size(); ++offset) {
        if (after[offset] != before[offset])
            call.changes.push_back({static_cast<uint32_t>(offset), after[offset]});
    }
    if (out)
        call.made = M<uint32_t>(out);
    g_bGameCalls.push_back(std::move(call));
    return g_bGameCalls.back().result;
}

// What the translation's ten calls go through: the real call, or in the
// comparison's replay the note of the game's.
uint32_t BOurs(uint32_t target, uint32_t a, uint32_t data, std::size_t size, const std::function<uint32_t()>& real,
               uint32_t out = 0)
{
    return g_bReplay ? BCallee(target, a, data, size, real, out) : real();
}

uint32_t SetEffectDescriptionB(char* self, uint32_t description)
{
    uint32_t stackmem[192] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = description;
    [[maybe_unused]] double st[8] = {};
    [[maybe_unused]] int top = -1;
    [[maybe_unused]] uint32_t status = 0;
#define ST(i) st[top - (i)]
#define PUSH(value)                                                                                                      \
    do {                                                                                                                 \
        const double pushed = (value);                                                                                   \
        st[++top] = pushed;                                                                                              \
    } while (0)
#define POP() --top
#define FCOM(value) status = CompareStatus(ST(0), (value))
#include "core/effect_description_b.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// Its comparison is the base class's, the game's first, with more to
// note. The objects it releases or ends, and the instance the loaded
// model gives, are seen through copies of their tables of functions (one
// slot each: 8, 0x20 or 0x40); the part's functions and the model's
// loading through detours. The pick among the description's models is
// made again by ours from the same generator.
struct BStandIn {
    uint32_t real, slot;
    int times;
};
constexpr std::size_t kBSlots = 512;
std::map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>> g_bTables;
std::unordered_map<char*, BStandIn> g_bReal;

uint32_t BRealSlot(char* object, uint32_t slot)
{
    const auto found = g_bReal.find(object);
    if (found != g_bReal.end())
        return M<uint32_t>(found->second.real + slot);
    for (const auto& [key, table] : g_bTables) {
        if (At<uint32_t*>(object, 0) == table.data())
            return M<uint32_t>(key.second + slot);
    }
    return 0;
}

// The object gets its own table back before a call that may be its last.
void BGiveBack(char* object, bool whole)
{
    const auto found = g_bReal.find(object);
    if (found == g_bReal.end())
        return;
    if (!whole && --found->second.times > 0)
        return;
    At<uint32_t>(object, 0) = found->second.real;
    g_bReal.erase(found);
}

uint32_t __fastcall BRelease(char* object, void*)
{
    const uint32_t real = BRealSlot(object, 0x8);
    if (!BNoting()) {
        BGiveBack(object, true);
        return CallThis<uint32_t>(real, object);
    }
    return BCallee(0x8, reinterpret_cast<uint32_t>(object), 0, 0, [&] {
        BGiveBack(object, false);
        return CallThis<uint32_t>(real, object);
    });
}

uint32_t __fastcall BEnd(char* object, void*)
{
    const uint32_t real = BRealSlot(object, 0x20);
    if (!BNoting()) {
        BGiveBack(object, true);
        return CallThis<uint32_t>(real, object);
    }
    return BCallee(0x20, reinterpret_cast<uint32_t>(object), 0, 0, [&] {
        BGiveBack(object, true);
        return CallThis<uint32_t>(real, object);
    });
}

uint32_t __fastcall BInfo(char* object, void*, uint32_t info)
{
    const uint32_t real = BRealSlot(object, 0x40);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, info);
    return BCallee(0x40, reinterpret_cast<uint32_t>(object), info, 0x14,
                   [&] { return CallThis<uint32_t>(real, object, info); });
}

// An object given a copy of its table of functions with some slots
// replaced; `role` names the set of slots.
void BSwapRole(char* object, uint32_t role, std::initializer_list<std::pair<uint32_t, uint32_t>> slots)
{
    if (!object)
        return;
    const auto found = g_bReal.find(object);
    if (found != g_bReal.end()) {
        if (found->second.slot == role)
            ++found->second.times;
        return;
    }
    const uint32_t real = At<uint32_t>(object, 0);
    std::vector<uint32_t>& table = g_bTables[{role, real}];
    if (table.empty()) {
        table.assign(reinterpret_cast<const uint32_t*>(real), reinterpret_cast<const uint32_t*>(real) + kBSlots);
        for (const auto& [slot, stub] : slots)
            table[slot / 4] = stub;
    }
    g_bReal[object] = {real, role, 1};
    At<uint32_t*>(object, 0) = table.data();
}

void BSwap(char* object, uint32_t slot, uint32_t stub)
{
    BSwapRole(object, slot, {{slot, stub}});
}

// Every object this engine gave a table gets its own back. Its roles are
// slots (and 0x1000, 0x2000); the noted comparisons of the compound
// effects' start, the making and the makers name theirs by function
// addresses, and theirs are left for them to give back.
void BGiveBackOwn()
{
    for (auto it = g_bReal.begin(); it != g_bReal.end();) {
        if (it->second.slot < 0x10000) {
            At<uint32_t>(it->first, 0) = it->second.real;
            it = g_bReal.erase(it);
        } else {
            ++it;
        }
    }
}

// The second class loading its model (0x005AC610, slot 0x54, with the
// model's instance ID; nothing for 0), translated instruction by
// instruction. The resource manager (0x00B43CA8, slot 0xC) is asked for
// the resource of type 0x5AD0E817, group 0xBADB57F1 and that instance; the
// old model (+0x224) is released and a new one made from the resource (its
// slot 0x10), whose part that counts its holders is kept at +0x220. The
// model's slot 0x40 is called with 3, the largest side of its box (slot
// 0x1C of the instance) is kept at +0x274, and each of its materials (the
// interface 0x26A68448) has its flags set by two flags of the description
// and the description's kind.
uint32_t LoadModelB(char* self, uint32_t which)
{
    uint32_t stackmem[192] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(self);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = which;
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
#include "core/effect_model_b.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// Its comparison runs both for real, on an effect that has no model yet
// (as its one caller leaves it): ours first, whose model's materials are
// read (eight bytes each, the flags it sets) and whose model is then
// released twice, as the class lets go of one; the effect is put back and
// the game's runs. The effect but for the two pointers, whether there is a
// model, and the materials must be the same.
StepMode g_modelModeB = StepMode::Original;
uint64_t g_modelsBCompared = 0;

std::vector<char> ModelMaterials(char* instance)
{
    std::vector<char> bytes;
    if (!instance)
        return bytes;
    char* materials = nullptr;
    CallThis<uint32_t>(At<uint32_t>(At<char*>(instance, 0), 0), instance, 0x26A68448u, &materials);
    if (!materials)
        return bytes;
    const uint32_t count = CallThis<uint32_t>(At<uint32_t>(At<char*>(materials, 0), 0xC), materials);
    for (uint32_t i = 0; i < count; ++i) {
        const char* const material =
            reinterpret_cast<const char*>(
                CallThis<uint32_t>(At<uint32_t>(At<char*>(materials, 0), 0x18), materials, i));
        bytes.insert(bytes.end(), material + 4, material + 12);
    }
    CallThis<uint32_t>(At<uint32_t>(At<char*>(materials, 0), 0x8), materials);
    return bytes;
}

uint32_t LoadModelBByMode(char* self, uint32_t which)
{
    if (g_modelModeB == StepMode::Original)
        return g_SC4EffectB_LoadModel(self, nullptr, which);
    if (g_modelModeB == StepMode::Ours)
        return LoadModelB(self, which);
    if (At<char*>(self, 0x220) || At<char*>(self, 0x224)) {
        compare::Record("effects.model_b.skipped", true, std::string(), std::string());
        return g_SC4EffectB_LoadModel(self, nullptr, which);
    }
    char before[kEffectSizeB], mine[kEffectSizeB], theirs[kEffectSizeB];
    std::memcpy(before, self, kEffectSizeB);
    LoadModelB(self, which);
    std::memcpy(mine, self, kEffectSizeB);
    char* const ourInstance = At<char*>(self, 0x220);
    char* const ourModel = At<char*>(self, 0x224);
    const std::vector<char> ourMaterials = ModelMaterials(ourInstance);
    if (ourInstance)
        CallThis<uint32_t>(At<uint32_t>(At<char*>(ourInstance, 0), 0x8), ourInstance);
    if (ourModel) {
        char* const part = ourModel + 4 + At<int32_t>(At<char*>(ourModel, 4), 4);
        CallThis<uint32_t>(At<uint32_t>(At<char*>(part, 0), 0x8), part);
    }
    std::memcpy(self, before, kEffectSizeB);
    const uint32_t result = g_SC4EffectB_LoadModel(self, nullptr, which);
    std::memcpy(theirs, self, kEffectSizeB);
    const std::vector<char> theirMaterials = ModelMaterials(At<char*>(self, 0x220));
    const bool sameModel = (ourInstance != nullptr) == (At<char*>(self, 0x220) != nullptr) &&
                           (ourModel != nullptr) == (At<char*>(self, 0x224) != nullptr);
    std::memset(mine + 0x220, 0, 8);
    std::memset(theirs + 0x220, 0, 8);
    ++g_modelsBCompared;
    if ((g_modelsBCompared & (g_modelsBCompared - 1)) == 0)
        log::Write("water effects: effects.model_b: %llu loads compared so far",
                   static_cast<unsigned long long>(g_modelsBCompared));
    const char* const id = theirMaterials.empty() ? "effects.model_b.none" : "effects.model_b";
    char text[64];
    if (!sameModel) {
        compare::Record(id, false, At<char*>(self, 0x220) ? "a model" : "no model",
                        ourInstance ? "a model" : "no model");
    } else if (theirMaterials != ourMaterials) {
        std::snprintf(text, sizeof text, "%zu materials ", theirMaterials.size() / 8);
        const std::string game = text + Hex(theirMaterials.data(), std::min<std::size_t>(theirMaterials.size(), 32));
        std::snprintf(text, sizeof text, "%zu materials ", ourMaterials.size() / 8);
        compare::Record(id, false, game,
                        text + Hex(ourMaterials.data(), std::min<std::size_t>(ourMaterials.size(), 32)));
    } else if (std::memcmp(theirs, mine, kEffectSizeB) != 0) {
        std::size_t at = 0;
        while (theirs[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "effect+%zX ", at);
        compare::Record(id, false, text + Hex(theirs + at, 8), text + Hex(mine + at, 8));
    } else {
        compare::Record(id, true, std::string(), std::string());
    }
    return result;
}

uint32_t __fastcall DSC4EffectB_LoadModel(char* self, void*, uint32_t which)
{
    if (!BNoting() || self != g_bSelf)
        return BBusy() ? g_SC4EffectB_LoadModel(self, nullptr, which) : LoadModelBByMode(self, which);
    const uint32_t result = BCallee(addr::SC4EffectB_LoadModel, which, 0, 0,
                                    [&] { return g_SC4EffectB_LoadModel(self, nullptr, which); });
    BSwap(At<char*>(self, 0x220), 0x40, reinterpret_cast<uint32_t>(&BInfo));
    return result;
}
OPENSC4_THUNK(TSC4EffectB_LoadModel, DSC4EffectB_LoadModel, 4)

uint32_t __fastcall DSC4EffectPartB_Attach(char* part, void*, uint32_t instance)
{
    if (!BNoting() || part != g_bSelf + kEffectPartB)
        return g_SC4EffectPartB_Attach(part, nullptr, instance);
    return BCallee(addr::SC4EffectPartB_Attach, instance, 0, 0,
                   [&] { return g_SC4EffectPartB_Attach(part, nullptr, instance); });
}
OPENSC4_THUNK(TSC4EffectPartB_Attach, DSC4EffectPartB_Attach, 4)

uint32_t __fastcall DSC4EffectPartB_Place(char* part, void*, uint32_t place)
{
    if (!BNoting() || part != g_bSelf + kEffectPartB)
        return g_SC4EffectPartB_Place(part, nullptr, place);
    return BCallee(addr::SC4EffectPartB_Place, 0, place, 12,
                   [&] { return g_SC4EffectPartB_Place(part, nullptr, place); });
}
OPENSC4_THUNK(TSC4EffectPartB_Place, DSC4EffectPartB_Place, 4)

uint32_t __fastcall DSC4EffectPartB_SetDescription(char* part, void*, uint32_t description)
{
    if (!BNoting() || part != g_bSelf + kEffectPartB)
        return BBusy() ? g_SC4EffectPartB_SetDescription(part, nullptr, description)
                       : PartDescriptionByMode(part, description);
    return BCallee(addr::SC4EffectPartB_SetDescription, description, 0, 0,
                   [&] { return g_SC4EffectPartB_SetDescription(part, nullptr, description); });
}
OPENSC4_THUNK(TSC4EffectPartB_SetDescription, DSC4EffectPartB_SetDescription, 4)

uint32_t __fastcall DescriptionBByMode(char* self, void*, uint32_t description)
{
    if (g_descModeB == StepMode::Original || g_effectRecord || g_effectReplay || g_descRecord || g_descReplay ||
        BBusy())
        return g_SC4EffectB_SetDescription(self, nullptr, description);
    if (g_descModeB == StepMode::Ours)
        return SetEffectDescriptionB(self, description);
    char* const generator = reinterpret_cast<char*>(kEffectGenerator);
    char before[kEffectSizeB], after[kEffectSizeB], mine[kEffectSizeB];
    char generatorBefore[kGeneratorSize], generatorAfter[kGeneratorSize];
    std::memcpy(before, self, kEffectSizeB);
    std::memcpy(generatorBefore, generator, kGeneratorSize);
    if (char* const model = At<char*>(self, 0x224)) {
        BSwap(At<char*>(self, 0x220), 0x8, reinterpret_cast<uint32_t>(&BRelease));
        BSwap(model + 4 + At<int32_t>(At<char*>(model, 4), 4), 0x8, reinterpret_cast<uint32_t>(&BRelease));
    } else {
        BSwap(At<char*>(self, 0x220), 0x8, reinterpret_cast<uint32_t>(&BRelease));
    }
    BSwap(At<char*>(self, 0x278), 0x20, reinterpret_cast<uint32_t>(&BEnd));
    g_bSelf = self;
    g_bRegions = {{self, kEffectSizeB}};
    g_bThread = GetCurrentThreadId();
    g_bGameCalls.clear();
    g_bRecord = true;
    const uint32_t result = g_SC4EffectB_SetDescription(self, nullptr, description);
    g_bRecord = false;
    std::memcpy(after, self, kEffectSizeB);
    std::memcpy(generatorAfter, generator, kGeneratorSize);
    std::memcpy(self, before, kEffectSizeB);
    std::memcpy(generator, generatorBefore, kGeneratorSize);
    g_bOurCalls.clear();
    g_bReplay = true;
    SetEffectDescriptionB(self, description);
    g_bReplay = false;
    std::memcpy(mine, self, kEffectSizeB);
    const bool sameGenerator = std::memcmp(generator, generatorAfter, kGeneratorSize) == 0;
    std::memcpy(self, after, kEffectSizeB);
    std::memcpy(generator, generatorAfter, kGeneratorSize);
    // What is still wearing a copy was not released: its own table back.
    BGiveBackOwn();
    ++g_bCompared;
    g_bCallsCompared += g_bGameCalls.size();
    if ((g_bCompared & (g_bCompared - 1)) == 0)
        log::Write("water effects: effects.description_b: %llu calls, %llu noted calls compared so far",
                   static_cast<unsigned long long>(g_bCompared), static_cast<unsigned long long>(g_bCallsCompared));
    const char* const id = g_bGameCalls.empty() ? "effects.description_b.none" : "effects.description_b";
    char text[96];
    if (!(g_bGameCalls == g_bOurCalls)) {
        std::size_t at = 0;
        while (at < g_bGameCalls.size() && at < g_bOurCalls.size() && g_bGameCalls[at] == g_bOurCalls[at])
            ++at;
        const auto describe = [&](const std::vector<BCall>& calls) {
            if (at >= calls.size()) {
                std::snprintf(text, sizeof text, "%zu calls", calls.size());
                return std::string(text);
            }
            std::snprintf(text, sizeof text, "call %zu of %zu: %08X (%08X) ", at, calls.size(), calls[at].target,
                          calls[at].a);
            return text + Hex(calls[at].data, sizeof calls[at].data);
        };
        const std::string game = describe(g_bGameCalls);
        compare::Record(id, false, game, describe(g_bOurCalls));
    } else if (!sameGenerator) {
        compare::Record(id, false, "generator", "generator differs");
    } else if (std::memcmp(after, mine, kEffectSizeB) != 0) {
        std::size_t at = 0;
        while (after[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "effect+%zX ", at);
        compare::Record(id, false, text + Hex(after + at, 8), text + Hex(mine + at, 8));
    } else {
        compare::Record(id, true, std::string(), std::string());
    }
    return result;
}
OPENSC4_THUNK(TSC4EffectB_SetDescription, DescriptionBByMode, 4)

// --- The particle pool ---------------------------------------------------------------------

// Particles are kept on a free list (0x00B0A688, linked through each one's
// first four bytes) of the pool at 0x00B0A67C. Three functions of the base
// effect class's sub-object (the effect + 4) use it, written here by hand.
constexpr std::uintptr_t kParticleFree = 0x00B0A688, kParticlePool = 0x00B0A67C, kFloatZero = 0x00B22D70;
constexpr std::size_t kParticleBytes = 0x44;
StepMode g_poolMode = StepMode::Original;
uint64_t g_poolCompared = 0;

void PoolCompared(const char* id, bool same, const std::string& game, const std::string& ours)
{
    ++g_poolCompared;
    if ((g_poolCompared & (g_poolCompared - 1)) == 0)
        log::Write("water effects: effects.pool: %llu calls compared so far",
                   static_cast<unsigned long long>(g_poolCompared));
    compare::Record(id, same, same ? std::string() : game, same ? std::string() : ours);
}

// Adding a particle to the effect's list (0x005B9F80, slot 0x2C): a new
// 12-byte node at the end of the list (+0x10), the count (+0x14) raised.
uint32_t AddParticle(char* base, uint32_t particle)
{
    char* const head = At<char*>(base, 0x10);
    char* const node = reinterpret_cast<char*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(12);
    At<uint32_t>(node, 8) = particle;
    char* const last = At<char*>(head, 4);
    At<char*>(node, 0) = head;
    At<char*>(node, 4) = last;
    At<char*>(last, 0) = node;
    At<char*>(head, 4) = node;
    return static_cast<uint32_t>(++At<int32_t>(base, 0x14));
}

// Giving a particle back (0x005B2240, slot 0x40): onto the free list.
uint32_t GiveBackParticle(uint32_t particle)
{
    if (particle) {
        M<uint32_t>(particle) = Global<uint32_t>(kParticleFree);
        Global<uint32_t>(kParticleFree) = particle;
    }
    return particle;
}

// Taking a particle (0x005B7C20, slot 0x3C): the first of the free list
// (after the pool has grown, 0x005A61B0, when the list is empty), cleared
// (0x005B9DF0), six of its floats set to zero, and started by the effect
// (0x005B7580), which the comparison only notes.
bool g_takeReplay = false;
uint32_t g_takeStarted = 0;

uint32_t TakeParticle(char* base)
{
    if (!Global<uint32_t>(kParticleFree))
        CallThis<void>(addr::SC4ParticlePool_Grow, reinterpret_cast<void*>(kParticlePool));
    const uint32_t particle = Global<uint32_t>(kParticleFree);
    Global<uint32_t>(kParticleFree) = M<uint32_t>(particle);
    CallThis<void>(addr::SC4Particle_Clear, reinterpret_cast<void*>(particle));
    for (const uint32_t offset : {0x1Cu, 0x20u, 0x24u, 0x38u, 0x3Cu, 0x40u})
        M<uint32_t>(particle + offset) = Global<uint32_t>(kFloatZero);
    if (g_takeReplay)
        g_takeStarted = particle;
    else
        CallThis<void>(addr::SC4Effect_StartParticle, base - 4, particle);
    return particle;
}

Call0Fn g_SC4EffectBase_TakeParticle = nullptr;
Call1Fn g_SC4EffectBase_AddParticle = nullptr, g_SC4EffectBase_GiveBackParticle = nullptr;

// The comparison of the take: the game's runs for real, and a detour on the
// start of the particle keeps the particle and the free list as they were on
// entry; then the free list and the particle are put back as before the
// take, ours runs up to that same point, the two states are compared, and
// what the game left is put back.
bool g_takeWatch = false;
uint32_t g_takeGameParticle = 0, g_takeGameFree = 0;
char g_takeGameBytes[kParticleBytes];

uint32_t __fastcall DStartParticle(char* effect, void*, uint32_t particle)
{
    if (g_takeWatch && !g_takeGameParticle) {
        g_takeGameParticle = particle;
        g_takeGameFree = Global<uint32_t>(kParticleFree);
        std::memcpy(g_takeGameBytes, reinterpret_cast<char*>(particle), kParticleBytes);
    }
    return StartByMode(effect, nullptr, particle);
}
OPENSC4_THUNK(TStartParticle, DStartParticle, 4)

uint32_t __fastcall TakeByMode(char* base, void*)
{
    const uint32_t first = Global<uint32_t>(kParticleFree);
    if (g_poolMode == StepMode::Original || g_effectReplay || (g_poolMode == StepMode::Compare && !first))
        return g_SC4EffectBase_TakeParticle(base, nullptr);
    if (g_poolMode == StepMode::Ours)
        return TakeParticle(base);
    char before[kParticleBytes], after[kParticleBytes], mine[kParticleBytes];
    char* const bytes = reinterpret_cast<char*>(first);
    std::memcpy(before, bytes, kParticleBytes);
    g_takeGameParticle = 0;
    g_takeWatch = true;
    const uint32_t result = g_SC4EffectBase_TakeParticle(base, nullptr);
    g_takeWatch = false;
    std::memcpy(after, bytes, kParticleBytes);
    const uint32_t freeAfter = Global<uint32_t>(kParticleFree);
    std::memcpy(bytes, before, kParticleBytes);
    Global<uint32_t>(kParticleFree) = first;
    g_takeStarted = 0;
    g_takeReplay = true;
    const uint32_t ourResult = TakeParticle(base);
    g_takeReplay = false;
    std::memcpy(mine, bytes, kParticleBytes);
    const uint32_t ourFree = Global<uint32_t>(kParticleFree);
    std::memcpy(bytes, after, kParticleBytes);
    Global<uint32_t>(kParticleFree) = freeAfter;
    const bool same = result == ourResult && g_takeStarted == g_takeGameParticle && ourFree == g_takeGameFree &&
                      std::memcmp(mine, g_takeGameBytes, kParticleBytes) == 0;
    char text[64];
    std::snprintf(text, sizeof text, "particle %08X, started %08X, free %08X ", result, g_takeGameParticle,
                  g_takeGameFree);
    const std::string game = text + Hex(g_takeGameBytes, kParticleBytes);
    std::snprintf(text, sizeof text, "particle %08X, started %08X, free %08X ", ourResult, g_takeStarted, ourFree);
    PoolCompared("effects.pool.take", same, game, text + Hex(mine, kParticleBytes));
    return result;
}

// The comparison of the add: ours adds a node, what the list then looks
// like is noted, the node is taken off again and freed, and the game's adds
// its own; the two shapes are compared (the nodes' addresses may differ).
std::string ListEnd(char* base, uint32_t particle, const char* lastBefore)
{
    char* const head = At<char*>(base, 0x10);
    char* const node = At<char*>(head, 4);
    char text[96];
    std::snprintf(text, sizeof text, "count %d, particle %d, next %d, previous %d, linked %d", At<int32_t>(base, 0x14),
                  At<uint32_t>(node, 8) == particle, At<char*>(node, 0) == head, At<char*>(node, 4) == lastBefore,
                  At<char*>(const_cast<char*>(lastBefore), 0) == node);
    return text;
}

uint32_t __fastcall AddByMode(char* base, void*, uint32_t particle)
{
    if (g_poolMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4EffectBase_AddParticle(base, nullptr, particle);
    if (g_poolMode == StepMode::Ours)
        return AddParticle(base, particle);
    char* const head = At<char*>(base, 0x10);
    char* const last = At<char*>(head, 4);
    AddParticle(base, particle);
    const std::string ours = ListEnd(base, particle, last);
    char* const node = At<char*>(head, 4);
    At<char*>(last, 0) = head;
    At<char*>(head, 4) = last;
    --At<int32_t>(base, 0x14);
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(node);
    const uint32_t result = g_SC4EffectBase_AddParticle(base, nullptr, particle);
    const std::string game = ListEnd(base, particle, last);
    PoolCompared("effects.pool.add", game == ours, game, ours);
    return result;
}

uint32_t __fastcall GiveBackByMode(char* base, void*, uint32_t particle)
{
    if (g_poolMode == StepMode::Original || g_effectReplay || !particle)
        return g_SC4EffectBase_GiveBackParticle(base, nullptr, particle);
    if (g_poolMode == StepMode::Ours)
        return GiveBackParticle(particle);
    const uint32_t freeBefore = Global<uint32_t>(kParticleFree), linkBefore = M<uint32_t>(particle);
    GiveBackParticle(particle);
    const uint32_t ourFree = Global<uint32_t>(kParticleFree), ourLink = M<uint32_t>(particle);
    Global<uint32_t>(kParticleFree) = freeBefore;
    M<uint32_t>(particle) = linkBefore;
    const uint32_t result = g_SC4EffectBase_GiveBackParticle(base, nullptr, particle);
    char text[48];
    std::snprintf(text, sizeof text, "free %08X, link %08X", Global<uint32_t>(kParticleFree), M<uint32_t>(particle));
    const std::string game = text;
    std::snprintf(text, sizeof text, "free %08X, link %08X", ourFree, ourLink);
    PoolCompared("effects.pool.giveback", game == text, game, text);
    return result;
}

uint32_t __fastcall DSC4EffectBase_TakeParticle(char* base, void*)
{
    return EffectRecorded(addr::SC4EffectBase_TakeParticle, &TakeByMode, base);
}
uint32_t __fastcall DSC4EffectBase_AddParticle(char* base, void*, uint32_t particle)
{
    return EffectRecorded(addr::SC4EffectBase_AddParticle, &AddByMode, base, particle);
}
uint32_t __fastcall DSC4EffectBase_GiveBackParticle(char* base, void*, uint32_t particle)
{
    return EffectRecorded(addr::SC4EffectBase_GiveBackParticle, &GiveBackByMode, base, particle);
}
OPENSC4_THUNK(TSC4EffectBase_TakeParticle, DSC4EffectBase_TakeParticle, 0)
OPENSC4_THUNK(TSC4EffectBase_AddParticle, DSC4EffectBase_AddParticle, 4)
OPENSC4_THUNK(TSC4EffectBase_GiveBackParticle, DSC4EffectBase_GiveBackParticle, 4)

// The second effect class taking a particle (0x005AEF20, slot 0x3C of its
// sub-object), translated instruction by instruction. Its particles are
// 0x54 bytes, from a pool of their own (0x00B0A5C0, free list 0x00B0A5CC):
// the first of the list is cleared (0x005ADCB0) and started as any
// particle. One that is running then gets its two words at +0x48 zeroed
// and an angle at +0x50 (from the effect's matrix, or -999 for some kinds
// of the description's +0xD5), and, when the description names one
// (+0x160), an effect of its own at +0x44: made by the effects manager
// (0x00B43D1C, slot 0x1C) by that name, its slot 0x34 called with 0xD and
// the effect's +0x270 when the byte +0x26F is set, and its slot 0xC called
// with the effect's +0x188.
uint32_t TakeParticleB(char* base)
{
    uint32_t stackmem[192] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(base);
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
#include "core/effect_take_b.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

constexpr std::uintptr_t kParticleFreeB = 0x00B0A5CC;
StepMode g_poolModeB = StepMode::Original;
Call0Fn g_SC4EffectBaseB_TakeParticle = nullptr;

// Its comparison runs both for real, from the same particle, free list,
// effect and generator: the game's first; then ours, whose particle must be
// the same but for the effect of its own (one each, or none each), which
// is taken off as giving the particle back would (slot 0x10 with the
// effect's +0x188, then released). What the game's left is put back.
uint32_t __fastcall TakeBByMode(char* base, void*)
{
    const uint32_t first = Global<uint32_t>(kParticleFreeB);
    if (g_poolModeB == StepMode::Original || g_effectRecord || g_effectReplay ||
        (g_poolModeB == StepMode::Compare && !first))
        return g_SC4EffectBaseB_TakeParticle(base, nullptr);
    if (g_poolModeB == StepMode::Ours)
        return TakeParticleB(base);
    char* const self = base - 4;
    char* const generator = reinterpret_cast<char*>(kEffectGenerator);
    char* const bytes = reinterpret_cast<char*>(first);
    constexpr std::size_t kEffectAt = kParticleBytesB, kGeneratorAt = kEffectAt + kEffectSize,
                          kFreeAt = kGeneratorAt + kGeneratorSize, kTotal = kFreeAt + 4;
    char before[kTotal], mine[kTotal], theirs[kTotal];
    const auto save = [&](char* to) {
        std::memcpy(to, bytes, kParticleBytesB);
        std::memcpy(to + kEffectAt, self, kEffectSize);
        std::memcpy(to + kGeneratorAt, generator, kGeneratorSize);
        std::memcpy(to + kFreeAt, reinterpret_cast<const void*>(kParticleFreeB), 4);
    };
    const auto restore = [&](const char* from) {
        std::memcpy(bytes, from, kParticleBytesB);
        std::memcpy(self, from + kEffectAt, kEffectSize);
        std::memcpy(generator, from + kGeneratorAt, kGeneratorSize);
        std::memcpy(reinterpret_cast<void*>(kParticleFreeB), from + kFreeAt, 4);
    };
    save(before);
    const uint32_t result = g_SC4EffectBaseB_TakeParticle(base, nullptr);
    save(theirs);
    restore(before);
    const uint32_t ourResult = TakeParticleB(base);
    save(mine);
    char* const child = At<char*>(bytes, kParticleChild);
    if (child) {
        CallThis<void>(At<uint32_t>(At<char*>(child, 0), 0x10), child, At<uint32_t>(self, 0x188));
        CallThis<void>(At<uint32_t>(At<char*>(child, 0), 0x8), child);
    }
    restore(theirs);
    const bool gameChild = At<uint32_t>(theirs, kParticleChild) != 0, ourChild = child != nullptr;
    static uint64_t withChild = 0;
    if (gameChild)
        ++withChild;
    if (gameChild && (withChild & (withChild - 1)) == 0)
        log::Write("water effects: effects.pool_b.take: %llu particles with an effect of their own so far",
                   static_cast<unsigned long long>(withChild));
    std::memset(theirs + kParticleChild, 0, 4);
    std::memset(mine + kParticleChild, 0, 4);
    const bool same = result == ourResult && gameChild == ourChild && std::memcmp(theirs, mine, kTotal) == 0;
    if (same) {
        PoolCompared("effects.pool_b.take", true, std::string(), std::string());
    } else {
        std::size_t at = 0;
        while (at < kTotal && theirs[at] == mine[at])
            ++at;
        at = at < kTotal ? at & ~std::size_t{3} : 0;
        const char* const part =
            at < kEffectAt ? "particle" : at < kGeneratorAt ? "effect" : at < kFreeAt ? "generator" : "free";
        const std::size_t offset =
            at < kEffectAt ? at : at < kGeneratorAt ? at - kEffectAt : at < kFreeAt ? at - kGeneratorAt : 0;
        char text[80];
        std::snprintf(text, sizeof text, "%08X child %d %s+%zX ", result, gameChild, part, offset);
        const std::string game = text + Hex(theirs + at, 4);
        std::snprintf(text, sizeof text, "%08X child %d %s+%zX ", ourResult, ourChild, part, offset);
        PoolCompared("effects.pool_b.take", false, game, text + Hex(mine + at, 4));
    }
    return result;
}

uint32_t __fastcall DSC4EffectBaseB_TakeParticle(char* base, void*)
{
    return EffectRecorded(addr::SC4EffectBaseB_TakeParticle, &TakeBByMode, base);
}
OPENSC4_THUNK(TSC4EffectBaseB_TakeParticle, DSC4EffectBaseB_TakeParticle, 0)

// The second class given a particle back (0x005AF390, slot 0x40 of its
// sub-object), translated instruction by instruction. An effect with an
// object at +0x274 whose particle has not outlived its life starts the
// particle again instead (0x005AF060). Otherwise the particle's own effect
// is stopped (its slot 0x10 with the effect's +0x188) and released; an
// effect with its byte +0x26A set and nothing at +0x188 has the effects
// manager make the effect named after it with "_death" where the particle
// was (placed by a block built from the particle and the effect's matrix,
// its slot 0x34 called when the byte +0x26B is set, started with 0, and
// let go); and the particle goes back on the free list (0x00B0A5CC). Last,
// an effect with the object at +0x274, no particle left and its byte +0xC
// set tells that object (its slot 4 with 0).
uint32_t GiveBackParticleB(char* base, uint32_t particle)
{
    uint32_t stackmem[320] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(base);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[224]);
    M<uint32_t>(esp + 4) = particle;
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
#include "core/effect_giveback_b.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// Its comparison is the one of the class taking a description: the game's
// first, with its calls noted, then ours replayed from the notes. Noted
// here: the restart (a detour), the particle's own effect stopped and
// released, the object at +0x274 told, and the effects manager asked to
// make an effect, all through copies of their tables of functions; the
// effect the manager makes gets a copy too, so what is done to it is seen.
// The bytes a noted call may change are the effect's, the particle's and
// the free list's head.
constexpr std::size_t kManagerSlotsB = 1024;
std::vector<uint32_t> g_bManagerTable;
uint32_t g_bManagerTableReal = 0, g_bManagerReal = 0;
StepMode g_giveBackModeB = StepMode::Original;
Call1Fn g_SC4EffectBaseB_GiveBackParticle = nullptr, g_SC4EffectB_RestartParticle = nullptr;
uint64_t g_giveBacksBCompared = 0, g_giveBackBCallsCompared = 0;

uint32_t __fastcall BChildStop(char* object, void*, uint32_t owner)
{
    const uint32_t real = BRealSlot(object, 0x10);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, owner);
    return BCallee(0x10, reinterpret_cast<uint32_t>(object), reinterpret_cast<uint32_t>(&owner), 4,
                   [&] { return CallThis<uint32_t>(real, object, owner); });
}

uint32_t __fastcall BOwnerTell(char* object, void*, uint32_t value)
{
    const uint32_t real = BRealSlot(object, 0x4);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, value);
    return BCallee(0x4, reinterpret_cast<uint32_t>(object), reinterpret_cast<uint32_t>(&value), 4,
                   [&] { return CallThis<uint32_t>(real, object, value); });
}

uint32_t __fastcall BMadeBlock(char* object, void*, uint32_t block)
{
    const uint32_t real = BRealSlot(object, 0x20);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, block);
    return BCallee(0x2020, reinterpret_cast<uint32_t>(object), block, kBBlock,
                   [&] { return CallThis<uint32_t>(real, object, block); });
}

uint32_t __fastcall BMadeValue(char* object, void*, uint32_t which, uint32_t values, uint32_t count)
{
    const uint32_t real = BRealSlot(object, 0x34);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, which, values, count);
    return BCallee(0x2034, reinterpret_cast<uint32_t>(object), values, 4,
                   [&] { return CallThis<uint32_t>(real, object, which, values, count); });
}

uint32_t __fastcall BMadeStart(char* object, void*, uint32_t owner)
{
    const uint32_t real = BRealSlot(object, 0xC);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, owner);
    return BCallee(0x200C, reinterpret_cast<uint32_t>(object), reinterpret_cast<uint32_t>(&owner), 4,
                   [&] { return CallThis<uint32_t>(real, object, owner); });
}

uint32_t __fastcall BMadeRelease(char* object, void*)
{
    const uint32_t real = BRealSlot(object, 0x8);
    if (!BNoting()) {
        BGiveBack(object, true);
        return CallThis<uint32_t>(real, object);
    }
    return BCallee(0x2008, reinterpret_cast<uint32_t>(object), 0, 0, [&] {
        BGiveBack(object, true);
        return CallThis<uint32_t>(real, object);
    });
}

uint32_t __fastcall BManagerMake(char* manager, void*, uint32_t name, uint32_t out)
{
    const uint32_t real = M<uint32_t>(g_bManagerTableReal + 0x1C);
    if (!BNoting())
        return CallThis<uint32_t>(real, manager, name, out);
    const uint32_t result = BCallee(0x1C, 0, name, kBString,
                                    [&] { return CallThis<uint32_t>(real, manager, name, out); }, out);
    if ((result & 0xFF) && M<char*>(out))
        BSwapRole(M<char*>(out), 0x2000,
                  {{0x20, reinterpret_cast<uint32_t>(&BMadeBlock)},
                   {0x34, reinterpret_cast<uint32_t>(&BMadeValue)},
                   {0xC, reinterpret_cast<uint32_t>(&BMadeStart)},
                   {0x8, reinterpret_cast<uint32_t>(&BMadeRelease)}});
    return result;
}

void BSwapManager(bool in)
{
    char* const manager = Global<char*>(0x00B43D1C);
    if (!manager)
        return;
    if (!in) {
        if (g_bManagerReal)
            At<uint32_t>(manager, 0) = g_bManagerReal;
        g_bManagerReal = 0;
        return;
    }
    const uint32_t real = At<uint32_t>(manager, 0);
    if (g_bManagerTableReal != real) {
        g_bManagerTable.assign(reinterpret_cast<const uint32_t*>(real),
                               reinterpret_cast<const uint32_t*>(real) + kManagerSlotsB);
        g_bManagerTable[0x1C / 4] = reinterpret_cast<uint32_t>(&BManagerMake);
        g_bManagerTableReal = real;
    }
    g_bManagerReal = real;
    At<uint32_t*>(manager, 0) = g_bManagerTable.data();
}

uint32_t __fastcall DSC4EffectB_RestartParticle(char* self, void*, uint32_t particle)
{
    if (!BNoting())
        return g_SC4EffectB_RestartParticle(self, nullptr, particle);
    return BCallee(addr::SC4EffectB_RestartParticle, particle, 0, 0,
                   [&] { return g_SC4EffectB_RestartParticle(self, nullptr, particle); });
}
OPENSC4_THUNK(TSC4EffectB_RestartParticle, DSC4EffectB_RestartParticle, 4)

uint32_t __fastcall GiveBackBByMode(char* base, void*, uint32_t particle)
{
    if (g_giveBackModeB == StepMode::Original || g_effectRecord || g_effectReplay || g_descRecord || g_descReplay ||
        BBusy())
        return g_SC4EffectBaseB_GiveBackParticle(base, nullptr, particle);
    if (g_giveBackModeB == StepMode::Ours)
        return GiveBackParticleB(base, particle);
    char* const self = base - 4;
    char* const bytes = reinterpret_cast<char*>(particle);
    g_bSelf = self;
    g_bRegions = {{self, kEffectSizeB}, {bytes, kParticleBytesB}, {reinterpret_cast<char*>(kParticleFreeB), 4}};
    g_bThread = GetCurrentThreadId();
    const std::vector<char> before = BSaveRegions();
    BSwapRole(At<char*>(bytes, kParticleChild), 0x1000,
              {{0x10, reinterpret_cast<uint32_t>(&BChildStop)}, {0x8, reinterpret_cast<uint32_t>(&BRelease)}});
    BSwap(At<char*>(base, 0x274), 0x4, reinterpret_cast<uint32_t>(&BOwnerTell));
    BSwapManager(true);
    g_bGameCalls.clear();
    g_bRecord = true;
    const uint32_t result = g_SC4EffectBaseB_GiveBackParticle(base, nullptr, particle);
    g_bRecord = false;
    const std::vector<char> after = BSaveRegions();
    for (std::size_t offset = 0; offset < before.size(); ++offset)
        *BRegionByte(offset) = before[offset];
    g_bOurCalls.clear();
    g_bReplay = true;
    GiveBackParticleB(base, particle);
    g_bReplay = false;
    const std::vector<char> mine = BSaveRegions();
    for (std::size_t offset = 0; offset < after.size(); ++offset)
        *BRegionByte(offset) = after[offset];
    BSwapManager(false);
    BGiveBackOwn();
    ++g_giveBacksBCompared;
    g_giveBackBCallsCompared += g_bGameCalls.size();
    if ((g_giveBacksBCompared & (g_giveBacksBCompared - 1)) == 0)
        log::Write("water effects: effects.giveback_b: %llu calls, %llu noted calls compared so far",
                   static_cast<unsigned long long>(g_giveBacksBCompared),
                   static_cast<unsigned long long>(g_giveBackBCallsCompared));
    // One name for each way it can go, so the totals show which a city has.
    bool restarted = false, made = false;
    for (const BCall& call : g_bGameCalls) {
        restarted = restarted || call.target == addr::SC4EffectB_RestartParticle;
        made = made || call.target == 0x1C;
    }
    const char* const id = restarted ? "effects.giveback_b.restart"
                           : made    ? "effects.giveback_b.death"
                                     : "effects.giveback_b";
    char text[96];
    if (!(g_bGameCalls == g_bOurCalls)) {
        std::size_t at = 0;
        while (at < g_bGameCalls.size() && at < g_bOurCalls.size() && g_bGameCalls[at] == g_bOurCalls[at])
            ++at;
        const auto describe = [&](const std::vector<BCall>& calls) {
            if (at >= calls.size()) {
                std::snprintf(text, sizeof text, "%zu calls", calls.size());
                return std::string(text);
            }
            std::snprintf(text, sizeof text, "call %zu of %zu: %08X (%08X) ", at, calls.size(), calls[at].target,
                          calls[at].a);
            return text + Hex(calls[at].data, sizeof calls[at].data);
        };
        const std::string game = describe(g_bGameCalls);
        compare::Record(id, false, game, describe(g_bOurCalls));
    } else if (after != mine) {
        std::size_t at = 0;
        while (after[at] == mine[at])
            ++at;
        const char* const part =
            at < kEffectSizeB ? "effect" : at < kEffectSizeB + kParticleBytesB ? "particle" : "free";
        const std::size_t offset =
            (at < kEffectSizeB ? at : at < kEffectSizeB + kParticleBytesB ? at - kEffectSizeB : 0) & ~std::size_t{3};
        std::snprintf(text, sizeof text, "%s+%zX %02X", part, offset, static_cast<unsigned char>(after[at]));
        const std::string game = text;
        std::snprintf(text, sizeof text, "%s+%zX %02X", part, offset, static_cast<unsigned char>(mine[at]));
        compare::Record(id, false, game, text);
    } else {
        compare::Record(id, true, std::string(), std::string());
    }
    return result;
}

uint32_t __fastcall DSC4EffectBaseB_GiveBackParticle(char* base, void*, uint32_t particle)
{
    return EffectRecorded(addr::SC4EffectBaseB_GiveBackParticle, &GiveBackBByMode, base, particle);
}
OPENSC4_THUNK(TSC4EffectBaseB_GiveBackParticle, DSC4EffectBaseB_GiveBackParticle, 4)

// Two setters of what an effect holds, written by hand. Both do nothing
// when given what is held already; else the new one is held first, kept,
// and the old one let go. The effect's object at +0x1EC (0x005B26B0; the
// effects manager calls it) is held and let go through its own slots 4 and
// 8; the description of the second class's part (0x005AC110, the part's
// +0x40) through SC4Effect::Hold and SC4EffectDescription::Release.
uint32_t SetEffectObject(char* self, uint32_t object)
{
    const uint32_t old = At<uint32_t>(self, 0x1EC);
    if (object == old)
        return object;
    if (object) {
        char* const held = reinterpret_cast<char*>(object);
        BOurs(0x1004, object, 0, 0, [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(held, 0), 0x4), held); });
    }
    At<uint32_t>(self, 0x1EC) = object;
    if (old) {
        char* const let = reinterpret_cast<char*>(old);
        return BOurs(0x8, old, 0, 0, [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(let, 0), 0x8), let); });
    }
    return object;
}

uint32_t SetPartDescription(char* part, uint32_t description)
{
    const uint32_t old = At<uint32_t>(part, 0x40);
    if (description == old)
        return description;
    if (description)
        BOurs(addr::SC4Effect_Hold, description, 0, 0,
              [&] { return CallThis<uint32_t>(addr::SC4Effect_Hold, reinterpret_cast<char*>(description)); });
    At<uint32_t>(part, 0x40) = description;
    if (old)
        return BOurs(addr::SC4EffectDescription_Release, old, 0, 0, [&] {
            return CallThis<uint32_t>(addr::SC4EffectDescription_Release, reinterpret_cast<char*>(old));
        });
    return description;
}

// Their comparison is the noted one, the game's first: the holding and the
// letting go are noted (the object's through copies of the two objects'
// tables of functions, the description's through the detours those two
// functions have), then the field is put back and ours is replayed.
StepMode g_holdersMode = StepMode::Original;
Call1Fn g_SC4Effect_SetObject = nullptr;

uint32_t __fastcall BAddRef(char* object, void*)
{
    const uint32_t real = BRealSlot(object, 0x4);
    if (!BNoting())
        return CallThis<uint32_t>(real, object);
    return BCallee(0x1004, reinterpret_cast<uint32_t>(object), 0, 0, [&] { return CallThis<uint32_t>(real, object); });
}

template <typename Game, typename Ours>
uint32_t HolderCompared(const char* id, char* at, std::size_t size, Game&& game, Ours&& ours)
{
    g_bSelf = at;
    g_bRegions = {{at, size}};
    g_bThread = GetCurrentThreadId();
    const std::vector<char> before = BSaveRegions();
    g_bGameCalls.clear();
    g_bRecord = true;
    const uint32_t result = game();
    g_bRecord = false;
    const std::vector<char> after = BSaveRegions();
    std::memcpy(at, before.data(), size);
    g_bOurCalls.clear();
    g_bReplay = true;
    ours();
    g_bReplay = false;
    const std::vector<char> mine = BSaveRegions();
    std::memcpy(at, after.data(), size);
    BGiveBackOwn();
    const bool same = g_bGameCalls == g_bOurCalls && after == mine;
    char text[48];
    std::snprintf(text, sizeof text, "%zu calls", g_bGameCalls.size());
    const std::string theirs = text;
    std::snprintf(text, sizeof text, "%zu calls%s", g_bOurCalls.size(), after == mine ? "" : ", bytes differ");
    compare::Record(id, same, same ? std::string() : theirs, same ? std::string() : std::string(text));
    return result;
}

uint32_t SetObjectRun(char* self, uint32_t object)
{
    if (g_holdersMode == StepMode::Original || g_effectRecord || g_effectReplay || g_descRecord || g_descReplay ||
        BBusy())
        return g_SC4Effect_SetObject(self, nullptr, object);
    if (g_holdersMode == StepMode::Ours)
        return SetEffectObject(self, object);
    const uint32_t old = At<uint32_t>(self, 0x1EC);
    if (object != old) {
        BSwap(reinterpret_cast<char*>(object), 0x4, reinterpret_cast<uint32_t>(&BAddRef));
        BSwap(reinterpret_cast<char*>(old), 0x8, reinterpret_cast<uint32_t>(&BRelease));
    }
    return HolderCompared(
        object == old ? "effects.holders.object.same" : "effects.holders.object", self, 0x1F0,
        [&] { return g_SC4Effect_SetObject(self, nullptr, object); }, [&] { return SetEffectObject(self, object); });
}
bool MxNoting();
uint32_t MxSetObjectNoted(char* self, uint32_t object, uint32_t (*run)(char*, uint32_t));

// While a maker is noted, its call is noted too.
uint32_t __fastcall SetObjectByMode(char* self, void*, uint32_t object)
{
    return MxNoting() ? MxSetObjectNoted(self, object, &SetObjectRun) : SetObjectRun(self, object);
}
OPENSC4_THUNK(TSC4Effect_SetObject, SetObjectByMode, 4)

uint32_t PartDescriptionByMode(char* part, uint32_t description)
{
    if (g_holdersMode == StepMode::Original || g_effectRecord || g_effectReplay || g_descRecord || g_descReplay)
        return g_SC4EffectPartB_SetDescription(part, nullptr, description);
    if (g_holdersMode == StepMode::Ours)
        return SetPartDescription(part, description);
    return HolderCompared(
        description == At<uint32_t>(part, 0x40) ? "effects.holders.part.same" : "effects.holders.part", part, 0x44,
        [&] { return g_SC4EffectPartB_SetDescription(part, nullptr, description); },
        [&] { return SetPartDescription(part, description); });
}
OPENSC4_EFFECT_HOOK1(SC4Effect_SpawnFromParticle)

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
    // The function compared may itself be reached through a recording
    // detour, which counts as one level of nesting: the calls it makes are
    // the ones to record.
    const int nested = g_effectNested;
    g_effectNested = 0;
    g_effectRecord = true;
    const uint32_t result = game();
    g_effectRecord = false;
    g_effectNested = nested;
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

// Whether an effect is over (0x005B9D40, on the sub-object at the effect's
// +4; the effect's draw asks it last): with a particle left (+0x14) it is
// not. With none, an effect marked at +0xC has its two marks cleared (its
// slot 0x20 with two zeros), and one marked only at +0xD lets go of what it
// holds (slot 0x14) and is released (slot 8); either way the answer is true.
uint32_t FinishEffect(char* base)
{
    if (At<int32_t>(base, 0x14) != 0)
        return 0;
    if (At<uint8_t>(base, 0xC)) {
        CallThisTraced(At<uint32_t>(At<char*>(base, 0), 0x20), base, 0u, 0u);
        return 1;
    }
    if (!At<uint8_t>(base, 0xD))
        return 0;
    CallThisTraced(At<uint32_t>(At<char*>(base, 0), 0x14), base);
    CallThisTraced(At<uint32_t>(At<char*>(base, 0), 0x8), base);
    return 1;
}

StepMode g_finishMode = StepMode::Original;

uint32_t __fastcall FinishByMode(char* base, void*)
{
    if (g_finishMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4EffectBase_Finish(base, nullptr);
    if (g_finishMode == StepMode::Ours)
        return FinishEffect(base);
    return RecordAndReplay(
        "effects.finish", base - 4, true, [&] { return g_SC4EffectBase_Finish(base, nullptr); },
        [](void* b) { return FinishEffect(static_cast<char*>(b)); }, base);
}

uint32_t __fastcall DSC4EffectBase_Finish(char* base, void*)
{
    return EffectRecorded(addr::SC4EffectBase_Finish, &FinishByMode, base);
}
OPENSC4_THUNK(TSC4EffectBase_Finish, DSC4EffectBase_Finish, 0)

// An effect stopped outright (0x005B9CC0, on the sub-object; letting go of
// what an effect holds ends in it): for one that is in use (+4), the two
// marks are cleared, it is told to end (slot 0x20 with two zeros, which
// gives every particle back) and +4 is cleared.
uint32_t StopEffect(char* base)
{
    if (!At<uint8_t>(base, 4))
        return 0;
    At<uint8_t>(base, 0xC) = 0;
    At<uint8_t>(base, 0xD) = 0;
    const uint32_t result = CallThisTraced(At<uint32_t>(At<char*>(base, 0), 0x20), base, 0u, 0u);
    At<uint8_t>(base, 4) = 0;
    return result;
}

StepMode g_stopMode = StepMode::Original;
Call0Fn g_SC4EffectBase_Stop = nullptr;

uint32_t __fastcall StopByMode(char* base, void*)
{
    if (g_stopMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4EffectBase_Stop(base, nullptr);
    if (g_stopMode == StepMode::Ours)
        return StopEffect(base);
    return RecordAndReplay(
        At<uint8_t>(base, 4) ? "effects.stop" : "effects.stop.unused", base - 4, false,
        [&] { return g_SC4EffectBase_Stop(base, nullptr); },
        [](void* b) { return StopEffect(static_cast<char*>(b)); }, base);
}
uint32_t __fastcall DSC4EffectBase_Stop(char* base, void*)
{
    return EffectRecorded(addr::SC4EffectBase_Stop, &StopByMode, base);
}
OPENSC4_THUNK(TSC4EffectBase_Stop, DSC4EffectBase_Stop, 0)

// Letting go of what an effect holds (0x005B2440, slot 0x14 of the base
// class's sub-object): the object at +0x1EC is released (its slot 8), the
// description at +0x20 let go (0x005B1C80) and the object at +0x1E8
// released, each field cleared first; then the effect is stopped outright
// (a jump to 0x005B9CC0).
std::vector<char*> g_heldGame, g_heldOurs;

void ReleaseHeld(char* object)
{
    if (g_effectReplay)
        g_heldOurs.push_back(object);
    else
        CallThis<void>(At<uint32_t>(At<char*>(object, 0), 0x8), object);
}

uint32_t LetGoEffect(char* base)
{
    if (char* const object = At<char*>(base, 0x1EC)) {
        At<char*>(base, 0x1EC) = nullptr;
        ReleaseHeld(object);
    }
    if (char* const description = At<char*>(base, 0x20)) {
        At<char*>(base, 0x20) = nullptr;
        CallThisTraced(addr::SC4EffectDescription_Release, description);
    }
    if (char* const object = At<char*>(base, 0x1E8)) {
        At<char*>(base, 0x1E8) = nullptr;
        ReleaseHeld(object);
    }
    return CallThisTraced(addr::SC4EffectBase_Stop, base);
}

// Its comparison is the record and replay of the effect's draw, with the
// two releases seen as well: for the game's run each of the two objects is
// given a copy of its table of functions whose slot 8 notes the release,
// gives the object its own table back and passes the release on; ours in
// the replay only notes which objects it would release. Other threads'
// calls through the copy pass straight on.
constexpr std::size_t kHeldSlots = 512;
std::map<uint32_t, std::vector<uint32_t>> g_heldTables;
std::unordered_map<char*, uint32_t> g_heldReal;
std::unordered_map<char*, int> g_heldTimes;
StepMode g_letGoMode = StepMode::Original;
Call0Fn g_SC4EffectBase_LetGo = nullptr;

uint32_t __fastcall HeldRelease(char* object, void*)
{
    const auto found = g_heldReal.find(object);
    if (found == g_heldReal.end() || GetCurrentThreadId() != g_effectThread) {
        // Not one of ours to note (or another thread's): its real table is the one the copy was made from.
        for (const auto& [real, table] : g_heldTables) {
            if (At<uint32_t*>(object, 0) == table.data())
                return CallThis<uint32_t>(M<uint32_t>(real + 0x8), object);
        }
        return 0;
    }
    const uint32_t real = found->second;
    g_heldGame.push_back(object);
    // An object held twice keeps the copy until its last release here.
    if (--g_heldTimes[object] == 0) {
        At<uint32_t>(object, 0) = real;
        g_heldReal.erase(found);
    }
    return CallThis<uint32_t>(M<uint32_t>(real + 0x8), object);
}

void SwapHeld(char* object)
{
    if (!object)
        return;
    if (g_heldReal.count(object)) {
        ++g_heldTimes[object];
        return;
    }
    g_heldTimes[object] = 1;
    const uint32_t real = At<uint32_t>(object, 0);
    std::vector<uint32_t>& table = g_heldTables[real];
    if (table.empty()) {
        table.assign(reinterpret_cast<const uint32_t*>(real), reinterpret_cast<const uint32_t*>(real) + kHeldSlots);
        table[0x8 / 4] = reinterpret_cast<uint32_t>(&HeldRelease);
    }
    g_heldReal[object] = real;
    At<uint32_t*>(object, 0) = table.data();
}

uint32_t __fastcall LetGoByMode(char* base, void*)
{
    if (g_letGoMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4EffectBase_LetGo(base, nullptr);
    if (g_letGoMode == StepMode::Ours)
        return LetGoEffect(base);
    g_heldGame.clear();
    g_heldOurs.clear();
    g_effectThread = GetCurrentThreadId();
    SwapHeld(At<char*>(base, 0x1EC));
    SwapHeld(At<char*>(base, 0x1E8));
    const uint32_t result = RecordAndReplay(
        "effects.letgo", base - 4, false, [&] { return g_SC4EffectBase_LetGo(base, nullptr); },
        [](void* b) { return LetGoEffect(static_cast<char*>(b)); }, base);
    for (const auto& [object, real] : g_heldReal)
        At<uint32_t>(object, 0) = real;
    g_heldReal.clear();
    char text[48];
    std::snprintf(text, sizeof text, "%zu released", g_heldGame.size());
    const std::string game = text;
    std::snprintf(text, sizeof text, "%zu released", g_heldOurs.size());
    const bool same = g_heldGame == g_heldOurs;
    compare::Record(g_heldGame.empty() ? "effects.letgo.released.none" : "effects.letgo.released", same,
                    same ? std::string() : game, same ? std::string() : std::string(text));
    return result;
}

uint32_t __fastcall DSC4EffectBase_LetGo(char* base, void*)
{
    return EffectRecorded(addr::SC4EffectBase_LetGo, &LetGoByMode, base);
}
OPENSC4_THUNK(TSC4EffectBase_LetGo, DSC4EffectBase_LetGo, 0)

// The second class's version (0x005AC140, slot 0x14 of its sub-object),
// for an effect in use (+4): its model instance (+0x21C) is released, and
// its model (+0x220) through the part of it that counts its holders (a
// pointer worked out from the model's own table of offsets); then the
// base class's.
uint32_t LetGoEffectB(char* base)
{
    if (!At<uint8_t>(base, 4))
        return 0;
    if (char* const instance = At<char*>(base, 0x21C)) {
        At<char*>(base, 0x21C) = nullptr;
        ReleaseHeld(instance);
    }
    if (char* const model = At<char*>(base, 0x220)) {
        At<char*>(base, 0x220) = nullptr;
        ReleaseHeld(model + 4 + At<int32_t>(At<char*>(model, 4), 4));
    }
    return CallThisTraced(addr::SC4EffectBase_LetGo, base);
}

// Its comparison is the base class's. The two fields are past the bytes
// the record and replay keeps of an effect, so they are kept here: put
// back for ours' run, checked, and left as the game's left them.
StepMode g_letGoModeB = StepMode::Original;
Call0Fn g_SC4EffectBaseB_LetGo = nullptr;
struct LetGoBReplay {
    char* base;
    char* instance;
    char* model;
    bool cleared;
};

uint32_t __fastcall LetGoBByMode(char* base, void*)
{
    if (g_letGoModeB == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4EffectBaseB_LetGo(base, nullptr);
    if (g_letGoModeB == StepMode::Ours)
        return LetGoEffectB(base);
    g_heldGame.clear();
    g_heldOurs.clear();
    g_effectThread = GetCurrentThreadId();
    LetGoBReplay replay{base, At<char*>(base, 0x21C), At<char*>(base, 0x220), false};
    const bool used = At<uint8_t>(base, 4) != 0;
    if (used) {
        SwapHeld(replay.instance);
        if (replay.model)
            SwapHeld(replay.model + 4 + At<int32_t>(At<char*>(replay.model, 4), 4));
        // The base class's two are its own comparison's: here its whole
        // run is one recorded call.
    }
    const uint32_t result = RecordAndReplay(
        used ? "effects.letgo_b" : "effects.letgo_b.unused", base - 4, false,
        [&] { return g_SC4EffectBaseB_LetGo(base, nullptr); },
        [](void* r) {
            LetGoBReplay& replay = *static_cast<LetGoBReplay*>(r);
            char* const instanceAfter = At<char*>(replay.base, 0x21C);
            char* const modelAfter = At<char*>(replay.base, 0x220);
            At<char*>(replay.base, 0x21C) = replay.instance;
            At<char*>(replay.base, 0x220) = replay.model;
            const uint32_t result = LetGoEffectB(replay.base);
            replay.cleared = At<char*>(replay.base, 0x21C) == instanceAfter &&
                             At<char*>(replay.base, 0x220) == modelAfter;
            At<char*>(replay.base, 0x21C) = instanceAfter;
            At<char*>(replay.base, 0x220) = modelAfter;
            return result;
        },
        &replay);
    for (const auto& [object, real] : g_heldReal)
        At<uint32_t>(object, 0) = real;
    g_heldReal.clear();
    char text[64];
    std::snprintf(text, sizeof text, "%zu released", g_heldGame.size());
    const std::string game = text;
    std::snprintf(text, sizeof text, "%zu released, fields %s", g_heldOurs.size(), replay.cleared ? "same" : "differ");
    const bool same = g_heldGame == g_heldOurs && replay.cleared;
    compare::Record(g_heldGame.empty() ? "effects.letgo_b.released.none" : "effects.letgo_b.released", same,
                    same ? std::string() : game, same ? std::string() : std::string(text));
    return result;
}

uint32_t __fastcall DSC4EffectBaseB_LetGo(char* base, void*)
{
    return EffectRecorded(addr::SC4EffectBaseB_LetGo, &LetGoBByMode, base);
}
OPENSC4_THUNK(TSC4EffectBaseB_LetGo, DSC4EffectBaseB_LetGo, 0)

// An effect told to go on or to end (0x005B9E70, slot 0x20 of both classes'
// sub-objects; nothing for one whose byte +5 is clear). The first argument
// is kept at +0xC. Set, the second is kept at +0xD and, when set as well,
// the sub-object's slot 4 is called; the answer is false. Clear, the effect
// loses its particles at once: the bytes +5 and +0xD are cleared, every
// particle of the list (+0x10) is given back (slot 0x40) with the count
// (+0x14) taken down first, the nodes are freed and the list is left empty;
// the answer is true.
uint32_t SetEffectEnding(char* base, uint32_t goOn, uint32_t whenEmpty)
{
    if (!At<uint8_t>(base, 5))
        return 0;
    At<uint8_t>(base, 0xC) = static_cast<uint8_t>(goOn);
    if (static_cast<uint8_t>(goOn)) {
        At<uint8_t>(base, 0xD) = static_cast<uint8_t>(whenEmpty);
        if (static_cast<uint8_t>(whenEmpty))
            CallThisTraced(At<uint32_t>(At<char*>(base, 0), 0x4), base);
        return 0;
    }
    At<uint8_t>(base, 5) = 0;
    At<uint8_t>(base, 0xD) = 0;
    for (char* node = At<char*>(At<char*>(base, 0x10), 0); node != At<char*>(base, 0x10); node = At<char*>(node, 0)) {
        --At<int32_t>(base, 0x14);
        CallThisTraced(At<uint32_t>(At<char*>(base, 0), 0x40), base, At<uint32_t>(node, 8));
    }
    for (char* node = At<char*>(At<char*>(base, 0x10), 0); node != At<char*>(base, 0x10);) {
        char* const next = At<char*>(node, 0);
        CallPlainTraced(addr::SC4_Free, reinterpret_cast<uint32_t>(node));
        node = next;
    }
    char* const head = At<char*>(base, 0x10);
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    return 1;
}

// An effect stopped or started again (0x005B21E0, slot 4 of the base
// class): the value is kept at +0x18C, and the sub-object is told to go on
// when it is 0 and to end otherwise. The game passes the whole register
// with only its low byte set to the flag, and so does this.
uint32_t SetEffectStopped(char* self, uint32_t value)
{
    At<uint32_t>(self, 0x18C) = value;
    return CallThisTraced(addr::SC4EffectBase_SetEnding, self + 4, value ? value & ~0xFFu : 1u, 0u);
}

// The same for the second class (0x005AC190): an effect with an object at
// +0x278 and nothing at +0x18 passes the value to that object's slot 4
// first.
uint32_t SetEffectStoppedB(char* self, uint32_t value)
{
    char* const object = At<char*>(self, 0x278);
    if (object && At<uint32_t>(self, 0x18) == 0)
        CallThisTraced(At<uint32_t>(At<char*>(object, 0), 0x4), object, value);
    return CallThisTraced(addr::SC4Effect_SetStopped, self, value);
}

StepMode g_endingMode = StepMode::Original;
Call1Fn g_SC4Effect_SetStopped = nullptr, g_SC4EffectB_SetStopped = nullptr;
Call2Fn g_SC4EffectBase_SetEnding = nullptr;

uint32_t __fastcall SetEndingByMode(char* base, void*, uint32_t goOn, uint32_t whenEmpty)
{
    if (g_endingMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4EffectBase_SetEnding(base, nullptr, goOn, whenEmpty);
    if (g_endingMode == StepMode::Ours)
        return SetEffectEnding(base, goOn, whenEmpty);
    struct Arguments {
        char* base;
        uint32_t goOn, whenEmpty;
    } arguments{base, goOn, whenEmpty};
    return RecordAndReplay(
        static_cast<uint8_t>(goOn) ? "effects.ending" : "effects.ending.clear", base - 4, true, [&] { return g_SC4EffectBase_SetEnding(base, nullptr, goOn, whenEmpty); },
        [](void* a) {
            const Arguments& args = *static_cast<Arguments*>(a);
            return SetEffectEnding(args.base, args.goOn, args.whenEmpty);
        },
        &arguments);
}

// The two below answer with what the sub-object answered, which the replay
// does not have: their answers are not compared.
uint32_t StoppedCompared(const char* id, Call1Fn game, uint32_t (*ours)(char*, uint32_t), char* self, uint32_t value)
{
    struct Arguments {
        uint32_t (*ours)(char*, uint32_t);
        char* self;
        uint32_t value;
    } arguments{ours, self, value};
    return RecordAndReplay(
        id, self, false, [&] { return game(self, nullptr, value); },
        [](void* a) {
            const Arguments& args = *static_cast<Arguments*>(a);
            return args.ours(args.self, args.value);
        },
        &arguments);
}

uint32_t __fastcall SetStoppedByMode(char* self, void*, uint32_t value)
{
    if (g_endingMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4Effect_SetStopped(self, nullptr, value);
    if (g_endingMode == StepMode::Ours)
        return SetEffectStopped(self, value);
    return StoppedCompared("effects.ending.stopped", g_SC4Effect_SetStopped, &SetEffectStopped, self, value);
}

uint32_t __fastcall SetStoppedBByMode(char* self, void*, uint32_t value)
{
    if (g_endingMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4EffectB_SetStopped(self, nullptr, value);
    if (g_endingMode == StepMode::Ours)
        return SetEffectStoppedB(self, value);
    return StoppedCompared("effects.ending.stopped_b", g_SC4EffectB_SetStopped, &SetEffectStoppedB, self, value);
}

uint32_t __fastcall DSC4EffectBase_SetEnding(char* base, void*, uint32_t goOn, uint32_t whenEmpty)
{
    return EffectRecorded(addr::SC4EffectBase_SetEnding, &SetEndingByMode, base, goOn, whenEmpty);
}
OPENSC4_THUNK(TSC4EffectBase_SetEnding, DSC4EffectBase_SetEnding, 8)

uint32_t __fastcall DSC4Effect_SetStopped(char* self, void*, uint32_t value)
{
    return EffectRecorded(addr::SC4Effect_SetStopped, &SetStoppedByMode, self, value);
}
OPENSC4_THUNK(TSC4Effect_SetStopped, DSC4Effect_SetStopped, 4)

uint32_t __fastcall DSC4EffectB_SetStopped(char* self, void*, uint32_t value)
{
    return EffectRecorded(addr::SC4EffectB_SetStopped, &SetStoppedBByMode, self, value);
}
OPENSC4_THUNK(TSC4EffectB_SetStopped, DSC4EffectB_SetStopped, 4)

uint32_t __fastcall DEffectDraw(char* self, void*, uint32_t step, uint32_t context)
{
    if (g_trace != Trace::Off) {
        g_calls.insert(g_calls.end(), {5u, reinterpret_cast<uint32_t>(self), step, context});
        if (g_trace == Trace::Record)
            return 0;
    }
    return EffectDraw(self, step, context);
}

// A decal's step (0x00582920, slot 0x24 of the class 0x00A9EE78, the
// objects of the manager's fourth list), translated instruction by
// instruction. Nothing for a decal without a handle (+0x70 is -1). Its
// time (+0x114) grows by the step, and the time times +0x118 is where it
// is in its life: a looping decal (+0x15 is 1) wraps, one past its end
// that is of kind 2 ends itself (its slot 4 with 0) and any other stays at
// the end. Then the description's curves (three vectors of floats from
// +0x34, and one of positions at +0x40) are read at that point and given
// to the owner (+0x120) for the handle: by the flags at +0xC only an
// opacity (slot 0x44), or a block of nine values (slot 0x34) and the
// opacity, and last a position (slot 0x48).
uint32_t StepDecal(char* self, uint32_t step)
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
#include "core/effect_decal.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// Its comparison: what it does is its own time and the calls it makes, to
// its owner and to its own slot 4. For the length of the comparison the
// decal is given a stand-in owner and a copy of its table of functions
// with slot 4 replaced, which note each call (the slot, the handle and the
// values behind the pointers). Ours runs first with the calls only noted;
// the decal is put back and the game's runs with the calls noted and
// passed on. The calls and what is left of the decal must be the same. A
// decal that ends itself is compared as it is when it asks to.
enum class DecalMode { Original, Ours, Compare };
DecalMode g_decalMode = DecalMode::Original;
constexpr std::uintptr_t kDecalVtable = 0x00A9EE78;
constexpr std::size_t kDecalBytes = 0x124, kDecalOwner = 0x120, kDecalSlots = 64;
struct DecalCall {
    uint32_t slot, handle;
    char data[0x24];
    bool operator==(const DecalCall& other) const
    {
        return slot == other.slot && handle == other.handle && std::memcmp(data, other.data, sizeof data) == 0;
    }
};
std::vector<DecalCall> g_decalOurCalls, g_decalGameCalls;
bool g_decalForward = false, g_decalEnded = false;
char* g_decalSelf = nullptr;
char* g_decalRealOwner = nullptr;
char g_decalAtEnd[kDecalBytes];
uint32_t g_decalFakeVtable[kDecalSlots], g_decalSelfVtable[kDecalSlots];
uint32_t* g_decalFakeOwner = g_decalFakeVtable;
uint64_t g_decalsCompared = 0, g_decalCallsCompared = 0;

void NoteDecalCall(uint32_t slot, uint32_t handle, const void* data, std::size_t size)
{
    DecalCall call{slot, handle, {}};
    std::memcpy(call.data, data, size);
    (g_decalForward ? g_decalGameCalls : g_decalOurCalls).push_back(call);
}

void UnswapDecal()
{
    At<uint32_t>(g_decalSelf, 0) = kDecalVtable;
    At<char*>(g_decalSelf, kDecalOwner) = g_decalRealOwner;
}

uint32_t __fastcall DecalOwnerBlock(void*, void*, uint32_t handle, uint32_t block)
{
    NoteDecalCall(0x34, handle, reinterpret_cast<const void*>(block), 0x24);
    return g_decalForward
               ? CallThis<uint32_t>(At<uint32_t>(At<char*>(g_decalRealOwner, 0), 0x34), g_decalRealOwner, handle, block)
               : 0;
}

uint32_t __fastcall DecalOwnerOpacity(void*, void*, uint32_t handle, uint32_t value)
{
    NoteDecalCall(0x44, handle, &value, 4);
    return g_decalForward
               ? CallThis<uint32_t>(At<uint32_t>(At<char*>(g_decalRealOwner, 0), 0x44), g_decalRealOwner, handle, value)
               : 0;
}

uint32_t __fastcall DecalOwnerPlace(void*, void*, uint32_t handle, uint32_t place)
{
    NoteDecalCall(0x48, handle, reinterpret_cast<const void*>(place), 12);
    return g_decalForward
               ? CallThis<uint32_t>(At<uint32_t>(At<char*>(g_decalRealOwner, 0), 0x48), g_decalRealOwner, handle, place)
               : 0;
}

uint32_t __fastcall DecalEnd(char* self, void*, uint32_t value)
{
    NoteDecalCall(0x4, value, &value, 0);
    std::memcpy(g_decalAtEnd, self, kDecalBytes);
    g_decalEnded = true;
    if (!g_decalForward)
        return 0;
    // The decal may be gone when this returns: it is given its own back
    // first, and not touched again.
    UnswapDecal();
    return CallThis<uint32_t>(Global<uint32_t>(kDecalVtable + 4), self, value);
}

uint32_t __fastcall DecalByMode(char* self, void*, uint32_t step)
{
    if (g_decalMode == DecalMode::Original)
        return g_decalDraw(self, nullptr, step);
    if (g_decalMode == DecalMode::Ours)
        return StepDecal(self, step);
    if (At<uint32_t>(self, 0) != kDecalVtable || g_decalSelf)
        return g_decalDraw(self, nullptr, step);
    g_decalSelf = self;
    g_decalRealOwner = At<char*>(self, kDecalOwner);
    std::memcpy(g_decalSelfVtable, reinterpret_cast<const void*>(kDecalVtable), sizeof g_decalSelfVtable);
    g_decalSelfVtable[1] = reinterpret_cast<uint32_t>(&DecalEnd);
    std::memset(g_decalFakeVtable, 0, sizeof g_decalFakeVtable);
    g_decalFakeVtable[0x34 / 4] = reinterpret_cast<uint32_t>(&DecalOwnerBlock);
    g_decalFakeVtable[0x44 / 4] = reinterpret_cast<uint32_t>(&DecalOwnerOpacity);
    g_decalFakeVtable[0x48 / 4] = reinterpret_cast<uint32_t>(&DecalOwnerPlace);
    At<uint32_t*>(self, 0) = g_decalSelfVtable;
    At<uint32_t**>(self, kDecalOwner) = &g_decalFakeOwner;
    char before[kDecalBytes], mine[kDecalBytes], theirs[kDecalBytes];
    std::memcpy(before, self, kDecalBytes);
    g_decalOurCalls.clear();
    g_decalGameCalls.clear();
    g_decalForward = false;
    g_decalEnded = false;
    StepDecal(self, step);
    std::memcpy(mine, g_decalEnded ? g_decalAtEnd : self, kDecalBytes);
    std::memcpy(self, before, kDecalBytes);
    g_decalForward = true;
    g_decalEnded = false;
    const uint32_t result = g_decalDraw(self, nullptr, step);
    if (g_decalEnded) {
        std::memcpy(theirs, g_decalAtEnd, kDecalBytes);
    } else {
        std::memcpy(theirs, self, kDecalBytes);
        UnswapDecal();
    }
    g_decalSelf = nullptr;
    ++g_decalsCompared;
    g_decalCallsCompared += g_decalGameCalls.size();
    if ((g_decalsCompared & (g_decalsCompared - 1)) == 0)
        log::Write("water effects: effects.decal: %llu steps, %llu calls compared so far",
                   static_cast<unsigned long long>(g_decalsCompared),
                   static_cast<unsigned long long>(g_decalCallsCompared));
    char text[80];
    if (!(g_decalGameCalls == g_decalOurCalls)) {
        std::size_t at = 0;
        while (at < g_decalGameCalls.size() && at < g_decalOurCalls.size() &&
               g_decalGameCalls[at] == g_decalOurCalls[at])
            ++at;
        const auto describe = [&](const std::vector<DecalCall>& calls) {
            if (at >= calls.size()) {
                std::snprintf(text, sizeof text, "%zu calls", calls.size());
                return std::string(text);
            }
            std::snprintf(text, sizeof text, "call %zu of %zu: slot %X (%08X) ", at, calls.size(), calls[at].slot,
                          calls[at].handle);
            return text + Hex(calls[at].data, sizeof calls[at].data);
        };
        const std::string game = describe(g_decalGameCalls);
        compare::Record("effects.decal", false, game, describe(g_decalOurCalls));
    } else if (std::memcmp(theirs, mine, kDecalBytes) != 0) {
        std::size_t at = 0;
        while (theirs[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "decal+%zX ", at);
        compare::Record("effects.decal", false, text + Hex(theirs + at, 4), text + Hex(mine + at, 4));
    } else {
        compare::Record("effects.decal", true, std::string(), std::string());
    }
    return result;
}

uint32_t __fastcall DDecalDraw(char* self, void*, uint32_t step)
{
    return Traced(6, &DecalByMode, self, step);
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
    "is renderer.effects.step_full); compare mode runs ours and the game's on the same particle (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallStep,
});

// --- Making an effect ----------------------------------------------------------------------

// The sub-object's constructor (0x005B9F00), written by hand: its table of
// functions (0x00AA0F48, which the effect's constructor then replaces), the
// bytes and words up to +0x1C cleared, and an empty list at +0x10 (a head
// node of 12 bytes that points at itself both ways).
uint32_t ConstructEffectBase(char* base)
{
    At<uint32_t>(base, 0) = 0x00AA0F48;
    At<uint8_t>(base, 4) = 0;
    At<uint8_t>(base, 5) = 0;
    At<uint32_t>(base, 8) = 0;
    At<uint8_t>(base, 0xC) = 0;
    At<uint8_t>(base, 0xD) = 0;
    At<uint32_t>(base, 0x10) = 0;
    char* const head = reinterpret_cast<char*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(12);
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    At<char*>(base, 0x10) = head;
    At<uint32_t>(base, 0x14) = 0;
    At<uint32_t>(base, 0x18) = 0;
    At<uint32_t>(base, 0x1C) = 0;
    return reinterpret_cast<uint32_t>(base);
}

// The effect's constructor (0x005B2B50), translated instruction by
// instruction: the sub-object at +4 and the batch at +0x28 (0x005EC690)
// constructed, the two tables of functions set (0x00AA0EA0, 0x00AA0E58),
// and the effect's fields given their first values. It leaves some of the
// object's bytes as they were.
uint32_t ConstructEffect(char* self)
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
#include "core/effect_ctor.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// Their comparison: ours on the object's memory, what it left kept, its
// list's head freed, the memory put back as it was, and the game's. The
// two must have left the same bytes but for the pointer to the head, and
// ours' head must have been an empty list.
StepMode g_ctorMode = StepMode::Original;
Call0Fn g_SC4EffectBase_Construct = nullptr, g_SC4Effect_Construct = nullptr;

uint32_t CtorCompared(const char* id, char* object, std::size_t size, std::size_t headAt, Call0Fn game,
                      uint32_t (*ours)(char*))
{
    std::vector<char> before(object, object + size);
    const uint32_t ourResult = ours(object);
    std::vector<char> mine(object, object + size);
    char* const head = At<char*>(object, headAt);
    const bool emptyList = head && At<char*>(head, 0) == head && At<char*>(head, 4) == head;
    if (head)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(head);
    std::memcpy(object, before.data(), size);
    const uint32_t result = game(object, nullptr);
    std::vector<char> theirs(object, object + size);
    std::memset(mine.data() + headAt, 0, 4);
    std::memset(theirs.data() + headAt, 0, 4);
    const bool same = emptyList && result == ourResult && theirs == mine;
    std::string text;
    if (!same) {
        std::size_t at = 0;
        while (at < size && theirs[at] == mine[at])
            ++at;
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%s at +%zX", emptyList ? "list empty" : "list not empty", at);
        text = buffer;
    }
    compare::Record(id, same, same ? std::string() : std::string("as the game's"), text);
    return result;
}

uint32_t __fastcall BaseCtorByMode(char* base, void*)
{
    if (g_ctorMode == StepMode::Original)
        return g_SC4EffectBase_Construct(base, nullptr);
    if (g_ctorMode == StepMode::Ours)
        return ConstructEffectBase(base);
    return CtorCompared("effects.ctor.base", base, 0x20, 0x10, g_SC4EffectBase_Construct, &ConstructEffectBase);
}
OPENSC4_THUNK(TSC4EffectBase_Construct, BaseCtorByMode, 0)

bool MxNoting();
template <typename Real>
uint32_t MxConstructNoted(uint32_t constructor, char* object, Real&& real);

uint32_t EffectCtorRun(char* self)
{
    if (g_ctorMode == StepMode::Original)
        return g_SC4Effect_Construct(self, nullptr);
    if (g_ctorMode == StepMode::Ours)
        return ConstructEffect(self);
    return CtorCompared("effects.ctor", self, kEffectSize, 0x14, g_SC4Effect_Construct, &ConstructEffect);
}

// While a maker is noted, its constructor call is noted too.
uint32_t __fastcall EffectCtorByMode(char* self, void*)
{
    if (!MxNoting())
        return EffectCtorRun(self);
    return MxConstructNoted(addr::SC4Effect_Construct, self, [&] { return EffectCtorRun(self); });
}
OPENSC4_THUNK(TSC4Effect_Construct, EffectCtorByMode, 0)

// The second class's constructor (0x005AC510), written by hand: the base
// class's, the class's own two tables of functions (0x00AA0C58,
// 0x00AA0C10), no model yet (+0x220, +0x224), the part at +0x228
// constructed (0x00803060) with no description (its +0x40), and the
// class's bytes and words from +0x26C to +0x278 cleared.
uint32_t ConstructEffectB(char* self)
{
    CallThis<uint32_t>(addr::SC4Effect_Construct, self);
    At<uint32_t>(self, 0) = 0x00AA0C58;
    At<uint32_t>(self, 4) = 0x00AA0C10;
    At<uint32_t>(self, 0x220) = 0;
    At<uint32_t>(self, 0x224) = 0;
    CallThis<uint32_t>(0x00803060, self + 0x228);
    At<uint32_t>(self, 0x268) = 0;
    At<uint8_t>(self, 0x26C) = 0;
    At<uint8_t>(self, 0x26D) = 0;
    At<uint8_t>(self, 0x26E) = 0;
    At<uint8_t>(self, 0x26F) = 0;
    At<uint32_t>(self, 0x270) = 0;
    At<uint32_t>(self, 0x274) = 0;
    At<uint32_t>(self, 0x278) = 0;
    return reinterpret_cast<uint32_t>(self);
}

Call0Fn g_SC4EffectB_Construct = nullptr;

uint32_t EffectBCtorRun(char* self)
{
    if (g_ctorMode == StepMode::Original)
        return g_SC4EffectB_Construct(self, nullptr);
    if (g_ctorMode == StepMode::Ours)
        return ConstructEffectB(self);
    return CtorCompared("effects.ctor.b", self, kEffectSizeB, 0x14, g_SC4EffectB_Construct, &ConstructEffectB);
}

uint32_t __fastcall EffectBCtorByMode(char* self, void*)
{
    if (!MxNoting())
        return EffectBCtorRun(self);
    return MxConstructNoted(addr::SC4EffectB_Construct, self, [&] { return EffectBCtorRun(self); });
}
OPENSC4_THUNK(TSC4EffectB_Construct, EffectBCtorByMode, 0)

// The two effect classes' constructors have one detour each, which their
// own replacement and the makers' comparison share.
bool HookEffectConstructors()
{
    return (g_SC4Effect_Construct ||
            Detour(addr::SC4Effect_Construct, &TSC4Effect_Construct, &g_SC4Effect_Construct)) &&
           (g_SC4EffectB_Construct ||
            Detour(addr::SC4EffectB_Construct, &TSC4EffectB_Construct, &g_SC4EffectB_Construct));
}

// An effect's destructor (0x005A62B0, also the end of a third class's at
// 0x005A90xx), written by hand: the objects at +0x1F0 and +0x1EC released
// (slot 8), the texture at +0x6C let go (0x008090E0), the batch at +0x28
// destroyed (0x005EC6E0), the description at +0x24 let go (0x005B1C80)
// and the sub-object at +4 destroyed (0x005B9F40: its list freed). The
// calls go through the comparison's notes; the texture's is kept apart,
// the game's having no detour of ours.
char* g_dtorTextureOurs = nullptr;

uint32_t DtorRelease(char* object, uint32_t slot)
{
    return BOurs(slot, reinterpret_cast<uint32_t>(object), 0, 0,
                 [&] { return CallThis<uint32_t>(M<uint32_t>(At<uint32_t>(object, 0) + slot), object); });
}

uint32_t DtorCall(uint32_t target, char* object)
{
    return BOurs(target, reinterpret_cast<uint32_t>(object), 0, 0, [&] { return CallThis<uint32_t>(target, object); });
}

void DestructEffect(char* self)
{
    if (char* const object = At<char*>(self, 0x1F0))
        DtorRelease(object, 0x8);
    if (char* const object = At<char*>(self, 0x1EC))
        DtorRelease(object, 0x8);
    if (char* const texture = At<char*>(self, 0x6C)) {
        if (g_bReplay)
            g_dtorTextureOurs = texture;
        else
            CallThis<uint32_t>(0x008090E0, texture);
    }
    DtorCall(addr::SC4EffectBatch_Destruct, self + 0x28);
    if (char* const description = At<char*>(self, 0x24))
        DtorCall(addr::SC4EffectDescription_Release, description);
    DtorCall(addr::SC4EffectBase_Destruct, self + 4);
}

// The second class's (0x005AC5B0): the object at +0x278 ended (slot
// 0x20), the description at +0x268 let go, the part at +0x228 destroyed
// (0x008030A0), the model at +0x224 released through the part of it that
// counts its holders, the instance at +0x220 released, then the base
// class's.
void DestructEffectB(char* self)
{
    if (char* const object = At<char*>(self, 0x278))
        DtorRelease(object, 0x20);
    if (char* const description = At<char*>(self, 0x268))
        DtorCall(addr::SC4EffectDescription_Release, description);
    DtorCall(addr::SC4EffectPartB_Destruct, self + 0x228);
    if (char* const model = At<char*>(self, 0x224))
        DtorRelease(model + 4 + At<int32_t>(At<char*>(model, 4), 4), 0x8);
    if (char* const instance = At<char*>(self, 0x220))
        DtorRelease(instance, 0x8);
    DestructEffect(self);
}

// Their comparison is the holders': the game's first with its calls
// noted (the objects through copies of their tables of functions, the
// three destructors and the description's letting go through detours),
// then ours with the effect's bytes put back, making no call but noting
// each. The texture is given one more holder for the game's run, must
// have lost exactly one, and is then let go for real; one whose last
// holder but its factory this is cannot be checked that way.
StepMode g_dtorMode = StepMode::Original;
Call0Fn g_SC4Effect_Destruct = nullptr, g_SC4EffectB_Destruct = nullptr, g_SC4EffectBatch_Destruct = nullptr,
        g_SC4EffectBase_Destruct = nullptr, g_SC4EffectPartB_Destruct = nullptr;
Call1Fn g_SC4Effect_Delete = nullptr, g_SC4EffectB_Delete = nullptr;

#define OPENSC4_DTOR_NOTED(Name)                                                                                       \
    uint32_t __fastcall D##Name(char* self, void*)                                                                     \
    {                                                                                                                  \
        if (!BNoting())                                                                                                \
            return g_##Name(self, nullptr);                                                                            \
        return BCallee(addr::Name, reinterpret_cast<uint32_t>(self), 0, 0, [&] { return g_##Name(self, nullptr); });   \
    }                                                                                                                  \
    OPENSC4_THUNK(T##Name, D##Name, 0)
OPENSC4_DTOR_NOTED(SC4EffectBatch_Destruct)
OPENSC4_DTOR_NOTED(SC4EffectBase_Destruct)
OPENSC4_DTOR_NOTED(SC4EffectPartB_Destruct)
#undef OPENSC4_DTOR_NOTED

template <typename Game, typename Ours>
void DtorCompared(const char* id, char* self, std::size_t size, Game&& game, Ours&& ours)
{
    BSwap(At<char*>(self, 0x1F0), 0x8, reinterpret_cast<uint32_t>(&BRelease));
    BSwap(At<char*>(self, 0x1EC), 0x8, reinterpret_cast<uint32_t>(&BRelease));
    char* const texture = At<char*>(self, 0x6C);
    const int32_t holders = texture ? At<int32_t>(texture, 0xC) : 0;
    const bool held = texture && !(holders == 1 && At<uint32_t>(texture, 8));
    if (held)
        ++At<int32_t>(texture, 0xC);
    g_dtorTextureOurs = nullptr;
    HolderCompared(
        id, self, size,
        [&] {
            game();
            return 0u;
        },
        ours);
    if (texture) {
        const bool same = g_dtorTextureOurs == texture && (!held || At<int32_t>(texture, 0xC) == holders);
        compare::Record(held ? "effects.dtor.texture" : "effects.dtor.texture.last", same,
                        same ? std::string() : std::string("one holder less"),
                        same ? std::string() : std::string(g_dtorTextureOurs == texture ? "not so" : "not ours'"));
        if (held)
            CallThis<uint32_t>(0x008090E0, texture);
    }
}

bool DtorPassed()
{
    return g_dtorMode == StepMode::Original || g_effectRecord || g_effectReplay || g_descRecord || g_descReplay ||
           BBusy();
}

uint32_t __fastcall EffectDtorByMode(char* self, void*)
{
    if (DtorPassed())
        return g_SC4Effect_Destruct(self, nullptr);
    if (g_dtorMode == StepMode::Ours)
        DestructEffect(self);
    else
        DtorCompared(
            "effects.dtor", self, kEffectSize, [&] { g_SC4Effect_Destruct(self, nullptr); },
            [&] { DestructEffect(self); });
    return 0;
}
OPENSC4_THUNK(TSC4Effect_Destruct, EffectDtorByMode, 0)

uint32_t __fastcall EffectBDtorByMode(char* self, void*)
{
    if (DtorPassed())
        return g_SC4EffectB_Destruct(self, nullptr);
    if (g_dtorMode == StepMode::Ours) {
        DestructEffectB(self);
        return 0;
    }
    BSwap(At<char*>(self, 0x278), 0x20, reinterpret_cast<uint32_t>(&BEnd));
    if (char* const model = At<char*>(self, 0x224))
        BSwap(model + 4 + At<int32_t>(At<char*>(model, 4), 4), 0x8, reinterpret_cast<uint32_t>(&BRelease));
    BSwap(At<char*>(self, 0x220), 0x8, reinterpret_cast<uint32_t>(&BRelease));
    DtorCompared(
        "effects.dtor.b", self, kEffectSizeB, [&] { g_SC4EffectB_Destruct(self, nullptr); },
        [&] { DestructEffectB(self); });
    return 0;
}
OPENSC4_THUNK(TSC4EffectB_Destruct, EffectBDtorByMode, 0)

// The two destructors as the tables of functions have them (0x005B2FE0
// and 0x005AC590, slot 0xC of the sub-object's through a thunk): the
// destructor, then with bit 0 of the flags the memory freed (0x005E5620).
uint32_t __fastcall EffectDeleteByMode(char* self, void*, uint32_t flags)
{
    if (g_dtorMode == StepMode::Original)
        return g_SC4Effect_Delete(self, nullptr, flags);
    EffectDtorByMode(self, nullptr);
    if (flags & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(0x005E5620)(self);
    return reinterpret_cast<uint32_t>(self);
}
OPENSC4_THUNK(TSC4Effect_Delete, EffectDeleteByMode, 4)

uint32_t __fastcall EffectBDeleteByMode(char* self, void*, uint32_t flags)
{
    if (g_dtorMode == StepMode::Original)
        return g_SC4EffectB_Delete(self, nullptr, flags);
    EffectBDtorByMode(self, nullptr);
    if (flags & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(0x005E5620)(self);
    return reinterpret_cast<uint32_t>(self);
}
OPENSC4_THUNK(TSC4EffectB_Delete, EffectBDeleteByMode, 4)

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
        Detour(addr::SC4EffectB_SetStopped, &TSC4EffectB_SetStopped, &g_SC4EffectB_SetStopped) &&
        Detour(addr::SC4EffectBase_SetEnding, &TSC4EffectBase_SetEnding, &g_SC4EffectBase_SetEnding) &&
        Detour(addr::SC4EffectBase_LetGo, &TSC4EffectBase_LetGo, &g_SC4EffectBase_LetGo) &&
        Detour(addr::SC4EffectBaseB_LetGo, &TSC4EffectBaseB_LetGo, &g_SC4EffectBaseB_LetGo);
    if (g_poolHooked) {
        const addr::Entry* const uniform = addr_check::Find(addr::SC4Random_Uniform);
        g_poolHooked = uniform && hook::Install(*uniform, &DUniform, &g_uniform);
    }
    return g_poolHooked;
}

bool HookStart()
{
    return g_startParticle || Detour(addr::SC4Effect_StartParticle, &TStartParticle, &g_startParticle);
}

bool InstallStart(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookStart())
        return false;
    g_startMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: particle start %s", mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredStart = registry::Register({
    "renderer.effects.start",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StartParticle,
    "A particle's start: its life, position, velocity and the rest from the effect, its description and random "
    "numbers; compare mode runs ours and the game's on the same particle and generator (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallStart,
});

bool InstallFullStep(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4Effect_StepParticleFull ||
          Detour(addr::SC4Effect_StepParticleFull, &TSC4Effect_StepParticleFull, &g_SC4Effect_StepParticleFull)))
        return false;
    if (mode == registry::Mode::Compare &&
        !((g_SC4Effect_StepFlag100 ||
           Detour(addr::SC4Effect_StepFlag100, &TSC4Effect_StepFlag100, &g_SC4Effect_StepFlag100)) &&
          (g_SC4Effect_StepFlagB1 ||
           Detour(addr::SC4Effect_StepFlagB1, &TSC4Effect_StepFlagB1, &g_SC4Effect_StepFlagB1))))
        return false;
    g_fullStepMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: particles' other step %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredFullStep = registry::Register({
    "renderer.effects.step_full",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StepParticleFull,
    "A particle's step for flagged descriptions: done in parts, with the effect's pull, the map's edge and the "
    "ground; compare mode runs the game's with two of its calls noted and replays ours (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallFullStep,
});

bool InstallHit(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4Effect_StepFlag100 ||
          Detour(addr::SC4Effect_StepFlag100, &TSC4Effect_StepFlag100, &g_SC4Effect_StepFlag100)))
        return false;
    if (mode == registry::Mode::Compare &&
        !(g_SC4Effect_StartNamed ||
          Detour(addr::SC4Effect_StartNamed, &TSC4Effect_StartNamed, &g_SC4Effect_StartNamed)))
        return false;
    g_hitMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: particles meeting the water or the ground %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredHit = registry::Register({
    "renderer.effects.hit",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StepFlag100,
    "A particle meeting the water or the ground: it ends, bounces, slides or stays, and may start another effect "
    "there; compare mode runs the game's with those starts noted and ours with them held back (docs/renderer.md, "
    "\"Water effects, sky and clouds\")",
    true,
    InstallHit,
});

bool InstallPoolB(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookPool())
        return false;
    g_poolModeB = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: second effect class's take of a particle %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredPoolB = registry::Register({
    "renderer.effects.pool_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectBaseB_TakeParticle,
    "The second effect class taking a particle from its pool: started as any particle, then given an angle and an "
    "effect of its own; compare mode runs the game's and ours from the same state (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallPoolB,
});

bool InstallNotDrawnB(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4EffectB_NotDrawn ||
          Detour(addr::SC4EffectB_NotDrawn, &TSC4EffectB_NotDrawn, &g_SC4EffectB_NotDrawn)))
        return false;
    g_notDrawnModeB = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: second effect class's step out of view %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredNotDrawnB = registry::Register({
    "renderer.effects.notdrawn_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectB_NotDrawn,
    "The second effect class's step when it is not drawn: its box, and where each particle's own effect is; "
    "compare mode notes the calls of ours and of the game's through stand-in tables of functions "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallNotDrawnB,
});

bool InstallDrawB(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4EffectB_DrawParticles ||
          Detour(addr::SC4EffectB_DrawParticles, &TSC4EffectB_DrawParticles, &g_SC4EffectB_DrawParticles)))
        return false;
    if (mode == registry::Mode::Compare &&
        !((g_SC4EffectPartB_Start ||
           Detour(addr::SC4EffectPartB_Start, &TSC4EffectPartB_Start, &g_SC4EffectPartB_Start)) &&
          (g_SC4EffectPartB_Step ||
           Detour(addr::SC4EffectPartB_Step, &TSC4EffectPartB_Step, &g_SC4EffectPartB_Step))))
        return false;
    g_drawModeB = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: second effect class's draw %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDrawB = registry::Register({
    "renderer.effects.draw_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectB_DrawParticles,
    "The second effect class's draw: each particle in view drawn as the effect's model, at the frame its clock "
    "has reached; compare mode notes the calls of ours and of the game's through stand-in tables of functions "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallDrawB,
});

bool InstallBlockB(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4EffectB_ParticleBlock ||
          Detour(addr::SC4EffectB_ParticleBlock, &TSC4EffectB_ParticleBlock, &g_SC4EffectB_ParticleBlock)))
        return false;
    g_blockModeB = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: second effect class's particle block %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredBlockB = registry::Register({
    "renderer.effects.block_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectB_ParticleBlock,
    "The block of a particle of the second effect class: its turning, size and place as a matrix, and the "
    "effect's box widened; compare mode runs ours and the game's from the same particle, effect and block "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallBlockB,
});

bool InstallSmall(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(Detour(addr::SC4Effect_GetBox, &TSC4Effect_GetBox, &g_SC4Effect_GetBox) &&
          Detour(addr::SC4Effect_GetPlace, &TSC4Effect_GetPlace, &g_SC4Effect_GetPlace) &&
          Detour(addr::SC4Effect_SetField1E8, &TSC4Effect_SetField1E8, &g_SC4Effect_SetField1E8) &&
          (g_SC4Effect_Hold || Detour(addr::SC4Effect_Hold, &TSC4Effect_Hold, &g_SC4Effect_Hold)) &&
          Detour(addr::SC4EffectBase_Restart, &TSC4EffectBase_Restart, &g_SC4EffectBase_Restart)))
        return false;
    g_smallMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: five small functions of the effect %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredSmall = registry::Register({
    "renderer.effects.small",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_GetBox,
    "Five small functions of an effect: its box and its place read, a field set, a holder counted, the sub-object "
    "set going again; compare mode runs ours and the game's on the same effect (docs/renderer.md, \"Water effects, "
    "sky and clouds\")",
    true,
    InstallSmall,
});

bool InstallTransform(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4Effect_SetTransform ||
          Detour(addr::SC4Effect_SetTransform, &TSC4Effect_SetTransform, &g_SC4Effect_SetTransform)))
        return false;
    g_transformMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effects' transform %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredTransform = registry::Register({
    "renderer.effects.transform",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_SetTransform,
    "An effect's transform set: the two blocks kept, and the effect's values that go by them worked out again, up "
    "to its box; compare mode runs ours and the game's on the same effect (docs/renderer.md, \"Water effects, sky "
    "and clouds\")",
    true,
    InstallTransform,
});

bool InstallBegin(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4Effect_Begin || Detour(addr::SC4Effect_Begin, &TSC4Effect_Begin, &g_SC4Effect_Begin)))
        return false;
    g_beginMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effects' start %s", mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredBegin = registry::Register({
    "renderer.effects.begin",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_Begin,
    "An effect started: its clocks and counts set back, its pull, and the random delays and speeds its "
    "description asks for; compare mode runs ours and the game's on the same effect and generator "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallBegin,
});

bool InstallKindState(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4Effect_SetKindState ||
          Detour(addr::SC4Effect_SetKindState, &TSC4Effect_SetKindState, &g_SC4Effect_SetKindState)))
        return false;
    g_kindStateMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effects' render state by kind %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredKindState = registry::Register({
    "renderer.effects.kind_state",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_SetKindState,
    "An effect's render state by the kind of its description, one of eight rows of values; compare mode runs ours "
    "and the game's on the same effect (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallKindState,
});

bool InstallDescription(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4Effect_SetDescription ||
          Detour(addr::SC4Effect_SetDescription, &TSC4Effect_SetDescription, &g_SC4Effect_SetDescription)))
        return false;
    if (mode == registry::Mode::Compare &&
        !((g_SC4Effect_Hold || Detour(addr::SC4Effect_Hold, &TSC4Effect_Hold, &g_SC4Effect_Hold)) &&
          (g_SC4EffectDescription_Release ||
           Detour(addr::SC4EffectDescription_Release, &TSC4EffectDescription_Release,
                  &g_SC4EffectDescription_Release)) &&
          (g_SC4Effect_LoadTexture ||
           Detour(addr::SC4Effect_LoadTexture, &TSC4Effect_LoadTexture, &g_SC4Effect_LoadTexture)) &&
          (g_SC4Effect_SortBatch || Detour(addr::SC4Effect_SortBatch, &TSC4Effect_SortBatch, &g_SC4Effect_SortBatch)) &&
          (g_SC4Effect_StepFlag2000 ||
           Detour(addr::SC4Effect_StepFlag2000, &TSC4Effect_StepFlag2000, &g_SC4Effect_StepFlag2000))))
        return false;
    g_descMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effects taking a description %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDescription = registry::Register({
    "renderer.effects.description",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_SetDescription,
    "An effect taking a description: the description held, its texture and render state, and the effect's values "
    "that come from it; compare mode runs the game's with five of its calls noted and replays ours "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallDescription,
});

bool InstallDescriptionB(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4EffectB_SetDescription ||
          Detour(addr::SC4EffectB_SetDescription, &TSC4EffectB_SetDescription, &g_SC4EffectB_SetDescription)))
        return false;
    if (mode == registry::Mode::Compare &&
        !((g_SC4Effect_SetDescription ||
           Detour(addr::SC4Effect_SetDescription, &TSC4Effect_SetDescription, &g_SC4Effect_SetDescription)) &&
          (g_SC4EffectB_LoadModel ||
           Detour(addr::SC4EffectB_LoadModel, &TSC4EffectB_LoadModel, &g_SC4EffectB_LoadModel)) &&
          Detour(addr::SC4EffectPartB_Attach, &TSC4EffectPartB_Attach, &g_SC4EffectPartB_Attach) &&
          Detour(addr::SC4EffectPartB_Place, &TSC4EffectPartB_Place, &g_SC4EffectPartB_Place) &&
          (g_SC4EffectPartB_SetDescription ||
           Detour(addr::SC4EffectPartB_SetDescription, &TSC4EffectPartB_SetDescription,
                  &g_SC4EffectPartB_SetDescription))))
        return false;
    g_descModeB = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: second effect class taking a description %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDescriptionB = registry::Register({
    "renderer.effects.description_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectB_SetDescription,
    "The second effect class taking a description: its old model released, a model loaded and its instance set "
    "up; compare mode runs the game's with its calls noted and replays ours (docs/renderer.md, \"Water effects, "
    "sky and clouds\")",
    true,
    InstallDescriptionB,
});

bool InstallModelB(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4EffectB_LoadModel ||
          Detour(addr::SC4EffectB_LoadModel, &TSC4EffectB_LoadModel, &g_SC4EffectB_LoadModel)))
        return false;
    g_modelModeB = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: second effect class's model loading %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredModelB = registry::Register({
    "renderer.effects.model_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectB_LoadModel,
    "The second effect class loading its model: the resource asked for, the model made, its size kept and its "
    "materials' flags set; compare mode runs ours and the game's for real and compares the effect and the "
    "materials (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallModelB,
});

bool InstallTexture(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4Effect_LoadTexture ||
          Detour(addr::SC4Effect_LoadTexture, &TSC4Effect_LoadTexture, &g_SC4Effect_LoadTexture)))
        return false;
    g_textureMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effects' texture loading %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredTexture = registry::Register({
    "renderer.effects.texture",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_LoadTexture,
    "An effect's texture loaded: the texture service asked for it by ID and its handle kept; compare mode runs "
    "ours and the game's for real (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallTexture,
});

bool InstallGiveBackB(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookPool())
        return false;
    if (mode == registry::Mode::Compare &&
        !(g_SC4EffectB_RestartParticle ||
          Detour(addr::SC4EffectB_RestartParticle, &TSC4EffectB_RestartParticle, &g_SC4EffectB_RestartParticle)))
        return false;
    g_giveBackModeB = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: second effect class given a particle back %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredGiveBackB = registry::Register({
    "renderer.effects.giveback_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectBaseB_GiveBackParticle,
    "The second effect class given a particle back: started again, or its own effect stopped, a last effect made "
    "where it was, and the particle put on the free list; compare mode runs the game's with its calls noted and "
    "replays ours (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallGiveBackB,
});

bool InstallHolders(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !((g_SC4Effect_SetObject ||
           Detour(addr::SC4Effect_SetObject, &TSC4Effect_SetObject, &g_SC4Effect_SetObject)) &&
          (g_SC4EffectPartB_SetDescription ||
           Detour(addr::SC4EffectPartB_SetDescription, &TSC4EffectPartB_SetDescription,
                  &g_SC4EffectPartB_SetDescription))))
        return false;
    if (mode == registry::Mode::Compare &&
        !((g_SC4Effect_Hold || Detour(addr::SC4Effect_Hold, &TSC4Effect_Hold, &g_SC4Effect_Hold)) &&
          (g_SC4EffectDescription_Release ||
           Detour(addr::SC4EffectDescription_Release, &TSC4EffectDescription_Release,
                  &g_SC4EffectDescription_Release))))
        return false;
    g_holdersMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: two setters of what an effect holds %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredHolders = registry::Register({
    "renderer.effects.holders",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_SetObject,
    "Two setters of what an effect holds, its object at +0x1EC and its part's description: the new one held, the "
    "old one let go; compare mode runs the game's with those noted and replays ours (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallHolders,
});

bool InstallCtor(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(Detour(addr::SC4EffectBase_Construct, &TSC4EffectBase_Construct, &g_SC4EffectBase_Construct) &&
          HookEffectConstructors()))
        return false;
    g_ctorMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effects' constructors %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredCtor = registry::Register({
    "renderer.effects.ctor",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_Construct,
    "The two effect classes' constructors and the sub-object's: the tables of functions, an empty particle list "
    "and the fields' first values; compare mode runs ours and the game's on the same memory (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallCtor,
});

// --- A description's lists read from the effects file ---------------------------------------
//
// A description keeps some of its values in lists (begin, end and end of
// the memory, as the game's own containers have them): one of records of
// seven floats (0x1C bytes), one of records of eight (0x20). Each is read
// from a stream by a function of its own (0x005B0930, 0x005B09D0, both
// (stream, list) answering the stream): the count (the stream's slot
// 0x24), the list resized to it (0x005B0840 with the record to fill with,
// 0x005B08A0 which fills with the float at 0x00B22D54 six times and two
// zeroes), then every float of every record (slot 0x30). The resize cuts
// the list short or has the game's insert (0x005B05A0, 0x005B0700) add
// the records. All four are written by hand; the first float of the
// record the first reader fills with is whatever was on the game's stack,
// zero in ours, and is read over at once.
constexpr uint32_t kListInsertA = 0x005B05A0, kListInsertB = 0x005B0700, kListFill = 0x00B22D54;

// The inserts (0x005B05A0 for records of 0x1C bytes, 0x005B0700 for 0x20;
// (where, count, record), ret 0xC), written by hand as one: with room for
// them the records from `where` on move up and `count` copies of the
// record go in; without, the game's growing insert does it all (0x0059BBA0,
// 0x0059B570). Compared by running ours on a copy of the list in memory of
// the same size and the game's on the list.
constexpr uint32_t kListGrowA = 0x0059BBA0, kListGrowB = 0x0059B570;
using ListInsertFn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t);
ListInsertFn g_SC4EffectDescription_InsertListA = nullptr, g_SC4EffectDescription_InsertListB = nullptr;

void InsertRecords(char* list, char* where, uint32_t count, uint32_t record, std::size_t size, uint32_t grow)
{
    if (!count)
        return;
    char* const end = At<char*>(list, 4);
    if (static_cast<uint32_t>((At<char*>(list, 8) - end) / static_cast<std::ptrdiff_t>(size)) < count) {
        CallThis<uint32_t>(grow, list, where, record, reinterpret_cast<uint32_t>(&record), count, 0u);
        return;
    }
    char value[0x20];
    std::memcpy(value, reinterpret_cast<const void*>(record), size);
    std::memmove(where + count * size, where, end - where);
    for (uint32_t i = 0; i < count; ++i)
        std::memcpy(where + i * size, value, size);
    At<char*>(list, 4) = end + count * size;
}

StepMode g_listsMode = StepMode::Original;

uint32_t InsertByMode(const char* id, char* list, uint32_t where, uint32_t count, uint32_t record, std::size_t size,
                      uint32_t grow, ListInsertFn game)
{
    if (g_listsMode == StepMode::Original)
        return game(list, nullptr, where, count, record);
    if (g_listsMode == StepMode::Ours) {
        InsertRecords(list, reinterpret_cast<char*>(where), count, record, size, grow);
        return 0;
    }
    char* const begin = At<char*>(list, 0);
    const std::size_t used = At<char*>(list, 4) - begin, room = At<char*>(list, 8) - begin;
    char* mine[3] = {nullptr, nullptr, nullptr};
    if (room) {
        mine[0] = reinterpret_cast<char*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(room);
        std::memcpy(mine[0], begin, used);
        mine[1] = mine[0] + used;
        mine[2] = mine[0] + room;
    }
    char value[0x20];
    std::memcpy(value, reinterpret_cast<const void*>(record), size);
    InsertRecords(reinterpret_cast<char*>(mine), mine[0] + (reinterpret_cast<char*>(where) - begin), count,
                  reinterpret_cast<uint32_t>(value), size, grow);
    const uint32_t result = game(list, nullptr, where, count, record);
    const std::size_t theirUsed = At<char*>(list, 4) - At<char*>(list, 0), ourUsed = mine[1] - mine[0];
    const bool same = theirUsed == ourUsed &&
                      static_cast<std::size_t>(At<char*>(list, 8) - At<char*>(list, 0)) ==
                          static_cast<std::size_t>(mine[2] - mine[0]) &&
                      (ourUsed == 0 || std::memcmp(At<char*>(list, 0), mine[0], ourUsed) == 0);
    if (mine[0])
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(mine[0]);
    compare::Record(room >= used + count * size ? (std::string(id) + ".room").c_str() : id, same,
                    same ? std::string() : std::to_string(theirUsed / size) + " records",
                    same ? std::string() : std::to_string(ourUsed / size) + " records");
    return result;
}

uint32_t __fastcall DInsertListA(char* list, void*, uint32_t where, uint32_t count, uint32_t record)
{
    return InsertByMode("effects.lists.insert.a", list, where, count, record, 0x1C, kListGrowA,
                        g_SC4EffectDescription_InsertListA);
}
OPENSC4_THUNK(TSC4EffectDescription_InsertListA, DInsertListA, 12)

uint32_t __fastcall DInsertListB(char* list, void*, uint32_t where, uint32_t count, uint32_t record)
{
    return InsertByMode("effects.lists.insert.b", list, where, count, record, 0x20, kListGrowB,
                        g_SC4EffectDescription_InsertListB);
}
OPENSC4_THUNK(TSC4EffectDescription_InsertListB, DInsertListB, 12)

void ResizeListA(char* list, uint32_t count, uint32_t record)
{
    char* const begin = At<char*>(list, 0);
    char* const end = At<char*>(list, 4);
    const uint32_t size = static_cast<uint32_t>((end - begin) / 0x1C);
    if (count < size)
        At<char*>(list, 4) = begin + count * 0x1C;
    else
        CallThis<uint32_t>(kListInsertA, list, end, count - size, record);
}

void ResizeListB(char* list, uint32_t count)
{
    char* const begin = At<char*>(list, 0);
    char* const end = At<char*>(list, 4);
    const uint32_t size = static_cast<uint32_t>((end - begin) >> 5);
    if (count < size) {
        At<char*>(list, 4) = begin + count * 0x20;
        return;
    }
    const uint32_t fill = M<uint32_t>(kListFill);
    uint32_t record[8] = {fill, fill, fill, fill, fill, fill, 0, 0};
    CallThis<uint32_t>(kListInsertB, list, end, count - size, record);
}

void ResizeListAByMode(char* list, uint32_t count, uint32_t record);
void ResizeListBByMode(char* list, uint32_t count);

uint32_t StreamRead(uint32_t stream, uint32_t slot, uint32_t to)
{
    return CallThis<uint32_t>(M<uint32_t>(M<uint32_t>(stream) + slot), reinterpret_cast<char*>(stream), to);
}

uint32_t ReadListA(uint32_t stream, uint32_t list)
{
    uint32_t count = 0;
    StreamRead(stream, 0x24, reinterpret_cast<uint32_t>(&count));
    const uint32_t fill = M<uint32_t>(kListFill);
    uint32_t record[7] = {0, fill, fill, fill, fill, fill, fill};
    ResizeListAByMode(reinterpret_cast<char*>(list), count, reinterpret_cast<uint32_t>(record));
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t at = M<uint32_t>(list) + i * 0x1C;
        for (uint32_t offset = 0; offset < 0x1C; offset += 4)
            StreamRead(stream, 0x30, at + offset);
    }
    return stream;
}

uint32_t ReadListB(uint32_t stream, uint32_t list)
{
    uint32_t count = 0;
    StreamRead(stream, 0x24, reinterpret_cast<uint32_t>(&count));
    ResizeListBByMode(reinterpret_cast<char*>(list), count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t at = M<uint32_t>(list) + i * 0x20;
        for (uint32_t offset = 0; offset < 0x20; offset += 4)
            StreamRead(stream, 0x30, at + offset);
    }
    return stream;
}

// Their comparison: the game's reads the list, with the stream's two
// slots seen through a copy of its table of functions that notes each
// read and the four bytes it gave; then ours reads into a copy of the list
// as it was before (in memory of the same size), the stream's copy giving
// back the noted values in order. The reads must be the same ones, and
// the two lists the same in length, room and bytes. In the game's run the
// resizes are the game's, in ours' ours.
using ListReadFn = uint32_t(__cdecl*)(uint32_t, uint32_t);
using ListResizeAFn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t);
ListReadFn g_SC4EffectDescription_ReadListA = nullptr, g_SC4EffectDescription_ReadListB = nullptr;
ListResizeAFn g_SC4EffectDescription_ResizeListA = nullptr;
Call1Fn g_SC4EffectDescription_ResizeListB = nullptr;
struct ListRead {
    uint32_t slot;
    uint64_t value;
    uint32_t result;
};
std::vector<ListRead> g_listReads;
std::size_t g_listReplayed = 0;
bool g_listGame = false, g_listReplay = false, g_listWrong = false;
uint32_t g_listStreamReal = 0;
DWORD g_listThread = 0;
std::map<uint32_t, std::vector<uint32_t>> g_listTables;

// A read of `size` bytes through one of the stream's slots (0x10 to 0x34:
// one, two, four or eight bytes).
uint32_t ListStreamRead(char* stream, uint32_t slot, uint32_t to, std::size_t size)
{
    uint32_t real = g_listStreamReal;
    if (!(g_listGame || g_listReplay) || GetCurrentThreadId() != g_listThread) {
        for (const auto& [table, copy] : g_listTables) {
            if (At<uint32_t*>(stream, 0) == copy.data())
                real = table;
        }
        return CallThis<uint32_t>(M<uint32_t>(real + slot), stream, to);
    }
    if (g_listGame) {
        const uint32_t result = CallThis<uint32_t>(M<uint32_t>(real + slot), stream, to);
        ListRead read{slot, 0, result};
        std::memcpy(&read.value, reinterpret_cast<const void*>(to), size);
        g_listReads.push_back(read);
        return result;
    }
    if (g_listReplayed >= g_listReads.size() || g_listReads[g_listReplayed].slot != slot) {
        g_listWrong = true;
        return 0;
    }
    std::memcpy(reinterpret_cast<void*>(to), &g_listReads[g_listReplayed].value, size);
    return g_listReads[g_listReplayed++].result;
}

#define OPENSC4_STREAM_SLOT(Slot, Size)                                                                                \
    uint32_t __fastcall ListStream##Slot(char* stream, void*, uint32_t to)                                             \
    {                                                                                                                  \
        return ListStreamRead(stream, Slot, to, Size);                                                                 \
    }
OPENSC4_STREAM_SLOT(0x10, 1)
OPENSC4_STREAM_SLOT(0x14, 1)
OPENSC4_STREAM_SLOT(0x18, 2)
OPENSC4_STREAM_SLOT(0x1C, 2)
OPENSC4_STREAM_SLOT(0x20, 4)
OPENSC4_STREAM_SLOT(0x24, 4)
OPENSC4_STREAM_SLOT(0x28, 8)
OPENSC4_STREAM_SLOT(0x2C, 8)
OPENSC4_STREAM_SLOT(0x30, 4)
OPENSC4_STREAM_SLOT(0x34, 8)
#undef OPENSC4_STREAM_SLOT

// The stream's slot 0x3C reads a string into a string object (its three
// pointers at +4); the note keeps the bytes, and the replay assigns them
// with the game's own assign (0x00408A70).
constexpr uint32_t kStringAssign = 0x00408A70;
std::vector<std::string> g_listStrings;
std::size_t g_listStringsReplayed = 0;

uint32_t __fastcall ListStreamString(char* stream, void*, uint32_t to)
{
    const bool mine = (g_listGame || g_listReplay) && GetCurrentThreadId() == g_listThread;
    if (mine && g_listReplay) {
        if (g_listReplayed >= g_listReads.size() || g_listReads[g_listReplayed].slot != 0x3C ||
            g_listStringsReplayed >= g_listStrings.size()) {
            g_listWrong = true;
            return 0;
        }
        const std::string& text = g_listStrings[g_listStringsReplayed++];
        uint32_t tag = 0;
        CallThis<uint32_t>(kStringAssign, reinterpret_cast<char*>(to + 4), reinterpret_cast<uint32_t>(text.data()),
                           reinterpret_cast<uint32_t>(text.data() + text.size()), reinterpret_cast<uint32_t>(&tag));
        return g_listReads[g_listReplayed++].result;
    }
    uint32_t real = g_listStreamReal;
    if (!mine) {
        for (const auto& [table, copy] : g_listTables) {
            if (At<uint32_t*>(stream, 0) == copy.data())
                real = table;
        }
    }
    const uint32_t result = CallThis<uint32_t>(M<uint32_t>(real + 0x3C), stream, to);
    if (mine) {
        g_listReads.push_back({0x3C, 0, result});
        g_listStrings.emplace_back(M<const char*>(to + 4), M<const char*>(to + 8));
    }
    return result;
}

// The stream's own table of functions kept, the notes emptied, and the
// copy of the table with the noting slots for the caller to put in.
const std::vector<uint32_t>& ListStreamNoted(uint32_t stream)
{
    g_listStreamReal = M<uint32_t>(stream);
    std::vector<uint32_t>& table = g_listTables[g_listStreamReal];
    if (table.empty()) {
        table.assign(reinterpret_cast<const uint32_t*>(g_listStreamReal),
                     reinterpret_cast<const uint32_t*>(g_listStreamReal) + kBSlots);
        table[0x10 / 4] = reinterpret_cast<uint32_t>(&ListStream0x10);
        table[0x14 / 4] = reinterpret_cast<uint32_t>(&ListStream0x14);
        table[0x18 / 4] = reinterpret_cast<uint32_t>(&ListStream0x18);
        table[0x1C / 4] = reinterpret_cast<uint32_t>(&ListStream0x1C);
        table[0x20 / 4] = reinterpret_cast<uint32_t>(&ListStream0x20);
        table[0x24 / 4] = reinterpret_cast<uint32_t>(&ListStream0x24);
        table[0x28 / 4] = reinterpret_cast<uint32_t>(&ListStream0x28);
        table[0x2C / 4] = reinterpret_cast<uint32_t>(&ListStream0x2C);
        table[0x30 / 4] = reinterpret_cast<uint32_t>(&ListStream0x30);
        table[0x34 / 4] = reinterpret_cast<uint32_t>(&ListStream0x34);
        table[0x3C / 4] = reinterpret_cast<uint32_t>(&ListStreamString);
    }
    g_listThread = GetCurrentThreadId();
    g_listReads.clear();
    g_listStrings.clear();
    g_listReplayed = 0;
    g_listStringsReplayed = 0;
    g_listWrong = false;
    return table;
}

uint32_t ListCompared(const char* id, uint32_t stream, uint32_t list, std::size_t record, ListReadFn game,
                      uint32_t (*ours)(uint32_t, uint32_t))
{
    char* const begin = M<char*>(list);
    const std::size_t used = M<char*>(list + 4) - begin, room = M<char*>(list + 8) - begin;
    uint32_t mine[3] = {0, 0, 0};
    if (room) {
        mine[0] = reinterpret_cast<uint32_t>(
            reinterpret_cast<char*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(room));
        std::memcpy(reinterpret_cast<void*>(mine[0]), begin, used);
        mine[1] = mine[0] + used;
        mine[2] = mine[0] + room;
    }
    const std::vector<uint32_t>& table = ListStreamNoted(stream);
    M<const uint32_t*>(stream) = table.data();
    g_listGame = true;
    const uint32_t result = game(stream, list);
    g_listGame = false;
    g_listReplay = true;
    const uint32_t ourResult = ours(stream, reinterpret_cast<uint32_t>(mine));
    g_listReplay = false;
    M<uint32_t>(stream) = g_listStreamReal;
    const std::size_t theirUsed = M<uint32_t>(list + 4) - M<uint32_t>(list),
                      ourUsed = mine[1] - mine[0];
    const bool same = !g_listWrong && g_listReplayed == g_listReads.size() && result == ourResult &&
                      theirUsed == ourUsed && M<uint32_t>(list + 8) - M<uint32_t>(list) == mine[2] - mine[0] &&
                      (ourUsed == 0 || std::memcmp(M<void*>(list), reinterpret_cast<void*>(mine[0]), ourUsed) == 0);
    if (mine[0])
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(reinterpret_cast<void*>(mine[0]));
    char text[64];
    std::snprintf(text, sizeof text, "%zu reads, %zu records", g_listReads.size(), theirUsed / record);
    const std::string theirs = text;
    std::snprintf(text, sizeof text, "%zu reads%s, %zu records", g_listReplayed, g_listWrong ? " then another" : "",
                  ourUsed / record);
    compare::Record(theirUsed ? id : (std::string(id) + ".empty").c_str(), same, same ? std::string() : theirs,
                    same ? std::string() : std::string(text));
    return result;
}

uint32_t __cdecl ReadListAByMode(uint32_t stream, uint32_t list)
{
    if (g_listsMode == StepMode::Original || g_listGame || g_listReplay)
        return g_SC4EffectDescription_ReadListA(stream, list);
    if (g_listsMode == StepMode::Ours)
        return ReadListA(stream, list);
    return ListCompared("effects.lists.a", stream, list, 0x1C, g_SC4EffectDescription_ReadListA, &ReadListA);
}

uint32_t __cdecl ReadListBByMode(uint32_t stream, uint32_t list)
{
    if (g_listsMode == StepMode::Original || g_listGame || g_listReplay)
        return g_SC4EffectDescription_ReadListB(stream, list);
    if (g_listsMode == StepMode::Ours)
        return ReadListB(stream, list);
    return ListCompared("effects.lists.b", stream, list, 0x20, g_SC4EffectDescription_ReadListB, &ReadListB);
}

void ResizeListAByMode(char* list, uint32_t count, uint32_t record)
{
    if (g_listsMode == StepMode::Original || g_listGame || (g_listsMode == StepMode::Compare && !g_listReplay))
        g_SC4EffectDescription_ResizeListA(list, nullptr, count, record);
    else
        ResizeListA(list, count, record);
}

void ResizeListBByMode(char* list, uint32_t count)
{
    if (g_listsMode == StepMode::Original || g_listGame || (g_listsMode == StepMode::Compare && !g_listReplay))
        g_SC4EffectDescription_ResizeListB(list, nullptr, count);
    else
        ResizeListB(list, count);
}

uint32_t __fastcall DResizeListA(char* list, void*, uint32_t count, uint32_t record)
{
    ResizeListAByMode(list, count, record);
    return 0;
}
OPENSC4_THUNK(TSC4EffectDescription_ResizeListA, DResizeListA, 8)

uint32_t __fastcall DResizeListB(char* list, void*, uint32_t count)
{
    ResizeListBByMode(list, count);
    return 0;
}
OPENSC4_THUNK(TSC4EffectDescription_ResizeListB, DResizeListB, 4)

// A description's constructor (0x005B0FE0, the first thing the whole
// object's constructor 0x005B1660 calls), translated instruction by
// instruction: the first values of its fields, an empty string (eight
// bytes of the game's memory), and one value put in each of seven lists
// (the game's growing inserts 0x00676CC0 and 0x0050C3B0 for a list with
// no room, which is every one here).
uint32_t ConstructDescription(char* self)
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
#include "core/effect_desc_ctor.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// Its comparison: ours on the object's memory, what it left kept, the
// memory put back as it was, and the game's. Where the two differ, the
// words must be the three pointers of a list or string (begin, end, end
// of the memory) of the same length and room and with the same bytes;
// ours' memory is then freed.
constexpr std::size_t kDescriptionCtorSize = 0x200;
StepMode g_descCtorMode = StepMode::Original;
Call0Fn g_SC4EffectDescription_Construct = nullptr;

uint32_t __fastcall DescriptionCtorByMode(char* self, void*)
{
    if (g_descCtorMode == StepMode::Original)
        return g_SC4EffectDescription_Construct(self, nullptr);
    if (g_descCtorMode == StepMode::Ours)
        return ConstructDescription(self);
    uint32_t before[kDescriptionCtorSize / 4], mine[kDescriptionCtorSize / 4];
    std::memcpy(before, self, sizeof before);
    const uint32_t ourResult = ConstructDescription(self);
    std::memcpy(mine, self, sizeof mine);
    std::memcpy(self, before, sizeof before);
    const uint32_t result = g_SC4EffectDescription_Construct(self, nullptr);
    const uint32_t* const theirs = reinterpret_cast<const uint32_t*>(self);
    bool same = result == ourResult;
    std::size_t lists = 0, wrongAt = 0;
    for (std::size_t i = 0; i < kDescriptionCtorSize / 4; ++i) {
        if (mine[i] == theirs[i])
            continue;
        const bool list = i + 2 < kDescriptionCtorSize / 4 && mine[i] && theirs[i] &&
                          mine[i + 1] - mine[i] == theirs[i + 1] - theirs[i] &&
                          mine[i + 2] - mine[i] == theirs[i + 2] - theirs[i] && mine[i + 1] - mine[i] <= 0x100 &&
                          std::memcmp(reinterpret_cast<const void*>(mine[i]), reinterpret_cast<const void*>(theirs[i]),
                                      mine[i + 1] - mine[i] ? mine[i + 1] - mine[i] : 1) == 0;
        if (!list) {
            if (same)
                wrongAt = i * 4;
            same = false;
            continue;
        }
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(reinterpret_cast<void*>(mine[i]));
        ++lists;
        i += 2;
    }
    char text[48];
    std::snprintf(text, sizeof text, "differs at +%zX", wrongAt);
    compare::Record(lists == 8 ? "effects.description_ctor" : "effects.description_ctor.lists", same && lists == 8,
                    same && lists == 8 ? std::string() : std::string("8 lists"),
                    same && lists == 8 ? std::string() : (same ? std::to_string(lists) + " lists" : std::string(text)));
    return result;
}
OPENSC4_THUNK(TSC4EffectDescription_Construct, DescriptionCtorByMode, 0)

bool InstallDescriptionCtor(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !Detour(addr::SC4EffectDescription_Construct, &TSC4EffectDescription_Construct,
                &g_SC4EffectDescription_Construct))
        return false;
    g_descCtorMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: a description's constructor %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDescriptionCtor = registry::Register({
    "renderer.effects.description_ctor",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDescription_Construct,
    "An effect description's constructor: its fields' first values, an empty string and one value in each of seven "
    "lists; compare mode runs ours and the game's on the same memory (docs/renderer.md, \"Water effects, sky and "
    "clouds\")",
    true,
    InstallDescriptionCtor,
});

// The third list: records of a string (the three pointers of the game's
// own) and a float, 0x10 bytes. Its reader (0x005B1D60, (stream, list)
// like the others) reads the count, resizes (0x005B1CD0), then for each
// record the string (0x005811F0, the stream's slot 0x3C into a string of
// the game's and from there into the record) and the float. The resize
// makes a record with an empty string (eight bytes of the game's memory,
// freed at the end) and cuts the list short (0x005B1790, which frees the
// strings) or inserts copies of that record at the end (0x005B1530). The
// insert, like the game's containers': without room the game's growing
// insert (0x005A17F0); with room the record is copied, the records after
// the place are moved up and the copies assigned, with the game's own
// helpers for records that hold strings. All three by hand.
constexpr uint32_t kStringCopy = 0x00408420, kNamedCopyNew = 0x005B0A50, kNamedFillNew = 0x005B0AA0,
                   kNamedCopyBack = 0x005B0AF0, kNamedFill = 0x005B0F40, kNamedGrow = 0x005A17F0,
                   kNamedErase = 0x005B1790, kStreamString = 0x005811F0;
using NamedCopyFn = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t);
ListInsertFn g_SC4EffectDescription_InsertListC = nullptr;
Call1Fn g_SC4EffectDescription_ResizeListC = nullptr;
ListReadFn g_SC4EffectDescription_ReadListC = nullptr;

void InsertNamed(char* list, uint32_t where, uint32_t count, uint32_t record)
{
    if (!count)
        return;
    const uint32_t end = At<uint32_t>(list, 4);
    uint32_t tag = 0;
    if (static_cast<uint32_t>(static_cast<int32_t>(At<uint32_t>(list, 8) - end) >> 4) < count) {
        CallThis<uint32_t>(kNamedGrow, list, where, record, reinterpret_cast<uint32_t>(&tag), count, 0u);
        return;
    }
    uint32_t value[4] = {0, 0, 0, 0};
    CallThis<uint32_t>(kStringCopy, reinterpret_cast<char*>(value), M<uint32_t>(record), M<uint32_t>(record + 4),
                       reinterpret_cast<uint32_t>(&tag));
    value[3] = M<uint32_t>(record + 0xC);
    const uint32_t mine = reinterpret_cast<uint32_t>(value);
    const uint32_t after = static_cast<uint32_t>(static_cast<int32_t>(end - where) >> 4);
    const NamedCopyFn copyNew = reinterpret_cast<NamedCopyFn>(kNamedCopyNew);
    const auto fill = reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t)>(kNamedFill);
    if (after > count) {
        const uint32_t bytes = count * 0x10, from = end - bytes;
        copyNew(from, end, end, reinterpret_cast<uint32_t>(&tag));
        At<uint32_t>(list, 4) += bytes;
        reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(kNamedCopyBack)(
            where, from, end, reinterpret_cast<uint32_t>(&tag), 0);
        fill(where, where + bytes, mine);
    } else {
        reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t)>(kNamedFillNew)(end, count - after, mine);
        const uint32_t filled = At<uint32_t>(list, 4) + (count - after) * 0x10;
        At<uint32_t>(list, 4) = filled;
        copyNew(where, end, filled, reinterpret_cast<uint32_t>(&tag));
        At<uint32_t>(list, 4) += after * 0x10;
        fill(where, end, mine);
    }
    if (value[0])
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(reinterpret_cast<void*>(value[0]));
}

bool ListGames()
{
    return g_listsMode == StepMode::Original || g_listGame || (g_listsMode == StepMode::Compare && !g_listReplay);
}

uint32_t __fastcall DInsertListC(char* list, void*, uint32_t where, uint32_t count, uint32_t record)
{
    if (ListGames())
        return g_SC4EffectDescription_InsertListC(list, nullptr, where, count, record);
    InsertNamed(list, where, count, record);
    return 0;
}
OPENSC4_THUNK(TSC4EffectDescription_InsertListC, DInsertListC, 12)

void ResizeNamed(char* list, uint32_t count)
{
    char* const text = reinterpret_cast<char*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8);
    text[0] = 0;
    uint32_t record[4] = {reinterpret_cast<uint32_t>(text), reinterpret_cast<uint32_t>(text),
                          reinterpret_cast<uint32_t>(text) + 8, 0};
    const uint32_t begin = At<uint32_t>(list, 0), end = At<uint32_t>(list, 4);
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(end - begin) >> 4);
    if (count < size)
        CallThis<uint32_t>(kNamedErase, list, begin + count * 0x10, end);
    else
        DInsertListC(list, nullptr, end, count - size, reinterpret_cast<uint32_t>(record));
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(text);
}

uint32_t __fastcall DResizeListC(char* list, void*, uint32_t count)
{
    if (ListGames())
        return g_SC4EffectDescription_ResizeListC(list, nullptr, count);
    ResizeNamed(list, count);
    return 0;
}
OPENSC4_THUNK(TSC4EffectDescription_ResizeListC, DResizeListC, 4)

uint32_t ReadListC(uint32_t stream, uint32_t list)
{
    uint32_t count = 0;
    StreamRead(stream, 0x24, reinterpret_cast<uint32_t>(&count));
    DResizeListC(reinterpret_cast<char*>(list), nullptr, count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t at = M<uint32_t>(list) + i * 0x10;
        reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t)>(kStreamString)(stream, at);
        StreamRead(stream, 0x30, at + 0xC);
    }
    return stream;
}

// Its comparison is the other two readers', for a list that is empty
// before (they all are): the game's reads the list with the stream's
// reads noted, ours reads the same into an empty list of its own, and the
// two must have the same records, string by string and float by float.
// Ours' list is then destroyed.
uint32_t __cdecl ReadListCByMode(uint32_t stream, uint32_t list)
{
    if (g_listsMode == StepMode::Original || g_listGame || g_listReplay)
        return g_SC4EffectDescription_ReadListC(stream, list);
    if (g_listsMode == StepMode::Ours)
        return ReadListC(stream, list);
    if (M<uint32_t>(list) != M<uint32_t>(list + 4)) {
        compare::Record("effects.lists.c.not_empty", true, std::string(), std::string());
        return g_SC4EffectDescription_ReadListC(stream, list);
    }
    const std::vector<uint32_t>& table = ListStreamNoted(stream);
    M<const uint32_t*>(stream) = table.data();
    g_listGame = true;
    const uint32_t result = g_SC4EffectDescription_ReadListC(stream, list);
    g_listGame = false;
    uint32_t mine[3] = {0, 0, 0};
    g_listReplay = true;
    const uint32_t ourResult = ReadListC(stream, reinterpret_cast<uint32_t>(mine));
    g_listReplay = false;
    M<uint32_t>(stream) = g_listStreamReal;
    const uint32_t theirUsed = M<uint32_t>(list + 4) - M<uint32_t>(list), ourUsed = mine[1] - mine[0];
    bool same = !g_listWrong && g_listReplayed == g_listReads.size() && result == ourResult && theirUsed == ourUsed &&
                M<uint32_t>(list + 8) - M<uint32_t>(list) == mine[2] - mine[0];
    for (uint32_t offset = 0; same && offset < ourUsed; offset += 0x10) {
        const uint32_t theirs = M<uint32_t>(list) + offset, ours = mine[0] + offset;
        const uint32_t length = M<uint32_t>(theirs + 4) - M<uint32_t>(theirs);
        same = length == M<uint32_t>(ours + 4) - M<uint32_t>(ours) && M<uint32_t>(theirs + 0xC) == M<uint32_t>(ours + 0xC) &&
               (length == 0 || std::memcmp(M<void*>(theirs), M<void*>(ours), length) == 0);
    }
    for (uint32_t at = mine[0]; at < mine[1]; at += 0x10) {
        if (M<uint32_t>(at))
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(M<void*>(at));
    }
    if (mine[0])
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(reinterpret_cast<void*>(mine[0]));
    char text[64];
    std::snprintf(text, sizeof text, "%zu reads, %u records", g_listReads.size(), theirUsed / 0x10);
    const std::string theirs = text;
    std::snprintf(text, sizeof text, "%zu reads%s, %u records", g_listReplayed, g_listWrong ? " then another" : "",
                  ourUsed / 0x10);
    compare::Record(theirUsed ? "effects.lists.c" : "effects.lists.c.empty", same, same ? std::string() : theirs,
                    same ? std::string() : std::string(text));
    return result;
}

// The whole description's constructor (0x005B1660): the inner one, then
// no holders (+0x204). Compared by letting the game's run and then ours'
// own line on a count set to something else.
Call0Fn g_SC4EffectDescription_ConstructWhole = nullptr;

uint32_t DescriptionNoHolders(char* self)
{
    At<uint32_t>(self, 0x204) = 0;
    return reinterpret_cast<uint32_t>(self);
}

uint32_t __fastcall DescriptionWholeByMode(char* self, void*)
{
    if (g_listsMode == StepMode::Original)
        return g_SC4EffectDescription_ConstructWhole(self, nullptr);
    if (g_listsMode == StepMode::Ours) {
        CallThis<uint32_t>(addr::SC4EffectDescription_Construct, self);
        return DescriptionNoHolders(self);
    }
    const uint32_t result = g_SC4EffectDescription_ConstructWhole(self, nullptr);
    const uint32_t holders = At<uint32_t>(self, 0x204);
    At<uint32_t>(self, 0x204) = 0xFFFFFFFF;
    const bool same = DescriptionNoHolders(self) == result && At<uint32_t>(self, 0x204) == holders;
    compare::Record("effects.lists.whole_ctor", same, same ? std::string() : std::to_string(holders),
                    same ? std::string() : std::string("0"));
    At<uint32_t>(self, 0x204) = holders;
    return result;
}
OPENSC4_THUNK(TSC4EffectDescription_ConstructWhole, DescriptionWholeByMode, 0)

// A description read from a stream (0x005B1DC0, (stream, description)
// answering the stream), translated instruction by instruction: its
// numbers one after another (the stream's slots 0x14, 0x1C, 0x24 and
// 0x30), its lists through their readers (among them the three above and
// the game's for lists of floats, of points and of IDs) and its string
// (0x005811F0).
uint32_t ReadDescription(uint32_t stream, uint32_t description)
{
    uint32_t stackmem[192] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = stream;
    M<uint32_t>(esp + 8) = description;
    [[maybe_unused]] double st[8] = {};
    [[maybe_unused]] int top = -1;
    [[maybe_unused]] uint32_t status = 0;
#define ST(i) st[top - (i)]
#define PUSH(value)                                                                                                      \
    do {                                                                                                                 \
        const double pushed = (value);                                                                                   \
        st[++top] = pushed;                                                                                              \
    } while (0)
#define POP() --top
#define FCOM(value) status = CompareStatus(ST(0), (value))
#include "core/effect_desc_read.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// Two descriptions' first 0x200 bytes compared: where they differ the
// words must be the three pointers of a list or string of the same length
// and room and with the same bytes, or at +0x1CC of the list of named
// records, compared record by record.
constexpr std::size_t kDescriptionNamed = 0x1CC;

bool SameDescription(const char* a, const char* b, std::size_t* wrongAt)
{
    const uint32_t* const x = reinterpret_cast<const uint32_t*>(a);
    const uint32_t* const y = reinterpret_cast<const uint32_t*>(b);
    for (std::size_t i = 0; i < kDescriptionCtorSize / 4; ++i) {
        if (x[i] == y[i])
            continue;
        *wrongAt = i * 4;
        if (i + 2 >= kDescriptionCtorSize / 4 || !x[i] || !y[i] || x[i + 1] - x[i] != y[i + 1] - y[i] ||
            x[i + 2] - x[i] != y[i + 2] - y[i])
            return false;
        const uint32_t used = x[i + 1] - x[i];
        if (i * 4 == kDescriptionNamed) {
            for (uint32_t offset = 0; offset < used; offset += 0x10) {
                const uint32_t p = x[i] + offset, q = y[i] + offset;
                const uint32_t length = M<uint32_t>(p + 4) - M<uint32_t>(p);
                if (length != M<uint32_t>(q + 4) - M<uint32_t>(q) || M<uint32_t>(p + 0xC) != M<uint32_t>(q + 0xC) ||
                    (length && std::memcmp(M<void*>(p), M<void*>(q), length) != 0))
                    return false;
            }
        } else if (std::memcmp(reinterpret_cast<const void*>(x[i]), reinterpret_cast<const void*>(y[i]),
                               used ? used : 1) != 0) {
            return false;
        }
        i += 2;
    }
    return true;
}

// Its comparison: a description of our own is made by the game's
// constructor; if the game's description is not as new as that one, the
// game's alone reads it. Else the game's reads its description with the
// stream's reads noted, ours reads the same into ours, and the two must be
// the same; ours is then let go. The readers of the lists are the game's
// in both runs.
StepMode g_descReadMode = StepMode::Original;
ListReadFn g_SC4EffectDescription_Read = nullptr;

uint32_t __cdecl ReadDescriptionByMode(uint32_t stream, uint32_t description)
{
    if (g_descReadMode == StepMode::Original || g_listGame || g_listReplay)
        return g_SC4EffectDescription_Read(stream, description);
    if (g_descReadMode == StepMode::Ours)
        return ReadDescription(stream, description);
    char* const mine = reinterpret_cast<char*(__cdecl*)(std::size_t)>(0x005E55E0)(0x208);
    CallThis<uint32_t>(addr::SC4EffectDescription_ConstructWhole, mine);
    std::size_t wrongAt = 0;
    const bool fresh = SameDescription(reinterpret_cast<const char*>(description), mine, &wrongAt);
    uint32_t result = 0;
    if (!fresh) {
        compare::Record("effects.description_read.not_new", true, std::string(), std::string());
        result = g_SC4EffectDescription_Read(stream, description);
    } else {
        const std::vector<uint32_t>& table = ListStreamNoted(stream);
        M<const uint32_t*>(stream) = table.data();
        g_listGame = true;
        result = g_SC4EffectDescription_Read(stream, description);
        g_listGame = false;
        g_listReplay = true;
        const uint32_t ourResult = ReadDescription(stream, reinterpret_cast<uint32_t>(mine));
        g_listReplay = false;
        M<uint32_t>(stream) = g_listStreamReal;
        const bool same = !g_listWrong && g_listReplayed == g_listReads.size() && result == ourResult &&
                          SameDescription(reinterpret_cast<const char*>(description), mine, &wrongAt);
        char text[64];
        std::snprintf(text, sizeof text, "%zu reads", g_listReads.size());
        const std::string theirs = text;
        std::snprintf(text, sizeof text, "%zu reads%s, differs at +%zX", g_listReplayed,
                      g_listWrong ? " then another" : "", wrongAt);
        compare::Record("effects.description_read", same, same ? std::string() : theirs,
                        same ? std::string() : std::string(text));
    }
    At<uint32_t>(mine, 0x204) = 1;
    CallThis<uint32_t>(addr::SC4EffectDescription_Release, mine);
    return result;
}

bool InstallDescriptionRead(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !Detour(addr::SC4EffectDescription_Read, reinterpret_cast<void (*)()>(&ReadDescriptionByMode),
                &g_SC4EffectDescription_Read))
        return false;
    g_descReadMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: a description's reading %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDescriptionRead = registry::Register({
    "renderer.effects.description_read",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDescription_Read,
    "An effect description read from the effects file, number by number and list by list; compare mode notes what "
    "the game's reads from the stream and has ours read the same into a description of its own (docs/renderer.md, "
    "\"Water effects, sky and clouds\")",
    true,
    InstallDescriptionRead,
});

// --- The compound descriptions read from the effects file -----------------------------------
//
// Four more kinds of object are read from the stream by functions of
// their own ((stream, object), by hand here). The first, which this
// file calls a compound description (what the game calls it is not
// known), is read 1,154 times at the start:
// - 0x005ABF50, 0x38 bytes: a word of nine bits, a word, a list of
//   records of 0x6C bytes (+8, reader 0x005ABE30, each record by
//   0x005AB690), a list of records of 0x18 bytes (+0x14, reader
//   0x005ABEC0), a string (+0x2C) and three words (+0x20);
// - 0x005AB760: a word and a string (+4);
// - 0x005AB860: a list of points (0x005A4AD0), a list of floats (+0xC,
//   0x005DB4B0) and a float (+0x18);
// - 0x005AB2F0: a 16-bit word that is dropped, a list of floats and four
//   floats (+0xC).
// A record of 0x6C bytes is a string, then numbers (0x005DA930 reads
// those from +0x14 to +0x4C); one of 0x18 bytes a word of four bits, a
// string (+4), a float and a word. The two lists are resized by
// 0x005ABCE0 and 0x005ABD50 (with a record to fill with; 0x005ABDC0 makes
// the second's), which cut the list short (0x005ABC20, 0x005ABC80) or
// insert at the end (0x005AB970, 0x005ABAD0), and the inserts are the
// game's containers': without room the game's growing insert, with room
// the record copied, the later records moved up and the copies assigned,
// by the game's helpers for these records.
struct RecordOps {
    std::size_t size, stringAt;
    uint32_t copy, copyNew, copyBack, fill, fillNew, grow;
    bool fillNewTagged;
};
constexpr RecordOps kModelRecord{0x6C, 0, 0x005888A0, 0x00589490, 0x005AB790, 0x005AB890, 0x005AB660, 0x0059EDC0, false};
constexpr RecordOps kModelRecordB{0x18, 4, 0x00588A00, 0x005894C0, 0x005AB7E0, 0x005AB8C0, 0x005AB600, 0x005A07B0, true};
constexpr uint32_t kModelRecordCtor = 0x005997B0, kModelErase = 0x005ABC20, kModelEraseB = 0x005ABC80,
                   kStreamPoints = 0x005A4AD0, kStreamFloats = 0x005DB4B0, kStreamNumbers = 0x005DA930;
StepMode g_compoundMode = StepMode::Original;

bool ModelGames()
{
    return g_compoundMode == StepMode::Original || g_listGame ||
           (g_compoundMode == StepMode::Compare && !g_listReplay);
}

void InsertWith(const RecordOps& ops, char* list, uint32_t where, uint32_t count, uint32_t record)
{
    if (!count)
        return;
    const uint32_t end = At<uint32_t>(list, 4);
    uint32_t tag = 0;
    const uint32_t tagAt = reinterpret_cast<uint32_t>(&tag);
    const int32_t size = static_cast<int32_t>(ops.size);
    if (static_cast<uint32_t>(static_cast<int32_t>(At<uint32_t>(list, 8) - end) / size) < count) {
        CallThis<uint32_t>(ops.grow, list, where, record, tagAt, count, 0u);
        return;
    }
    uint32_t value[0x6C / 4];
    CallThis<uint32_t>(ops.copy, reinterpret_cast<char*>(value), record);
    const uint32_t mine = reinterpret_cast<uint32_t>(value);
    const uint32_t after = static_cast<uint32_t>(static_cast<int32_t>(end - where) / size);
    const NamedCopyFn copyNew = reinterpret_cast<NamedCopyFn>(ops.copyNew);
    const auto fill = reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t)>(ops.fill);
    if (after > count) {
        const uint32_t bytes = count * ops.size, from = end - bytes;
        copyNew(from, end, end, tagAt);
        At<uint32_t>(list, 4) += bytes;
        reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(ops.copyBack)(
            where, from, end, tagAt, 0);
        fill(where, where + bytes, mine);
    } else {
        if (ops.fillNewTagged)
            reinterpret_cast<NamedCopyFn>(ops.fillNew)(end, count - after, mine, tagAt);
        else
            reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t)>(ops.fillNew)(end, count - after, mine);
        const uint32_t filled = At<uint32_t>(list, 4) + (count - after) * ops.size;
        At<uint32_t>(list, 4) = filled;
        copyNew(where, end, filled, tagAt);
        At<uint32_t>(list, 4) += after * ops.size;
        fill(where, end, mine);
    }
    if (value[ops.stringAt / 4])
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(reinterpret_cast<void*>(value[ops.stringAt / 4]));
}

ListInsertFn g_SC4EffectCompound_InsertRecords = nullptr, g_SC4EffectCompound_InsertRecordsB = nullptr;
ListResizeAFn g_SC4EffectCompound_ResizeRecords = nullptr, g_SC4EffectCompound_ResizeRecordsB = nullptr;
Call1Fn g_SC4EffectCompound_ResizeRecordsDefaultB = nullptr;
ListReadFn g_SC4EffectCompound_ReadRecord = nullptr, g_SC4EffectCompound_ReadRecords = nullptr,
           g_SC4EffectCompound_ReadRecordsB = nullptr, g_SC4EffectCompound_Read = nullptr,
           g_SC4EffectCompound_ReadNamed = nullptr, g_SC4EffectCompound_ReadCurve = nullptr,
           g_SC4EffectCompound_ReadFloats = nullptr;

uint32_t __fastcall DModelInsert(char* list, void*, uint32_t where, uint32_t count, uint32_t record)
{
    if (ModelGames())
        return g_SC4EffectCompound_InsertRecords(list, nullptr, where, count, record);
    InsertWith(kModelRecord, list, where, count, record);
    return 0;
}
OPENSC4_THUNK(TSC4EffectCompound_InsertRecords, DModelInsert, 12)

uint32_t __fastcall DModelInsertB(char* list, void*, uint32_t where, uint32_t count, uint32_t record)
{
    if (ModelGames())
        return g_SC4EffectCompound_InsertRecordsB(list, nullptr, where, count, record);
    InsertWith(kModelRecordB, list, where, count, record);
    return 0;
}
OPENSC4_THUNK(TSC4EffectCompound_InsertRecordsB, DModelInsertB, 12)

uint32_t __fastcall DModelResize(char* list, void*, uint32_t count, uint32_t record)
{
    if (ModelGames())
        return g_SC4EffectCompound_ResizeRecords(list, nullptr, count, record);
    const uint32_t begin = At<uint32_t>(list, 0), end = At<uint32_t>(list, 4);
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(end - begin) / 0x6C);
    if (count < size)
        return CallThis<uint32_t>(kModelErase, list, begin + count * 0x6C, end);
    return DModelInsert(list, nullptr, end, count - size, record);
}
OPENSC4_THUNK(TSC4EffectCompound_ResizeRecords, DModelResize, 8)

uint32_t __fastcall DModelResizeB(char* list, void*, uint32_t count, uint32_t record)
{
    if (ModelGames())
        return g_SC4EffectCompound_ResizeRecordsB(list, nullptr, count, record);
    const uint32_t begin = At<uint32_t>(list, 0), end = At<uint32_t>(list, 4);
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(end - begin) / 0x18);
    if (count < size)
        return CallThis<uint32_t>(kModelEraseB, list, begin + count * 0x18, end);
    return DModelInsertB(list, nullptr, end, count - size, record);
}
OPENSC4_THUNK(TSC4EffectCompound_ResizeRecordsB, DModelResizeB, 8)

uint32_t __fastcall DModelResizeDefaultB(char* list, void*, uint32_t count)
{
    if (ModelGames())
        return g_SC4EffectCompound_ResizeRecordsDefaultB(list, nullptr, count);
    char* const text = reinterpret_cast<char*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8);
    text[0] = 0;
    uint32_t record[6] = {0, reinterpret_cast<uint32_t>(text), reinterpret_cast<uint32_t>(text),
                          reinterpret_cast<uint32_t>(text) + 8, 0, 0xFFFFFFFF};
    DModelResizeB(list, nullptr, count, reinterpret_cast<uint32_t>(record));
    if (record[1])
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(reinterpret_cast<void*>(record[1]));
    return 0;
}
OPENSC4_THUNK(TSC4EffectCompound_ResizeRecordsDefaultB, DModelResizeDefaultB, 4)

uint32_t StreamHelper(uint32_t helper, uint32_t stream, uint32_t to)
{
    return reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t)>(helper)(stream, to);
}

uint32_t __cdecl DModelReadRecord(uint32_t stream, uint32_t record)
{
    if (ModelGames())
        return g_SC4EffectCompound_ReadRecord(stream, record);
    StreamHelper(kStreamString, stream, record);
    uint32_t scratch = stream;
    StreamRead(stream, 0x14, reinterpret_cast<uint32_t>(&scratch));
    M<uint32_t>(record + 0xC) = scratch & 0xFF;
    uint32_t word = record;
    StreamRead(stream, 0x24, reinterpret_cast<uint32_t>(&word));
    M<uint32_t>(record + 0x10) = word & 3;
    StreamHelper(kStreamNumbers, stream, record + 0x14);
    StreamRead(stream, 0x14, record + 0x4C);
    StreamRead(stream, 0x14, record + 0x4D);
    StreamRead(stream, 0x1C, record + 0x4E);
    StreamRead(stream, 0x1C, record + 0x50);
    StreamRead(stream, 0x30, record + 0x54);
    StreamRead(stream, 0x30, record + 0x58);
    StreamRead(stream, 0x30, record + 0x5C);
    StreamRead(stream, 0x30, record + 0x60);
    StreamRead(stream, 0x1C, record + 0x64);
    StreamRead(stream, 0x1C, record + 0x66);
    return StreamRead(stream, 0x20, record + 0x68);
}

uint32_t __cdecl DModelReadRecords(uint32_t stream, uint32_t list)
{
    if (ModelGames())
        return g_SC4EffectCompound_ReadRecords(stream, list);
    uint32_t count = 0;
    StreamRead(stream, 0x24, reinterpret_cast<uint32_t>(&count));
    uint32_t record[0x6C / 4];
    CallThis<uint32_t>(kModelRecordCtor, reinterpret_cast<char*>(record));
    DModelResize(reinterpret_cast<char*>(list), nullptr, count, reinterpret_cast<uint32_t>(record));
    if (record[0])
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(reinterpret_cast<void*>(record[0]));
    for (uint32_t i = 0; i < count; ++i)
        DModelReadRecord(stream, M<uint32_t>(list) + i * 0x6C);
    return stream;
}

uint32_t __cdecl DModelReadRecordsB(uint32_t stream, uint32_t list)
{
    if (ModelGames())
        return g_SC4EffectCompound_ReadRecordsB(stream, list);
    uint32_t count = 0;
    StreamRead(stream, 0x24, reinterpret_cast<uint32_t>(&count));
    DModelResizeDefaultB(reinterpret_cast<char*>(list), nullptr, count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t at = M<uint32_t>(list) + i * 0x18;
        uint32_t word = 0;
        StreamRead(stream, 0x24, reinterpret_cast<uint32_t>(&word));
        M<uint32_t>(at) = word & 0xF;
        StreamHelper(kStreamString, stream, at + 4);
        StreamRead(stream, 0x30, at + 0x10);
        StreamRead(stream, 0x24, at + 0x14);
    }
    return stream;
}

uint32_t ReadModel(uint32_t stream, uint32_t object)
{
    uint32_t word = 0;
    StreamRead(stream, 0x24, reinterpret_cast<uint32_t>(&word));
    M<uint32_t>(object) = word & 0x1FF;
    StreamRead(stream, 0x24, object + 4);
    DModelReadRecords(stream, object + 8);
    DModelReadRecordsB(stream, object + 0x14);
    StreamHelper(kStreamString, stream, object + 0x2C);
    StreamRead(stream, 0x24, object + 0x20);
    StreamRead(stream, 0x24, object + 0x24);
    return StreamRead(stream, 0x24, object + 0x28);
}

uint32_t ReadModelNamed(uint32_t stream, uint32_t object)
{
    StreamRead(stream, 0x24, object);
    return StreamHelper(kStreamString, stream, object + 4);
}

uint32_t ReadModelCurve(uint32_t stream, uint32_t object)
{
    StreamHelper(kStreamPoints, stream, object);
    StreamHelper(kStreamFloats, stream, object + 0xC);
    return StreamRead(stream, 0x30, object + 0x18);
}

uint32_t ReadModelFloats(uint32_t stream, uint32_t object)
{
    uint32_t dropped = stream;
    StreamRead(stream, 0x1C, reinterpret_cast<uint32_t>(&dropped));
    StreamHelper(kStreamFloats, stream, object);
    StreamRead(stream, 0x30, object + 0xC);
    StreamRead(stream, 0x30, object + 0x10);
    StreamRead(stream, 0x30, object + 0x14);
    return StreamRead(stream, 0x30, object + 0x18);
}

// Their comparison is the description's, without a constructor to make a
// new object with: ours reads into a copy of the object as it was. A
// list of plain numbers is copied; a list of records must have been
// empty with no memory and a string empty (the copy gets an empty one of
// its own), else the game's alone reads. What the two then hold is compared
// field by field and ours' memory freed. The bytes of a 0x6C record that
// nothing ever writes (+0x16, +0x17, +0x52, +0x53) are left out.
struct ModelField {
    enum Kind { Words, Plain, String, Records, RecordsB } kind;
    std::size_t at, size;
};

bool SameText(uint32_t a, uint32_t b)
{
    const uint32_t length = M<uint32_t>(a + 4) - M<uint32_t>(a);
    return length == M<uint32_t>(b + 4) - M<uint32_t>(b) &&
           (length == 0 || std::memcmp(M<void*>(a), M<void*>(b), length) == 0);
}

void FreeAt(uint32_t pointerAt)
{
    if (M<uint32_t>(pointerAt))
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(M<void*>(pointerAt));
}

bool SameModelField(const ModelField& field, uint32_t theirs, uint32_t ours)
{
    const uint32_t a = theirs + field.at, b = ours + field.at;
    if (field.kind == ModelField::Words)
        return std::memcmp(reinterpret_cast<const void*>(a), reinterpret_cast<const void*>(b), field.size) == 0;
    if (field.kind == ModelField::String)
        return SameText(a, b);
    const uint32_t used = M<uint32_t>(a + 4) - M<uint32_t>(a);
    if (used != M<uint32_t>(b + 4) - M<uint32_t>(b) || M<uint32_t>(a + 8) - M<uint32_t>(a) != M<uint32_t>(b + 8) - M<uint32_t>(b))
        return false;
    if (field.kind == ModelField::Plain)
        return used == 0 || std::memcmp(M<void*>(a), M<void*>(b), used) == 0;
    for (uint32_t offset = 0; offset < used; offset += field.size) {
        const uint32_t p = M<uint32_t>(a) + offset, q = M<uint32_t>(b) + offset;
        if (field.kind == ModelField::RecordsB) {
            if (M<uint32_t>(p) != M<uint32_t>(q) || !SameText(p + 4, q + 4) || M<uint32_t>(p + 0x10) != M<uint32_t>(q + 0x10) ||
                M<uint32_t>(p + 0x14) != M<uint32_t>(q + 0x14))
                return false;
            continue;
        }
        if (!SameText(p, q))
            return false;
        for (uint32_t i = 0xC; i < 0x6C; ++i) {
            if (i != 0x16 && i != 0x17 && i != 0x52 && i != 0x53 && M<uint8_t>(p + i) != M<uint8_t>(q + i))
                return false;
        }
    }
    return true;
}

uint32_t ModelCompared(const char* id, uint32_t stream, uint32_t object, std::size_t size,
                       std::initializer_list<ModelField> fields, ListReadFn game, uint32_t (*ours)(uint32_t, uint32_t))
{
    uint32_t mine[0x38 / 4];
    std::memcpy(mine, reinterpret_cast<const void*>(object), size);
    const uint32_t mineAt = reinterpret_cast<uint32_t>(mine);
    bool asNew = true;
    for (const ModelField& field : fields) {
        if (field.kind == ModelField::String)
            asNew = asNew && M<uint32_t>(object + field.at) == M<uint32_t>(object + field.at + 4);
        else if (field.kind == ModelField::Records || field.kind == ModelField::RecordsB)
            asNew = asNew && M<uint32_t>(object + field.at) == 0;
    }
    if (!asNew) {
        compare::Record((std::string(id) + ".not_new").c_str(), true, std::string(), std::string());
        return game(stream, object);
    }
    for (const ModelField& field : fields) {
        const uint32_t from = object + field.at, room = M<uint32_t>(from + 8) - M<uint32_t>(from);
        if (field.kind == ModelField::Plain && room) {
            char* const copy = reinterpret_cast<char*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(room);
            const uint32_t used = M<uint32_t>(from + 4) - M<uint32_t>(from);
            std::memcpy(copy, M<const void*>(from), used);
            M<char*>(mineAt + field.at) = copy;
            M<char*>(mineAt + field.at + 4) = copy + used;
            M<char*>(mineAt + field.at + 8) = copy + room;
        }
        if (field.kind != ModelField::String)
            continue;
        char* const text = reinterpret_cast<char*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8);
        text[0] = 0;
        M<char*>(mineAt + field.at) = text;
        M<char*>(mineAt + field.at + 4) = text;
        M<char*>(mineAt + field.at + 8) = text + 8;
    }
    const std::vector<uint32_t>& table = ListStreamNoted(stream);
    M<const uint32_t*>(stream) = table.data();
    g_listGame = true;
    const uint32_t result = game(stream, object);
    g_listGame = false;
    g_listReplay = true;
    const uint32_t ourResult = ours(stream, mineAt);
    g_listReplay = false;
    M<uint32_t>(stream) = g_listStreamReal;
    bool same = !g_listWrong && g_listReplayed == g_listReads.size() && result == ourResult;
    std::size_t wrongAt = 0;
    for (const ModelField& field : fields) {
        if (same && !SameModelField(field, object, mineAt)) {
            same = false;
            wrongAt = field.at;
        }
    }
    for (const ModelField& field : fields) {
        const uint32_t at = mineAt + field.at;
        if (field.kind == ModelField::Words)
            continue;
        if (field.kind == ModelField::Records || field.kind == ModelField::RecordsB) {
            for (uint32_t p = M<uint32_t>(at); p < M<uint32_t>(at + 4); p += field.size)
                FreeAt(p + (field.kind == ModelField::RecordsB ? 4 : 0));
        }
        FreeAt(at);
    }
    char text[64];
    std::snprintf(text, sizeof text, "%zu reads", g_listReads.size());
    const std::string theirs = text;
    std::snprintf(text, sizeof text, "%zu reads%s, differs at +%zX", g_listReplayed, g_listWrong ? " then another" : "",
                  wrongAt);
    compare::Record(id, same, same ? std::string() : theirs, same ? std::string() : std::string(text));
    return result;
}

bool ModelPassed()
{
    return g_compoundMode == StepMode::Original || g_listGame || g_listReplay;
}

uint32_t __cdecl ReadModelByMode(uint32_t stream, uint32_t object)
{
    if (ModelPassed())
        return g_SC4EffectCompound_Read(stream, object);
    if (g_compoundMode == StepMode::Ours)
        return ReadModel(stream, object);
    return ModelCompared("effects.compound_read", stream, object, 0x38,
                         {{ModelField::Words, 0, 8},
                          {ModelField::Records, 8, 0x6C},
                          {ModelField::RecordsB, 0x14, 0x18},
                          {ModelField::Words, 0x20, 0xC},
                          {ModelField::String, 0x2C, 0}},
                         g_SC4EffectCompound_Read, &ReadModel);
}

uint32_t __cdecl ReadModelNamedByMode(uint32_t stream, uint32_t object)
{
    if (ModelPassed())
        return g_SC4EffectCompound_ReadNamed(stream, object);
    if (g_compoundMode == StepMode::Ours)
        return ReadModelNamed(stream, object);
    return ModelCompared("effects.compound_read.named", stream, object, 0x10,
                         {{ModelField::Words, 0, 4}, {ModelField::String, 4, 0}}, g_SC4EffectCompound_ReadNamed,
                         &ReadModelNamed);
}

uint32_t __cdecl ReadModelCurveByMode(uint32_t stream, uint32_t object)
{
    if (ModelPassed())
        return g_SC4EffectCompound_ReadCurve(stream, object);
    if (g_compoundMode == StepMode::Ours)
        return ReadModelCurve(stream, object);
    return ModelCompared("effects.compound_read.curve", stream, object, 0x1C,
                         {{ModelField::Plain, 0, 0}, {ModelField::Plain, 0xC, 0}, {ModelField::Words, 0x18, 4}},
                         g_SC4EffectCompound_ReadCurve, &ReadModelCurve);
}

uint32_t __cdecl ReadModelFloatsByMode(uint32_t stream, uint32_t object)
{
    if (ModelPassed())
        return g_SC4EffectCompound_ReadFloats(stream, object);
    if (g_compoundMode == StepMode::Ours)
        return ReadModelFloats(stream, object);
    return ModelCompared("effects.compound_read.floats", stream, object, 0x1C,
                         {{ModelField::Plain, 0, 0}, {ModelField::Words, 0xC, 0x10}}, g_SC4EffectCompound_ReadFloats,
                         &ReadModelFloats);
}

bool InstallModelDescription(registry::Mode mode)
{
    const auto plain = [](auto function) { return reinterpret_cast<void (*)()>(function); };
    if (!game::IsBuild641() ||
        !(Detour(addr::SC4EffectCompound_Read, plain(&ReadModelByMode), &g_SC4EffectCompound_Read) &&
          Detour(addr::SC4EffectCompound_ReadNamed, plain(&ReadModelNamedByMode), &g_SC4EffectCompound_ReadNamed) &&
          Detour(addr::SC4EffectCompound_ReadCurve, plain(&ReadModelCurveByMode), &g_SC4EffectCompound_ReadCurve) &&
          Detour(addr::SC4EffectCompound_ReadFloats, plain(&ReadModelFloatsByMode), &g_SC4EffectCompound_ReadFloats) &&
          Detour(addr::SC4EffectCompound_ReadRecord, plain(&DModelReadRecord), &g_SC4EffectCompound_ReadRecord) &&
          Detour(addr::SC4EffectCompound_ReadRecords, plain(&DModelReadRecords), &g_SC4EffectCompound_ReadRecords) &&
          Detour(addr::SC4EffectCompound_ReadRecordsB, plain(&DModelReadRecordsB), &g_SC4EffectCompound_ReadRecordsB) &&
          Detour(addr::SC4EffectCompound_ResizeRecords, &TSC4EffectCompound_ResizeRecords,
                 &g_SC4EffectCompound_ResizeRecords) &&
          Detour(addr::SC4EffectCompound_ResizeRecordsB, &TSC4EffectCompound_ResizeRecordsB,
                 &g_SC4EffectCompound_ResizeRecordsB) &&
          Detour(addr::SC4EffectCompound_ResizeRecordsDefaultB, &TSC4EffectCompound_ResizeRecordsDefaultB,
                 &g_SC4EffectCompound_ResizeRecordsDefaultB) &&
          Detour(addr::SC4EffectCompound_InsertRecords, &TSC4EffectCompound_InsertRecords,
                 &g_SC4EffectCompound_InsertRecords) &&
          Detour(addr::SC4EffectCompound_InsertRecordsB, &TSC4EffectCompound_InsertRecordsB,
                 &g_SC4EffectCompound_InsertRecordsB)))
        return false;
    g_compoundMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the compound descriptions' reading %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredModelDescription = registry::Register({
    "renderer.effects.compound_read",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectCompound_Read,
    "The compound descriptions and three smaller objects read from the effects file, with their two lists of "
    "records, the lists' resizes and inserts; compare mode notes what the game's reads from the stream and has ours "
    "read the same into a copy of the object (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallModelDescription,
});

bool InstallLists(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(Detour(addr::SC4EffectDescription_ReadListA, reinterpret_cast<void (*)()>(&ReadListAByMode),
                 &g_SC4EffectDescription_ReadListA) &&
          Detour(addr::SC4EffectDescription_ReadListB, reinterpret_cast<void (*)()>(&ReadListBByMode),
                 &g_SC4EffectDescription_ReadListB) &&
          Detour(addr::SC4EffectDescription_ResizeListA, &TSC4EffectDescription_ResizeListA,
                 &g_SC4EffectDescription_ResizeListA) &&
          Detour(addr::SC4EffectDescription_ResizeListB, &TSC4EffectDescription_ResizeListB,
                 &g_SC4EffectDescription_ResizeListB) &&
          Detour(addr::SC4EffectDescription_InsertListA, &TSC4EffectDescription_InsertListA,
                 &g_SC4EffectDescription_InsertListA) &&
          Detour(addr::SC4EffectDescription_InsertListB, &TSC4EffectDescription_InsertListB,
                 &g_SC4EffectDescription_InsertListB) &&
          Detour(addr::SC4EffectDescription_ReadListC, reinterpret_cast<void (*)()>(&ReadListCByMode),
                 &g_SC4EffectDescription_ReadListC) &&
          Detour(addr::SC4EffectDescription_ResizeListC, &TSC4EffectDescription_ResizeListC,
                 &g_SC4EffectDescription_ResizeListC) &&
          Detour(addr::SC4EffectDescription_InsertListC, &TSC4EffectDescription_InsertListC,
                 &g_SC4EffectDescription_InsertListC) &&
          Detour(addr::SC4EffectDescription_ConstructWhole, &TSC4EffectDescription_ConstructWhole,
                 &g_SC4EffectDescription_ConstructWhole)))
        return false;
    g_listsMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: a description's lists' reading %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredLists = registry::Register({
    "renderer.effects.lists",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDescription_ReadListA,
    "A description's three lists read from the effects file, their resizing and the inserts under it, and the "
    "whole constructor's last line; "
    "compare mode notes what the game's reads from the stream and has ours read the same into a copy of the list, "
    "and runs the inserts on a copy (docs/renderer.md, "
    "\"Water effects, sky and clouds\")",
    true,
    InstallLists,
});

bool InstallRelease(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4EffectDescription_Release ||
          Detour(addr::SC4EffectDescription_Release, &TSC4EffectDescription_Release,
                 &g_SC4EffectDescription_Release)))
        return false;
    g_releaseMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: a description's letting go %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredRelease = registry::Register({
    "renderer.effects.release",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDescription_Release,
    "A description let go by one of its holders, and destroyed by the last; compare mode runs ours and the game's "
    "on the count while a holder is left (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallRelease,
});

bool InstallDtor(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(Detour(addr::SC4Effect_Destruct, &TSC4Effect_Destruct, &g_SC4Effect_Destruct) &&
          Detour(addr::SC4EffectB_Destruct, &TSC4EffectB_Destruct, &g_SC4EffectB_Destruct) &&
          Detour(addr::SC4Effect_Delete, &TSC4Effect_Delete, &g_SC4Effect_Delete) &&
          Detour(addr::SC4EffectB_Delete, &TSC4EffectB_Delete, &g_SC4EffectB_Delete)))
        return false;
    // The comparison notes what the game's destructors call.
    if (mode == registry::Mode::Compare &&
        !(Detour(addr::SC4EffectBatch_Destruct, &TSC4EffectBatch_Destruct, &g_SC4EffectBatch_Destruct) &&
          Detour(addr::SC4EffectBase_Destruct, &TSC4EffectBase_Destruct, &g_SC4EffectBase_Destruct) &&
          Detour(addr::SC4EffectPartB_Destruct, &TSC4EffectPartB_Destruct, &g_SC4EffectPartB_Destruct) &&
          (g_SC4EffectDescription_Release ||
           Detour(addr::SC4EffectDescription_Release, &TSC4EffectDescription_Release,
                  &g_SC4EffectDescription_Release))))
        return false;
    g_dtorMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effects' destructors %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDtor = registry::Register({
    "renderer.effects.dtor",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_Destruct,
    "The two effect classes' destructors, alone and with the memory freed: what the effect holds released or let "
    "go, its batch, part and sub-object destroyed; compare mode notes the game's calls and checks ours would make "
    "the same (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallDtor,
});

bool InstallGround(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4Effect_PlaceOnGround ||
          Detour(addr::SC4Effect_PlaceOnGround, &TSC4Effect_PlaceOnGround, &g_SC4Effect_PlaceOnGround)))
        return false;
    g_groundMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: particles' placing on the ground %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredGround = registry::Register({
    "renderer.effects.ground",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_PlaceOnGround,
    "A new particle put on the ground or ended, by the terrain's height and the kind of its cell; compare mode runs "
    "ours and the game's on the same particle and generator (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallGround,
});

bool InstallPool(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookPool())
        return false;
    if (mode == registry::Mode::Compare && !HookStart())
        return false;
    g_poolMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: particle pool %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredPool = registry::Register({
    "renderer.effects.pool",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectBase_TakeParticle,
    "The base effect class's use of the particle pool: taking a particle, adding it to the effect's list and "
    "giving it back; compare mode runs ours and the game's from the same state (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallPool,
});

bool InstallEnding(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookPool())
        return false;
    if (mode == registry::Mode::Compare && !HookFrees())
        return false;
    g_endingMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effects' stopping and ending %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredEnding = registry::Register({
    "renderer.effects.ending",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectBase_SetEnding,
    "An effect stopped, started again or ended: both classes' stop setters and the sub-object's routine that keeps "
    "the marks or gives every particle back; compare mode records the game's run and replays ours, as "
    "renderer.effects.effect (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallEnding,
});

bool InstallLetGo(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookPool())
        return false;
    if (mode == registry::Mode::Compare) {
        if (!(HookFrees() &&
              (g_SC4EffectBase_Stop ||
               Detour(addr::SC4EffectBase_Stop, &TSC4EffectBase_Stop, &g_SC4EffectBase_Stop)) &&
              (g_SC4EffectDescription_Release ||
               Detour(addr::SC4EffectDescription_Release, &TSC4EffectDescription_Release,
                      &g_SC4EffectDescription_Release))))
            return false;
        g_letGoHooked = true;
    }
    g_letGoMode = g_letGoModeB = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effects letting go of what they hold %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredLetGo = registry::Register({
    "renderer.effects.letgo",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectBase_LetGo,
    "An effect letting go of what it holds (both classes): objects released, its description let go, and the "
    "effect stopped; "
    "compare mode records the game's run and replays ours, with the two releases noted through stand-in tables of "
    "functions (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallLetGo,
});

bool InstallStop(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4EffectBase_Stop || Detour(addr::SC4EffectBase_Stop, &TSC4EffectBase_Stop, &g_SC4EffectBase_Stop)))
        return false;
    if (mode == registry::Mode::Compare && !(HookFrees() && HookPool()))
        return false;
    g_stopMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effects stopped outright %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredStop = registry::Register({
    "renderer.effects.stop",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectBase_Stop,
    "An effect stopped outright: its marks cleared, every particle given back, no longer in use; compare mode "
    "records the game's run and replays ours, as renderer.effects.effect (docs/renderer.md, \"Water effects, sky "
    "and clouds\")",
    true,
    InstallStop,
});

bool HookFinish()
{
    return g_SC4EffectBase_Finish ||
           Detour(addr::SC4EffectBase_Finish, &TSC4EffectBase_Finish, &g_SC4EffectBase_Finish);
}

bool InstallFinish(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookFinish())
        return false;
    if (mode == registry::Mode::Compare && !(HookFrees() && HookPool()))
        return false;
    g_finishMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effect end check %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredFinish = registry::Register({
    "renderer.effects.finish",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectBase_Finish,
    "Whether an effect is over: with no particle left, a marked effect is ended or released; compare mode records "
    "the game's run and replays ours, as renderer.effects.effect (docs/renderer.md, \"Water effects, sky and "
    "clouds\")",
    true,
    InstallFinish,
});

bool HookWander()
{
    return g_SC4Effect_StepFlag800000 ||
           Detour(addr::SC4Effect_StepFlag800000, &TSC4Effect_StepFlag800000, &g_SC4Effect_StepFlag800000);
}

bool InstallWander(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookWander())
        return false;
    g_wanderMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: effect wandering %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredWander = registry::Register({
    "renderer.effects.wander",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StepFlag800000,
    "An effect's wandering: a timer, and at its end a random turn of the effect's own velocity; compare mode runs "
    "ours and the game's from the same effect and the same generator state (docs/renderer.md, \"Water effects, "
    "sky and clouds\")",
    true,
    InstallWander,
});

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
                ok = (*original || Detour(address, thunk, original)) && ok;
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
        if (bits & bit)
            ok = HookFinish() && ok;
        bit <<= 1;
        if (bits & bit)
            ok = HookWander() && ok;
        bit <<= 1;
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

bool HookDecalDraw()
{
    return g_decalDraw || Detour(addr::SC4EffectDecal_Draw, &TDecalDraw, &g_decalDraw);
}

bool InstallDecal(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookDecalDraw())
        return false;
    g_decalMode = mode == registry::Mode::Compare ? DecalMode::Compare : DecalMode::Ours;
    log::Write("water effects: decals' step %s", mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDecal = registry::Register({
    "renderer.effects.decal",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDecal_Draw,
    "A decal's step: its time, and the opacity, values and position its curves give at that time, passed to its "
    "owner; compare mode notes the calls of ours and of the game's through a stand-in owner (docs/renderer.md, "
    "\"Water effects, sky and clouds\")",
    true,
    InstallDecal,
});

// --- The effects manager's clean-up ----------------------------------------------------------

// The clean-up after the draw and the three functions under it
// (effects_cleanup.inc). The clean-up cannot run twice: a compound effect
// that has ended lets go of its effects, and the other lists go by who still
// holds what. So its comparison runs the game's first, noting the answers of
// the end test (0x005BFD70) and the erases, and stops it at its last call
// (the start of the waiting effects), where nothing has changed yet but
// what the clean-up itself does. There ours runs on a copy of the manager
// with copies of the twelve vectors, the compound effects put back as they
// were, the end test answered from the notes and the erase only noted, each
// with what the game's call changed in any of the compound effects (one
// ends itself in the test, and the effects an erase lets go of may let go of
// another); the
// other lists' questions are asked again for real, being plain reads. Then
// the calls, the vectors (what they hold and their room), the count and the
// compound effects are compared.
constexpr uint32_t kAssignEffectRecord = 0x0058DBA0; // (this = record, from), ret 4
constexpr uint32_t kInsertWords = 0x00414040;        // (this = vector, at, first, last, unused), ret 0x10

StepMode g_cleanMode = StepMode::Original;
// The manager object, once its clean-up has run.
char* g_effectsManager = nullptr;
Call0Fn g_SC4EffectsManager_StartQueued = nullptr, g_SC4CompoundEffect_Ended = nullptr;
Call2Fn g_SC4EffectsManager_StartCompound = nullptr, g_SC4EffectRecords_Erase = nullptr;
using CopyRecordsFn = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
CopyRecordsFn g_SC4EffectRecords_Copy = nullptr;

struct CleanCall {
    uint32_t target, self, a, b;
    bool operator==(const CleanCall& other) const
    {
        return target == other.target && self == other.self && a == other.a && b == other.b;
    }
};
bool g_cleanNoting = false, g_cleanReplay = false, g_cleanReplayed = false;
std::vector<CleanCall> g_cleanGameCalls, g_cleanOurCalls;
std::vector<uint32_t> g_cleanAnswers;
std::vector<std::vector<std::pair<uint32_t, char>>> g_cleanChanges;
std::size_t g_cleanAnswer = 0;
constexpr std::size_t kCompoundSize = 0x120;
char* g_cleanSelf = nullptr;
char* g_cleanMine = nullptr;
std::vector<char*> g_cleanCompounds;
std::vector<char> g_cleanBefore, g_cleanShadow;
uint64_t g_cleanCompared = 0, g_cleanEnded = 0, g_cleanFreed = 0;

bool g_queueNoting = false, g_queueReplay = false;
std::vector<CleanCall> g_queueGameCalls, g_queueOurCalls;
std::vector<std::vector<uint32_t>> g_queueAfter;
char* g_queueSelf = nullptr;
uint64_t g_queueCompared = 0, g_queueStarted = 0;

void RecordEnd(char* held)
{
    BOurs(0x20, reinterpret_cast<uint32_t>(held), 0, 0,
          [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(held, 0), 0x20), held); });
}

// What a noted call of the game's changed in the compound effects, by its
// place in the copy of them all; the replay makes the same changes at the
// same call. The end test is made of each compound effect in turn, and
// one that leaves its own unchanged has ended nothing: only that one is
// looked at then (`own`, its place among them).
void CleanNoteChanges(std::size_t own)
{
    std::vector<std::pair<uint32_t, char>> changes;
    if (own < g_cleanCompounds.size() &&
        std::memcmp(g_cleanCompounds[own], g_cleanShadow.data() + own * kCompoundSize, kCompoundSize) == 0) {
        g_cleanChanges.push_back(std::move(changes));
        return;
    }
    for (std::size_t i = 0; i < g_cleanCompounds.size(); ++i) {
        char* const shadow = g_cleanShadow.data() + i * kCompoundSize;
        if (std::memcmp(g_cleanCompounds[i], shadow, kCompoundSize) == 0)
            continue;
        for (std::size_t at = 0; at < kCompoundSize; ++at) {
            if (g_cleanCompounds[i][at] != shadow[at])
                changes.push_back({static_cast<uint32_t>(i * kCompoundSize + at), g_cleanCompounds[i][at]});
        }
        std::memcpy(shadow, g_cleanCompounds[i], kCompoundSize);
    }
    g_cleanChanges.push_back(std::move(changes));
}

void CleanApplyChanges()
{
    const std::size_t call = g_cleanOurCalls.size() - 1;
    if (call >= g_cleanChanges.size())
        return;
    for (const auto& [at, value] : g_cleanChanges[call])
        g_cleanCompounds[at / kCompoundSize][at % kCompoundSize] = value;
}

uint32_t CleanEnded(char* compound)
{
    if (!g_cleanReplay)
        return CallThis<uint32_t>(addr::SC4CompoundEffect_Ended, compound) & 0xFF;
    g_cleanOurCalls.push_back({addr::SC4CompoundEffect_Ended, reinterpret_cast<uint32_t>(compound), 0, 0});
    CleanApplyChanges();
    return g_cleanAnswer < g_cleanAnswers.size() ? g_cleanAnswers[g_cleanAnswer++] : 0u;
}

char* CopyEffectRecords(char* first, char* last, char* to);
char* EraseEffectRecords(char* records, char* first, char* last);
void StartQueuedEffects(char* self);
// The start of a compound effect by the mode of its own replacement, and
// what its comparison notes of an erase; defined with it.
uint32_t StartCompoundRun(char* self, uint32_t compound, uint32_t flag);
bool ScNoting();
uint32_t ScErased(char* records, uint32_t first, uint32_t last);

void CleanErase(char* records, char* first, char* last)
{
    if (!g_cleanReplay) {
        EraseEffectRecords(records, first, last);
        return;
    }
    g_cleanOurCalls.push_back({addr::SC4EffectRecords_Erase, reinterpret_cast<uint32_t>(records),
                               reinterpret_cast<uint32_t>(first), reinterpret_cast<uint32_t>(last)});
    CleanApplyChanges();
}

void CleanStartQueued(char* self)
{
    if (!g_cleanReplay) {
        StartQueuedEffects(self);
        return;
    }
    g_cleanOurCalls.push_back({addr::SC4EffectsManager_StartQueued, reinterpret_cast<uint32_t>(g_cleanSelf), 0, 0});
}

// What a start left waiting, put into the copy's vector in the replay.
void AssignWords(char* vector, const std::vector<uint32_t>& words);

void QueueStart(char* self, char* compound)
{
    if (!g_queueReplay) {
        CallThis<void>(addr::SC4EffectsManager_StartCompound, self, compound, 1u);
        return;
    }
    const std::size_t at = g_queueOurCalls.size();
    g_queueOurCalls.push_back({addr::SC4EffectsManager_StartCompound, reinterpret_cast<uint32_t>(g_queueSelf),
                               reinterpret_cast<uint32_t>(compound), 1});
    if (at < g_queueAfter.size())
        AssignWords(self + 0xBA0, g_queueAfter[at]);
}

#include "core/effects_cleanup.inc"

WordVector CloneWords(const WordVector& vector)
{
    const std::size_t used = static_cast<std::size_t>(vector.end - vector.begin);
    const std::size_t room = static_cast<std::size_t>(vector.room - vector.begin);
    WordVector clone{nullptr, nullptr, nullptr};
    if (room) {
        clone.begin = reinterpret_cast<uint32_t*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(room * 4);
        if (used)
            std::memcpy(clone.begin, vector.begin, used * 4);
    }
    clone.end = clone.begin + used;
    clone.room = clone.begin + room;
    return clone;
}

void FreeWords(WordVector& vector)
{
    if (vector.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(vector.begin);
    vector = {nullptr, nullptr, nullptr};
}

void AssignWords(char* at, const std::vector<uint32_t>& words)
{
    WordVector& vector = At<WordVector>(at, 0);
    if (static_cast<std::size_t>(vector.room - vector.begin) < words.size()) {
        FreeWords(vector);
        vector.begin = reinterpret_cast<uint32_t*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(words.size() * 4);
        vector.room = vector.begin + words.size();
    }
    if (!words.empty())
        std::memcpy(vector.begin, words.data(), words.size() * 4);
    vector.end = vector.begin + words.size();
}

bool SameWords(const WordVector& a, const WordVector& b, bool room)
{
    const std::size_t used = static_cast<std::size_t>(a.end - a.begin);
    return used == static_cast<std::size_t>(b.end - b.begin) &&
           (!room || a.room - a.begin == b.room - b.begin) &&
           (used == 0 || std::memcmp(a.begin, b.begin, used * 4) == 0);
}

// The index vector and the free vector of each list, the compound effects' first.
constexpr std::size_t kCleanVectors[12] = {0xBAC, 0xBB8, 0xBF4, 0xC00, 0xC3C, 0xC48,
                                           0xC60, 0xC6C, 0xC18, 0xC24, 0xBD0, 0xBDC};
// How many effects of the five other lists are in use.
std::size_t IndexedEffects(char* self)
{
    std::size_t count = 0;
    for (std::size_t i = 2; i < 12; i += 2)
        count += static_cast<std::size_t>(At<WordVector>(self, kCleanVectors[i]).end -
                                          At<WordVector>(self, kCleanVectors[i]).begin);
    return count;
}

std::string CallsText(const std::vector<CleanCall>& calls, const std::vector<CleanCall>& other)
{
    std::size_t at = 0;
    while (at < calls.size() && at < other.size() && calls[at] == other[at])
        ++at;
    char text[96];
    if (at < calls.size())
        std::snprintf(text, sizeof text, "call %zu of %zu: %08X on %08X (%08X, %08X)", at, calls.size(),
                      calls[at].target, calls[at].self, calls[at].a, calls[at].b);
    else
        std::snprintf(text, sizeof text, "%zu calls", calls.size());
    return text;
}

// Ours against what the game's has done when it reaches its last call.
void CleanReplay()
{
    char* const self = g_cleanSelf;
    char* const mine = g_cleanMine;
    g_cleanNoting = false;
    g_cleanReplayed = true;
    g_cleanGameCalls.push_back({addr::SC4EffectsManager_StartQueued, reinterpret_cast<uint32_t>(self), 0, 0});
    std::vector<char> after(g_cleanBefore.size()), ours(g_cleanBefore.size());
    for (std::size_t i = 0; i < g_cleanCompounds.size(); ++i) {
        std::memcpy(after.data() + i * kCompoundSize, g_cleanCompounds[i], kCompoundSize);
        std::memcpy(g_cleanCompounds[i], g_cleanBefore.data() + i * kCompoundSize, kCompoundSize);
    }
    const std::size_t indexed = IndexedEffects(mine);
    g_cleanOurCalls.clear();
    g_cleanAnswer = 0;
    g_cleanReplay = true;
    CleanUpEffects(mine);
    g_cleanReplay = false;
    for (std::size_t i = 0; i < g_cleanCompounds.size(); ++i) {
        std::memcpy(ours.data() + i * kCompoundSize, g_cleanCompounds[i], kCompoundSize);
        std::memcpy(g_cleanCompounds[i], after.data() + i * kCompoundSize, kCompoundSize);
    }
    ++g_cleanCompared;
    for (const CleanCall& call : g_cleanGameCalls)
        g_cleanEnded += call.target == addr::SC4EffectRecords_Erase;
    g_cleanFreed += indexed - IndexedEffects(mine);
    if ((g_cleanCompared & (g_cleanCompared - 1)) == 0)
        log::Write("water effects: effects.cleanup: %llu clean-ups compared so far, %llu compound effects emptied, "
                   "%llu effects of the other lists freed",
                   static_cast<unsigned long long>(g_cleanCompared), static_cast<unsigned long long>(g_cleanEnded),
                   static_cast<unsigned long long>(g_cleanFreed));
    const char* const id = "effects.cleanup";
    if (!(g_cleanGameCalls == g_cleanOurCalls)) {
        compare::Record(id, false, CallsText(g_cleanGameCalls, g_cleanOurCalls),
                        CallsText(g_cleanOurCalls, g_cleanGameCalls));
        return;
    }
    char text[64];
    for (const std::size_t vector : kCleanVectors) {
        const WordVector& theirs = At<WordVector>(self, vector);
        const WordVector& own = At<WordVector>(mine, vector);
        if (SameWords(theirs, own, true))
            continue;
        std::snprintf(text, sizeof text, "vector +%zX: %zu words, room for %zu", vector,
                      static_cast<std::size_t>(theirs.end - theirs.begin),
                      static_cast<std::size_t>(theirs.room - theirs.begin));
        const std::string game = text;
        std::snprintf(text, sizeof text, "vector +%zX: %zu words, room for %zu", vector,
                      static_cast<std::size_t>(own.end - own.begin), static_cast<std::size_t>(own.room - own.begin));
        compare::Record(id, false, game, text);
        return;
    }
    if (At<uint32_t>(self, 0xC94) != At<uint32_t>(mine, 0xC94) || At<uint8_t>(self, 0x1B4) != At<uint8_t>(mine, 0x1B4)) {
        std::snprintf(text, sizeof text, "count %u, flag %u", At<uint32_t>(self, 0xC94), At<uint8_t>(self, 0x1B4));
        const std::string game = text;
        std::snprintf(text, sizeof text, "count %u, flag %u", At<uint32_t>(mine, 0xC94), At<uint8_t>(mine, 0x1B4));
        compare::Record(id, false, game, text);
        return;
    }
    if (after != ours) {
        std::size_t at = 0;
        while (after[at] == ours[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "compound %zu +%zX ", at / kCompoundSize, at % kCompoundSize);
        compare::Record(id, false, text + Hex(after.data() + at, 4), text + Hex(ours.data() + at, 4));
        return;
    }
    compare::Record(id, true, std::string(), std::string());
}

uint32_t CleanCompared(char* self)
{
    std::vector<char> copy(self, self + kManagerSize);
    char* const mine = copy.data();
    for (const std::size_t vector : kCleanVectors)
        At<WordVector>(mine, vector) = CloneWords(At<WordVector>(self, vector));
    g_cleanCompounds.clear();
    const WordVector& index = At<WordVector>(self, 0xBAC);
    for (const uint32_t* it = index.begin; it != index.end; ++it)
        g_cleanCompounds.push_back(At<char**>(self, 0xB94)[*it]);
    g_cleanBefore.resize(g_cleanCompounds.size() * kCompoundSize);
    for (std::size_t i = 0; i < g_cleanCompounds.size(); ++i)
        std::memcpy(g_cleanBefore.data() + i * kCompoundSize, g_cleanCompounds[i], kCompoundSize);
    g_cleanGameCalls.clear();
    g_cleanAnswers.clear();
    g_cleanChanges.clear();
    g_cleanShadow = g_cleanBefore;
    g_cleanSelf = self;
    g_cleanMine = mine;
    g_cleanReplayed = false;
    g_cleanNoting = true;
    const uint32_t result = g_afterDraw(self, nullptr);
    g_cleanNoting = false;
    if (!g_cleanReplayed)
        compare::Record("effects.cleanup", false, "the last call was not reached", "not run");
    for (const std::size_t vector : kCleanVectors)
        FreeWords(At<WordVector>(mine, vector));
    return result;
}

uint32_t __fastcall AfterDrawByMode(char* self, void*)
{
    g_effectsManager = self;
    if (g_cleanMode == StepMode::Original)
        return g_afterDraw(self, nullptr);
    if (g_cleanMode == StepMode::Compare)
        return CleanCompared(self);
    CleanUpEffects(self);
    return 0;
}

uint32_t __fastcall EndedNoted(char* self, void*)
{
    if (!g_cleanNoting)
        return g_SC4CompoundEffect_Ended(self, nullptr);
    const uint32_t result = g_SC4CompoundEffect_Ended(self, nullptr);
    g_cleanGameCalls.push_back({addr::SC4CompoundEffect_Ended, reinterpret_cast<uint32_t>(self), 0, 0});
    const std::size_t own = g_cleanAnswers.size();
    g_cleanAnswers.push_back(result & 0xFF);
    CleanNoteChanges(own < g_cleanCompounds.size() && g_cleanCompounds[own] == self ? own : ~std::size_t{0});
    return result;
}
OPENSC4_THUNK(TSC4CompoundEffect_Ended, EndedNoted, 0)

// The start of the waiting effects is compared the same way round: the
// game's first, each start it makes noted with what was left waiting after
// it; then ours on a copy of the manager with copies of the two vectors,
// the starts only noted and the waiting ones set from the notes.
int g_queueDepth = 0;

uint32_t __fastcall StartCompoundNoted(char* self, void*, uint32_t compound, uint32_t flag)
{
    const bool noting = g_queueNoting && self == g_queueSelf && g_queueDepth == 0;
    if (noting)
        g_queueGameCalls.push_back({addr::SC4EffectsManager_StartCompound, reinterpret_cast<uint32_t>(self), compound, flag});
    ++g_queueDepth;
    const uint32_t result = StartCompoundRun(self, compound, flag);
    --g_queueDepth;
    if (noting) {
        const WordVector& queued = At<WordVector>(self, 0xBA0);
        g_queueAfter.emplace_back(queued.begin, queued.end);
    }
    return result;
}
OPENSC4_THUNK(TSC4EffectsManager_StartCompound, StartCompoundNoted, 8)

uint32_t QueueCompared(char* self)
{
    std::vector<char> copy(self, self + kManagerSize);
    char* const mine = copy.data();
    At<WordVector>(mine, 0xBA0) = CloneWords(At<WordVector>(self, 0xBA0));
    At<WordVector>(mine, 0xBAC) = CloneWords(At<WordVector>(self, 0xBAC));
    g_queueGameCalls.clear();
    g_queueAfter.clear();
    g_queueSelf = self;
    g_queueNoting = true;
    const uint32_t result = g_SC4EffectsManager_StartQueued(self, nullptr);
    g_queueNoting = false;
    // A start may have made the table grow: the copy gets it as it is now.
    std::memcpy(mine + 0xB94, self + 0xB94, 12);
    g_queueOurCalls.clear();
    g_queueReplay = true;
    StartQueuedEffects(mine);
    g_queueReplay = false;
    ++g_queueCompared;
    g_queueStarted += g_queueGameCalls.size();
    if ((g_queueCompared & (g_queueCompared - 1)) == 0)
        log::Write("water effects: effects.cleanup.queue: %llu calls compared so far, %llu effects started",
                   static_cast<unsigned long long>(g_queueCompared), static_cast<unsigned long long>(g_queueStarted));
    const char* const id = "effects.cleanup.queue";
    if (!(g_queueGameCalls == g_queueOurCalls))
        compare::Record(id, false, CallsText(g_queueGameCalls, g_queueOurCalls),
                        CallsText(g_queueOurCalls, g_queueGameCalls));
    else if (!SameWords(At<WordVector>(self, 0xBAC), At<WordVector>(mine, 0xBAC), true))
        compare::Record(id, false, "the indices in use", "others, or other room");
    else if (!SameWords(At<WordVector>(self, 0xBA0), At<WordVector>(mine, 0xBA0), false))
        compare::Record(id, false, "the effects left waiting", "others");
    else
        compare::Record(id, true, std::string(), std::string());
    FreeWords(At<WordVector>(mine, 0xBA0));
    FreeWords(At<WordVector>(mine, 0xBAC));
    return result;
}

uint32_t __fastcall StartQueuedByMode(char* self, void*)
{
    if (g_cleanNoting && self == g_cleanSelf)
        CleanReplay();
    if (g_cleanMode == StepMode::Original || g_queueNoting || g_queueReplay)
        return g_SC4EffectsManager_StartQueued(self, nullptr);
    if (g_cleanMode == StepMode::Compare)
        return QueueCompared(self);
    StartQueuedEffects(self);
    return 0;
}
OPENSC4_THUNK(TSC4EffectsManager_StartQueued, StartQueuedByMode, 0)

// The erase is compared as the setters of what an effect holds are: what
// each record to go holds gets a copy of its table of functions whose slot
// 0x20 notes the call. Only an erase to the end of the vector, the one the
// test cities make, which copies no record down.
uint32_t EraseCompared(char* records, uint32_t first, uint32_t last);

uint32_t __fastcall EraseByMode(char* records, void*, uint32_t first, uint32_t last)
{
    if (ScNoting())
        return ScErased(records, first, last);
    if (!g_cleanNoting)
        return EraseCompared(records, first, last);
    g_cleanGameCalls.push_back({addr::SC4EffectRecords_Erase, reinterpret_cast<uint32_t>(records), first, last});
    const uint32_t result = EraseCompared(records, first, last);
    CleanNoteChanges(~std::size_t{0});
    return result;
}
OPENSC4_THUNK(TSC4EffectRecords_Erase, EraseByMode, 8)

uint32_t EraseCompared(char* records, uint32_t first, uint32_t last)
{
    if (g_cleanMode == StepMode::Ours)
        return reinterpret_cast<uint32_t>(EraseEffectRecords(records, reinterpret_cast<char*>(first),
                                                             reinterpret_cast<char*>(last)));
    if (g_cleanMode == StepMode::Original || BBusy())
        return g_SC4EffectRecords_Erase(records, nullptr, first, last);
    if (last != At<uint32_t>(records, 4)) {
        static bool logged = false;
        if (!logged)
            log::Write("water effects: effects.cleanup.erase: an erase short of the end, not compared");
        logged = true;
        return g_SC4EffectRecords_Erase(records, nullptr, first, last);
    }
    for (char* record = reinterpret_cast<char*>(first); record != reinterpret_cast<char*>(last); record += 0x44)
        BSwap(At<char*>(record, 0x38), 0x20, reinterpret_cast<uint32_t>(&BEnd));
    return HolderCompared(
        "effects.cleanup.erase", records, 0xC, [&] { return g_SC4EffectRecords_Erase(records, nullptr, first, last); },
        [&] {
            return reinterpret_cast<uint32_t>(EraseEffectRecords(records, reinterpret_cast<char*>(first),
                                                                 reinterpret_cast<char*>(last)));
        });
}

// The copy changes what the records hold, so only one of no record (the
// only kind the test cities make) is run twice.
uint32_t __cdecl CopyRecordsByMode(uint32_t first, uint32_t last, uint32_t to, uint32_t unused, uint32_t zero)
{
    const auto ours = [&] {
        return reinterpret_cast<uint32_t>(CopyEffectRecords(reinterpret_cast<char*>(first), reinterpret_cast<char*>(last),
                                                            reinterpret_cast<char*>(to)));
    };
    if (g_cleanMode == StepMode::Ours)
        return ours();
    const uint32_t result = g_SC4EffectRecords_Copy(first, last, to, unused, zero);
    if (g_cleanMode == StepMode::Original)
        return result;
    if (static_cast<int32_t>(last - first) / 0x44 > 0) {
        static bool logged = false;
        if (!logged)
            log::Write("water effects: effects.cleanup.copy: a copy of records, not compared");
        logged = true;
        return result;
    }
    const uint32_t mine = ours();
    char theirs[16], own[16];
    std::snprintf(theirs, sizeof theirs, "%08X", result);
    std::snprintf(own, sizeof own, "%08X", mine);
    compare::Record("effects.cleanup.copy", mine == result, mine == result ? std::string() : std::string(theirs),
                    mine == result ? std::string() : std::string(own));
    return result;
}

bool HookAfterDraw()
{
    return g_afterDraw || Detour(addr::SC4EffectsManager_AfterDraw, &TAfterDraw, &g_afterDraw);
}

// The start of a compound effect and the erase of records each have one
// detour, which the clean-up and the start's own replacement share.
bool HookStartCompound()
{
    return g_SC4EffectsManager_StartCompound ||
           Detour(addr::SC4EffectsManager_StartCompound, &TSC4EffectsManager_StartCompound,
                  &g_SC4EffectsManager_StartCompound);
}

bool HookEraseRecords()
{
    return g_SC4EffectRecords_Erase ||
           Detour(addr::SC4EffectRecords_Erase, &TSC4EffectRecords_Erase, &g_SC4EffectRecords_Erase);
}

bool InstallCleanup(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(HookAfterDraw() &&
          Detour(addr::SC4EffectsManager_StartQueued, &TSC4EffectsManager_StartQueued,
                 &g_SC4EffectsManager_StartQueued) &&
          HookEraseRecords() &&
          Detour(addr::SC4EffectRecords_Copy, reinterpret_cast<void (*)()>(&CopyRecordsByMode),
                 &g_SC4EffectRecords_Copy)))
        return false;
    if (mode == registry::Mode::Compare &&
        !(Detour(addr::SC4CompoundEffect_Ended, &TSC4CompoundEffect_Ended, &g_SC4CompoundEffect_Ended) &&
          HookStartCompound()))
        return false;
    g_cleanMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the manager's clean-up after its draw %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredCleanup = registry::Register({
    "renderer.effects.cleanup",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectsManager_AfterDraw,
    "The effects manager's clean-up after its draw: the compound effects that have ended emptied, the effects "
    "nothing holds any more freed for reuse, the waiting ones started (the start is renderer.effects.compound); compare "
    "mode runs the game's with its calls noted and replays ours on a copy of the lists (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallCleanup,
});

// --- The effects manager's sort by depth -----------------------------------------------------

// The six functions the draw sorts the effects in view with
// (effects_sort.inc). Each only moves entries of the manager's vector at
// +0xDA4, so each is compared by running the game's and then ours from the
// same vector; the game's calls of the ones under it are compared on the
// way.
constexpr uint32_t kDepthHeapSort = 0x0058F9D0; // cdecl (first, middle, last, unused, unused)

#include "core/effects_sort.inc"

StepMode g_sortMode = StepMode::Original;
using Sort3Fn = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t);
using Sort4Fn = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t);
using Sort5Fn = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
Sort5Fn g_SC4EffectDepths_SortLoop = nullptr, g_SC4EffectDepths_Partition = nullptr, g_SC4EffectDepths_Insert = nullptr;
Sort4Fn g_SC4EffectDepths_InsertBelow = nullptr, g_SC4EffectDepths_InsertionSortAfter = nullptr;
Sort3Fn g_SC4EffectDepths_InsertionSort = nullptr;

template <typename Game, typename Ours>
uint32_t SortCompared(const char* id, uint32_t first, uint32_t last, Game&& game, Ours&& ours)
{
    char* const begin = g_effectsManager ? At<char*>(g_effectsManager, 0xDA4) : nullptr;
    char* const end = g_effectsManager ? At<char*>(g_effectsManager, 0xDA8) : nullptr;
    if (!begin || first < reinterpret_cast<uint32_t>(begin) || last > reinterpret_cast<uint32_t>(end) || first > last) {
        static bool logged = false;
        if (!logged)
            log::Write("water effects: effects.sort: a sort outside the manager's vector, not compared");
        logged = true;
        return game();
    }
    const std::vector<char> before(begin, end);
    const uint32_t result = game();
    const std::vector<char> after(begin, end);
    std::memcpy(begin, before.data(), before.size());
    const uint32_t mine = ours();
    const bool same = mine == result && std::memcmp(begin, after.data(), after.size()) == 0;
    if (same) {
        compare::Record(id, true, std::string(), std::string());
        return result;
    }
    std::size_t at = 0;
    while (at < after.size() && begin[at] == after[at])
        ++at;
    at &= ~std::size_t{7};
    char theirs[64], own[64];
    std::snprintf(theirs, sizeof theirs, "%08X, entry %zu: %s", result, at / 8,
                  at < after.size() ? Hex(after.data() + at, 8).c_str() : "same");
    std::snprintf(own, sizeof own, "%08X, entry %zu: %s", mine, at / 8,
                  at < after.size() ? Hex(begin + at, 8).c_str() : "same");
    compare::Record(id, false, theirs, own);
    std::memcpy(begin, after.data(), after.size());
    return result;
}

DepthEntry* Depths(uint32_t at)
{
    return reinterpret_cast<DepthEntry*>(at);
}

uint32_t __cdecl DepthSortLoopByMode(uint32_t first, uint32_t last, uint32_t zero, uint32_t depth, uint32_t unused)
{
    const auto ours = [&] {
        DepthSortLoop(Depths(first), Depths(last), depth, unused);
        return 0u;
    };
    if (g_sortMode == StepMode::Ours)
        return ours();
    const auto game = [&] {
        g_SC4EffectDepths_SortLoop(first, last, zero, depth, unused);
        return 0u;
    };
    return g_sortMode == StepMode::Original ? game() : SortCompared("effects.sort.loop", first, last, game, ours);
}

uint32_t __cdecl DepthPartitionByMode(uint32_t first, uint32_t last, uint32_t depth, uint32_t effect, uint32_t unused)
{
    const auto ours = [&] {
        return reinterpret_cast<uint32_t>(DepthPartition(Depths(first), Depths(last), {depth, effect}));
    };
    if (g_sortMode == StepMode::Ours)
        return ours();
    const auto game = [&] { return g_SC4EffectDepths_Partition(first, last, depth, effect, unused); };
    return g_sortMode == StepMode::Original ? game() : SortCompared("effects.sort.partition", first, last, game, ours);
}

uint32_t __cdecl DepthInsertionSortByMode(uint32_t first, uint32_t last, uint32_t unused)
{
    const auto ours = [&] {
        DepthInsertionSort(Depths(first), Depths(last));
        return 0u;
    };
    if (g_sortMode == StepMode::Ours)
        return ours();
    const auto game = [&] {
        g_SC4EffectDepths_InsertionSort(first, last, unused);
        return 0u;
    };
    return g_sortMode == StepMode::Original ? game() : SortCompared("effects.sort.insertion", first, last, game, ours);
}

uint32_t __cdecl DepthInsertByMode(uint32_t first, uint32_t at, uint32_t depth, uint32_t effect, uint32_t unused)
{
    const auto ours = [&] {
        DepthInsert(Depths(first), Depths(at), {depth, effect});
        return 0u;
    };
    if (g_sortMode == StepMode::Ours)
        return ours();
    const auto game = [&] {
        g_SC4EffectDepths_Insert(first, at, depth, effect, unused);
        return 0u;
    };
    return g_sortMode == StepMode::Original ? game() : SortCompared("effects.sort.insert", first, at + 8, game, ours);
}

uint32_t __cdecl DepthInsertBelowByMode(uint32_t at, uint32_t depth, uint32_t effect, uint32_t unused)
{
    const auto ours = [&] {
        DepthInsertBelow(Depths(at), {depth, effect});
        return 0u;
    };
    if (g_sortMode == StepMode::Ours)
        return ours();
    const auto game = [&] {
        g_SC4EffectDepths_InsertBelow(at, depth, effect, unused);
        return 0u;
    };
    return g_sortMode == StepMode::Original ? game() : SortCompared("effects.sort.insert_below", at, at + 8, game, ours);
}

uint32_t __cdecl DepthInsertionSortAfterByMode(uint32_t first, uint32_t last, uint32_t zero, uint32_t unused)
{
    const auto ours = [&] {
        DepthInsertionSortAfter(Depths(first), Depths(last));
        return 0u;
    };
    if (g_sortMode == StepMode::Ours)
        return ours();
    const auto game = [&] {
        g_SC4EffectDepths_InsertionSortAfter(first, last, zero, unused);
        return 0u;
    };
    return g_sortMode == StepMode::Original ? game()
                                            : SortCompared("effects.sort.insertion_after", first, last, game, ours);
}

bool InstallSort(registry::Mode mode)
{
    const auto plain = [](auto* function) { return reinterpret_cast<void (*)()>(function); };
    // The comparison finds the vector through the manager, which the clean-up's detour sees.
    if (!game::IsBuild641() || (mode == registry::Mode::Compare && !HookAfterDraw()) ||
        !(Detour(addr::SC4EffectDepths_SortLoop, plain(&DepthSortLoopByMode), &g_SC4EffectDepths_SortLoop) &&
          Detour(addr::SC4EffectDepths_Partition, plain(&DepthPartitionByMode), &g_SC4EffectDepths_Partition) &&
          Detour(addr::SC4EffectDepths_InsertionSort, plain(&DepthInsertionSortByMode),
                 &g_SC4EffectDepths_InsertionSort) &&
          Detour(addr::SC4EffectDepths_Insert, plain(&DepthInsertByMode), &g_SC4EffectDepths_Insert) &&
          Detour(addr::SC4EffectDepths_InsertBelow, plain(&DepthInsertBelowByMode), &g_SC4EffectDepths_InsertBelow) &&
          Detour(addr::SC4EffectDepths_InsertionSortAfter, plain(&DepthInsertionSortAfterByMode),
                 &g_SC4EffectDepths_InsertionSortAfter)))
        return false;
    g_sortMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the sort of the effects in view %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredSort = registry::Register({
    "renderer.effects.sort",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDepths_SortLoop,
    "The sort of the effects in view by depth, six functions under the effects manager's draw; compare mode runs "
    "the game's and ours from the same vector (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallSort,
});

// --- The start of a compound effect ----------------------------------------------------------

// The start and the four small functions under it (effects_compound.inc).
// The start makes effects, so it cannot run twice. Its comparison runs the
// game's first, noting each call it makes that ours makes too: the level's
// values, the assign and the erase of records, the grow of the vector, the
// making of a record's effect, the compound effect's own values (through
// detours), and the stop, the letting go, the transform and the start of a
// record's effect (through a copy of the effect's table of functions, as
// the setters of what an effect holds are compared). With each note goes
// what the call changed in the compound effect and its records. Then ours
// runs on a copy of the compound effect as it was, with a copy of its
// records, from the same state of the manager's generator; its calls are
// only noted, each followed by the changes the game's made. The calls
// (the transform's block and the record put into the vector among them),
// the compound effect, its records and the generator are compared. A start
// with the manager's byte +0xB8C set (no test city has one) is not
// compared.
constexpr uint32_t kFindDescriptionByName = 0x0058F5A0; // (this = manager, name), ret 4
constexpr uint32_t kFindDescriptionByID = 0x0058F2E0;   // (this = manager, a, b), ret 8
constexpr std::size_t kScCompound = 0x13C, kScRecord = 0x44, kScGenerator = 0x9C8;
constexpr uint32_t kScRoom = 4096; // the most records a copy has room for
constexpr uint32_t kScRole = 0x005919D0;

StepMode g_scMode = StepMode::Original;
Call0Fn g_SC4EffectRecord_Construct = nullptr, g_SC4CompoundEffect_ApplyValues = nullptr;
Call1Fn g_SC4EffectRecord_CopyConstruct = nullptr, g_SC4EffectRecord_Assign = nullptr;
Call2Fn g_SC4EffectsManager_FadeByLevel = nullptr;
Call3Fn g_SC4EffectsManager_MakeRecordEffect = nullptr;
Call5Fn g_SC4EffectRecords_GrowInsert = nullptr;

struct ScCall {
    uint32_t target, a, b, c, made;
    char data[kScRecord];
    // What the game's call changed: bytes of the compound effect (below
    // kScCompound) and of its records (from there on), and the records'
    // count and room when those changed.
    std::vector<std::pair<uint32_t, char>> changes;
    bool resized;
    uint32_t used, room;
    bool operator==(const ScCall& other) const
    {
        return target == other.target && a == other.a && b == other.b && c == other.c &&
               std::memcmp(data, other.data, sizeof data) == 0;
    }
};
struct ScImage {
    std::vector<char> bytes;
    uint32_t used, room;
};
bool g_scNoting = false, g_scReplay = false;
int g_scDepth = 0;
DWORD g_scThread = 0;
// The compound effect whose records a note's places are counted in: the
// game's while it runs, ours' copy in the replay.
char* g_scCurrent = nullptr;
std::vector<ScCall> g_scGame, g_scOurs;
std::vector<char*> g_scSwapped;
uint64_t g_scCompared = 0, g_scCalls = 0, g_scMade = 0, g_scStopped = 0;

bool ScNoting()
{
    return g_scNoting && g_scDepth == 0 && GetCurrentThreadId() == g_scThread;
}

uint32_t ScIndex(const char* record)
{
    return static_cast<uint32_t>(static_cast<int32_t>(record - At<char*>(g_scCurrent, 0x114)) /
                                 static_cast<int32_t>(kScRecord));
}

// The compound effect without the three pointers of its vector, then its
// records without the two bytes of each that nothing sets.
ScImage ScTakeImage(char* compound)
{
    ScImage image;
    char* const begin = At<char*>(compound, 0x114);
    image.used = static_cast<uint32_t>((At<char*>(compound, 0x118) - begin) / static_cast<int32_t>(kScRecord));
    image.room = static_cast<uint32_t>((At<char*>(compound, 0x11C) - begin) / static_cast<int32_t>(kScRecord));
    image.bytes.assign(compound, compound + kScCompound);
    std::memset(image.bytes.data() + 0x114, 0, 12);
    if (image.used)
        image.bytes.insert(image.bytes.end(), begin, begin + image.used * kScRecord);
    for (uint32_t i = 0; i < image.used; ++i)
        image.bytes[kScCompound + i * kScRecord + 2] = image.bytes[kScCompound + i * kScRecord + 3] = 0;
    return image;
}

void ScNoteChanges(ScCall& call, const ScImage& before, const ScImage& after)
{
    call.resized = before.used != after.used || before.room != after.room;
    call.used = after.used;
    call.room = after.room;
    for (std::size_t at = 0; at < after.bytes.size(); ++at) {
        if (at >= before.bytes.size() || before.bytes[at] != after.bytes[at])
            call.changes.push_back({static_cast<uint32_t>(at), after.bytes[at]});
    }
}

template <typename Real>
uint32_t ScNoted(ScCall call, Real&& real, uint32_t* out = nullptr)
{
    char* const compound = g_scCurrent;
    const ScImage before = ScTakeImage(compound);
    ++g_scDepth;
    const uint32_t result = real();
    --g_scDepth;
    ScNoteChanges(call, before, ScTakeImage(compound));
    if (out)
        call.made = *out;
    g_scGame.push_back(std::move(call));
    return result;
}

// Ours' call in the replay: noted, and the compound effect's copy changed
// as the game's call at the same place changed the game's.
void ScReplayed(const ScCall& call, uint32_t* out = nullptr)
{
    const std::size_t at = g_scOurs.size();
    g_scOurs.push_back(call);
    if (at >= g_scGame.size() || !(g_scGame[at] == call))
        return;
    const ScCall& game = g_scGame[at];
    char* const begin = At<char*>(g_scCurrent, 0x114);
    if (game.resized && game.room <= kScRoom) {
        At<char*>(g_scCurrent, 0x118) = begin + game.used * kScRecord;
        At<char*>(g_scCurrent, 0x11C) = begin + game.room * kScRecord;
    }
    for (const auto& [offset, value] : game.changes) {
        if (offset < kScCompound)
            g_scCurrent[offset] = value;
        else if (offset - kScCompound < kScRoom * kScRecord)
            begin[offset - kScCompound] = value;
    }
    if (out)
        *out = game.made;
}

ScCall ScMakeCall(uint32_t target, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0)
{
    return {target, a, b, c, 0, {}, {}, false, 0, 0};
}

// A record as the grow of the vector is given it, without what nothing has set yet.
ScCall ScGrowCall(const char* at, const char* record)
{
    ScCall call = ScMakeCall(addr::SC4EffectRecords_GrowInsert, ScIndex(at));
    std::memcpy(call.data, record, kScRecord);
    std::memset(call.data + 2, 0, 2);
    std::memset(call.data + 0x3C, 0, 4);
    return call;
}

ScCall ScTransformCall(char* held, uint32_t block, const char* place)
{
    ScCall call = ScMakeCall(0xC, reinterpret_cast<uint32_t>(held), static_cast<uint32_t>(place - g_scCurrent));
    CopyBlock(call.data, block);
    return call;
}

uint32_t HeldSlot(char* held, uint32_t slot)
{
    return At<uint32_t>(At<char*>(held, 0), slot);
}

void FadeEffectByLevel(char* self, char* part, char* record);
void ApplyCompoundValues(char* compound);

// What the start calls: the real thing, or in the replay its note.
void ScStop(char* held)
{
    if (g_scReplay)
        ScReplayed(ScMakeCall(0x4, reinterpret_cast<uint32_t>(held), 1));
    else
        CallThis<void>(HeldSlot(held, 0x4), held, 1u);
}

void ScEnd(char* held)
{
    if (g_scReplay)
        ScReplayed(ScMakeCall(0x20, reinterpret_cast<uint32_t>(held)));
    else
        CallThis<void>(HeldSlot(held, 0x20), held);
}

void ScFade(char* self, char* part, char* record)
{
    if (g_scReplay)
        ScReplayed(ScMakeCall(addr::SC4EffectsManager_FadeByLevel, reinterpret_cast<uint32_t>(part), ScIndex(record)));
    else
        FadeEffectByLevel(self, part, record);
}

void ScAssign(char* to, char* from)
{
    if (g_scReplay)
        ScReplayed(ScMakeCall(addr::SC4EffectRecord_Assign, ScIndex(to), ScIndex(from)));
    else
        CallThis<void>(addr::SC4EffectRecord_Assign, to, from);
}

void ScErase(char* records, char* first, char* last)
{
    if (g_scReplay)
        ScReplayed(ScMakeCall(addr::SC4EffectRecords_Erase, ScIndex(first), ScIndex(last)));
    else
        CallThis<void>(addr::SC4EffectRecords_Erase, records, first, last);
}

void ScGrow(char* records, char* at, char* record, uint8_t* tag)
{
    if (!g_scReplay) {
        CallThis<void>(addr::SC4EffectRecords_GrowInsert, records, at, record, tag, 1u, 1u);
        return;
    }
    const char* const end = At<char*>(records, 4);
    ScReplayed(ScGrowCall(at, record));
    // The translation goes on to the record before the end: one there must be.
    if (At<char*>(records, 4) == end)
        At<char*>(records, 4) += kScRecord;
}

void ScMake(char* self, char* part, char* record, uint32_t* made)
{
    if (g_scReplay)
        ScReplayed(ScMakeCall(addr::SC4EffectsManager_MakeRecordEffect, reinterpret_cast<uint32_t>(part),
                              ScIndex(record), *made),
                   made);
    else
        CallThis<void>(addr::SC4EffectsManager_MakeRecordEffect, self, part, record, made);
}

void ScTransform(char* held, char* block, char* place)
{
    if (g_scReplay)
        ScReplayed(ScTransformCall(held, reinterpret_cast<uint32_t>(block), place));
    else
        CallThis<void>(HeldSlot(held, 0xC), held, block, place);
}

void ScBegin(char* held, uint32_t value)
{
    if (g_scReplay)
        ScReplayed(ScMakeCall(0x0, reinterpret_cast<uint32_t>(held), value));
    else
        CallThis<void>(HeldSlot(held, 0x0), held, value);
}

void ScValues(char* compound)
{
    if (g_scReplay)
        ScReplayed(ScMakeCall(addr::SC4CompoundEffect_ApplyValues));
    else
        ApplyCompoundValues(compound);
}

#include "core/effects_compound.inc"

// The stand-ins for six of a held effect's functions. The four the start
// calls are noted; the values (slot 0x14) and the hold (slot 0x1C) are
// what the small functions' own comparisons look at: mode 1 notes the call
// and makes it, mode 2 only notes it.
struct ScValueCall {
    uint32_t object, which, count;
    char data[64];
    bool operator==(const ScValueCall& other) const
    {
        return object == other.object && which == other.which && count == other.count &&
               std::memcmp(data, other.data, sizeof data) == 0;
    }
};
int g_scValueMode = 0, g_scHoldMode = 0;
std::vector<ScValueCall> g_scValueCalls;
uint32_t g_scHolds = 0;

uint32_t __fastcall ScBeginStub(char* object, void*, uint32_t value)
{
    const uint32_t real = BRealSlot(object, 0x0);
    if (!ScNoting())
        return CallThis<uint32_t>(real, object, value);
    return ScNoted(ScMakeCall(0x0, reinterpret_cast<uint32_t>(object), value),
                   [&] { return CallThis<uint32_t>(real, object, value); });
}

uint32_t __fastcall ScStopStub(char* object, void*, uint32_t value)
{
    const uint32_t real = BRealSlot(object, 0x4);
    if (!ScNoting())
        return CallThis<uint32_t>(real, object, value);
    ++g_scStopped;
    return ScNoted(ScMakeCall(0x4, reinterpret_cast<uint32_t>(object), value),
                   [&] { return CallThis<uint32_t>(real, object, value); });
}

uint32_t __fastcall ScTransformStub(char* object, void*, uint32_t block, uint32_t place)
{
    const uint32_t real = BRealSlot(object, 0xC);
    if (!ScNoting())
        return CallThis<uint32_t>(real, object, block, place);
    return ScNoted(ScTransformCall(object, block, reinterpret_cast<const char*>(place)),
                   [&] { return CallThis<uint32_t>(real, object, block, place); });
}

uint32_t __fastcall ScValueStub(char* object, void*, uint32_t which, uint32_t values, uint32_t count)
{
    const uint32_t real = BRealSlot(object, 0x14);
    if (g_scValueMode) {
        ScValueCall call{reinterpret_cast<uint32_t>(object), which, count, {}};
        std::memcpy(call.data, reinterpret_cast<const void*>(values), (count < 16 ? count : 16) * 4);
        g_scValueCalls.push_back(call);
        if (g_scValueMode == 2)
            return 0;
    }
    return CallThis<uint32_t>(real, object, which, values, count);
}

uint32_t __fastcall ScHoldStub(char* object, void*)
{
    const uint32_t real = BRealSlot(object, 0x1C);
    if (g_scHoldMode) {
        ++g_scHolds;
        if (g_scHoldMode == 2)
            return 0;
    }
    return CallThis<uint32_t>(real, object);
}

// The call may be the effect's last: it gets its own table back first.
uint32_t __fastcall ScEndStub(char* object, void*)
{
    const uint32_t real = BRealSlot(object, 0x20);
    const bool noting = ScNoting();
    BGiveBack(object, true);
    if (!noting)
        return CallThis<uint32_t>(real, object);
    return ScNoted(ScMakeCall(0x20, reinterpret_cast<uint32_t>(object)), [&] { return CallThis<uint32_t>(real, object); });
}

// False for nothing, and for an object another comparison has given a table.
bool ScSwap(char* held)
{
    if (!held)
        return false;
    BSwapRole(held, kScRole,
              {{0x0, reinterpret_cast<uint32_t>(&ScBeginStub)},
               {0x4, reinterpret_cast<uint32_t>(&ScStopStub)},
               {0xC, reinterpret_cast<uint32_t>(&ScTransformStub)},
               {0x14, reinterpret_cast<uint32_t>(&ScValueStub)},
               {0x1C, reinterpret_cast<uint32_t>(&ScHoldStub)},
               {0x20, reinterpret_cast<uint32_t>(&ScEndStub)}});
    const auto found = g_bReal.find(held);
    return found != g_bReal.end() && found->second.slot == kScRole;
}

void ScLogOnce(bool& logged, const char* what)
{
    if (!logged)
        log::Write("water effects: effects.compound: %s, not compared", what);
    logged = true;
}

// A new record: the game's and ours on the same bytes.
uint32_t __fastcall ConstructRecordByMode(char* record, void*)
{
    if (g_scMode == StepMode::Ours)
        return reinterpret_cast<uint32_t>(ConstructEffectRecord(record));
    if (g_scMode == StepMode::Original)
        return g_SC4EffectRecord_Construct(record, nullptr);
    char mine[kScRecord];
    std::memcpy(mine, record, kScRecord);
    const uint32_t result = g_SC4EffectRecord_Construct(record, nullptr);
    ConstructEffectRecord(mine);
    const bool same = result == reinterpret_cast<uint32_t>(record) && std::memcmp(record, mine, kScRecord) == 0;
    compare::Record("effects.compound.record", same, same ? std::string() : Hex(record, kScRecord),
                    same ? std::string() : Hex(mine, kScRecord));
    return result;
}
OPENSC4_THUNK(TSC4EffectRecord_Construct, ConstructRecordByMode, 0)

// A record's copy: the game's, then ours on the bytes the place had, with
// the hold of the effect only counted.
uint32_t __fastcall CopyConstructRecordByMode(char* record, void*, uint32_t from)
{
    char* const source = reinterpret_cast<char*>(from);
    if (g_scMode == StepMode::Ours)
        return reinterpret_cast<uint32_t>(CopyConstructEffectRecord(record, source));
    char* const held = At<char*>(source, 0x38);
    if (g_scMode == StepMode::Original || g_scHoldMode || (held && !ScSwap(held))) {
        static bool logged = false;
        if (g_scMode != StepMode::Original && !g_scHoldMode)
            ScLogOnce(logged, "a record's copy whose effect another comparison watches");
        return g_SC4EffectRecord_CopyConstruct(record, nullptr, from);
    }
    char mine[kScRecord];
    std::memcpy(mine, record, kScRecord);
    g_scHolds = 0;
    g_scHoldMode = 1;
    const uint32_t result = g_SC4EffectRecord_CopyConstruct(record, nullptr, from);
    const uint32_t holds = g_scHolds;
    g_scHolds = 0;
    g_scHoldMode = 2;
    CopyConstructEffectRecord(mine, source);
    g_scHoldMode = 0;
    if (held)
        BGiveBack(held, false);
    const bool same = result == reinterpret_cast<uint32_t>(record) && holds == g_scHolds && holds == (held ? 1u : 0u) &&
                      std::memcmp(record, mine, kScRecord) == 0;
    char text[32];
    std::snprintf(text, sizeof text, ", %u holds", holds);
    const std::string theirs = Hex(record, kScRecord) + text;
    std::snprintf(text, sizeof text, ", %u holds", g_scHolds);
    compare::Record("effects.compound.record_copy", same, same ? std::string() : theirs,
                    same ? std::string() : Hex(mine, kScRecord) + text);
    return result;
}
OPENSC4_THUNK(TSC4EffectRecord_CopyConstruct, CopyConstructRecordByMode, 4)

// The values an effect is told, ours' first and only noted, then the game's.
template <typename Ours, typename Game>
uint32_t ScValuesCompared(const char* id, Ours&& ours, Game&& game)
{
    g_scValueCalls.clear();
    g_scValueMode = 2;
    ours();
    const std::vector<ScValueCall> mine = std::move(g_scValueCalls);
    g_scValueCalls.clear();
    g_scValueMode = 1;
    const uint32_t result = game();
    g_scValueMode = 0;
    const bool same = mine == g_scValueCalls;
    char text[48];
    std::snprintf(text, sizeof text, "%zu values told", g_scValueCalls.size());
    const std::string theirs = text;
    std::snprintf(text, sizeof text, "%zu values told", mine.size());
    compare::Record(id, same, same ? std::string() : theirs, same ? std::string() : std::string(text));
    return result;
}

uint32_t FadeRun(char* self, uint32_t part, uint32_t record)
{
    const auto game = [&] { return g_SC4EffectsManager_FadeByLevel(self, nullptr, part, record); };
    if (g_scMode == StepMode::Ours) {
        FadeEffectByLevel(self, reinterpret_cast<char*>(part), reinterpret_cast<char*>(record));
        return 0;
    }
    char* const held = At<char*>(reinterpret_cast<char*>(record), 0x38);
    if (g_scMode == StepMode::Original || g_scValueMode || !ScSwap(held)) {
        static bool logged = false;
        if (g_scMode != StepMode::Original && !g_scValueMode)
            ScLogOnce(logged, "the level's values for an effect another comparison watches");
        return game();
    }
    const uint32_t result = ScValuesCompared(
        "effects.compound.fade",
        [&] { FadeEffectByLevel(self, reinterpret_cast<char*>(part), reinterpret_cast<char*>(record)); }, game);
    BGiveBack(held, false);
    return result;
}

uint32_t __fastcall FadeByMode(char* self, void*, uint32_t part, uint32_t record)
{
    if (!ScNoting())
        return FadeRun(self, part, record);
    return ScNoted(ScMakeCall(addr::SC4EffectsManager_FadeByLevel, part, ScIndex(reinterpret_cast<char*>(record))),
                   [&] { return FadeRun(self, part, record); });
}
OPENSC4_THUNK(TSC4EffectsManager_FadeByLevel, FadeByMode, 8)

uint32_t ValuesRun(char* compound)
{
    const auto game = [&] { return g_SC4CompoundEffect_ApplyValues(compound, nullptr); };
    if (g_scMode == StepMode::Ours) {
        ApplyCompoundValues(compound);
        return 0;
    }
    if (g_scMode == StepMode::Original || g_scValueMode)
        return game();
    std::vector<char*> swapped;
    bool all = true;
    for (char* record = At<char*>(compound, 0x114); record != At<char*>(compound, 0x118); record += kScRecord) {
        if (ScSwap(At<char*>(record, 0x38)))
            swapped.push_back(At<char*>(record, 0x38));
        else
            all = false;
    }
    uint32_t result = 0;
    if (all) {
        result = ScValuesCompared("effects.compound.values", [&] { ApplyCompoundValues(compound); }, game);
    } else {
        static bool logged = false;
        ScLogOnce(logged, "a compound effect's values for an effect another comparison watches");
        result = game();
    }
    for (char* const held : swapped)
        BGiveBack(held, false);
    return result;
}

uint32_t __fastcall ValuesByMode(char* compound, void*)
{
    if (!ScNoting())
        return ValuesRun(compound);
    return ScNoted(ScMakeCall(addr::SC4CompoundEffect_ApplyValues), [&] { return ValuesRun(compound); });
}
OPENSC4_THUNK(TSC4CompoundEffect_ApplyValues, ValuesByMode, 0)

// The game's functions the start's comparison notes.
uint32_t __fastcall AssignRecordNoted(char* to, void*, uint32_t from)
{
    if (!ScNoting())
        return g_SC4EffectRecord_Assign(to, nullptr, from);
    return ScNoted(ScMakeCall(addr::SC4EffectRecord_Assign, ScIndex(to), ScIndex(reinterpret_cast<char*>(from))),
                   [&] { return g_SC4EffectRecord_Assign(to, nullptr, from); });
}
OPENSC4_THUNK(TSC4EffectRecord_Assign, AssignRecordNoted, 4)

uint32_t ScErased(char* records, uint32_t first, uint32_t last)
{
    return ScNoted(ScMakeCall(addr::SC4EffectRecords_Erase, ScIndex(reinterpret_cast<char*>(first)),
                              ScIndex(reinterpret_cast<char*>(last))),
                   [&] { return EraseCompared(records, first, last); });
}

uint32_t __fastcall GrowRecordsNoted(char* records, void*, uint32_t at, uint32_t record, uint32_t tag, uint32_t count,
                                     uint32_t atEnd)
{
    if (!ScNoting())
        return g_SC4EffectRecords_GrowInsert(records, nullptr, at, record, tag, count, atEnd);
    return ScNoted(ScGrowCall(reinterpret_cast<char*>(at), reinterpret_cast<char*>(record)),
                   [&] { return g_SC4EffectRecords_GrowInsert(records, nullptr, at, record, tag, count, atEnd); });
}
OPENSC4_THUNK(TSC4EffectRecords_GrowInsert, GrowRecordsNoted, 20)

// The making by the mode of its own replacement (further down).
uint32_t MakeRun(char* self, uint32_t part, uint32_t record, uint32_t made);

// The effect a record has just been given is watched from here on.
uint32_t __fastcall MakeRecordEffectNoted(char* self, void*, uint32_t part, uint32_t record, uint32_t made)
{
    if (!ScNoting())
        return MakeRun(self, part, record, made);
    ++g_scMade;
    return ScNoted(
        ScMakeCall(addr::SC4EffectsManager_MakeRecordEffect, part, ScIndex(reinterpret_cast<char*>(record)),
                   M<uint32_t>(made)),
        [&] {
            const uint32_t result = MakeRun(self, part, record, made);
            char* const held = At<char*>(reinterpret_cast<char*>(record), 0x38);
            if (ScSwap(held))
                g_scSwapped.push_back(held);
            return result;
        },
        &M<uint32_t>(made));
}
OPENSC4_THUNK(TSC4EffectsManager_MakeRecordEffect, MakeRecordEffectNoted, 12)

// The making has one detour, which the start's comparison and the making's
// own replacement share.
bool HookMakeRecordEffect()
{
    return g_SC4EffectsManager_MakeRecordEffect ||
           Detour(addr::SC4EffectsManager_MakeRecordEffect, &TSC4EffectsManager_MakeRecordEffect,
                  &g_SC4EffectsManager_MakeRecordEffect);
}

std::string ScCallsText(const std::vector<ScCall>& calls, const std::vector<ScCall>& other)
{
    std::size_t at = 0;
    while (at < calls.size() && at < other.size() && calls[at] == other[at])
        ++at;
    char text[96];
    if (at >= calls.size()) {
        std::snprintf(text, sizeof text, "%zu calls", calls.size());
        return text;
    }
    std::snprintf(text, sizeof text, "call %zu of %zu: %08X (%08X, %08X, %08X) ", at, calls.size(), calls[at].target,
                  calls[at].a, calls[at].b, calls[at].c);
    return text + Hex(calls[at].data, kScRecord);
}

uint32_t CompoundCompared(char* self, char* compound, uint8_t first)
{
    const auto game = [&] {
        return g_SC4EffectsManager_StartCompound(self, nullptr, reinterpret_cast<uint32_t>(compound), first);
    };
    char* const begin = At<char*>(compound, 0x114);
    const uint32_t used = static_cast<uint32_t>((At<char*>(compound, 0x118) - begin) / static_cast<int32_t>(kScRecord));
    const uint32_t room = static_cast<uint32_t>((At<char*>(compound, 0x11C) - begin) / static_cast<int32_t>(kScRecord));
    if (At<uint8_t>(self, 0xB8C) || room > kScRoom / 4) {
        static bool logged = false;
        ScLogOnce(logged, At<uint8_t>(self, 0xB8C) ? "a start with the manager's byte +0xB8C set"
                                                   : "a compound effect of very many records");
        return game();
    }
    const std::vector<char> before(compound, compound + kScCompound);
    const std::vector<char> recordsBefore(begin, begin + used * kScRecord);
    const std::vector<char> generator(self + 0x1C0, self + 0x1C0 + kScGenerator);
    g_scGame.clear();
    g_scSwapped.clear();
    for (uint32_t i = 0; i < used; ++i) {
        if (ScSwap(At<char*>(begin + i * kScRecord, 0x38)))
            g_scSwapped.push_back(At<char*>(begin + i * kScRecord, 0x38));
    }
    g_scCurrent = compound;
    g_scThread = GetCurrentThreadId();
    g_scDepth = 0;
    g_scNoting = true;
    const uint32_t result = game();
    g_scNoting = false;
    for (char* const held : g_scSwapped)
        BGiveBack(held, true);
    g_scSwapped.clear();
    const ScImage after = ScTakeImage(compound);
    const std::vector<char> generatorAfter(self + 0x1C0, self + 0x1C0 + kScGenerator);

    // Ours, on a copy of the compound effect and its records as they were.
    static std::vector<char> storage((kScRoom + 2) * kScRecord);
    std::vector<char> mine = before;
    char* const mineBegin = storage.data() + kScRecord;
    if (used)
        std::memcpy(mineBegin, recordsBefore.data(), recordsBefore.size());
    At<char*>(mine.data(), 0x114) = mineBegin;
    At<char*>(mine.data(), 0x118) = mineBegin + used * kScRecord;
    At<char*>(mine.data(), 0x11C) = mineBegin + room * kScRecord;
    std::memcpy(self + 0x1C0, generator.data(), kScGenerator);
    g_scOurs.clear();
    g_scCurrent = mine.data();
    g_scReplay = true;
    StartCompoundEffect(self, mine.data(), first);
    g_scReplay = false;
    const ScImage ours = ScTakeImage(mine.data());
    g_scCurrent = nullptr;
    const bool sameGenerator = std::memcmp(self + 0x1C0, generatorAfter.data(), kScGenerator) == 0;
    std::memcpy(self + 0x1C0, generatorAfter.data(), kScGenerator);

    ++g_scCompared;
    g_scCalls += g_scGame.size();
    if ((g_scCompared & (g_scCompared - 1)) == 0)
        log::Write("water effects: effects.compound: %llu starts compared so far, %llu calls, %llu effects made, "
                   "%llu stopped",
                   static_cast<unsigned long long>(g_scCompared), static_cast<unsigned long long>(g_scCalls),
                   static_cast<unsigned long long>(g_scMade), static_cast<unsigned long long>(g_scStopped));
    const char* const id = "effects.compound";
    char text[96];
    if (!(g_scGame == g_scOurs)) {
        compare::Record(id, false, ScCallsText(g_scGame, g_scOurs), ScCallsText(g_scOurs, g_scGame));
    } else if (after.used != ours.used || after.room != ours.room) {
        std::snprintf(text, sizeof text, "%u records, room for %u", after.used, after.room);
        const std::string theirs = text;
        std::snprintf(text, sizeof text, "%u records, room for %u", ours.used, ours.room);
        compare::Record(id, false, theirs, text);
    } else if (after.bytes != ours.bytes) {
        std::size_t at = 0;
        while (after.bytes[at] == ours.bytes[at])
            ++at;
        at &= ~std::size_t{3};
        if (at < kScCompound)
            std::snprintf(text, sizeof text, "compound +%zX ", at);
        else
            std::snprintf(text, sizeof text, "record %zu +%zX ", (at - kScCompound) / kScRecord,
                          (at - kScCompound) % kScRecord);
        compare::Record(id, false, text + Hex(after.bytes.data() + at, 4), text + Hex(ours.bytes.data() + at, 4));
    } else if (!sameGenerator) {
        compare::Record(id, false, "the generator's state", "another");
    } else {
        compare::Record(id, true, std::string(), std::string());
    }
    return result;
}

uint32_t StartCompoundRun(char* self, uint32_t compound, uint32_t flag)
{
    if (g_scMode == StepMode::Ours) {
        StartCompoundEffect(self, reinterpret_cast<char*>(compound), static_cast<uint8_t>(flag));
        return 0;
    }
    if (g_scMode == StepMode::Compare && !g_scNoting && !g_scReplay)
        return CompoundCompared(self, reinterpret_cast<char*>(compound), static_cast<uint8_t>(flag));
    // A start inside a start that is being noted is part of the call it is in.
    ++g_scDepth;
    const uint32_t result = g_SC4EffectsManager_StartCompound(self, nullptr, compound, flag);
    --g_scDepth;
    return result;
}

bool InstallCompoundStart(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(HookStartCompound() &&
          Detour(addr::SC4EffectRecord_Construct, &TSC4EffectRecord_Construct, &g_SC4EffectRecord_Construct) &&
          Detour(addr::SC4EffectRecord_CopyConstruct, &TSC4EffectRecord_CopyConstruct,
                 &g_SC4EffectRecord_CopyConstruct) &&
          Detour(addr::SC4EffectsManager_FadeByLevel, &TSC4EffectsManager_FadeByLevel,
                 &g_SC4EffectsManager_FadeByLevel) &&
          Detour(addr::SC4CompoundEffect_ApplyValues, &TSC4CompoundEffect_ApplyValues,
                 &g_SC4CompoundEffect_ApplyValues)))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookEraseRecords() &&
          Detour(addr::SC4EffectRecord_Assign, &TSC4EffectRecord_Assign, &g_SC4EffectRecord_Assign) &&
          Detour(addr::SC4EffectRecords_GrowInsert, &TSC4EffectRecords_GrowInsert, &g_SC4EffectRecords_GrowInsert) &&
          HookMakeRecordEffect()))
        return false;
    g_scMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the start of a compound effect %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredCompoundStart = registry::Register({
    "renderer.effects.compound",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectsManager_StartCompound,
    "The effects manager's start of a compound effect: the records of parts no longer within the level of detail "
    "stopped and erased, a record made for each part new to it (the making of its effect is renderer.effects.make), "
    "each given its transform and started; with a record's two constructors, the level's values and the compound "
    "effect's own; compare mode runs the game's with its calls noted and replays ours on a copy of the compound "
    "effect (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallCompoundStart,
});

// --- The making of a record's effect ---------------------------------------------------------

// The making and the small function under it (effects_make.inc). The
// making makes effects, so it cannot run twice. Its comparison is the
// start's kind: the game's first, noting each call it makes that ours
// makes too: the six makers, the taking of a description, the value and
// the hold of a new effect (through a copy of its table of functions),
// the two calls for a remembered effect, the decal's description, the
// look-up of a name, the compound effect's description and the record's
// taking of what was made (through detours). Then ours runs on a copy of
// the record with its calls only noted, each maker answered with what the
// game's made; a compound effect it is answered with is a copy, so that
// the index ours stores in it can be compared. The calls, the record and
// the remembered word are compared; pointers into the record and into
// what was made are compared by place. The name given to a new compound
// effect is not a noted call (the string's assign is every string's): ours
// is checked against the name the game's left. The entry a part of the
// kinds 3 to 0xF asks is watched through a copy of its table too. A making
// for a record that already holds something is not compared.
constexpr uint32_t kMkRole = 0x00591730, kMkRole10 = 0x00591000, kMkHoldRole = 0x0058D8D0, kMkAskRole = 0x00591964;
constexpr uint32_t kMkStringAssign = 0x00408C40; // (this = string, const string*), ret 4
constexpr std::size_t kMkCompound = 0x13C, kMkRecord = 0x44;

StepMode g_mkMode = StepMode::Original;
Call1Fn g_SC4EffectHolder_Assign = nullptr, g_SC4EffectB_SetLinked = nullptr, g_SC4EffectDecal_SetDescription = nullptr,
        g_SC4EffectsManager_FindDescriptionByName = nullptr, g_SC4CompoundEffect_SetDescription = nullptr;
Call0Fn g_SC4Effect_SetMarked = nullptr;

struct MkCall {
    uint32_t target, a, b, c, value, result;
    bool operator==(const MkCall& other) const
    {
        return target == other.target && a == other.a && b == other.b && c == other.c && value == other.value;
    }
};
bool g_mkNoting = false, g_mkReplay = false, g_mkSkip = false;
int g_mkDepth = 0, g_mkHoldMode = 0;
DWORD g_mkThread = 0;
// The record and the made objects that pointers are placed in: the game's
// while it runs, ours' in the replay.
char* g_mkRecord = nullptr;
std::vector<char*>* g_mkMade = nullptr;
std::vector<char*> g_mkMadeGame, g_mkMadeOurs, g_mkSwapped;
std::vector<MkCall> g_mkGame, g_mkOurs;
std::vector<std::pair<uint32_t, char*>> g_mkHoldEvents;
char g_mkScratch[kMkCompound], g_mkDummy[0x400];
char* g_mkScratchOf = nullptr;
uint64_t g_mkCompared = 0, g_mkCalls = 0, g_mkAssigns = 0;
std::map<uint32_t, uint64_t> g_mkMakerCounts;
uint64_t g_mkAsked = 0;

bool MkNoting()
{
    return g_mkNoting && g_mkDepth == 0 && GetCurrentThreadId() == g_mkThread;
}

// A pointer into the record or to the start of something made, by place.
uint32_t MkPlace(uint32_t pointer)
{
    char* const at = reinterpret_cast<char*>(pointer);
    if (g_mkRecord && at >= g_mkRecord && at < g_mkRecord + kMkRecord)
        return 0xD0000000u | static_cast<uint32_t>(at - g_mkRecord);
    if (g_mkMade) {
        for (std::size_t i = 0; i < g_mkMade->size(); ++i) {
            if (at >= (*g_mkMade)[i] && at < (*g_mkMade)[i] + 0x10)
                return 0xE0000000u | static_cast<uint32_t>(i << 16) | static_cast<uint32_t>(at - (*g_mkMade)[i]);
        }
    }
    return pointer;
}

uint32_t MkPlace(const char* pointer)
{
    return MkPlace(reinterpret_cast<uint32_t>(pointer));
}

MkCall MkMakeCall(uint32_t target, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t value = 0)
{
    return {target, a, b, c, value, 0};
}

template <typename Real>
uint32_t MkNoted(MkCall call, Real&& real)
{
    ++g_mkDepth;
    call.result = real();
    --g_mkDepth;
    g_mkGame.push_back(call);
    return call.result;
}

// Ours' call in the replay: noted, and answered as the game's at the same place was.
uint32_t MkReplayed(const MkCall& call)
{
    const std::size_t at = g_mkOurs.size();
    g_mkOurs.push_back(call);
    return at < g_mkGame.size() && g_mkGame[at] == call ? g_mkGame[at].result : 0;
}

uint32_t MkSlot(char* object, uint32_t slot)
{
    return At<uint32_t>(At<char*>(object, 0), slot);
}

// What the making calls: the real thing, or in the replay its note.
char* MkMaker(uint32_t target, char* self)
{
    if (!g_mkReplay)
        return CallThis<char*>(target, self);
    char* result = reinterpret_cast<char*>(MkReplayed(MkMakeCall(target)));
    if (target == addr::SC4EffectsManager_NewCompound) {
        g_mkScratchOf = result;
        if (result)
            std::memcpy(g_mkScratch, result, kMkCompound);
        At<int32_t>(g_mkScratch, 0x20) = 0x7FFFFFFE;
        result = g_mkScratch;
    } else if (!result) {
        result = g_mkDummy;
    }
    g_mkMadeOurs.push_back(result);
    return result;
}

void MkSetDescription(char* effect, char* description)
{
    if (g_mkReplay)
        MkReplayed(MkMakeCall(0x28, MkPlace(effect), reinterpret_cast<uint32_t>(description)));
    else
        CallThis<void>(MkSlot(effect, 0x28), effect, description);
}

void MkValues(char* effect, uint32_t which, float* values, uint32_t count)
{
    if (g_mkReplay)
        MkReplayed(MkMakeCall(0x14, MkPlace(effect), which, count, At<uint32_t>(reinterpret_cast<char*>(values), 0)));
    else
        CallThis<void>(MkSlot(effect, 0x14), effect, which, values, count);
}

void MkHold(char* object)
{
    if (g_mkReplay)
        MkReplayed(MkMakeCall(0x1C, MkPlace(object)));
    else
        CallThis<void>(MkSlot(object, 0x1C), object);
}

void MkEnd(char* object)
{
    if (g_mkReplay)
        MkReplayed(MkMakeCall(0x20, MkPlace(object)));
    else
        CallThis<void>(MkSlot(object, 0x20), object);
}

void MkGive(char* object, uint32_t value)
{
    if (g_mkReplay)
        MkReplayed(MkMakeCall(0x3C, MkPlace(object), value));
    else
        CallThis<void>(MkSlot(object, 0x3C), object, value);
}

void MkLink(char* before, char* second)
{
    if (g_mkReplay)
        MkReplayed(MkMakeCall(addr::SC4EffectB_SetLinked, MkPlace(before), MkPlace(second)));
    else
        CallThis<void>(addr::SC4EffectB_SetLinked, before, second);
}

void MkMark(char* second)
{
    if (g_mkReplay)
        MkReplayed(MkMakeCall(addr::SC4Effect_SetMarked, MkPlace(second)));
    else
        CallThis<void>(addr::SC4Effect_SetMarked, second);
}

void MkDecalDescription(char* decal, char* description)
{
    if (g_mkReplay)
        MkReplayed(
            MkMakeCall(addr::SC4EffectDecal_SetDescription, MkPlace(decal), reinterpret_cast<uint32_t>(description)));
    else
        CallThis<void>(addr::SC4EffectDecal_SetDescription, decal, description);
}

// In the replay the compound effect is a copy of the game's as the game's
// making left it: the string ours names must be the one that has the name.
void MkString(char* string, char* name)
{
    if (!g_mkReplay) {
        CallThis<void>(kMkStringAssign, string, name);
        return;
    }
    const std::size_t size = static_cast<std::size_t>(At<char*>(name, 4) - At<char*>(name, 0));
    const bool same = string == g_mkScratch + 0xC && g_mkScratchOf &&
                      static_cast<std::size_t>(At<char*>(string, 4) - At<char*>(string, 0)) == size &&
                      std::memcmp(At<char*>(string, 0), At<char*>(name, 0), size) == 0;
    if (!same)
        g_mkOurs.push_back(MkMakeCall(kMkStringAssign, MkPlace(string), reinterpret_cast<uint32_t>(name)));
}

int32_t MkFind(char* self, char* name)
{
    if (g_mkReplay)
        return static_cast<int32_t>(
            MkReplayed(MkMakeCall(addr::SC4EffectsManager_FindDescriptionByName, reinterpret_cast<uint32_t>(name))));
    return CallThis<int32_t>(addr::SC4EffectsManager_FindDescriptionByName, self, name);
}

void MkCompoundDescription(char* compound, char* description)
{
    if (g_mkReplay)
        MkReplayed(MkMakeCall(addr::SC4CompoundEffect_SetDescription, MkPlace(compound),
                              reinterpret_cast<uint32_t>(description)));
    else
        CallThis<void>(addr::SC4CompoundEffect_SetDescription, compound, description);
}

char* MkAsk(char* source)
{
    if (g_mkReplay)
        return reinterpret_cast<char*>(MkReplayed(MkMakeCall(0x4, reinterpret_cast<uint32_t>(source))));
    return CallThis<char*>(MkSlot(source, 0x4), source);
}

char* AssignHeld(char* holder, char* object);

void MkAssign(char* holder, char* object)
{
    if (g_mkReplay) {
        MkReplayed(MkMakeCall(addr::SC4EffectHolder_Assign, MkPlace(holder), MkPlace(object)));
        At<char*>(holder, 0) = object;
    } else {
        AssignHeld(holder, object);
    }
}

#include "core/effects_make.inc"

// The stand-ins for five functions of what the making makes. The hold and
// the letting go are also what the small function's own comparison looks
// at: mode 1 notes the call and makes it, mode 2 only notes it.
uint32_t __fastcall MkDescriptionStub(char* object, void*, uint32_t description)
{
    const uint32_t real = BRealSlot(object, 0x28);
    if (!MkNoting())
        return CallThis<uint32_t>(real, object, description);
    return MkNoted(MkMakeCall(0x28, MkPlace(object), description),
                   [&] { return CallThis<uint32_t>(real, object, description); });
}

uint32_t __fastcall MkValuesStub(char* object, void*, uint32_t which, uint32_t values, uint32_t count)
{
    const uint32_t real = BRealSlot(object, 0x14);
    if (!MkNoting())
        return CallThis<uint32_t>(real, object, which, values, count);
    return MkNoted(MkMakeCall(0x14, MkPlace(object), which, count, M<uint32_t>(values)),
                   [&] { return CallThis<uint32_t>(real, object, which, values, count); });
}

uint32_t __fastcall MkGiveStub(char* object, void*, uint32_t value)
{
    const uint32_t real = BRealSlot(object, 0x3C);
    if (!MkNoting())
        return CallThis<uint32_t>(real, object, value);
    return MkNoted(MkMakeCall(0x3C, MkPlace(object), value), [&] { return CallThis<uint32_t>(real, object, value); });
}

// The object asked of an entry of one of the manager's lists.
uint32_t __fastcall MkAskStub(char* source, void*)
{
    const uint32_t real = BRealSlot(source, 0x4);
    if (!MkNoting())
        return CallThis<uint32_t>(real, source);
    return MkNoted(MkMakeCall(0x4, reinterpret_cast<uint32_t>(source)),
                   [&] { return CallThis<uint32_t>(real, source); });
}

uint32_t __fastcall MkHoldStub(char* object, void*)
{
    if (g_mkHoldMode) {
        g_mkHoldEvents.push_back({0x1C, object});
        if (g_mkHoldMode == 2)
            return 0;
    }
    const uint32_t real = BRealSlot(object, 0x1C);
    if (g_mkHoldMode || !MkNoting())
        return CallThis<uint32_t>(real, object);
    return MkNoted(MkMakeCall(0x1C, MkPlace(object)), [&] { return CallThis<uint32_t>(real, object); });
}

// The call may be the object's last: it gets its own table back first.
uint32_t __fastcall MkEndStub(char* object, void*)
{
    if (g_mkHoldMode) {
        g_mkHoldEvents.push_back({0x20, object});
        if (g_mkHoldMode == 2)
            return 0;
    }
    const uint32_t real = BRealSlot(object, 0x20);
    const bool noting = !g_mkHoldMode && MkNoting();
    const uint32_t place = MkPlace(object);
    BGiveBack(object, true);
    if (!noting)
        return CallThis<uint32_t>(real, object);
    return MkNoted(MkMakeCall(0x20, place), [&] { return CallThis<uint32_t>(real, object); });
}

// True when the object has been given one of the making's tables.
bool MkWatched(char* object)
{
    const auto found = g_bReal.find(object);
    return found != g_bReal.end() &&
           (found->second.slot == kMkRole || found->second.slot == kMkRole10 || found->second.slot == kMkHoldRole);
}

// A maker by the mode of its own replacement (further down).
uint32_t MakerRun(uint32_t maker, char* self);

// A maker of the game's: what it made is watched from here on.
uint32_t MkMakerNoted(uint32_t target, char* self)
{
    if (!MkNoting())
        return MakerRun(target, self);
    char* const made = reinterpret_cast<char*>(MkNoted(MkMakeCall(target), [&] { return MakerRun(target, self); }));
    g_mkMadeGame.push_back(made);
    ++g_mkMakerCounts[target];
    if (target == addr::SC4EffectsManager_NewDecal || target == addr::SC4EffectsManager_NewCompound || !made)
        return reinterpret_cast<uint32_t>(made);
    if (g_bReal.find(made) != g_bReal.end()) {
        g_mkSkip = true;
        return reinterpret_cast<uint32_t>(made);
    }
    if (target == addr::SC4EffectsManager_NewEffect10)
        BSwapRole(made, kMkRole10,
                  {{0x1C, reinterpret_cast<uint32_t>(&MkHoldStub)},
                   {0x20, reinterpret_cast<uint32_t>(&MkEndStub)},
                   {0x3C, reinterpret_cast<uint32_t>(&MkGiveStub)}});
    else
        BSwapRole(made, kMkRole,
                  {{0x14, reinterpret_cast<uint32_t>(&MkValuesStub)},
                   {0x1C, reinterpret_cast<uint32_t>(&MkHoldStub)},
                   {0x20, reinterpret_cast<uint32_t>(&MkEndStub)},
                   {0x28, reinterpret_cast<uint32_t>(&MkDescriptionStub)}});
    g_mkSwapped.push_back(made);
    return reinterpret_cast<uint32_t>(made);
}

#define OPENSC4_MK_MAKER(Name)                                                                                           \
    Call0Fn g_##Name = nullptr;                                                                                          \
    uint32_t __fastcall Name##Noted(char* self, void*)                                                                   \
    {                                                                                                                    \
        return MkMakerNoted(addr::Name, self);                                                                 \
    }                                                                                                                    \
    OPENSC4_THUNK(T##Name, Name##Noted, 0)
OPENSC4_MK_MAKER(SC4EffectsManager_NewEffect)
OPENSC4_MK_MAKER(SC4EffectsManager_NewEffectB)
OPENSC4_MK_MAKER(SC4EffectsManager_NewEffectC)
OPENSC4_MK_MAKER(SC4EffectsManager_NewEffect10)
OPENSC4_MK_MAKER(SC4EffectsManager_NewDecal)
OPENSC4_MK_MAKER(SC4EffectsManager_NewCompound)
#undef OPENSC4_MK_MAKER

// The six makers have one detour each, which the making's comparison and
// the makers' own replacement share.
struct MkMakerHook {
    uint32_t address;
    void (*thunk)();
    Call0Fn* original;
};
const MkMakerHook kMkMakerHooks[] = {
    {addr::SC4EffectsManager_NewEffect, &TSC4EffectsManager_NewEffect, &g_SC4EffectsManager_NewEffect},
    {addr::SC4EffectsManager_NewEffectB, &TSC4EffectsManager_NewEffectB, &g_SC4EffectsManager_NewEffectB},
    {addr::SC4EffectsManager_NewEffectC, &TSC4EffectsManager_NewEffectC, &g_SC4EffectsManager_NewEffectC},
    {addr::SC4EffectsManager_NewEffect10, &TSC4EffectsManager_NewEffect10, &g_SC4EffectsManager_NewEffect10},
    {addr::SC4EffectsManager_NewDecal, &TSC4EffectsManager_NewDecal, &g_SC4EffectsManager_NewDecal},
    {addr::SC4EffectsManager_NewCompound, &TSC4EffectsManager_NewCompound, &g_SC4EffectsManager_NewCompound},
};

Call0Fn MakerOriginal(uint32_t maker)
{
    for (const MkMakerHook& hook : kMkMakerHooks) {
        if (hook.address == maker)
            return *hook.original;
    }
    return nullptr;
}

bool HookMaker(uint32_t maker)
{
    for (const MkMakerHook& hook : kMkMakerHooks) {
        if (hook.address == maker)
            return *hook.original || Detour(hook.address, hook.thunk, hook.original);
    }
    return false;
}

// The game's other functions the making's comparison notes.
uint32_t __fastcall MkLinkNoted(char* before, void*, uint32_t second)
{
    if (!MkNoting())
        return g_SC4EffectB_SetLinked(before, nullptr, second);
    return MkNoted(MkMakeCall(addr::SC4EffectB_SetLinked, MkPlace(before), MkPlace(second)),
                   [&] { return g_SC4EffectB_SetLinked(before, nullptr, second); });
}
OPENSC4_THUNK(TSC4EffectB_SetLinked, MkLinkNoted, 4)

uint32_t __fastcall MkMarkNoted(char* second, void*)
{
    if (!MkNoting())
        return g_SC4Effect_SetMarked(second, nullptr);
    return MkNoted(MkMakeCall(addr::SC4Effect_SetMarked, MkPlace(second)),
                   [&] { return g_SC4Effect_SetMarked(second, nullptr); });
}
OPENSC4_THUNK(TSC4Effect_SetMarked, MkMarkNoted, 0)

uint32_t DecalDescriptionRun(char* decal, uint32_t description);

uint32_t __fastcall MkDecalDescriptionNoted(char* decal, void*, uint32_t description)
{
    if (!MkNoting())
        return DecalDescriptionRun(decal, description);
    return MkNoted(MkMakeCall(addr::SC4EffectDecal_SetDescription, MkPlace(decal), description),
                   [&] { return DecalDescriptionRun(decal, description); });
}
OPENSC4_THUNK(TSC4EffectDecal_SetDescription, MkDecalDescriptionNoted, 4)

// The decal's description has one detour, which the making's comparison
// and its own replacement share.
bool HookDecalDescription()
{
    return g_SC4EffectDecal_SetDescription ||
           Detour(addr::SC4EffectDecal_SetDescription, &TSC4EffectDecal_SetDescription,
                  &g_SC4EffectDecal_SetDescription);
}

// The look-up and the compound effect's description by the modes of
// their own replacements (further down).
uint32_t FindRun(char* self, uint32_t name);
uint32_t CompoundDescriptionRun(char* compound, uint32_t description);

uint32_t __fastcall MkFindNoted(char* self, void*, uint32_t name)
{
    if (!MkNoting())
        return FindRun(self, name);
    return MkNoted(MkMakeCall(addr::SC4EffectsManager_FindDescriptionByName, name),
                   [&] { return FindRun(self, name); });
}
OPENSC4_THUNK(TSC4EffectsManager_FindDescriptionByName, MkFindNoted, 4)

uint32_t __fastcall MkCompoundDescriptionNoted(char* compound, void*, uint32_t description)
{
    if (!MkNoting())
        return CompoundDescriptionRun(compound, description);
    return MkNoted(MkMakeCall(addr::SC4CompoundEffect_SetDescription, MkPlace(compound), description),
                   [&] { return CompoundDescriptionRun(compound, description); });
}
OPENSC4_THUNK(TSC4CompoundEffect_SetDescription, MkCompoundDescriptionNoted, 4)

// The look-up and the compound effect's description have one detour each,
// which the making's comparison and their own replacements share.
bool HookFindDescriptionByName()
{
    return g_SC4EffectsManager_FindDescriptionByName ||
           Detour(addr::SC4EffectsManager_FindDescriptionByName, &TSC4EffectsManager_FindDescriptionByName,
                  &g_SC4EffectsManager_FindDescriptionByName);
}

bool HookCompoundDescription()
{
    return g_SC4CompoundEffect_SetDescription ||
           Detour(addr::SC4CompoundEffect_SetDescription, &TSC4CompoundEffect_SetDescription,
                  &g_SC4CompoundEffect_SetDescription);
}

void MkLogOnce(bool& logged, const char* what)
{
    if (!logged)
        log::Write("water effects: effects.make: %s, not compared", what);
    logged = true;
}

// True when this call gave the object the table.
bool MkWatchHeld(char* object)
{
    if (!object || g_bReal.find(object) != g_bReal.end())
        return false;
    BSwapRole(object, kMkHoldRole,
              {{0x1C, reinterpret_cast<uint32_t>(&MkHoldStub)}, {0x20, reinterpret_cast<uint32_t>(&MkEndStub)}});
    return true;
}

// The record's taking of what was made: the game's with the hold and the
// letting go noted, then ours from the same pointer with them only noted.
uint32_t AssignRun(char* holder, uint32_t object)
{
    char* const next = reinterpret_cast<char*>(object);
    if (g_mkMode == StepMode::Ours)
        return reinterpret_cast<uint32_t>(AssignHeld(holder, next));
    if (g_mkMode == StepMode::Original || g_mkHoldMode)
        return g_SC4EffectHolder_Assign(holder, nullptr, object);
    char* const old = At<char*>(holder, 0);
    const bool gaveNext = MkWatchHeld(next), gaveOld = old != next && MkWatchHeld(old);
    if ((next && !MkWatched(next)) || (old && !MkWatched(old))) {
        static bool logged = false;
        MkLogOnce(logged, "a record's taking of something another comparison watches");
        if (gaveNext)
            BGiveBack(next, true);
        if (gaveOld)
            BGiveBack(old, true);
        return g_SC4EffectHolder_Assign(holder, nullptr, object);
    }
    g_mkHoldEvents.clear();
    g_mkHoldMode = 1;
    const uint32_t result = g_SC4EffectHolder_Assign(holder, nullptr, object);
    const std::vector<std::pair<uint32_t, char*>> theirs = std::move(g_mkHoldEvents);
    g_mkHoldEvents.clear();
    char* const after = At<char*>(holder, 0);
    At<char*>(holder, 0) = old;
    g_mkHoldMode = 2;
    const uint32_t mine = reinterpret_cast<uint32_t>(AssignHeld(holder, next));
    g_mkHoldMode = 0;
    const bool same = result == mine && theirs == g_mkHoldEvents && At<char*>(holder, 0) == after;
    At<char*>(holder, 0) = after;
    if (gaveNext)
        BGiveBack(next, true);
    if (gaveOld)
        BGiveBack(old, true);
    ++g_mkAssigns;
    char text[64];
    std::snprintf(text, sizeof text, "%08X, %zu holds and endings", result, theirs.size());
    const std::string game = text;
    std::snprintf(text, sizeof text, "%08X, %zu holds and endings", mine, g_mkHoldEvents.size());
    compare::Record("effects.make.assign", same, same ? std::string() : game, same ? std::string() : std::string(text));
    return result;
}

uint32_t __fastcall AssignByMode(char* holder, void*, uint32_t object)
{
    if (!MkNoting())
        return AssignRun(holder, object);
    return MkNoted(MkMakeCall(addr::SC4EffectHolder_Assign, MkPlace(holder), MkPlace(object)),
                   [&] { return AssignRun(holder, object); });
}
OPENSC4_THUNK(TSC4EffectHolder_Assign, AssignByMode, 4)

std::string MkCallsText(const std::vector<MkCall>& calls, const std::vector<MkCall>& other)
{
    std::size_t at = 0;
    while (at < calls.size() && at < other.size() && calls[at] == other[at])
        ++at;
    char text[112];
    if (at >= calls.size())
        std::snprintf(text, sizeof text, "%zu calls", calls.size());
    else
        std::snprintf(text, sizeof text, "call %zu of %zu: %08X (%08X, %08X, %08X, %08X)", at, calls.size(),
                      calls[at].target, calls[at].a, calls[at].b, calls[at].c, calls[at].value);
    return text;
}

uint32_t MakeCompared(char* self, uint32_t partArg, uint32_t recordArg, uint32_t madeArg)
{
    const auto game = [&] { return g_SC4EffectsManager_MakeRecordEffect(self, nullptr, partArg, recordArg, madeArg); };
    char* const part = reinterpret_cast<char*>(partArg);
    char* const record = reinterpret_cast<char*>(recordArg);
    uint32_t* const made = reinterpret_cast<uint32_t*>(madeArg);
    const int32_t kind = At<int32_t>(part, 0xC);
    // The entry a part of the kinds 3 to 0xF asks, to watch its answer.
    char* source = nullptr;
    if (kind >= 3 && kind <= 0xF && At<int32_t>(part, 0x68) >= 0) {
        char* const list = At<char*>(self, 0x158) + (kind - 3) * 0x20 + 0x14;
        if (At<uint32_t>(part, 0x68) < static_cast<uint32_t>((At<char*>(list, 4) - At<char*>(list, 0)) >> 2))
            source = At<char**>(list, 0)[At<int32_t>(part, 0x68)];
    }
    if (At<char*>(record, 0x38) || (source && g_bReal.find(source) != g_bReal.end())) {
        static bool logged = false;
        MkLogOnce(logged, At<char*>(record, 0x38) ? "a making for a record that holds something"
                                                 : "a making from an entry another comparison watches");
        return game();
    }
    if (source) {
        BSwapRole(source, kMkAskRole, {{0x4, reinterpret_cast<uint32_t>(&MkAskStub)}});
        ++g_mkAsked;
    }
    char before[kMkRecord], after[kMkRecord], mine[kMkRecord];
    std::memcpy(before, record, kMkRecord);
    const uint32_t madeBefore = *made;
    g_mkGame.clear();
    g_mkMadeGame.clear();
    g_mkSwapped.clear();
    g_mkSkip = false;
    g_mkMade = &g_mkMadeGame;
    g_mkRecord = record;
    g_mkThread = GetCurrentThreadId();
    g_mkDepth = 0;
    g_mkNoting = true;
    const uint32_t result = game();
    g_mkNoting = false;
    if (source)
        BGiveBack(source, true);
    for (char* const object : g_mkSwapped)
        BGiveBack(object, true);
    g_mkSwapped.clear();
    std::memcpy(after, record, kMkRecord);
    At<uint32_t>(after, 0x38) = MkPlace(At<uint32_t>(after, 0x38));
    const uint32_t madeAfter = MkPlace(*made);
    if (g_mkSkip) {
        static bool logged = false;
        MkLogOnce(logged, "a making of an effect another comparison watches");
        g_mkRecord = nullptr;
        g_mkMade = nullptr;
        return result;
    }

    // Ours, on a copy of the record as it was.
    std::memcpy(mine, before, kMkRecord);
    uint32_t mineMade = madeBefore;
    g_mkOurs.clear();
    g_mkMadeOurs.clear();
    g_mkMade = &g_mkMadeOurs;
    g_mkRecord = mine;
    g_mkScratchOf = nullptr;
    g_mkReplay = true;
    MakeRecordEffect(self, part, mine, &mineMade);
    g_mkReplay = false;
    At<uint32_t>(mine, 0x38) = MkPlace(At<uint32_t>(mine, 0x38));
    mineMade = MkPlace(mineMade);
    g_mkRecord = nullptr;
    g_mkMade = nullptr;

    ++g_mkCompared;
    g_mkCalls += g_mkGame.size();
    if ((g_mkCompared & (g_mkCompared - 1)) == 0) {
        std::string makers;
        char text[40];
        for (const auto& [target, count] : g_mkMakerCounts) {
            std::snprintf(text, sizeof text, " %08X x %llu", target, static_cast<unsigned long long>(count));
            makers += text;
        }
        log::Write("water effects: effects.make: %llu makings compared so far, %llu calls, %llu takings, %llu asked of "
                   "the lists; made by%s",
                   static_cast<unsigned long long>(g_mkCompared), static_cast<unsigned long long>(g_mkCalls),
                   static_cast<unsigned long long>(g_mkAssigns), static_cast<unsigned long long>(g_mkAsked),
                   makers.c_str());
    }
    const char* const id = "effects.make";
    char text[64];
    if (!(g_mkGame == g_mkOurs)) {
        compare::Record(id, false, MkCallsText(g_mkGame, g_mkOurs), MkCallsText(g_mkOurs, g_mkGame));
    } else if (std::memcmp(after, mine, kMkRecord) != 0) {
        std::size_t at = 0;
        while (after[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "record +%zX ", at);
        compare::Record(id, false, text + Hex(after + at, 4), text + Hex(mine + at, 4));
    } else if (madeAfter != mineMade) {
        std::snprintf(text, sizeof text, "remembered %08X", madeAfter);
        const std::string theirs = text;
        std::snprintf(text, sizeof text, "remembered %08X", mineMade);
        compare::Record(id, false, theirs, text);
    } else if (g_mkScratchOf && std::memcmp(g_mkScratch, g_mkScratchOf, kMkCompound) != 0) {
        std::size_t at = 0;
        while (g_mkScratch[at] == g_mkScratchOf[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "compound +%zX ", at);
        compare::Record(id, false, text + Hex(g_mkScratchOf + at, 4), text + Hex(g_mkScratch + at, 4));
    } else {
        compare::Record(id, true, std::string(), std::string());
    }
    return result;
}

uint32_t MakeRun(char* self, uint32_t part, uint32_t record, uint32_t made)
{
    if (g_mkMode == StepMode::Ours) {
        MakeRecordEffect(self, reinterpret_cast<char*>(part), reinterpret_cast<char*>(record),
                         reinterpret_cast<uint32_t*>(made));
        return 0;
    }
    if (g_mkMode == StepMode::Compare && !g_mkNoting && !g_mkReplay)
        return MakeCompared(self, part, record, made);
    return g_SC4EffectsManager_MakeRecordEffect(self, nullptr, part, record, made);
}

bool InstallMake(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(HookMakeRecordEffect() &&
          Detour(addr::SC4EffectHolder_Assign, &TSC4EffectHolder_Assign, &g_SC4EffectHolder_Assign)))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookMaker(addr::SC4EffectsManager_NewEffect) && HookMaker(addr::SC4EffectsManager_NewEffectB) &&
          HookMaker(addr::SC4EffectsManager_NewEffectC) && HookMaker(addr::SC4EffectsManager_NewEffect10) &&
          HookMaker(addr::SC4EffectsManager_NewDecal) && HookMaker(addr::SC4EffectsManager_NewCompound) &&
          Detour(addr::SC4EffectB_SetLinked, &TSC4EffectB_SetLinked, &g_SC4EffectB_SetLinked) &&
          Detour(addr::SC4Effect_SetMarked, &TSC4Effect_SetMarked, &g_SC4Effect_SetMarked) &&
          HookDecalDescription() &&
          HookFindDescriptionByName() && HookCompoundDescription()))
        return false;
    g_mkMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the making of a record's effect %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredMake = registry::Register({
    "renderer.effects.make",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectsManager_MakeRecordEffect,
    "The effects manager's making of what a record of a compound effect holds: an effect of particles of one of "
    "three classes given its description, a decal, another compound effect or an object of one of the manager's "
    "lists (the six makers, the decal's and the compound effect's descriptions and the look-up of a name still the "
    "game's), held by the record through the small function that is ours too; compare mode runs the game's with its "
    "calls noted and replays ours on a copy of the record (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallMake,
});

// --- The makers of what a record holds ------------------------------------------------------

// The four makers the test cities use and the decal's reset
// (effects_makers.inc). A maker that takes a free one only moves an index
// between two vectors, so its comparison runs the game's and ours on the
// same memory: the manager's vectors and their storage, put back as they
// were between the two. One that makes a new one allocates and constructs
// it, so it cannot run twice: the game's runs first with its calls noted
// (the allocation, in the pool allocator under operator new, the
// constructor, the effect's taking of the manager's object and the
// decal's reset through detours,
// the calls on the new one, its sub-object and the manager through copies
// of their tables of functions), then ours is replayed on the vectors put
// back, each call answered as the game's was, and the calls, the vectors,
// the answer and the manager a new compound effect holds at +0x148 are
// compared. A making that would grow a vector is not compared.
constexpr uint32_t kMxRole = 0x00590BC0, kMxSubRole = 0x00590BC4, kMxManagerRole = 0x00590A64;
constexpr uint32_t kMxEffectInsert = 0x0058FF70; // (this = table, at, const value*, tag*, count, atEnd), ret 0x14
constexpr std::size_t kMxListsAt = 0xB94, kMxListsSize = 0xC54 - 0xB94;

// What one maker works on: its list, the size of a new one and its
// constructor.
struct MxSpec {
    uint32_t maker, table, used, freed, size, constructor;
};

const MxSpec kMxSpecs[] = {
    {addr::SC4EffectsManager_NewEffect, 0xBE8, 0xBF4, 0xC00, 0x220, addr::SC4Effect_Construct},
    {addr::SC4EffectsManager_NewEffectB, 0xC30, 0xC3C, 0xC48, 0x280, addr::SC4EffectB_Construct},
    {addr::SC4EffectsManager_NewDecal, 0xBC4, 0xBD0, 0xBDC, 0x124, addr::SC4EffectDecal_Construct},
    {addr::SC4EffectsManager_NewCompound, 0xB94, 0xBA0, 0xBB8, 0x14C, addr::SC4CompoundEffect_Construct},
};

StepMode g_mxMode = StepMode::Original;
using NewFn = void*(__cdecl*)(uint32_t);
Call1Fn g_SC4_PoolAllocate = nullptr;
Call0Fn g_SC4EffectDecal_Construct = nullptr, g_SC4CompoundEffect_Construct = nullptr, g_SC4EffectDecal_Reset = nullptr;

struct MxNote {
    uint32_t target, a, b, result;
    bool operator==(const MxNote& other) const
    {
        return target == other.target && a == other.a && b == other.b;
    }
};
bool g_mxNoting = false, g_mxReplay = false;
int g_mxDepth = 0;
DWORD g_mxThread = 0;
char* g_mxSelf = nullptr;
char* g_mxMade = nullptr;
uint32_t g_mxHeld148 = 0;
std::vector<MxNote> g_mxGame, g_mxOurs;
std::vector<char*> g_mxSwapped;
uint64_t g_mxTaken = 0, g_mxNew = 0, g_mxGrows = 0, g_mxResets = 0, g_mxObjects = 0;
std::map<uint32_t, uint64_t> g_mxCounts;

bool MxNoting()
{
    return g_mxNoting && g_mxDepth == 0 && GetCurrentThreadId() == g_mxThread;
}

// A pointer into what is being made or into the manager, by place.
uint32_t MxPlace(const void* pointer)
{
    const char* const at = static_cast<const char*>(pointer);
    if (g_mxMade && at >= g_mxMade && at < g_mxMade + 0x10)
        return 0xE0000000u | static_cast<uint32_t>(at - g_mxMade);
    if (g_mxSelf && at >= g_mxSelf && at < g_mxSelf + 0x1000)
        return 0xF0000000u | static_cast<uint32_t>(at - g_mxSelf);
    return reinterpret_cast<uint32_t>(pointer);
}

template <typename Real>
uint32_t MxNoted(MxNote call, Real&& real)
{
    ++g_mxDepth;
    call.result = real();
    --g_mxDepth;
    g_mxGame.push_back(call);
    return call.result;
}

uint32_t MxReplayed(const MxNote& call)
{
    const std::size_t at = g_mxOurs.size();
    g_mxOurs.push_back(call);
    return at < g_mxGame.size() && g_mxGame[at] == call ? g_mxGame[at].result : 0;
}

// What the makers call: the real thing, or in the replay its note.
char* MxNew(uint32_t size)
{
    if (!g_mxReplay)
        return static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(size));
    g_mxMade = reinterpret_cast<char*>(MxReplayed({addr::SC4_operator_new, size, 0, 0}));
    return g_mxMade;
}

char* MxConstruct(uint32_t constructor, char* object)
{
    if (!g_mxReplay)
        return CallThis<char*>(constructor, object);
    char* const made = reinterpret_cast<char*>(MxReplayed({constructor, MxPlace(object), 0, 0}));
    // The compound effect's holder as the game's constructor left it.
    if (made && constructor == addr::SC4CompoundEffect_Construct)
        At<uint32_t>(made, 0x148) = g_mxHeld148;
    return made;
}

void MxCall(char* object, uint32_t slot, char* argument = nullptr)
{
    if (g_mxReplay)
        MxReplayed({slot, MxPlace(object), argument ? MxPlace(argument) : 0, 0});
    else if (slot == 0x34)
        CallThis<void>(At<uint32_t>(At<char*>(object, 0), slot), object, argument);
    else
        CallThis<void>(At<uint32_t>(At<char*>(object, 0), slot), object);
}

void MxResetDecal(char* decal)
{
    if (g_mxReplay)
        MxReplayed({addr::SC4EffectDecal_Reset, MxPlace(decal), 0, 0});
    else
        CallThis<void>(addr::SC4EffectDecal_Reset, decal);
}

void MxSetObject(char* effect, uint32_t object)
{
    if (g_mxReplay)
        MxReplayed({addr::SC4Effect_SetObject, MxPlace(effect), object, 0});
    else
        CallThis<void>(addr::SC4Effect_SetObject, effect, object);
}

void MxGrowIndex(char* vector, uint32_t* at, uint32_t index)
{
    uint8_t tag = 0;
    if (g_mxReplay)
        MxReplayed({addr::SC4PtrVector_InsertN, MxPlace(vector), index, 0});
    else
        CallThis<void>(addr::SC4PtrVector_InsertN, vector, at, &index, &tag, 1u, 1u);
}

void MxGrowTable(bool compound, char* table, char** at, char* object)
{
    uint8_t tag = 0;
    const uint32_t insert = compound ? addr::SC4RefPtrVector_InsertN : kMxEffectInsert;
    if (g_mxReplay)
        MxReplayed({insert, MxPlace(table), MxPlace(object), 0});
    else
        CallThis<void>(insert, table, at, &object, &tag, 1u, 1u);
}

#include "core/effects_makers.inc"
#include "core/effects_lookup.inc"

// The stand-ins for the calls on what is made and on the manager. The
// letting go may be an object's last: it gets its own table back first.
uint32_t MxSlotNoted(char* object, uint32_t slot, char* argument, bool last)
{
    const uint32_t real = BRealSlot(object, slot);
    const bool noting = MxNoting();
    const MxNote call{slot, MxPlace(object), argument ? MxPlace(argument) : 0, 0};
    if (last)
        BGiveBack(object, true);
    const auto run = [&] {
        return argument ? CallThis<uint32_t>(real, object, argument) : CallThis<uint32_t>(real, object);
    };
    return noting ? MxNoted(call, run) : run();
}

uint32_t __fastcall MxSlot04(char* object, void*)
{
    return MxSlotNoted(object, 0x4, nullptr, false);
}

uint32_t __fastcall MxSlot08(char* object, void*)
{
    return MxSlotNoted(object, 0x8, nullptr, true);
}

uint32_t __fastcall MxSlot10(char* object, void*)
{
    return MxSlotNoted(object, 0x10, nullptr, false);
}

uint32_t __fastcall MxSlot1C(char* object, void*)
{
    return MxSlotNoted(object, 0x1C, nullptr, false);
}

uint32_t __fastcall MxSlot20(char* object, void*)
{
    return MxSlotNoted(object, 0x20, nullptr, true);
}

uint32_t __fastcall MxSlot34(char* object, void*, char* block)
{
    return MxSlotNoted(object, 0x34, block, false);
}

void MxWatch(char* object, uint32_t role, std::initializer_list<std::pair<uint32_t, uint32_t>> slots)
{
    if (!object || g_bReal.find(object) != g_bReal.end())
        return;
    BSwapRole(object, role, slots);
    g_mxSwapped.push_back(object);
}

// A constructor of the game's while a maker is noted: what it made is
// watched from here on.
template <typename Real>
uint32_t MxConstructNoted(uint32_t constructor, char* object, Real&& real)
{
    char* const made = reinterpret_cast<char*>(MxNoted({constructor, MxPlace(object), 0, 0}, real));
    if (!made)
        return 0;
    if (constructor == addr::SC4CompoundEffect_Construct) {
        g_mxHeld148 = At<uint32_t>(made, 0x148);
        MxWatch(made, kMxRole,
                {{0x4, reinterpret_cast<uint32_t>(&MxSlot04)}, {0x8, reinterpret_cast<uint32_t>(&MxSlot08)}});
    } else if (constructor == addr::SC4EffectDecal_Construct) {
        MxWatch(made, kMxRole,
                {{0x1C, reinterpret_cast<uint32_t>(&MxSlot1C)}, {0x20, reinterpret_cast<uint32_t>(&MxSlot20)}});
    } else {
        MxWatch(made, kMxRole,
                {{0x1C, reinterpret_cast<uint32_t>(&MxSlot1C)},
                 {0x20, reinterpret_cast<uint32_t>(&MxSlot20)},
                 {0x34, reinterpret_cast<uint32_t>(&MxSlot34)}});
        MxWatch(made + 4, kMxSubRole, {{0x10, reinterpret_cast<uint32_t>(&MxSlot10)}});
    }
    return reinterpret_cast<uint32_t>(made);
}

// The allocation is noted in the pool allocator under operator new
// (whose own entry determinism.zero_alloc has): a maker allocates nothing
// else outside the constructor.
uint32_t __fastcall MxAllocateNoted(char* pool, void*, uint32_t size)
{
    if (!MxNoting())
        return g_SC4_PoolAllocate(pool, nullptr, size);
    g_mxMade = reinterpret_cast<char*>(
        MxNoted({addr::SC4_operator_new, size, 0, 0}, [&] { return g_SC4_PoolAllocate(pool, nullptr, size); }));
    return reinterpret_cast<uint32_t>(g_mxMade);
}
OPENSC4_THUNK(TSC4_PoolAllocate, MxAllocateNoted, 4)

// The two constructors by the mode of their own replacement (further
// down).
uint32_t DecalCtorRun(char* self);
uint32_t CompoundCtorRun(char* self);

uint32_t __fastcall DecalConstructNoted(char* object, void*)
{
    if (!MxNoting())
        return DecalCtorRun(object);
    return MxConstructNoted(addr::SC4EffectDecal_Construct, object, [&] { return DecalCtorRun(object); });
}
OPENSC4_THUNK(TSC4EffectDecal_Construct, DecalConstructNoted, 0)

uint32_t __fastcall CompoundConstructNoted(char* object, void*)
{
    if (!MxNoting())
        return CompoundCtorRun(object);
    return MxConstructNoted(addr::SC4CompoundEffect_Construct, object, [&] { return CompoundCtorRun(object); });
}
OPENSC4_THUNK(TSC4CompoundEffect_Construct, CompoundConstructNoted, 0)

// The two constructors have one detour each, which the makers' comparison
// and their own replacement share.
bool HookDecalConstruct()
{
    return g_SC4EffectDecal_Construct ||
           Detour(addr::SC4EffectDecal_Construct, &TSC4EffectDecal_Construct, &g_SC4EffectDecal_Construct);
}

bool HookCompoundConstruct()
{
    return g_SC4CompoundEffect_Construct ||
           Detour(addr::SC4CompoundEffect_Construct, &TSC4CompoundEffect_Construct, &g_SC4CompoundEffect_Construct);
}

uint32_t MxSetObjectNoted(char* self, uint32_t object, uint32_t (*run)(char*, uint32_t))
{
    ++g_mxObjects;
    return MxNoted({addr::SC4Effect_SetObject, MxPlace(self), object, 0}, [&] { return run(self, object); });
}

// The decal's reset: its own comparison runs both on the decal's bytes.
uint32_t ResetRun(char* decal)
{
    if (g_mxMode == StepMode::Ours) {
        ResetDecal(decal);
        return 0;
    }
    if (g_mxMode == StepMode::Original || !decal)
        return g_SC4EffectDecal_Reset(decal, nullptr);
    char before[0x124], after[0x124];
    std::memcpy(before, decal, sizeof before);
    const uint32_t result = g_SC4EffectDecal_Reset(decal, nullptr);
    std::memcpy(after, decal, sizeof after);
    std::memcpy(decal, before, sizeof before);
    ResetDecal(decal);
    const bool same = std::memcmp(decal, after, sizeof after) == 0;
    std::size_t at = 0;
    while (!same && at < sizeof after && decal[at] == after[at])
        ++at;
    at &= ~std::size_t{3};
    char text[24];
    std::snprintf(text, sizeof text, "+%zX ", at);
    compare::Record("effects.makers.reset", same, same ? std::string() : text + Hex(after + at, 4),
                    same ? std::string() : text + Hex(decal + at, 4));
    std::memcpy(decal, after, sizeof after);
    ++g_mxResets;
    return result;
}

uint32_t __fastcall ResetByMode(char* decal, void*)
{
    if (!MxNoting())
        return ResetRun(decal);
    return MxNoted({addr::SC4EffectDecal_Reset, MxPlace(decal), 0, 0}, [&] { return ResetRun(decal); });
}
OPENSC4_THUNK(TSC4EffectDecal_Reset, ResetByMode, 0)

// The memory a maker may change: the manager's lists and the storage of
// the two vectors it adds to.
std::vector<std::pair<char*, std::size_t>> MxRegions(char* self, const MxSpec& spec)
{
    std::vector<std::pair<char*, std::size_t>> regions{{self + kMxListsAt, kMxListsSize}};
    for (const uint32_t vector : {spec.used, spec.table}) {
        char* const begin = At<char*>(self, vector);
        const std::size_t room = static_cast<std::size_t>(At<char*>(self, vector + 8) - begin);
        if (begin && room)
            regions.push_back({begin, room});
    }
    return regions;
}

std::vector<char> MxSave(const std::vector<std::pair<char*, std::size_t>>& regions)
{
    std::vector<char> bytes;
    for (const auto& [at, size] : regions)
        bytes.insert(bytes.end(), at, at + size);
    return bytes;
}

void MxLoad(const std::vector<std::pair<char*, std::size_t>>& regions, const std::vector<char>& bytes)
{
    std::size_t from = 0;
    for (const auto& [at, size] : regions) {
        std::memcpy(at, bytes.data() + from, size);
        from += size;
    }
}

void MxLogOnce(bool& logged, const char* what)
{
    if (!logged)
        log::Write("water effects: effects.makers: %s, not compared", what);
    logged = true;
}

std::string MxCallsText(const std::vector<MxNote>& calls, const std::vector<MxNote>& other)
{
    std::size_t at = 0;
    while (at < calls.size() && at < other.size() && calls[at] == other[at])
        ++at;
    char text[96];
    if (at >= calls.size())
        std::snprintf(text, sizeof text, "%zu calls", calls.size());
    else
        std::snprintf(text, sizeof text, "call %zu of %zu: %08X (%08X, %08X)", at, calls.size(), calls[at].target,
                      calls[at].a, calls[at].b);
    return text;
}

uint32_t MakerCompared(char* self, const MxSpec& spec, Call0Fn original)
{
    const auto game = [&] { return original(self, nullptr); };
    char* const freed = self + spec.freed;
    char* const used = self + spec.used;
    char* const table = self + spec.table;
    const bool taking = At<char*>(freed, 0) != At<char*>(freed, 4);
    const bool full =
        At<char*>(used, 4) == At<char*>(used, 8) || (!taking && At<char*>(table, 4) == At<char*>(table, 8));
    if (full || (!taking && g_bReal.find(self) != g_bReal.end())) {
        static bool logged[2] = {};
        MxLogOnce(logged[full ? 0 : 1],
                  full ? "a making that grows a vector" : "a making while another comparison watches the manager");
        if (full)
            ++g_mxGrows;
        return game();
    }
    const std::vector<std::pair<char*, std::size_t>> regions = MxRegions(self, spec);
    const std::vector<char> before = MxSave(regions);
    g_mxGame.clear();
    g_mxSwapped.clear();
    g_mxSelf = self;
    g_mxMade = nullptr;
    g_mxHeld148 = 0;
    if (!taking && spec.maker == addr::SC4EffectsManager_NewCompound)
        MxWatch(self, kMxManagerRole,
                {{0x4, reinterpret_cast<uint32_t>(&MxSlot04)}, {0x8, reinterpret_cast<uint32_t>(&MxSlot08)}});
    g_mxThread = GetCurrentThreadId();
    g_mxDepth = 0;
    g_mxNoting = true;
    const uint32_t result = game();
    g_mxNoting = false;
    for (char* const watched : g_mxSwapped)
        BGiveBack(watched, true);
    g_mxSwapped.clear();
    char* const made = g_mxMade;
    const std::vector<char> after = MxSave(regions);
    const uint32_t held148 = made && spec.maker == addr::SC4EffectsManager_NewCompound ? At<uint32_t>(made, 0x148) : 0;

    // Ours, on the vectors put back as they were.
    MxLoad(regions, before);
    g_mxOurs.clear();
    g_mxMade = nullptr;
    g_mxReplay = true;
    const uint32_t mine = reinterpret_cast<uint32_t>(MakeFromPool(self, spec));
    g_mxReplay = false;
    const std::vector<char> ours = MxSave(regions);
    const uint32_t mine148 = made && spec.maker == addr::SC4EffectsManager_NewCompound ? At<uint32_t>(made, 0x148) : 0;
    if (made && spec.maker == addr::SC4EffectsManager_NewCompound)
        At<uint32_t>(made, 0x148) = held148;
    MxLoad(regions, after);
    g_mxSelf = nullptr;
    g_mxMade = nullptr;

    ++(taking ? g_mxTaken : g_mxNew);
    ++g_mxCounts[spec.maker];
    if (((g_mxTaken + g_mxNew) & (g_mxTaken + g_mxNew - 1)) == 0) {
        std::string makers;
        char text[40];
        for (const auto& [maker, count] : g_mxCounts) {
            std::snprintf(text, sizeof text, " %08X x %llu", maker, static_cast<unsigned long long>(count));
            makers += text;
        }
        log::Write("water effects: effects.makers: %llu taken from the free ones and %llu made new compared so far, "
                   "%llu not for a vector's growth, %llu resets, %llu given the manager's object; by maker%s",
                   static_cast<unsigned long long>(g_mxTaken), static_cast<unsigned long long>(g_mxNew),
                   static_cast<unsigned long long>(g_mxGrows), static_cast<unsigned long long>(g_mxResets),
                   static_cast<unsigned long long>(g_mxObjects),
                   makers.c_str());
    }
    const char* const id = taking ? "effects.makers.free" : "effects.makers.new";
    char text[64];
    if (!(g_mxGame == g_mxOurs)) {
        compare::Record(id, false, MxCallsText(g_mxGame, g_mxOurs), MxCallsText(g_mxOurs, g_mxGame));
    } else if (after != ours) {
        std::size_t at = 0;
        while (after[at] == ours[at])
            ++at;
        std::snprintf(text, sizeof text, "byte %zu of the lists ", at);
        compare::Record(id, false, text + Hex(after.data() + (at & ~std::size_t{3}), 4),
                        text + Hex(ours.data() + (at & ~std::size_t{3}), 4));
    } else if (mine != result || mine148 != held148) {
        std::snprintf(text, sizeof text, "%08X, holding %08X", result, held148);
        const std::string theirs = text;
        std::snprintf(text, sizeof text, "%08X, holding %08X", mine, mine148);
        compare::Record(id, false, theirs, text);
    } else {
        compare::Record(id, true, std::string(), std::string());
    }
    return result;
}

const MxSpec* MxFind(uint32_t maker)
{
    for (const MxSpec& spec : kMxSpecs) {
        if (spec.maker == maker)
            return &spec;
    }
    return nullptr;
}

uint32_t MakerRun(uint32_t maker, char* self)
{
    const Call0Fn original = MakerOriginal(maker);
    const MxSpec* const spec = MxFind(maker);
    if (!spec || g_mxMode == StepMode::Original || g_mxNoting || g_mxReplay)
        return original(self, nullptr);
    if (g_mxMode == StepMode::Ours)
        return reinterpret_cast<uint32_t>(MakeFromPool(self, *spec));
    return MakerCompared(self, *spec, original);
}

bool InstallMakers(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(HookMaker(addr::SC4EffectsManager_NewEffect) && HookMaker(addr::SC4EffectsManager_NewEffectB) &&
          HookMaker(addr::SC4EffectsManager_NewDecal) && HookMaker(addr::SC4EffectsManager_NewCompound) &&
          Detour(addr::SC4EffectDecal_Reset, &TSC4EffectDecal_Reset, &g_SC4EffectDecal_Reset)))
        return false;
    if (mode == registry::Mode::Compare &&
        !(Detour(addr::SC4_PoolAllocate, &TSC4_PoolAllocate, &g_SC4_PoolAllocate) &&
          HookEffectConstructors() &&
          (g_SC4Effect_SetObject || Detour(addr::SC4Effect_SetObject, &TSC4Effect_SetObject, &g_SC4Effect_SetObject)) &&
          HookDecalConstruct() && HookCompoundConstruct()))
        return false;
    g_mxMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the makers of what a record holds %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredMakers = registry::Register({
    "renderer.effects.makers",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectsManager_NewEffect,
    "The effects manager's makers of effects of the first and second class, decals and compound effects: a free one "
    "taken from the manager's list, or a new one allocated, constructed (the constructors are not part of it), set up "
    "and entered in the list; with the decal's reset; compare mode runs both on the lists for a free one, and for a new "
    "one the game's with its calls noted, ours replayed (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallMakers,
});

// --- The look-up by name and the compound effect's description -----------------------------

// The look-up, the name map's equal range and the compound effect's
// taking of its description (effects_lookup.inc). All three only read, or
// write plain values into the compound effect, so compare mode runs the
// game's and then ours: the look-up's answers, the equal range's sixteen
// bytes and answer, and the compound effect's bytes (put back as the game
// left them) are compared.
StepMode g_lookupMode = StepMode::Original, g_compoundDescriptionMode = StepMode::Original;
Call2Fn g_SC4NameMap_EqualRange = nullptr;
uint64_t g_lookups = 0;

uint32_t FindRun(char* self, uint32_t name)
{
    if (g_lookupMode == StepMode::Ours)
        return static_cast<uint32_t>(FindDescriptionByName(self, reinterpret_cast<char*>(name)));
    const uint32_t result = g_SC4EffectsManager_FindDescriptionByName(self, nullptr, name);
    if (g_lookupMode == StepMode::Original)
        return result;
    const uint32_t mine = static_cast<uint32_t>(FindDescriptionByName(self, reinterpret_cast<char*>(name)));
    char theirs[16], own[16];
    std::snprintf(theirs, sizeof theirs, "%d", static_cast<int32_t>(result));
    std::snprintf(own, sizeof own, "%d", static_cast<int32_t>(mine));
    compare::Record("effects.lookup", mine == result, mine == result ? std::string() : std::string(theirs),
                    mine == result ? std::string() : std::string(own));
    ++g_lookups;
    if ((g_lookups & (g_lookups - 1)) == 0)
        log::Write("water effects: effects.lookup: %llu look-ups compared so far, the last answering %d",
                   static_cast<unsigned long long>(g_lookups), static_cast<int32_t>(result));
    return result;
}

uint32_t __fastcall EqualRangeByMode(char* map, void*, uint32_t out, uint32_t key)
{
    if (g_lookupMode == StepMode::Ours)
        return reinterpret_cast<uint32_t>(
            NameEqualRange(map, reinterpret_cast<char*>(out), reinterpret_cast<char*>(key)));
    const uint32_t result = g_SC4NameMap_EqualRange(map, nullptr, out, key);
    if (g_lookupMode == StepMode::Original)
        return result;
    char mine[0x10];
    const bool same = reinterpret_cast<uint32_t>(NameEqualRange(map, mine, reinterpret_cast<char*>(key))) ==
                          reinterpret_cast<uint32_t>(mine) &&
                      result == out && std::memcmp(mine, reinterpret_cast<char*>(out), sizeof mine) == 0;
    compare::Record("effects.lookup.range", same, same ? std::string() : Hex(reinterpret_cast<char*>(out), 0x10),
                    same ? std::string() : Hex(mine, 0x10));
    return result;
}
OPENSC4_THUNK(TSC4NameMap_EqualRange, EqualRangeByMode, 8)

uint32_t CompoundDescriptionRun(char* compound, uint32_t description)
{
    if (g_compoundDescriptionMode == StepMode::Ours) {
        SetCompoundDescription(compound, reinterpret_cast<char*>(description));
        return 0;
    }
    if (g_compoundDescriptionMode == StepMode::Original)
        return g_SC4CompoundEffect_SetDescription(compound, nullptr, description);
    constexpr std::size_t size = 0x14C;
    char before[size], after[size];
    std::memcpy(before, compound, size);
    const uint32_t result = g_SC4CompoundEffect_SetDescription(compound, nullptr, description);
    std::memcpy(after, compound, size);
    std::memcpy(compound, before, size);
    SetCompoundDescription(compound, reinterpret_cast<char*>(description));
    const bool same = std::memcmp(compound, after, size) == 0;
    std::size_t at = 0;
    while (!same && at < size && compound[at] == after[at])
        ++at;
    at &= ~std::size_t{3};
    char text[24];
    std::snprintf(text, sizeof text, "+%zX ", at);
    compare::Record("effects.compound_description", same, same ? std::string() : text + Hex(after + at, 4),
                    same ? std::string() : text + Hex(compound + at, 4));
    std::memcpy(compound, after, size);
    return result;
}

bool InstallLookup(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookFindDescriptionByName() ||
        !Detour(addr::SC4NameMap_EqualRange, &TSC4NameMap_EqualRange, &g_SC4NameMap_EqualRange))
        return false;
    g_lookupMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the look-up of a compound description by name %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredLookup = registry::Register({
    "renderer.effects.lookup",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectsManager_FindDescriptionByName,
    "The effects manager's look-up of a compound description by name: the nodes of the name in its hash map (the "
    "string's hash still the game's), and of their descriptions the one of the largest count within the manager's "
    "limit; compare mode runs the game's and then ours (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallLookup,
});

bool InstallCompoundDescription(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookCompoundDescription())
        return false;
    g_compoundDescriptionMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the compound effect's taking of its description %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredCompoundDescription = registry::Register({
    "renderer.effects.compound_description",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4CompoundEffect_SetDescription,
    "A compound effect's taking of its description: the description's first word and three more kept, the four "
    "blocks that changed made the identity's again and the values emptied; compare mode runs the game's and then ours "
    "on the compound effect's bytes (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallCompoundDescription,
});

// --- A decal's taking of its description ----------------------------------------------------

// The decal's description (effects_decal_description.inc). It copies the
// description in, which can allocate, and lets go of a texture, so it
// cannot run twice. Its comparison runs the game's first, noting the copy
// with what it changed in the decal (through a detour), then puts the
// decal and the generator back and replays ours: the copy only noted and
// its changes made, the letting go only noted, the generator's draws and
// the city's sizes asked again for real (both only read, the generator
// being put back between the two runs). The calls (the game's letting go
// being of the texture the decal held), the decal's bytes and the
// generator's are compared.
constexpr uint32_t kDecalGenerator = 0x00B22C10, kTextureRelease = 0x008090E0;
constexpr std::size_t kDecalSize = 0x124, kDecalGeneratorSize = 0x10;

StepMode g_dsMode = StepMode::Original;
Call1Fn g_SC4DecalDescription_Assign = nullptr;

struct DsNote {
    uint32_t target, a;
    bool operator==(const DsNote& other) const
    {
        return target == other.target && a == other.a;
    }
};
bool g_dsNoting = false, g_dsReplay = false;
DWORD g_dsThread = 0;
char* g_dsDecal = nullptr;
std::vector<DsNote> g_dsGame, g_dsOurs;
std::vector<std::pair<uint32_t, char>> g_dsChanges;
uint64_t g_dsCompared = 0, g_dsReleases = 0, g_dsFlagged = 0;

void DsCopy(char* to, char* description)
{
    if (!g_dsReplay) {
        CallThis<void>(addr::SC4DecalDescription_Assign, to, description);
        return;
    }
    g_dsOurs.push_back({addr::SC4DecalDescription_Assign, static_cast<uint32_t>(to - g_dsDecal)});
    for (const auto& [offset, value] : g_dsChanges)
        g_dsDecal[offset] = value;
}

void DsRelease(char* texture)
{
    if (g_dsReplay)
        g_dsOurs.push_back({kTextureRelease, reinterpret_cast<uint32_t>(texture)});
    else
        CallThis<void>(kTextureRelease, texture);
}

double DsUniform(double low, double high)
{
    return reinterpret_cast<double(__thiscall*)(void*, double, double)>(addr::SC4Random_Uniform)(
        reinterpret_cast<void*>(kDecalGenerator), low, high);
}

double DsCityFloat(char* city, uint32_t slot)
{
    return reinterpret_cast<double(__thiscall*)(void*)>(At<uint32_t>(At<char*>(city, 0), slot))(city);
}

#include "core/effects_decal_description.inc"

// The copy of the game's while a decal's description is noted.
uint32_t __fastcall DecalDescriptionAssignNoted(char* to, void*, uint32_t description)
{
    if (!g_dsNoting || GetCurrentThreadId() != g_dsThread || to != g_dsDecal + 0xC)
        return g_SC4DecalDescription_Assign(to, nullptr, description);
    char before[kDecalSize];
    std::memcpy(before, g_dsDecal, kDecalSize);
    const uint32_t result = g_SC4DecalDescription_Assign(to, nullptr, description);
    for (uint32_t at = 0; at < kDecalSize; ++at) {
        if (before[at] != g_dsDecal[at])
            g_dsChanges.push_back({at, g_dsDecal[at]});
    }
    g_dsGame.push_back({addr::SC4DecalDescription_Assign, 0xC});
    return result;
}
OPENSC4_THUNK(TSC4DecalDescription_Assign, DecalDescriptionAssignNoted, 4)

std::string DsNotesText(const std::vector<DsNote>& notes)
{
    std::string text;
    char note[32];
    for (const DsNote& at : notes) {
        std::snprintf(note, sizeof note, "%s%08X(%08X)", text.empty() ? "" : " ", at.target, at.a);
        text += note;
    }
    return text.empty() ? std::string("no calls") : text;
}

uint32_t DecalDescriptionCompared(char* decal, uint32_t description)
{
    const auto game = [&] { return g_SC4EffectDecal_SetDescription(decal, nullptr, description); };
    if (g_dsNoting || g_dsReplay)
        return game();
    char* const generator = reinterpret_cast<char*>(kDecalGenerator);
    char before[kDecalSize], after[kDecalSize], mine[kDecalSize];
    char generatorBefore[kDecalGeneratorSize], generatorAfter[kDecalGeneratorSize], generatorMine[kDecalGeneratorSize];
    std::memcpy(before, decal, kDecalSize);
    std::memcpy(generatorBefore, generator, kDecalGeneratorSize);
    g_dsGame.clear();
    g_dsChanges.clear();
    g_dsDecal = decal;
    g_dsThread = GetCurrentThreadId();
    g_dsNoting = true;
    const uint32_t result = game();
    g_dsNoting = false;
    if (const uint32_t texture = At<uint32_t>(before, 0x11C)) {
        g_dsGame.push_back({kTextureRelease, texture});
        ++g_dsReleases;
    }
    std::memcpy(after, decal, kDecalSize);
    std::memcpy(generatorAfter, generator, kDecalGeneratorSize);

    // Ours, on the decal and the generator as they were.
    std::memcpy(decal, before, kDecalSize);
    std::memcpy(generator, generatorBefore, kDecalGeneratorSize);
    g_dsOurs.clear();
    g_dsReplay = true;
    SetDecalDescription(decal, reinterpret_cast<char*>(description));
    g_dsReplay = false;
    std::memcpy(mine, decal, kDecalSize);
    std::memcpy(generatorMine, generator, kDecalGeneratorSize);
    std::memcpy(decal, after, kDecalSize);
    std::memcpy(generator, generatorAfter, kDecalGeneratorSize);
    g_dsDecal = nullptr;

    ++g_dsCompared;
    if (At<uint32_t>(after, 0xC) >> 4 & 1)
        ++g_dsFlagged;
    if ((g_dsCompared & (g_dsCompared - 1)) == 0)
        log::Write("water effects: effects.decal_description: %llu compared so far, %llu letting go of a texture, "
                   "%llu with the city's sizes",
                   static_cast<unsigned long long>(g_dsCompared), static_cast<unsigned long long>(g_dsReleases),
                   static_cast<unsigned long long>(g_dsFlagged));
    const char* const id = "effects.decal_description";
    char text[48];
    if (!(g_dsGame == g_dsOurs)) {
        compare::Record(id, false, DsNotesText(g_dsGame), DsNotesText(g_dsOurs));
    } else if (std::memcmp(after, mine, kDecalSize) != 0) {
        std::size_t at = 0;
        while (after[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "+%zX ", at);
        compare::Record(id, false, text + Hex(after + at, 4), text + Hex(mine + at, 4));
    } else if (std::memcmp(generatorAfter, generatorMine, kDecalGeneratorSize) != 0) {
        compare::Record(id, false, "the generator's state", "another");
    } else {
        compare::Record(id, true, std::string(), std::string());
    }
    return result;
}

uint32_t DecalDescriptionRun(char* decal, uint32_t description)
{
    if (g_dsMode == StepMode::Ours) {
        SetDecalDescription(decal, reinterpret_cast<char*>(description));
        return 0;
    }
    if (g_dsMode == StepMode::Original)
        return g_SC4EffectDecal_SetDescription(decal, nullptr, description);
    return DecalDescriptionCompared(decal, description);
}

bool InstallDecalDescription(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookDecalDescription())
        return false;
    if (mode == registry::Mode::Compare &&
        !Detour(addr::SC4DecalDescription_Assign, &TSC4DecalDescription_Assign, &g_SC4DecalDescription_Assign))
        return false;
    g_dsMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the decal's taking of its description %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDecalDescription = registry::Register({
    "renderer.effects.decal_description",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDecal_SetDescription,
    "A decal's taking of its description: the description copied in (the copy still the game's), the texture it held "
    "let go, its rate and three scales drawn from the generator, and with the description's flag the city's sizes; "
    "compare mode runs the game's with the copy noted and replays ours (docs/renderer.md, \"Water effects, sky and "
    "clouds\")",
    true,
    InstallDecalDescription,
});

// --- The constructors of a decal and a compound effect -------------------------------------

// The three constructors (effects_ctors.inc). Each allocates (the decal's
// description five vectors of one value, the compound effect the room of
// its name), so their comparison runs ours on a copy of the memory, then
// the game's on the object itself: the two are compared byte for byte but
// for the allocated pointers, which are compared by what they hold and by
// the sizes they give; ours' allocations are then freed.
StepMode g_ctMode = StepMode::Original;
Call0Fn g_SC4DecalDescription_Construct = nullptr;
uint64_t g_ctCompared = 0;

struct CtVector {
    uint32_t at, used, room, compared;
};

// The vector's place in the two copies agrees in what it holds and in its
// sizes; then its three pointers are set aside in both.
bool CtSameVector(char* theirs, char* mine, const CtVector& vector)
{
    char* const a = At<char*>(theirs, vector.at);
    char* const b = At<char*>(mine, vector.at);
    const bool same = a && b && At<char*>(theirs, vector.at + 4) - a == static_cast<int32_t>(vector.used) &&
                      At<char*>(mine, vector.at + 4) - b == static_cast<int32_t>(vector.used) &&
                      At<char*>(theirs, vector.at + 8) - a == static_cast<int32_t>(vector.room) &&
                      At<char*>(mine, vector.at + 8) - b == static_cast<int32_t>(vector.room) &&
                      std::memcmp(a, b, vector.compared) == 0;
    std::memset(theirs + vector.at, 0, 12);
    std::memset(mine + vector.at, 0, 12);
    return same;
}

template <typename Game>
uint32_t CtCompared(const char* id, char* object, std::size_t size, Game&& game, char* (*ours)(char*),
                    std::initializer_list<CtVector> vectors)
{
    std::vector<char> mine(object, object + size);
    ours(mine.data());
    const uint32_t result = game();
    std::vector<char> theirs(object, object + size);
    bool same = result == reinterpret_cast<uint32_t>(object);
    std::vector<char*> ourAllocations;
    for (const CtVector& vector : vectors) {
        ourAllocations.push_back(At<char*>(mine.data(), vector.at));
        same = CtSameVector(theirs.data(), mine.data(), vector) && same;
    }
    for (char* const allocation : ourAllocations) {
        if (allocation)
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(allocation);
    }
    std::size_t at = 0;
    while (at < size && theirs[at] == mine[at])
        ++at;
    same = same && at == size;
    at &= ~std::size_t{3};
    char text[24];
    std::snprintf(text, sizeof text, "+%zX ", at < size ? at : 0);
    compare::Record(id, same, same ? std::string() : text + Hex(theirs.data() + (at < size ? at : 0), 4),
                    same ? std::string() : text + Hex(mine.data() + (at < size ? at : 0), 4));
    ++g_ctCompared;
    if ((g_ctCompared & (g_ctCompared - 1)) == 0)
        log::Write("water effects: effects.ctors: %llu constructions compared so far",
                   static_cast<unsigned long long>(g_ctCompared));
    return result;
}

#include "core/effects_ctors.inc"

uint32_t DecalDescriptionCtorRun(char* self)
{
    if (g_ctMode == StepMode::Ours)
        return reinterpret_cast<uint32_t>(ConstructDecalDescription(self));
    if (g_ctMode == StepMode::Original)
        return g_SC4DecalDescription_Construct(self, nullptr);
    return CtCompared(
        "effects.ctors.decal_description", self, 0x64, [&] { return g_SC4DecalDescription_Construct(self, nullptr); },
        &ConstructDecalDescription,
        {{0x10, 4, 4, 4}, {0x1C, 4, 4, 4}, {0x28, 4, 4, 4}, {0x34, 0xC, 0xC, 0xC}, {0x40, 4, 4, 4}});
}

uint32_t __fastcall DecalDescriptionCtorByMode(char* self, void*)
{
    return DecalDescriptionCtorRun(self);
}
OPENSC4_THUNK(TSC4DecalDescription_Construct, DecalDescriptionCtorByMode, 0)

uint32_t DecalCtorRun(char* self)
{
    if (g_ctMode == StepMode::Ours)
        return reinterpret_cast<uint32_t>(ConstructDecal(self));
    if (g_ctMode == StepMode::Original)
        return g_SC4EffectDecal_Construct(self, nullptr);
    return CtCompared(
        "effects.ctors.decal", self, 0x124, [&] { return g_SC4EffectDecal_Construct(self, nullptr); }, &ConstructDecal,
        {{0x1C, 4, 4, 4}, {0x28, 4, 4, 4}, {0x34, 4, 4, 4}, {0x40, 0xC, 0xC, 0xC}, {0x4C, 4, 4, 4}});
}

uint32_t CompoundCtorRun(char* self)
{
    if (g_ctMode == StepMode::Ours)
        return reinterpret_cast<uint32_t>(ConstructCompoundEffect(self));
    if (g_ctMode == StepMode::Original)
        return g_SC4CompoundEffect_Construct(self, nullptr);
    return CtCompared(
        "effects.ctors.compound", self, 0x14C, [&] { return g_SC4CompoundEffect_Construct(self, nullptr); },
        &ConstructCompoundEffect, {{0xC, 0, 8, 1}});
}

bool InstallCtors(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookDecalConstruct() || !HookCompoundConstruct() ||
        !Detour(addr::SC4DecalDescription_Construct, &TSC4DecalDescription_Construct, &g_SC4DecalDescription_Construct))
        return false;
    g_ctMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the constructors of a decal and a compound effect %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredCtors = registry::Register({
    "renderer.effects.ctors",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDecal_Construct,
    "The constructors of a decal, its description and a compound effect: tables of functions, first values, the "
    "description's five vectors of one value and the compound effect's empty name; compare mode runs ours on a copy "
    "and the game's on the object (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallCtors,
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
          HookAfterDraw() &&
          HookEffectDraw() &&
          HookDecalDraw()))
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
