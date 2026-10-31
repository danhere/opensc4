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
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
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

// Memory by address.
template <typename T>
T& M(uint32_t address)
{
    return *reinterpret_cast<T*>(static_cast<std::uintptr_t>(address));
}

constexpr std::size_t kManagerSize = 0xDE4; // from the object, 4 below the interface

using Call0Fn = uint32_t(__fastcall*)(char*, void*);
using Call1Fn = uint32_t(__fastcall*)(char*, void*, uint32_t);
using Call2Fn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t);
using Call3Fn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t);

// The noted comparisons' engine (the "B" notes): some calls of functions
// with detours of their own further up are noted through it; defined below.
bool BNoting();
bool BBusy();
uint32_t PartDescriptionByMode(char* part, uint32_t description);
uint32_t BCallee(uint32_t target, uint32_t a, uint32_t data, std::size_t size, const std::function<uint32_t()>& real,
                 uint32_t out = 0);
Call0Fn g_afterDraw = nullptr;

// The clean-up by the mode of its own replacement, defined with it.
uint32_t __fastcall AfterDrawByMode(char* self, void*);

// --- An effect -----------------------------------------------------------------------------

// Some of an effect's functions change the effect and free memory, so they
// cannot run twice. Their comparison runs the game's for real with the
// functions it calls recorded (which, on what, with what, and what the two
// that steer it answered), puts the effect back as it was, and runs ours
// with every call only recorded and answered from the game's record; then
// the two records and the two effects are compared, and the effect is put
// back as the game left it.
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

// What a recorded call changed in the effect and its list nodes (the bytes
// that differ after it, by their place in the saved copy), which the replay
// applies at the same call: ours goes by what those calls leave in the
// effect.
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

// The recorded functions whose answer the caller goes by: the end check (a
// byte) and taking a particle (a pointer).
bool g_poolHooked = false;
// Letting go of what an effect holds records two more, once their detours are in.
bool g_letGoHooked = false;
// While an emitter is compared, its steps (0x005B9BA0) are recorded and answered too.
bool g_emitterRecording = false;
// While an effect's draw is compared, so are its steps and the other calls
// DwRecorded lists (effects_draw.h's switches, at the end of the file).
bool g_drawRecording = false;
bool DwRecorded(uint32_t target);

bool Steers(uint32_t target)
{
    return target == addr::SC4EffectBase_Finish ||
           ((g_emitterRecording || g_drawRecording) && target == addr::SC4Effect_StepParticle) ||
           (g_poolHooked &&
            (target == addr::SC4EffectBase_TakeParticle || target == addr::SC4EffectBaseB_TakeParticle));
}

// The functions of the effect the comparison records in the game's run (the
// particle pool's only once their detours are in).
bool Recorded(uint32_t target)
{
    if (target == addr::SC4EffectBase_Finish || (g_emitterRecording && target == addr::SC4Effect_StepParticle) ||
        (g_drawRecording && DwRecorded(target)))
        return true;
    return g_poolHooked &&
           (target == addr::SC4EffectBase_TakeParticle || target == addr::SC4EffectBaseB_TakeParticle ||
            target == addr::SC4EffectBase_AddParticle || target == addr::SC4EffectBase_GiveBackParticle ||
            target == addr::SC4EffectBaseB_GiveBackParticle || target == addr::SC4Effect_StartAtBirth ||
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

#define OPENSC4_EFFECT_HOOK1(Name)                                                                                     \
    Call1Fn g_##Name = nullptr;                                                                                        \
    uint32_t __fastcall D##Name(char* self, void*, uint32_t a) { return EffectRecorded(addr::Name, g_##Name, self, a); } \
    OPENSC4_THUNK(T##Name, D##Name, 4)

enum class StepMode { Original, Ours, Compare };
using Call5Fn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
Call0Fn g_SC4EffectBase_Finish = nullptr;
void TSC4EffectBase_Finish();

// A particle's start (0x005B7580), written by hand in effects_setup.h; the
// pool's comparison watches it.
Call1Fn g_startParticle = nullptr;

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

// A call noted by the store's engine (the lists of held pointers hold and
// let go of descriptions through these two).
bool StNoting();
uint32_t StCallNoted(uint32_t target, char* self, Call0Fn real);

uint32_t __fastcall HoldByMode(char* self, void*)
{
    if (StNoting())
        return StCallNoted(addr::SC4Effect_Hold, self, g_SC4Effect_Hold);
    if (BNoting())
        return BCallee(addr::SC4Effect_Hold, reinterpret_cast<uint32_t>(self), 0, 0,
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

Call1Fn g_SC4Effect_LoadTexture = nullptr;
Call0Fn g_SC4EffectDescription_Release = nullptr;

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
    if (StNoting())
        return StCallNoted(addr::SC4EffectDescription_Release, self, g_SC4EffectDescription_Release);
    if (BNoting())
        return BCallee(addr::SC4EffectDescription_Release, reinterpret_cast<uint32_t>(self), 0, 0,
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

// In a noted run (an effect given its description) noted, with the two
// words it leaves (SuTextureNoted, with the set-up).
uint32_t SuTextureNoted(char* self, uint32_t which);

uint32_t __fastcall DSC4Effect_LoadTexture(char* self, void*, uint32_t which)
{
    if (StNoting())
        return SuTextureNoted(self, which);
    return LoadTextureByMode(self, which);
}
OPENSC4_THUNK(TSC4Effect_LoadTexture, DSC4Effect_LoadTexture, 4)

// The second effect class: its size, and its part at +0x228.
constexpr std::size_t kEffectSizeB = 0x27C, kEffectPartB = 0x228;

// A block's two bytes and thirteen floats (its two unset bytes left out).
void CopyBlock(char* to, uint32_t block)
{
    const char* const from = reinterpret_cast<const char*>(block);
    std::memcpy(to, from, 2);
    std::memcpy(to + 4, from + 4, 0x34);
}

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
Call1Fn g_SC4EffectPartB_SetDescription = nullptr;
bool g_bRecord = false, g_bReplay = false;
// A dry run: the render context's noted calls (and the batch's draw) are
// noted but not made (the third class's forced draws, on made-up effects
// with a made-up context).
bool g_bDry = false;
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

// What ours calls go through: the real call, or in the comparison's
// replay the note of the game's.
uint32_t BOurs(uint32_t target, uint32_t a, uint32_t data, std::size_t size, const std::function<uint32_t()>& real,
               uint32_t out = 0)
{
    return g_bReplay ? BCallee(target, a, data, size, real, out) : real();
}

// The objects a noted function releases or ends are seen through copies
// of their tables of functions (one slot each, 8 or 0x20).
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

// Noted in the second class's noted run (renderer.effects.description_b).
bool BdNoting(char* self);
uint32_t BdPartDescriptionNoted(char* part, uint32_t description);

uint32_t __fastcall DSC4EffectPartB_SetDescription(char* part, void*, uint32_t description)
{
    if (BdNoting(part - kEffectPartB))
        return BdPartDescriptionNoted(part, description);
    if (!BNoting() || part != g_bSelf + kEffectPartB)
        return BBusy() ? g_SC4EffectPartB_SetDescription(part, nullptr, description)
                       : PartDescriptionByMode(part, description);
    return BCallee(addr::SC4EffectPartB_SetDescription, description, 0, 0,
                   [&] { return g_SC4EffectPartB_SetDescription(part, nullptr, description); });
}
OPENSC4_THUNK(TSC4EffectPartB_SetDescription, DSC4EffectPartB_SetDescription, 4)

// The pool's grow and allocate and the particle's clear (effects_pool.h),
// ours when their switches are ours (set at install, "The pool's three
// parts" at the end).
bool g_growOurs = false, g_clearOurs = false;

#include "core/effects_pool.h"

// The index batch's clear, adds and draw (effects_index_batch.h), ours when
// their switches are ours (set at install, "The index batch's three parts"
// and "The index batch's draw" at the end).
bool g_batchClearOurs = false, g_batchAddQuadsOurs = false, g_batchAddVerticesOurs = false, g_batchDrawOurs = false;

#include "core/effects_index_batch.h"

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
        PlGrowFor(kParticlePool);
    const uint32_t particle = Global<uint32_t>(kParticleFree);
    Global<uint32_t>(kParticleFree) = M<uint32_t>(particle);
    PlClearFor(reinterpret_cast<void*>(particle));
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

// The start by its own mode (renderer.effects.start, with the set-up below).
uint32_t StartByMode(char* effect, uint32_t particle);

uint32_t __fastcall DStartParticle(char* effect, void*, uint32_t particle)
{
    if (g_takeWatch && !g_takeGameParticle) {
        g_takeGameParticle = particle;
        g_takeGameFree = Global<uint32_t>(kParticleFree);
        std::memcpy(g_takeGameBytes, reinterpret_cast<char*>(particle), kParticleBytes);
    }
    return StartByMode(effect, particle);
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

Call0Fn g_SC4EffectBaseB_TakeParticle = nullptr;

// The second class's take and give back by their own switches
// (effects_second.h, at the end of the file).
uint32_t __fastcall TakeBByMode(char* base, void*);
uint32_t __fastcall GiveBackBByMode(char* base, void*, uint32_t particle);

uint32_t __fastcall DSC4EffectBaseB_TakeParticle(char* base, void*)
{
    return EffectRecorded(addr::SC4EffectBaseB_TakeParticle, &TakeBByMode, base);
}
OPENSC4_THUNK(TSC4EffectBaseB_TakeParticle, DSC4EffectBaseB_TakeParticle, 0)

Call1Fn g_SC4EffectBaseB_GiveBackParticle = nullptr;

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
    if (g_holdersMode == StepMode::Original || g_effectRecord || g_effectReplay ||
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
bool TlStNoting();
uint32_t TlNoted1(uint32_t target, char* self, uint32_t word, uint32_t (*run)(char*, uint32_t));

uint32_t __fastcall SetObjectByMode(char* self, void*, uint32_t object)
{
    if (TlStNoting())
        return TlNoted1(addr::SC4Effect_SetObject, self, object, &SetObjectRun);
    return MxNoting() ? MxSetObjectNoted(self, object, &SetObjectRun) : SetObjectRun(self, object);
}
OPENSC4_THUNK(TSC4Effect_SetObject, SetObjectByMode, 4)

uint32_t PartDescriptionByMode(char* part, uint32_t description)
{
    if (g_holdersMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4EffectPartB_SetDescription(part, nullptr, description);
    if (g_holdersMode == StepMode::Ours)
        return SetPartDescription(part, description);
    return HolderCompared(
        description == At<uint32_t>(part, 0x40) ? "effects.holders.part.same" : "effects.holders.part", part, 0x44,
        [&] { return g_SC4EffectPartB_SetDescription(part, nullptr, description); },
        [&] { return SetPartDescription(part, description); });
}
// The starts at birth (0x005B84C0) are recorded through their switch's
// detour (TLfBirth, with the starts at set ages below).
Call1Fn g_SC4Effect_StartAtBirth = nullptr;
void TLfBirth();

// While the game's run is recorded, what it frees is only noted and freed
// once the comparison is over: until then the replay reads and writes the
// nodes the game let go of, and nothing else may be given their memory.
using FreeFn = void(__cdecl*)(void*);
FreeFn g_free = nullptr, g_delete = nullptr;
std::vector<std::pair<FreeFn*, void*>> g_effectFreed;
DWORD g_effectThread = 0;

// Other threads free memory too while the draw runs; only the drawing
// thread's frees are held back.
// The rest go on to the other comparisons' notes, which share the detour.
void __cdecl FreeNoted(uint32_t storage);
void __cdecl DeleteNoted(uint32_t object);

void __cdecl DFree(void* p)
{
    if (g_effectRecord && GetCurrentThreadId() == g_effectThread)
        g_effectFreed.push_back({&g_free, p});
    else
        FreeNoted(reinterpret_cast<uint32_t>(p));
}

void __cdecl DDelete(void* p)
{
    if (g_effectRecord && GetCurrentThreadId() == g_effectThread)
        g_effectFreed.push_back({&g_delete, p});
    else
        DeleteNoted(reinterpret_cast<uint32_t>(p));
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

OPENSC4_THUNK(TAfterDraw, AfterDrawByMode, 0)

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

// The effect's own constructor (0x005B2B50), written by hand in
// effects_setup.h. While a maker is noted, its constructor call is noted
// too.
uint32_t ConstructEffect(char* self);

uint32_t EffectCtorRun(char* self)
{
    if (g_ctorMode == StepMode::Original)
        return g_SC4Effect_Construct(self, nullptr);
    if (g_ctorMode == StepMode::Ours)
        return ConstructEffect(self);
    return CtorCompared("effects.ctor.effect", self, kEffectSize, 0x14, g_SC4Effect_Construct, &ConstructEffect);
}

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

// The third class's constructor (0x005A8ED0), written by hand: the base
// class's, the class's own two tables of functions (0x00AA0B88,
// 0x00AA0B40), the words from +0x220 to +0x228 cleared, the byte +0x22C
// set, its three vectors (+0x230, +0x23C, +0x248) and the model (+0x254)
// cleared, the byte +0x258 cleared, the point at +0x25C the float at
// 0x00B22D40 three times, +0x278 cleared and +0x268 10.0. The words from
// +0x26C to +0x274 are left as they were.
constexpr std::size_t kEffectSizeC = 0x27C;

uint32_t ConstructEffectC(char* self)
{
    CallThis<uint32_t>(addr::SC4Effect_Construct, self);
    At<uint32_t>(self, 0) = 0x00AA0B88;
    At<uint32_t>(self, 4) = 0x00AA0B40;
    At<uint32_t>(self, 0x220) = 0;
    At<uint32_t>(self, 0x224) = 0;
    At<uint32_t>(self, 0x228) = 0;
    At<uint8_t>(self, 0x22C) = 1;
    for (uint32_t at = 0x230; at <= 0x254; at += 4)
        At<uint32_t>(self, at) = 0;
    At<uint8_t>(self, 0x258) = 0;
    const uint32_t point = M<uint32_t>(0x00B22D40);
    At<uint32_t>(self, 0x25C) = point;
    At<uint32_t>(self, 0x260) = point;
    At<uint32_t>(self, 0x264) = point;
    At<uint32_t>(self, 0x278) = 0;
    At<uint32_t>(self, 0x268) = 0x41200000;
    return reinterpret_cast<uint32_t>(self);
}

Call0Fn g_SC4EffectC_Construct = nullptr;

uint32_t EffectCCtorRun(char* self)
{
    if (g_ctorMode == StepMode::Original)
        return g_SC4EffectC_Construct(self, nullptr);
    if (g_ctorMode == StepMode::Ours)
        return ConstructEffectC(self);
    return CtorCompared("effects.ctor.c", self, kEffectSizeC, 0x14, g_SC4EffectC_Construct, &ConstructEffectC);
}

uint32_t __fastcall EffectCCtorByMode(char* self, void*)
{
    if (!MxNoting())
        return EffectCCtorRun(self);
    return MxConstructNoted(addr::SC4EffectC_Construct, self, [&] { return EffectCCtorRun(self); });
}
OPENSC4_THUNK(TSC4EffectC_Construct, EffectCCtorByMode, 0)

// The three effect classes' constructors have one detour each, which their
// own replacement and the makers' comparison share.
bool HookEffectConstructors()
{
    return (g_SC4Effect_Construct ||
            Detour(addr::SC4Effect_Construct, &TSC4Effect_Construct, &g_SC4Effect_Construct)) &&
           (g_SC4EffectB_Construct ||
            Detour(addr::SC4EffectB_Construct, &TSC4EffectB_Construct, &g_SC4EffectB_Construct)) &&
           (g_SC4EffectC_Construct ||
            Detour(addr::SC4EffectC_Construct, &TSC4EffectC_Construct, &g_SC4EffectC_Construct));
}

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
    "The effects' constructors (the base class's, the second and third classes' and the sub-object's): the "
    "tables of functions, an empty particle list and the fields' first values; compare mode runs ours and the "
    "game's on the same memory (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallCtor,
});

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

// The third class's (0x005A90A0): the model at +0x254 released (slot 8),
// each texture of the vector at +0x248 let go (0x008090E0) and the
// vector's storage freed (SC4::Free), the storage of the vector at +0x23C
// freed, each 0x40-byte batch of the vector at +0x230 destroyed (its slot
// 0 with 0) and that storage freed, the storage at +0x220 freed, then the
// base class's. Nothing is cleared. The textures' letting go is kept
// apart as the texture at +0x6C is; a batch's destruction is noted under
// kDtorBatchEnd.
constexpr uint32_t kDtorBatchEnd = 0x3000;
bool g_dtorFrees = false;
std::vector<uint32_t> g_dtorFreed;
std::vector<char*> g_dtorTexturesOurs;

void DtorFree(char* storage)
{
    BOurs(addr::SC4_Free, reinterpret_cast<uint32_t>(storage), 0, 0, [&] {
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(storage);
        return 0u;
    });
}

void DestructEffectC(char* self)
{
    if (char* const model = At<char*>(self, 0x254))
        DtorRelease(model, 0x8);
    for (char** it = At<char**>(self, 0x248); it != At<char**>(self, 0x24C); ++it) {
        if (char* const texture = *it) {
            if (g_bReplay)
                g_dtorTexturesOurs.push_back(texture);
            else
                CallThis<uint32_t>(0x008090E0, texture);
        }
    }
    if (char* const storage = At<char*>(self, 0x248))
        DtorFree(storage);
    if (char* const storage = At<char*>(self, 0x23C))
        DtorFree(storage);
    for (char* batch = At<char*>(self, 0x230); batch != At<char*>(self, 0x234); batch += 0x40) {
        BOurs(kDtorBatchEnd, reinterpret_cast<uint32_t>(batch), 0, 0,
              [&] { return CallThis<uint32_t>(M<uint32_t>(At<uint32_t>(batch, 0)), batch, 0u); });
    }
    if (char* const storage = At<char*>(self, 0x230))
        DtorFree(storage);
    if (char* const storage = At<char*>(self, 0x220))
        DtorFree(storage);
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
    return g_dtorMode == StepMode::Original || g_effectRecord || g_effectReplay ||
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

// The third class's is compared the same way, with three more things
// seen: each batch's destruction through a copy of its table of functions
// (slot 0), the frees of SC4::Free the destructor itself makes (noted
// through the shared detour, and held back until ours has read the
// vectors' storage), and the textures of +0x248, given one more holder
// each for the game's run, which must have taken exactly one from each,
// and let go for real after (the model, held once more for the game's run
// and released after, cannot let go of its own textures in it).
Call0Fn g_SC4EffectC_Destruct = nullptr;
Call1Fn g_SC4EffectC_Delete = nullptr;

uint32_t __fastcall DtorBatchEnd(char* batch, void*, uint32_t flags)
{
    const uint32_t real = BRealSlot(batch, 0);
    const auto run = [&] {
        BGiveBack(batch, true);
        return CallThis<uint32_t>(real, batch, flags);
    };
    if (!BNoting())
        return run();
    return BCallee(kDtorBatchEnd, reinterpret_cast<uint32_t>(batch), 0, 0, run);
}

uint32_t __fastcall EffectCDtorByMode(char* self, void*)
{
    if (DtorPassed())
        return g_SC4EffectC_Destruct(self, nullptr);
    if (g_dtorMode == StepMode::Ours) {
        DestructEffectC(self);
        return 0;
    }
    char* const model = At<char*>(self, 0x254);
    if (model) {
        CallThis<uint32_t>(M<uint32_t>(At<uint32_t>(model, 0) + 4), model);
        BSwap(model, 0x8, reinterpret_cast<uint32_t>(&BRelease));
    }
    for (char* batch = At<char*>(self, 0x230); batch != At<char*>(self, 0x234); batch += 0x40)
        BSwap(batch, 0, reinterpret_cast<uint32_t>(&DtorBatchEnd));
    std::vector<char*> textures;
    std::map<char*, int32_t> holders;
    for (char** it = At<char**>(self, 0x248); it != At<char**>(self, 0x24C); ++it) {
        if (char* const texture = *it) {
            textures.push_back(texture);
            holders.emplace(texture, At<int32_t>(texture, 0xC));
            ++At<int32_t>(texture, 0xC);
        }
    }
    g_dtorTexturesOurs.clear();
    g_dtorFreed.clear();
    g_dtorFrees = true;
    DtorCompared(
        "effects.dtor.c", self, kEffectSizeC, [&] { g_SC4EffectC_Destruct(self, nullptr); },
        [&] { DestructEffectC(self); });
    g_dtorFrees = false;
    for (const uint32_t storage : g_dtorFreed)
        reinterpret_cast<void(__cdecl*)(uint32_t)>(addr::SC4_Free)(storage);
    g_dtorFreed.clear();
    if (!textures.empty()) {
        bool counted = true;
        for (const auto& [texture, count] : holders)
            counted = counted && At<int32_t>(texture, 0xC) == count;
        const bool same = counted && g_dtorTexturesOurs == textures;
        char text[48];
        std::snprintf(text, sizeof text, "%zu let go", textures.size());
        const std::string game = text;
        std::snprintf(text, sizeof text, "%zu let go%s", g_dtorTexturesOurs.size(), counted ? "" : ", counts differ");
        compare::Record("effects.dtor.c.textures", same, same ? std::string() : game,
                        same ? std::string() : std::string(text));
        for (char* const texture : textures)
            CallThis<uint32_t>(0x008090E0, texture);
    }
    if (model)
        CallThis<uint32_t>(M<uint32_t>(At<uint32_t>(model, 0) + 8), model);
    return 0;
}
OPENSC4_THUNK(TSC4EffectC_Destruct, EffectCDtorByMode, 0)

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

// The third class's (0x005A9080, through the thunk at 0x005A8FD0).
uint32_t __fastcall EffectCDeleteByMode(char* self, void*, uint32_t flags)
{
    if (g_dtorMode == StepMode::Original)
        return g_SC4EffectC_Delete(self, nullptr, flags);
    EffectCDtorByMode(self, nullptr);
    if (flags & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(0x005E5620)(self);
    return reinterpret_cast<uint32_t>(self);
}
OPENSC4_THUNK(TSC4EffectC_Delete, EffectCDeleteByMode, 4)

// What every record-and-replay comparison needs: the frees held back
// (HookFrees, below the other comparisons' notes it shares the detour
// with), and the particle pool's functions recorded.
bool HookFrees();

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
        (g_SC4Effect_StartAtBirth || Detour(addr::SC4Effect_StartAtBirth, &TLfBirth, &g_SC4Effect_StartAtBirth)) &&
        Detour(addr::SC4Effect_SetStopped, &TSC4Effect_SetStopped, &g_SC4Effect_SetStopped) &&
        Detour(addr::SC4EffectB_SetStopped, &TSC4EffectB_SetStopped, &g_SC4EffectB_SetStopped) &&
        Detour(addr::SC4EffectBase_SetEnding, &TSC4EffectBase_SetEnding, &g_SC4EffectBase_SetEnding) &&
        Detour(addr::SC4EffectBase_LetGo, &TSC4EffectBase_LetGo, &g_SC4EffectBase_LetGo) &&
        Detour(addr::SC4EffectBaseB_LetGo, &TSC4EffectBaseB_LetGo, &g_SC4EffectBaseB_LetGo);
    return g_poolHooked;
}

bool HookStart()
{
    return g_startParticle || Detour(addr::SC4Effect_StartParticle, &TStartParticle, &g_startParticle);
}

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
    uint32_t mine[0x40 / 4];
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
          Detour(addr::SC4EffectB_Delete, &TSC4EffectB_Delete, &g_SC4EffectB_Delete) &&
          Detour(addr::SC4EffectC_Destruct, &TSC4EffectC_Destruct, &g_SC4EffectC_Destruct) &&
          Detour(addr::SC4EffectC_Delete, &TSC4EffectC_Delete, &g_SC4EffectC_Delete)))
        return false;
    // The comparison notes what the game's destructors call.
    if (mode == registry::Mode::Compare &&
        !(HookFrees() &&
          Detour(addr::SC4EffectBatch_Destruct, &TSC4EffectBatch_Destruct, &g_SC4EffectBatch_Destruct) &&
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
    "The three effect classes' destructors, alone and with the memory freed: what the effect holds released or let "
    "go, its batches, part, vectors and sub-object destroyed; compare mode notes the game's calls and checks ours would make "
    "the same (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallDtor,
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
    "the marks or gives every particle back; compare mode records the game's run and replays ours "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
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
    "records the game's run and replays ours (docs/renderer.md, \"Water effects, sky and clouds\")",
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
    "the game's run and replays ours (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallFinish,
});

// --- The effects manager's clean-up ----------------------------------------------------------

// The clean-up after the draw and the three functions under it
// (effects_cleanup.h). The clean-up cannot run twice: a compound effect
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

#include "core/effects_cleanup.h"

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

bool SbNoting();
template <typename Real>
uint32_t SbNotedStart(uint32_t target, uint32_t a, uint32_t b, Real&& real);

uint32_t __fastcall StartCompoundNoted(char* self, void*, uint32_t compound, uint32_t flag)
{
    if (SbNoting())
        return SbNotedStart(addr::SC4EffectsManager_StartCompound, compound, flag,
                            [&] { return StartCompoundNoted(self, nullptr, compound, flag); });
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
    if (SbNoting())
        return SbNotedStart(addr::SC4EffectsManager_StartQueued, 0, 0,
                            [&] { return StartQueuedByMode(self, nullptr); });
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

// The queued start has one detour, which the clean-up's replacement and the
// starts' comparison share.
bool HookStartQueued()
{
    return g_SC4EffectsManager_StartQueued ||
           Detour(addr::SC4EffectsManager_StartQueued, &TSC4EffectsManager_StartQueued,
                  &g_SC4EffectsManager_StartQueued);
}

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
          HookStartQueued() &&
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
// (effects_sort.h). Each only moves entries of the manager's vector at
// +0xDA4, so each is compared by running the game's and then ours from the
// same vector; the game's calls of the ones under it are compared on the
// way.
constexpr uint32_t kDepthHeapSort = 0x0058F9D0; // cdecl (first, middle, last, unused, unused)

#include "core/effects_sort.h"

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

// The start and the four small functions under it (effects_compound.h).
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
    // The game's goes on to the record before the end: one there must be.
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

#include "core/effects_compound.h"

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
bool GrNoting();
uint32_t GrCopyConstructNoted(char* record, uint32_t from, uint32_t (*run)(char*, uint32_t));
uint32_t CopyConstructRecordRun(char* record, uint32_t from);

// While a growth is noted, its copies are noted too.
uint32_t __fastcall CopyConstructRecordByMode(char* record, void*, uint32_t from)
{
    return GrNoting() ? GrCopyConstructNoted(record, from, &CopyConstructRecordRun)
                      : CopyConstructRecordRun(record, from);
}

uint32_t CopyConstructRecordRun(char* record, uint32_t from)
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
uint32_t AssignRecordRun(char* to, uint32_t from);
uint32_t GrowRun(char* vector, uint32_t at, uint32_t record, uint32_t tag, uint32_t count, uint32_t atEnd);

uint32_t __fastcall AssignRecordNoted(char* to, void*, uint32_t from)
{
    if (!ScNoting())
        return AssignRecordRun(to, from);
    return ScNoted(ScMakeCall(addr::SC4EffectRecord_Assign, ScIndex(to), ScIndex(reinterpret_cast<char*>(from))),
                   [&] { return AssignRecordRun(to, from); });
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
        return GrowRun(records, at, record, tag, count, atEnd);
    return ScNoted(ScGrowCall(reinterpret_cast<char*>(at), reinterpret_cast<char*>(record)),
                   [&] { return GrowRun(records, at, record, tag, count, atEnd); });
}
OPENSC4_THUNK(TSC4EffectRecords_GrowInsert, GrowRecordsNoted, 20)

// The record's assign, the growth and the copy constructor have one detour
// each, which the start's comparison and the records' replacement share.
bool HookRecordAssign()
{
    return g_SC4EffectRecord_Assign ||
           Detour(addr::SC4EffectRecord_Assign, &TSC4EffectRecord_Assign, &g_SC4EffectRecord_Assign);
}

bool HookGrowInsert()
{
    return g_SC4EffectRecords_GrowInsert ||
           Detour(addr::SC4EffectRecords_GrowInsert, &TSC4EffectRecords_GrowInsert, &g_SC4EffectRecords_GrowInsert);
}

bool HookCopyConstruct()
{
    return g_SC4EffectRecord_CopyConstruct ||
           Detour(addr::SC4EffectRecord_CopyConstruct, &TSC4EffectRecord_CopyConstruct,
                  &g_SC4EffectRecord_CopyConstruct);
}

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
          HookCopyConstruct() &&
          Detour(addr::SC4EffectsManager_FadeByLevel, &TSC4EffectsManager_FadeByLevel,
                 &g_SC4EffectsManager_FadeByLevel) &&
          Detour(addr::SC4CompoundEffect_ApplyValues, &TSC4CompoundEffect_ApplyValues,
                 &g_SC4CompoundEffect_ApplyValues)))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookEraseRecords() &&
          HookRecordAssign() && HookGrowInsert() &&
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

// The making and the small function under it (effects_make.h). The
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

#include "core/effects_make.h"

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

bool SbNoting();
uint32_t SbMakerNoted(uint32_t target, char* self);

// A maker of the game's: what it made is watched from here on.
uint32_t MkMakerNoted(uint32_t target, char* self)
{
    if (!MkNoting())
        return SbNoting() ? SbMakerNoted(target, self) : MakerRun(target, self);
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
uint32_t SbCompoundDescriptionNoted(char* compound, uint32_t description);
uint32_t SbFindByNameNoted(char* self, uint32_t name);

uint32_t __fastcall MkFindNoted(char* self, void*, uint32_t name)
{
    if (!MkNoting())
        return SbNoting() ? SbFindByNameNoted(self, name) : FindRun(self, name);
    return MkNoted(MkMakeCall(addr::SC4EffectsManager_FindDescriptionByName, name),
                   [&] { return FindRun(self, name); });
}
OPENSC4_THUNK(TSC4EffectsManager_FindDescriptionByName, MkFindNoted, 4)

uint32_t __fastcall MkCompoundDescriptionNoted(char* compound, void*, uint32_t description)
{
    if (!MkNoting())
        return SbNoting() ? SbCompoundDescriptionNoted(compound, description)
                          : CompoundDescriptionRun(compound, description);
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
// (effects_makers.h). A maker that takes a free one only moves an index
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
constexpr std::size_t kMxListsAt = 0xB94, kMxListsSize = 0xC78 - 0xB94;

// What one maker works on: its list, the size of a new one and its
// constructor.
struct MxSpec {
    uint32_t maker, table, used, freed, size, constructor;
};

const MxSpec kMxSpecs[] = {
    {addr::SC4EffectsManager_NewEffect, 0xBE8, 0xBF4, 0xC00, 0x220, addr::SC4Effect_Construct},
    {addr::SC4EffectsManager_NewEffectB, 0xC30, 0xC3C, 0xC48, 0x280, addr::SC4EffectB_Construct},
    {addr::SC4EffectsManager_NewEffectC, 0xC54, 0xC60, 0xC6C, 0x280, addr::SC4EffectC_Construct},
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

#include "core/effects_makers.h"
#include "core/effects_lookup.h"

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
bool GrNoting();
uint32_t GrAllocateNoted(char* pool, uint32_t size);
bool StNoting();
uint32_t StAllocateNoted(char* pool, uint32_t size);

uint32_t __fastcall MxAllocateNoted(char* pool, void*, uint32_t size)
{
    if (StNoting())
        return StAllocateNoted(pool, size);
    if (GrNoting())
        return GrAllocateNoted(pool, size);
    if (!MxNoting())
        return g_SC4_PoolAllocate(pool, nullptr, size);
    g_mxMade = reinterpret_cast<char*>(
        MxNoted({addr::SC4_operator_new, size, 0, 0}, [&] { return g_SC4_PoolAllocate(pool, nullptr, size); }));
    return reinterpret_cast<uint32_t>(g_mxMade);
}
OPENSC4_THUNK(TSC4_PoolAllocate, MxAllocateNoted, 4)

// The pool allocator has one detour, which the makers', the records', the
// store's, the lists of held pointers' and the vectors' comparisons share.
bool HookPoolAllocate()
{
    return g_SC4_PoolAllocate || Detour(addr::SC4_PoolAllocate, &TSC4_PoolAllocate, &g_SC4_PoolAllocate);
}

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
          HookMaker(addr::SC4EffectsManager_NewEffectC) && HookMaker(addr::SC4EffectsManager_NewDecal) &&
          HookMaker(addr::SC4EffectsManager_NewCompound) &&
          Detour(addr::SC4EffectDecal_Reset, &TSC4EffectDecal_Reset, &g_SC4EffectDecal_Reset)))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookPoolAllocate() &&
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
    "The effects manager's makers of effects of the three classes, decals and compound effects: a free one "
    "taken from the manager's list, or a new one allocated, constructed (the constructors are not part of it), set up "
    "and entered in the list; with the decal's reset; compare mode runs both on the lists for a free one, and for a new "
    "one the game's with its calls noted, ours replayed (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallMakers,
});

// --- The look-up by name and the compound effect's description -----------------------------

// The look-up, the name map's equal range and the compound effect's
// taking of its description (effects_lookup.h). All three only read, or
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

// The equal range has one detour, shared with renderer.effects.tail
// (defined with the tail: it notes in a noted run whatever the mode).
bool HookNameEqualRange();

uint32_t EqualRangeRun(char* map, uint32_t out, uint32_t key)
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

bool InstallIdLookup();

bool InstallLookup(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookFindDescriptionByName() ||
        !HookNameEqualRange() ||
        !InstallIdLookup())
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

// The decal's description (effects_decal_description.h). It copies the
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

#include "core/effects_decal_description.h"

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

// One detour, shared with renderer.effects.decal_parts.
bool HookDecalDescriptionAssign();

bool InstallDecalDescription(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookDecalDescription())
        return false;
    if (mode == registry::Mode::Compare && !HookDecalDescriptionAssign())
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

// The three constructors (effects_ctors.h). Each allocates (the decal's
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

#include "core/effects_ctors.h"

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

// --- A compound effect's records: the assign and the growth --------------------------------

// The assign, the destroy and the growth (effects_records.h). The first
// two hold or let go of effects, so
// their comparisons are the small holder's (renderer.effects.make's): the
// game's first with the holds and the endings noted and made, through
// copies of the effects' tables of functions, then ours on the bytes put
// back with them only noted (the destroy's free only noted, the game's
// being of the storage it had). The growth allocates and frees, so its
// comparison is the noted kind: the game's first, noting the allocation
// (in the pool allocator under the STL allocator), each copy constructed,
// the range copy and the destroy, then ours replayed on the vector put
// back with each call answered; the calls and the vector are compared.
constexpr uint32_t kGrFree = 0x0090CF63;
StepMode g_grMode = StepMode::Original;
using RangeFn = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t);
RangeFn g_SC4EffectRecords_CopyRange = nullptr;
Call0Fn g_SC4EffectRecords_Destroy = nullptr;

struct GrNote {
    uint32_t target, a, b, c, result;
    bool operator==(const GrNote& other) const
    {
        return target == other.target && a == other.a && b == other.b && c == other.c;
    }
};
bool g_grNoting = false, g_grReplay = false;
int g_grDepth = 0;
DWORD g_grThread = 0;
std::vector<GrNote> g_grGame, g_grOurs;
uint64_t g_grGrows = 0, g_grAssigns = 0, g_grDestroys = 0;

bool GrNoting()
{
    return g_grNoting && g_grDepth == 0 && GetCurrentThreadId() == g_grThread;
}

template <typename Real>
uint32_t GrNoted(GrNote note, Real&& real)
{
    ++g_grDepth;
    note.result = real();
    --g_grDepth;
    g_grGame.push_back(note);
    return note.result;
}

uint32_t GrReplayed(const GrNote& note)
{
    const std::size_t at = g_grOurs.size();
    g_grOurs.push_back(note);
    return at < g_grGame.size() && g_grGame[at] == note ? g_grGame[at].result : 0;
}

// What the growth calls: the real thing, or in the replay its note.
char* GrAllocate(uint32_t size)
{
    if (g_grReplay)
        return reinterpret_cast<char*>(GrReplayed({addr::SC4_PoolAllocate, size, 0, 0, 0}));
    return StlAllocate(size);
}

void GrCopyConstruct(char* to, char* from)
{
    if (g_grReplay)
        GrReplayed({addr::SC4EffectRecord_CopyConstruct, reinterpret_cast<uint32_t>(to),
                    reinterpret_cast<uint32_t>(from), 0, 0});
    else
        CallThis<void>(addr::SC4EffectRecord_CopyConstruct, to, from);
}

void DestroyEffectRecords(char* vector);

char* GrCopyRange(char* first, char* last, char* to)
{
    if (g_grReplay)
        return reinterpret_cast<char*>(GrReplayed({addr::SC4EffectRecords_CopyRange,
                                                   reinterpret_cast<uint32_t>(first), reinterpret_cast<uint32_t>(last),
                                                   reinterpret_cast<uint32_t>(to), 0}));
    uint8_t tag = 0;
    using RangeCopy = char*(__cdecl*)(char*, char*, char*, uint8_t*);
    return reinterpret_cast<RangeCopy>(addr::SC4EffectRecords_CopyRange)(first, last, to, &tag);
}

void GrDestroy(char* vector)
{
    if (g_grReplay)
        GrReplayed({addr::SC4EffectRecords_Destroy, reinterpret_cast<uint32_t>(vector), 0, 0, 0});
    else
        DestroyEffectRecords(vector);
}

// The destroy's ending and free: in the holder's comparison (mode 2) only
// noted.
void GrEnd(char* held)
{
    if (g_mkHoldMode == 2)
        g_mkHoldEvents.push_back({0x20, held});
    else
        CallThis<void>(At<uint32_t>(At<char*>(held, 0), 0x20), held);
}

void GrFree(char* storage)
{
    if (g_mkHoldMode == 2)
        g_mkHoldEvents.push_back({kGrFree, storage});
    else
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(storage);
}

#include "core/effects_records.h"

uint32_t GrAllocateNoted(char* pool, uint32_t size)
{
    return GrNoted({addr::SC4_PoolAllocate, size, 0, 0, 0}, [&] { return g_SC4_PoolAllocate(pool, nullptr, size); });
}

uint32_t GrCopyConstructNoted(char* record, uint32_t from, uint32_t (*run)(char*, uint32_t))
{
    return GrNoted({addr::SC4EffectRecord_CopyConstruct, reinterpret_cast<uint32_t>(record), from, 0, 0},
                   [&] { return run(record, from); });
}

// The holder's kind of comparison over some records: `watch` are the
// effects whose holds and endings are noted, `bytes` the memory the call
// writes; `extra` is added to the game's events after its run.
template <typename Game, typename Ours>
bool GrHeldCompared(const char* id, std::vector<char*> watch, char* bytes, std::size_t size, Game&& game, Ours&& ours,
                    const std::vector<std::pair<uint32_t, char*>>& extra)
{
    std::vector<char*> given;
    for (char* const held : watch) {
        if (!held)
            continue;
        if (MkWatchHeld(held))
            given.push_back(held);
        else if (!MkWatched(held)) {
            for (char* const object : given)
                BGiveBack(object, true);
            static bool logged = false;
            if (!logged)
                log::Write("water effects: %s: an effect another comparison watches, not compared", id);
            logged = true;
            game();
            return false;
        }
    }
    const std::vector<char> before(bytes, bytes + size);
    g_mkHoldEvents.clear();
    g_mkHoldMode = 1;
    const uint32_t result = game();
    std::vector<std::pair<uint32_t, char*>> theirs = std::move(g_mkHoldEvents);
    theirs.insert(theirs.end(), extra.begin(), extra.end());
    g_mkHoldEvents.clear();
    const std::vector<char> after(bytes, bytes + size);
    std::memcpy(bytes, before.data(), size);
    g_mkHoldMode = 2;
    const uint32_t mine = ours();
    g_mkHoldMode = 0;
    const std::vector<char> own(bytes, bytes + size);
    std::memcpy(bytes, after.data(), size);
    for (char* const object : given)
        BGiveBack(object, true);
    const bool same = result == mine && theirs == g_mkHoldEvents && own == after;
    char text[64];
    std::snprintf(text, sizeof text, "%08X, %zu holds and endings%s", result, theirs.size(), "");
    const std::string game_text = text;
    std::snprintf(text, sizeof text, "%08X, %zu holds and endings%s", mine, g_mkHoldEvents.size(),
                  own == after ? "" : ", bytes differ");
    compare::Record(id, same, same ? std::string() : game_text, same ? std::string() : std::string(text));
    g_mkHoldEvents.clear();
    return true;
}

uint32_t AssignRecordRun(char* to, uint32_t from)
{
    char* const source = reinterpret_cast<char*>(from);
    if (g_grMode == StepMode::Ours)
        return reinterpret_cast<uint32_t>(AssignEffectRecord(to, source));
    if (g_grMode == StepMode::Original || g_mkHoldMode || g_grNoting || g_grReplay)
        return g_SC4EffectRecord_Assign(to, nullptr, from);
    uint32_t result = 0;
    GrHeldCompared(
        "effects.records.assign", {At<char*>(source, 0x38), At<char*>(to, 0x38)}, to, 0x44,
        [&] { return result = g_SC4EffectRecord_Assign(to, nullptr, from); },
        [&] { return reinterpret_cast<uint32_t>(AssignEffectRecord(to, source)); }, {});
    ++g_grAssigns;
    return result;
}

// The range copy of the game's while a growth is noted.
uint32_t __cdecl CopyRangeNoted(uint32_t first, uint32_t last, uint32_t to, uint32_t tag)
{
    if (!GrNoting())
        return g_SC4EffectRecords_CopyRange(first, last, to, tag);
    return GrNoted({addr::SC4EffectRecords_CopyRange, first, last, to, 0},
                   [&] { return g_SC4EffectRecords_CopyRange(first, last, to, tag); });
}

uint32_t __fastcall DestroyByMode(char* vector, void*)
{
    if (GrNoting())
        return GrNoted({addr::SC4EffectRecords_Destroy, reinterpret_cast<uint32_t>(vector), 0, 0, 0},
                       [&] { return g_SC4EffectRecords_Destroy(vector, nullptr); });
    if (g_grMode == StepMode::Ours) {
        DestroyEffectRecords(vector);
        return 0;
    }
    if (g_grMode == StepMode::Original || g_mkHoldMode || g_grNoting || g_grReplay)
        return g_SC4EffectRecords_Destroy(vector, nullptr);
    std::vector<char*> watch;
    for (char* record = At<char*>(vector, 0); record != At<char*>(vector, 4); record += 0x44)
        watch.push_back(At<char*>(record, 0x38));
    std::vector<std::pair<uint32_t, char*>> extra;
    if (At<char*>(vector, 0))
        extra.push_back({kGrFree, At<char*>(vector, 0)});
    // The end returns nothing: what the game's leaves in eax (an address on
    // the stack) is not compared.
    GrHeldCompared(
        "effects.records.destroy", watch, vector, 0xC,
        [&] {
            g_SC4EffectRecords_Destroy(vector, nullptr);
            return 0u;
        },
        [&] {
            DestroyEffectRecords(vector);
            return 0u;
        },
        extra);
    ++g_grDestroys;
    return 0;
}
OPENSC4_THUNK(TSC4EffectRecords_Destroy, DestroyByMode, 0)

std::string GrNotesText(const std::vector<GrNote>& notes, const std::vector<GrNote>& other)
{
    std::size_t at = 0;
    while (at < notes.size() && at < other.size() && notes[at] == other[at])
        ++at;
    char text[96];
    if (at >= notes.size())
        std::snprintf(text, sizeof text, "%zu calls", notes.size());
    else
        std::snprintf(text, sizeof text, "call %zu of %zu: %08X (%08X, %08X, %08X)", at, notes.size(), notes[at].target,
                      notes[at].a, notes[at].b, notes[at].c);
    return text;
}

uint32_t GrowRun(char* vector, uint32_t at, uint32_t record, uint32_t tag, uint32_t count, uint32_t atEnd)
{
    const auto game = [&] { return g_SC4EffectRecords_GrowInsert(vector, nullptr, at, record, tag, count, atEnd); };
    const auto ours = [&] {
        GrowInsertEffectRecords(vector, reinterpret_cast<char*>(at), reinterpret_cast<char*>(record), count,
                                static_cast<uint8_t>(atEnd) != 0);
    };
    if (g_grMode == StepMode::Ours) {
        ours();
        return 0;
    }
    if (g_grMode == StepMode::Original || g_grNoting || g_grReplay || g_mkHoldMode)
        return game();
    char before[0xC], after[0xC], mine[0xC];
    std::memcpy(before, vector, sizeof before);
    g_grGame.clear();
    g_grThread = GetCurrentThreadId();
    g_grDepth = 0;
    g_grNoting = true;
    const uint32_t result = game();
    g_grNoting = false;
    std::memcpy(after, vector, sizeof after);
    std::memcpy(vector, before, sizeof before);
    g_grOurs.clear();
    g_grReplay = true;
    ours();
    g_grReplay = false;
    std::memcpy(mine, vector, sizeof mine);
    std::memcpy(vector, after, sizeof after);
    ++g_grGrows;
    if ((g_grGrows & (g_grGrows - 1)) == 0)
        log::Write("water effects: effects.records: growth %llu (%zu calls); %llu assigns, %llu destroys compared "
                   "so far",
                   static_cast<unsigned long long>(g_grGrows), g_grGame.size(),
                   static_cast<unsigned long long>(g_grAssigns), static_cast<unsigned long long>(g_grDestroys));
    if (!(g_grGame == g_grOurs))
        compare::Record("effects.records.grow", false, GrNotesText(g_grGame, g_grOurs),
                        GrNotesText(g_grOurs, g_grGame));
    else
        compare::Record("effects.records.grow", std::memcmp(after, mine, sizeof after) == 0, Hex(after, sizeof after),
                        Hex(mine, sizeof mine));
    return result;
}

bool InstallRecords(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookRecordAssign() || !HookGrowInsert() ||
        !Detour(addr::SC4EffectRecords_Destroy, &TSC4EffectRecords_Destroy, &g_SC4EffectRecords_Destroy))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookPoolAllocate() && HookCopyConstruct() &&
          Detour(addr::SC4EffectRecords_CopyRange, reinterpret_cast<void (*)()>(&CopyRangeNoted),
                 &g_SC4EffectRecords_CopyRange)))
        return false;
    g_grMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: a compound effect's records' assign and growth %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredRecords = registry::Register({
    "renderer.effects.records",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectRecords_GrowInsert,
    "A compound effect's records: the assign of one to another, and the vector's growth with its destroy (the range "
    "copy for an insert short of the end still the game's); compare mode runs the game's with the holds and endings "
    "(or, for the growth, its calls) noted and replays "
    "ours (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallRecords,
});

// --- The look-up by IDs and the manager's starts ------------------------------------------

// The ID map's equal range, the look-up by IDs (effects_starts.h, in
// renderer.effects.lookup with the look-up by name: only read, so compare
// mode runs the game's and then ours), and the starts by IDs and by name,
// the restart of those in use and the change of zoom that restarts them
// (renderer.effects.starts). The starts make and
// start compound effects, so their comparison is the noted kind: the
// game's first, noting the look-ups, the maker, the description, the
// starts and the new compound effect's hold (through a copy of its table
// of functions), then ours replayed with each call answered; the calls,
// the answer, the place the caller gets the compound effect and the three
// words written into it are compared. The restart's replay goes through a
// copy of the indices in use as they were before the game's run.
constexpr uint32_t kSbRole = 0x00593AE0;
StepMode g_sbMode = StepMode::Original;
Call1Fn g_SC4IdMap_FindNode = nullptr;
Call2Fn g_SC4IdMap_EqualRange = nullptr;
using FindByIdFn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t);
FindByIdFn g_SC4EffectsManager_FindDescriptionById = nullptr;
Call3Fn g_SC4EffectsManager_StartById = nullptr;
Call0Fn g_SC4EffectsManager_RestartInUse = nullptr;
Call2Fn g_SC4EffectsManager_StartByName = nullptr;
Call1Fn g_SC4EffectsManager_OnZoomChanged = nullptr;

struct SbNote {
    uint32_t target, a, b, result;
    bool operator==(const SbNote& other) const
    {
        return target == other.target && a == other.a && b == other.b;
    }
};
bool g_sbNoting = false, g_sbReplay = false;
int g_sbDepth = 0;
DWORD g_sbThread = 0;
char* g_sbMade = nullptr;
char g_sbMadeFields[0xC];
std::vector<SbNote> g_sbGame, g_sbOurs;
// The lowered names the start by name looks up, as each look-up sees them.
std::vector<std::string> g_sbNamesGame, g_sbNamesOurs;
std::vector<char*> g_sbSwapped;
uint64_t g_sbStarts = 0, g_sbRestarts = 0, g_sbIdLookups = 0, g_sbNames = 0, g_sbZooms = 0;

bool SbNoting()
{
    return g_sbNoting && g_sbDepth == 0 && GetCurrentThreadId() == g_sbThread;
}

template <typename Real>
uint32_t SbNoted(SbNote note, Real&& real)
{
    ++g_sbDepth;
    note.result = real();
    --g_sbDepth;
    g_sbGame.push_back(note);
    return note.result;
}

template <typename Real>
uint32_t SbNotedStart(uint32_t target, uint32_t a, uint32_t b, Real&& real)
{
    return SbNoted({target, a, b, 0}, real);
}

uint32_t SbReplayed(const SbNote& note)
{
    const std::size_t at = g_sbOurs.size();
    g_sbOurs.push_back(note);
    return at < g_sbGame.size() && g_sbGame[at] == note ? g_sbGame[at].result : 0;
}

int32_t FindDescriptionById(char* self, uint32_t group, uint32_t instance);

// What the starts call: the real thing, or in the replay its note.
int32_t SbFindById(char* self, uint32_t group, uint32_t instance)
{
    if (g_sbReplay)
        return static_cast<int32_t>(SbReplayed({addr::SC4EffectsManager_FindDescriptionById, group, instance, 0}));
    return FindDescriptionById(self, group, instance);
}

bool SbFindNode(char* map, const uint32_t* key)
{
    if (g_sbReplay)
        return SbReplayed({addr::SC4IdMap_FindNode, key[0], key[1], 0}) != 0;
    return CallThis<uint32_t>(addr::SC4IdMap_FindNode, map, key) != 0;
}

char* SbNewCompound(char* self)
{
    if (!g_sbReplay)
        return CallThis<char*>(addr::SC4EffectsManager_NewCompound, self);
    char* const made = reinterpret_cast<char*>(SbReplayed({addr::SC4EffectsManager_NewCompound, 0, 0, 0}));
    // What the game's maker left in the three words the start writes.
    if (made && made == g_sbMade)
        std::memcpy(made + 0x18, g_sbMadeFields, sizeof g_sbMadeFields);
    return made ? made : g_mkDummy;
}

void SbCompoundDescription(char* compound, char* description)
{
    if (g_sbReplay)
        SbReplayed({addr::SC4CompoundEffect_SetDescription, reinterpret_cast<uint32_t>(compound),
                    reinterpret_cast<uint32_t>(description), 0});
    else
        CallThis<void>(addr::SC4CompoundEffect_SetDescription, compound, description);
}

void SbStartQueued(char* self)
{
    if (g_sbReplay)
        SbReplayed({addr::SC4EffectsManager_StartQueued, 0, 0, 0});
    else
        CallThis<void>(addr::SC4EffectsManager_StartQueued, self);
}

void SbStartCompound(char* self, char* compound, uint32_t first)
{
    if (g_sbReplay)
        SbReplayed({addr::SC4EffectsManager_StartCompound, reinterpret_cast<uint32_t>(compound), first, 0});
    else
        CallThis<void>(addr::SC4EffectsManager_StartCompound, self, compound, first);
}

void SbHold(char* compound)
{
    if (g_sbReplay)
        SbReplayed({0x4, reinterpret_cast<uint32_t>(compound), 0, 0});
    else
        CallThis<void>(At<uint32_t>(At<char*>(compound, 0), 0x4), compound);
}

std::string SbText(char* body);

int32_t SbFindByName(char* self, char* name)
{
    if (g_sbReplay) {
        g_sbNamesOurs.push_back(SbText(name));
        return static_cast<int32_t>(
            SbReplayed({addr::SC4EffectsManager_FindDescriptionByName, reinterpret_cast<uint32_t>(name), 0, 0}));
    }
    return CallThis<int32_t>(addr::SC4EffectsManager_FindDescriptionByName, self, name);
}

// The name given to the new compound effect (0x00408C40, shared, the
// game's). In the replay it goes to the game's compound effect, whose name
// the comparison has emptied; not to the stand-in when the game made none.
void SbAssignName(char* compound, char* name)
{
    if (g_sbReplay && compound == g_mkDummy)
        return;
    CallThis<char*>(addr::cSC4Advice_F408C40, compound + 0xC, name);
}

void SbRestart(char* self)
{
    if (g_sbReplay)
        SbReplayed({addr::SC4EffectsManager_RestartInUse, 0, 0, 0});
    else
        CallThis<void>(addr::SC4EffectsManager_RestartInUse, self);
}

#include "core/effects_starts.h"

// The game's calls the starts' comparison notes.
uint32_t __fastcall SbHoldStub(char* object, void*)
{
    const uint32_t real = BRealSlot(object, 0x4);
    if (!SbNoting())
        return CallThis<uint32_t>(real, object);
    return SbNoted({0x4, reinterpret_cast<uint32_t>(object), 0, 0}, [&] { return CallThis<uint32_t>(real, object); });
}

uint32_t SbMakerNoted(uint32_t target, char* self)
{
    char* const made =
        reinterpret_cast<char*>(SbNoted({target, 0, 0, 0}, [&] { return MakerRun(target, self); }));
    if (made && target == addr::SC4EffectsManager_NewCompound) {
        g_sbMade = made;
        std::memcpy(g_sbMadeFields, made + 0x18, sizeof g_sbMadeFields);
        if (g_bReal.find(made) == g_bReal.end()) {
            BSwapRole(made, kSbRole, {{0x4, reinterpret_cast<uint32_t>(&SbHoldStub)}});
            g_sbSwapped.push_back(made);
        }
    }
    return reinterpret_cast<uint32_t>(made);
}

uint32_t SbFindByNameNoted(char* self, uint32_t name)
{
    g_sbNamesGame.push_back(SbText(reinterpret_cast<char*>(name)));
    return SbNoted({addr::SC4EffectsManager_FindDescriptionByName, name, 0, 0}, [&] { return FindRun(self, name); });
}

uint32_t SbCompoundDescriptionNoted(char* compound, uint32_t description)
{
    return SbNoted({addr::SC4CompoundEffect_SetDescription, reinterpret_cast<uint32_t>(compound), description, 0},
                   [&] { return CompoundDescriptionRun(compound, description); });
}

uint32_t __fastcall FindNodeNoted(char* map, void*, uint32_t key)
{
    if (!SbNoting())
        return g_SC4IdMap_FindNode(map, nullptr, key);
    return SbNoted({addr::SC4IdMap_FindNode, M<uint32_t>(key), M<uint32_t>(key + 4), 0},
                   [&] { return g_SC4IdMap_FindNode(map, nullptr, key); });
}
OPENSC4_THUNK(TSC4IdMap_FindNode, FindNodeNoted, 4)

// The look-up by IDs and its equal range, by the look-up's mode.
uint32_t FindByIdRun(char* self, uint32_t group, uint32_t instance)
{
    if (g_lookupMode == StepMode::Ours)
        return static_cast<uint32_t>(FindDescriptionById(self, group, instance));
    const uint32_t result = g_SC4EffectsManager_FindDescriptionById(self, nullptr, group, instance);
    if (g_lookupMode == StepMode::Original)
        return result;
    const uint32_t mine = static_cast<uint32_t>(FindDescriptionById(self, group, instance));
    char theirs[16], own[16];
    std::snprintf(theirs, sizeof theirs, "%d", static_cast<int32_t>(result));
    std::snprintf(own, sizeof own, "%d", static_cast<int32_t>(mine));
    compare::Record("effects.lookup.id", mine == result, mine == result ? std::string() : std::string(theirs),
                    mine == result ? std::string() : std::string(own));
    ++g_sbIdLookups;
    return result;
}

uint32_t __fastcall FindByIdByMode(char* self, void*, uint32_t group, uint32_t instance)
{
    if (!SbNoting())
        return FindByIdRun(self, group, instance);
    return SbNoted({addr::SC4EffectsManager_FindDescriptionById, group, instance, 0},
                   [&] { return FindByIdRun(self, group, instance); });
}
OPENSC4_THUNK(TSC4EffectsManager_FindDescriptionById, FindByIdByMode, 8)

uint32_t __fastcall IdEqualRangeByMode(char* map, void*, uint32_t out, uint32_t key)
{
    if (g_lookupMode == StepMode::Ours)
        return reinterpret_cast<uint32_t>(
            IdEqualRange(map, reinterpret_cast<char*>(out), reinterpret_cast<const uint32_t*>(key)));
    const uint32_t result = g_SC4IdMap_EqualRange(map, nullptr, out, key);
    if (g_lookupMode == StepMode::Original)
        return result;
    char mine[0x10];
    const bool same = IdEqualRange(map, mine, reinterpret_cast<const uint32_t*>(key)) == mine && result == out &&
                      std::memcmp(mine, reinterpret_cast<char*>(out), sizeof mine) == 0;
    compare::Record("effects.lookup.id_range", same, same ? std::string() : Hex(reinterpret_cast<char*>(out), 0x10),
                    same ? std::string() : Hex(mine, 0x10));
    return result;
}
OPENSC4_THUNK(TSC4IdMap_EqualRange, IdEqualRangeByMode, 8)

// The look-up by IDs has one detour, which renderer.effects.lookup and the
// starts' comparison share; its equal range is the look-up's alone.
bool HookFindById()
{
    return g_SC4EffectsManager_FindDescriptionById ||
           Detour(addr::SC4EffectsManager_FindDescriptionById, &TSC4EffectsManager_FindDescriptionById,
                  &g_SC4EffectsManager_FindDescriptionById);
}

bool InstallIdLookup()
{
    return HookFindById() && Detour(addr::SC4IdMap_EqualRange, &TSC4IdMap_EqualRange, &g_SC4IdMap_EqualRange);
}

std::string SbNotesText(const std::vector<SbNote>& notes, const std::vector<SbNote>& other)
{
    std::size_t at = 0;
    while (at < notes.size() && at < other.size() && notes[at] == other[at])
        ++at;
    char text[96];
    if (at >= notes.size())
        std::snprintf(text, sizeof text, "%zu calls", notes.size());
    else
        std::snprintf(text, sizeof text, "call %zu of %zu: %08X (%08X, %08X)", at, notes.size(), notes[at].target,
                      notes[at].a, notes[at].b);
    return text;
}

// The noting of a game's start, around `game`.
template <typename Game>
uint32_t SbNoteGame(Game&& game)
{
    g_sbGame.clear();
    g_sbSwapped.clear();
    g_sbMade = nullptr;
    g_sbThread = GetCurrentThreadId();
    g_sbDepth = 0;
    g_sbNoting = true;
    const uint32_t result = game();
    g_sbNoting = false;
    for (char* const object : g_sbSwapped)
        BGiveBack(object, true);
    g_sbSwapped.clear();
    return result;
}

void SbLog()
{
    const uint64_t all = g_sbStarts + g_sbRestarts + g_sbNames + g_sbZooms;
    if ((all & (all - 1)) == 0)
        log::Write("water effects: effects.starts: %llu starts by IDs, %llu restarts, %llu starts by name and %llu "
                   "changes of zoom compared so far, %llu look-ups by IDs",
                   static_cast<unsigned long long>(g_sbStarts), static_cast<unsigned long long>(g_sbRestarts),
                   static_cast<unsigned long long>(g_sbNames), static_cast<unsigned long long>(g_sbZooms),
                   static_cast<unsigned long long>(g_sbIdLookups));
}

uint32_t StartByIdRun(char* self, uint32_t group, uint32_t instance, uint32_t outArg)
{
    char** const out = reinterpret_cast<char**>(outArg);
    if (g_sbMode == StepMode::Ours)
        return StartCompoundById(self, group, instance, out) ? 1 : 0;
    const auto game = [&] { return g_SC4EffectsManager_StartById(self, nullptr, group, instance, outArg); };
    if (g_sbMode == StepMode::Original || g_sbNoting || g_sbReplay)
        return game();
    char* const outBefore = out ? *out : nullptr;
    const uint32_t result = SbNoteGame(game);
    char* const made = g_sbMade;
    char* const outAfter = out ? *out : nullptr;
    char madeAfter[0xC];
    if (made)
        std::memcpy(madeAfter, made + 0x18, sizeof madeAfter);
    if (out)
        *out = outBefore;
    g_sbOurs.clear();
    g_sbReplay = true;
    const uint32_t mine = StartCompoundById(self, group, instance, out) ? 1 : 0;
    g_sbReplay = false;
    char* const outMine = out ? *out : nullptr;
    char madeMine[0xC];
    if (made) {
        std::memcpy(madeMine, made + 0x18, sizeof madeMine);
        std::memcpy(made + 0x18, madeAfter, sizeof madeAfter);
    }
    if (out)
        *out = outAfter;
    ++g_sbStarts;
    SbLog();
    const char* const id = "effects.starts.by_id";
    char text[64];
    if (!(g_sbGame == g_sbOurs)) {
        compare::Record(id, false, SbNotesText(g_sbGame, g_sbOurs), SbNotesText(g_sbOurs, g_sbGame));
    } else if ((result & 0xFF) != mine || outAfter != outMine ||
               (made && std::memcmp(madeAfter, madeMine, sizeof madeAfter) != 0)) {
        std::snprintf(text, sizeof text, "%u, %08X, %s", result & 0xFF, reinterpret_cast<uint32_t>(outAfter),
                      made ? Hex(madeAfter, 0xC).c_str() : "");
        const std::string theirs = text;
        std::snprintf(text, sizeof text, "%u, %08X, %s", mine, reinterpret_cast<uint32_t>(outMine),
                      made ? Hex(madeMine, 0xC).c_str() : "");
        compare::Record(id, false, theirs, text);
    } else {
        compare::Record(id, true, std::string(), std::string());
    }
    return result;
}

uint32_t __fastcall StartByIdByMode(char* self, void*, uint32_t group, uint32_t instance, uint32_t out)
{
    return StartByIdRun(self, group, instance, out);
}
OPENSC4_THUNK(TSC4EffectsManager_StartById, StartByIdByMode, 12)

uint32_t __fastcall RestartInUseByMode(char* self, void*);

// The restart under the change of zoom's noting is noted there and still
// compared on its own: the change of zoom's notes are set aside meanwhile.
uint32_t SbRestartUnderNoting(char* self)
{
    const int depth = g_sbDepth;
    const DWORD thread = g_sbThread;
    char* const made = g_sbMade;
    char fields[sizeof g_sbMadeFields];
    std::memcpy(fields, g_sbMadeFields, sizeof fields);
    std::vector<SbNote> game, ours;
    std::vector<char*> swapped;
    game.swap(g_sbGame);
    ours.swap(g_sbOurs);
    swapped.swap(g_sbSwapped);
    g_sbNoting = false;
    g_sbDepth = 0;
    const uint32_t result = RestartInUseByMode(self, nullptr);
    g_sbNoting = true;
    g_sbDepth = depth;
    g_sbThread = thread;
    g_sbMade = made;
    std::memcpy(g_sbMadeFields, fields, sizeof fields);
    game.swap(g_sbGame);
    ours.swap(g_sbOurs);
    swapped.swap(g_sbSwapped);
    return result;
}

uint32_t RestartInUseRun(char* self);
uint32_t TlNoted0(uint32_t target, char* self, uint32_t (*run)(char*));

uint32_t __fastcall RestartInUseByMode(char* self, void*)
{
    if (TlStNoting())
        return TlNoted0(addr::SC4EffectsManager_RestartInUse, self, &RestartInUseRun);
    return RestartInUseRun(self);
}

uint32_t RestartInUseRun(char* self)
{
    if (SbNoting())
        return SbNotedStart(addr::SC4EffectsManager_RestartInUse, 0, 0, [&] { return SbRestartUnderNoting(self); });
    if (g_sbMode == StepMode::Ours) {
        RestartCompoundsInUse(self);
        return 0;
    }
    const auto game = [&] { return g_SC4EffectsManager_RestartInUse(self, nullptr); };
    if (g_sbMode == StepMode::Original || g_sbNoting || g_sbReplay)
        return game();
    // The indices in use as they were: the game's queued start at its end
    // adds to them, so ours goes through a copy.
    const std::vector<uint32_t> inUse(At<uint32_t*>(self, 0xBAC), At<uint32_t*>(self, 0xBB0));
    const uint32_t result = SbNoteGame(game);
    char header[0xC];
    std::memcpy(header, self + 0xBAC, sizeof header);
    std::vector<uint32_t> copy = inUse;
    At<uint32_t*>(self, 0xBAC) = copy.data();
    At<uint32_t*>(self, 0xBB0) = copy.data() + copy.size();
    At<uint32_t*>(self, 0xBB4) = copy.data() + copy.size();
    g_sbOurs.clear();
    g_sbReplay = true;
    RestartCompoundsInUse(self);
    g_sbReplay = false;
    std::memcpy(self + 0xBAC, header, sizeof header);
    ++g_sbRestarts;
    SbLog();
    const bool same = g_sbGame == g_sbOurs;
    compare::Record("effects.starts.restart", same, same ? std::string() : SbNotesText(g_sbGame, g_sbOurs),
                    same ? std::string() : SbNotesText(g_sbOurs, g_sbGame));
    return result;
}
OPENSC4_THUNK(TSC4EffectsManager_RestartInUse, RestartInUseByMode, 0)

// The characters of a string body (begin, end, capacity), and a string
// body given characters (SC4StringBody::AssignChars, the game's).
std::string SbText(char* body)
{
    return std::string(At<char*>(body, 0), At<char*>(body, 4));
}

void SbSetText(char* body, const std::string& text)
{
    CallThis<char*>(addr::SC4StringBody_AssignChars, body, text.data(), text.data() + text.size());
}

// The start by name is compared as the start by IDs is, and also by the
// lowered name each look-up sees and the name the new compound effect is
// given (+0xC). The manager's string the name is lowered in (+0x1A0) and
// that name are emptied before ours is replayed, and the game's put back
// after. The string itself is not compared after the run: a start by name
// under the game's queued start at the end uses it too, and in the replay
// that start is only noted.
uint32_t StartByNameRun(char* self, uint32_t name, uint32_t outArg)
{
    char** const out = reinterpret_cast<char**>(outArg);
    const char* const text = reinterpret_cast<const char*>(name);
    if (g_sbMode == StepMode::Ours)
        return StartCompoundByName(self, text, out) ? 1 : 0;
    const auto game = [&] { return g_SC4EffectsManager_StartByName(self, nullptr, name, outArg); };
    // A name in the manager's own string would be emptied with it.
    char* const lowered = self + 0x1A0;
    const bool inside = text >= At<char*>(lowered, 0) && text < At<char*>(lowered, 8);
    if (g_sbMode == StepMode::Original || g_sbNoting || g_sbReplay || inside)
        return game();
    char* const outBefore = out ? *out : nullptr;
    g_sbNamesGame.clear();
    g_sbNamesOurs.clear();
    const uint32_t result = SbNoteGame(game);
    char* const made = g_sbMade;
    char* const outAfter = out ? *out : nullptr;
    char madeAfter[0xC];
    std::string nameAfter;
    if (made) {
        std::memcpy(madeAfter, made + 0x18, sizeof madeAfter);
        nameAfter = SbText(made + 0xC);
    }
    const std::string loweredAfter = SbText(lowered);
    if (out)
        *out = outBefore;
    SbSetText(lowered, std::string());
    if (made)
        SbSetText(made + 0xC, std::string());
    g_sbOurs.clear();
    g_sbReplay = true;
    const uint32_t mine = StartCompoundByName(self, text, out) ? 1 : 0;
    g_sbReplay = false;
    char* const outMine = out ? *out : nullptr;
    char madeMine[0xC];
    std::string nameMine;
    if (made) {
        std::memcpy(madeMine, made + 0x18, sizeof madeMine);
        nameMine = SbText(made + 0xC);
        std::memcpy(made + 0x18, madeAfter, sizeof madeAfter);
        SbSetText(made + 0xC, nameAfter);
    }
    SbSetText(lowered, loweredAfter);
    if (out)
        *out = outAfter;
    ++g_sbNames;
    SbLog();
    const char* const id = "effects.starts.by_name";
    if (!(g_sbGame == g_sbOurs)) {
        compare::Record(id, false, SbNotesText(g_sbGame, g_sbOurs), SbNotesText(g_sbOurs, g_sbGame));
    } else if ((result & 0xFF) != mine || outAfter != outMine ||
               (made && (std::memcmp(madeAfter, madeMine, sizeof madeAfter) != 0 || nameAfter != nameMine)) ||
               g_sbNamesGame != g_sbNamesOurs) {
        const auto looked = [](const std::vector<std::string>& names) {
            return names.empty() ? std::string("-") : names.front();
        };
        char theirs[64], own[64];
        std::snprintf(theirs, sizeof theirs, "%u, %08X, %s, ", result & 0xFF, reinterpret_cast<uint32_t>(outAfter),
                      made ? Hex(madeAfter, 0xC).c_str() : "");
        std::snprintf(own, sizeof own, "%u, %08X, %s, ", mine, reinterpret_cast<uint32_t>(outMine),
                      made ? Hex(madeMine, 0xC).c_str() : "");
        compare::Record(id, false, theirs + looked(g_sbNamesGame) + ", " + nameAfter,
                        own + looked(g_sbNamesOurs) + ", " + nameMine);
    } else {
        compare::Record(id, true, std::string(), std::string());
    }
    return result;
}

uint32_t __fastcall StartByNameByMode(char* self, void*, uint32_t name, uint32_t out)
{
    return StartByNameRun(self, name, out);
}
OPENSC4_THUNK(TSC4EffectsManager_StartByName, StartByNameByMode, 8)

// The change of zoom writes five words and calls the restart: the game's
// runs with the restart noted (the restart itself runs by its own mode,
// compared on its own), then ours is replayed on the words put back, and
// the words and the calls are compared.
uint32_t __fastcall ZoomChangedByMode(char* self, void*, uint32_t zoom)
{
    if (g_sbMode == StepMode::Ours) {
        ZoomChanged(self, static_cast<int32_t>(zoom));
        return 0;
    }
    const auto game = [&] { return g_SC4EffectsManager_OnZoomChanged(self, nullptr, zoom); };
    if (g_sbMode == StepMode::Original || g_sbNoting || g_sbReplay)
        return game();
    char before[0x14], after[0x14], mine[0x14];
    const auto save = [&](char* to) {
        std::memcpy(to, self + 0xC80, 0xC);
        std::memcpy(to + 0xC, self + 0xCD0, 8);
    };
    const auto load = [&](const char* from) {
        std::memcpy(self + 0xC80, from, 0xC);
        std::memcpy(self + 0xCD0, from + 0xC, 8);
    };
    save(before);
    const uint32_t result = SbNoteGame(game);
    save(after);
    load(before);
    g_sbOurs.clear();
    g_sbReplay = true;
    ZoomChanged(self, static_cast<int32_t>(zoom));
    g_sbReplay = false;
    save(mine);
    load(after);
    ++g_sbZooms;
    SbLog();
    const char* const id = "effects.starts.zoom";
    if (!(g_sbGame == g_sbOurs))
        compare::Record(id, false, SbNotesText(g_sbGame, g_sbOurs), SbNotesText(g_sbOurs, g_sbGame));
    else if (std::memcmp(after, mine, sizeof after) != 0)
        compare::Record(id, false, Hex(after, sizeof after), Hex(mine, sizeof mine));
    else
        compare::Record(id, true, std::string(), std::string());
    return result;
}
OPENSC4_THUNK(TSC4EffectsManager_OnZoomChanged, ZoomChangedByMode, 4)

bool InstallStarts(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(Detour(addr::SC4EffectsManager_StartById, &TSC4EffectsManager_StartById, &g_SC4EffectsManager_StartById) &&
          (g_SC4EffectsManager_RestartInUse || Detour(addr::SC4EffectsManager_RestartInUse,
                                                      &TSC4EffectsManager_RestartInUse,
                                                      &g_SC4EffectsManager_RestartInUse)) &&
          Detour(addr::SC4EffectsManager_StartByName, &TSC4EffectsManager_StartByName,
                 &g_SC4EffectsManager_StartByName) &&
          Detour(addr::SC4EffectsManager_OnZoomChanged, &TSC4EffectsManager_OnZoomChanged,
                 &g_SC4EffectsManager_OnZoomChanged)))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookFindById() && HookFindDescriptionByName() && HookMaker(addr::SC4EffectsManager_NewCompound) &&
          HookCompoundDescription() &&
          HookStartQueued() && HookStartCompound() &&
          Detour(addr::SC4IdMap_FindNode, &TSC4IdMap_FindNode, &g_SC4IdMap_FindNode)))
        return false;
    g_sbMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the manager's starts by IDs and by name, restart of those in use and change of "
               "zoom %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredStarts = registry::Register({
    "renderer.effects.starts",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectsManager_StartById,
    "The effects manager's starts: a compound effect started by its IDs or by its name (made, given the description "
    "of those IDs or that lowered name, the waiting ones started, handed to the caller held), every compound effect in "
    "use started again, and the change of zoom that does so; compare mode runs the game's with its calls noted and "
    "replays ours (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallStarts,
});

// --- The store of compound descriptions -------------------------------------------------
//
// The objects the effects file reader reads into the store, their copy and
// end, the store's growth, insert and two resizes (effects_store.h).
// They allocate and free, so the comparison is the noted kind, nested: the
// game's runs with each call it makes noted (the allocations in the pool
// allocator, the frees, the game's helpers and these functions; a call to
// one of these is itself compared where it is made), then ours is replayed
// on the memory put back, each call answered with the game's result and
// the bytes the call wrote to its object (a copy's object, a list or the
// store's begin, end and end of storage). The calls and those bytes are
// compared. A pointer into the stack is noted as such, and the object the
// resize is given by its bytes (but +0x24 and +0x28, which the game leaves
// as its stack held them). The three ends read memory that the game's run
// frees, so for them ours is run first with its frees only noted.
constexpr uint32_t kStStack = 0x57AC0000;
StepMode g_stMode = StepMode::Original;
bool g_stNoting = false, g_stReplay = false;
int g_stDepth = 0;
DWORD g_stThread = 0;

struct StNote {
    uint32_t target, a, b, c, d, result;
    std::vector<char> out;
    bool operator==(const StNote& other) const
    {
        return target == other.target && a == other.a && b == other.b && c == other.c && d == other.d;
    }
};
std::vector<StNote> g_stGame, g_stOurs;
std::map<std::string, uint64_t> g_stCounts;

uint32_t StArg(const void* pointer)
{
    const uint32_t value = reinterpret_cast<uint32_t>(pointer);
    const NT_TIB* const tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    return value >= reinterpret_cast<uint32_t>(tib->StackLimit) && value < reinterpret_cast<uint32_t>(tib->StackBase)
               ? kStStack
               : value;
}

// The object the resize is given, by its bytes.
uint32_t StObjectHash(const char* object)
{
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < 0x38; ++i) {
        if (i < 0x24 || i >= 0x2C)
            hash = (hash ^ static_cast<uint8_t>(object[i])) * 16777619u;
    }
    return hash;
}

bool StNoting()
{
    return g_stNoting && g_stDepth == 0 && GetCurrentThreadId() == g_stThread;
}

StNote StMake(uint32_t target, const void* a, const void* b = nullptr, const void* c = nullptr, uint32_t d = 0)
{
    return {target, StArg(a), StArg(b), StArg(c), d, 0, {}};
}

// A call the game's makes, run at one level down and noted with the bytes
// it left at `out`.
template <typename Real>
uint32_t StNoted(StNote note, const char* out, std::size_t size, Real&& real)
{
    ++g_stDepth;
    note.result = real();
    --g_stDepth;
    if (out)
        note.out.assign(out, out + size);
    const uint32_t result = note.result;
    g_stGame.push_back(std::move(note));
    return result;
}

// A call ours makes in the replay: the game's result and bytes when it is
// the call the game made at that place.
uint32_t StReplayed(StNote note, char* out)
{
    const std::size_t at = g_stOurs.size();
    g_stOurs.push_back(note);
    if (at >= g_stGame.size() || !(g_stGame[at] == note))
        return 0;
    const StNote& theirs = g_stGame[at];
    if (out && !theirs.out.empty())
        std::memcpy(out, theirs.out.data(), theirs.out.size());
    return theirs.result;
}

std::string StNotesText(const std::vector<StNote>& notes, const std::vector<StNote>& other)
{
    std::size_t at = 0;
    while (at < notes.size() && at < other.size() && notes[at] == other[at])
        ++at;
    char text[112];
    if (at >= notes.size())
        std::snprintf(text, sizeof text, "%zu calls", notes.size());
    else
        std::snprintf(text, sizeof text, "call %zu of %zu: %08X (%08X, %08X, %08X, %08X)", at, notes.size(),
                      notes[at].target, notes[at].a, notes[at].b, notes[at].c, notes[at].d);
    return text;
}

// One comparison, inside another's noted run or not: the engine's state
// is put aside and back.
struct StSaved {
    bool noting = g_stNoting, replay = g_stReplay;
    int depth = g_stDepth;
    DWORD thread = g_stThread;
    std::vector<StNote> game = std::move(g_stGame), ours = std::move(g_stOurs);
    ~StSaved()
    {
        g_stNoting = noting;
        g_stReplay = replay;
        g_stDepth = depth;
        g_stThread = thread;
        g_stGame = std::move(game);
        g_stOurs = std::move(ours);
    }
};

void StRecord(const char* id, bool same, const std::string& game, const std::string& ours)
{
    const uint64_t n = ++g_stCounts[id];
    if ((n & (n - 1)) == 0)
        log::Write("water effects: %s: %llu compared", id, static_cast<unsigned long long>(n));
    compare::Record(id, same, same ? std::string() : game, same ? std::string() : ours);
}

// `region` is what the function writes besides its calls; `result` says
// whether its EAX means anything.
template <typename Game, typename Ours>
uint32_t StCompared(const char* id, char* region, std::size_t size, bool result, Game&& game, Ours&& ours)
{
    StSaved saved;
    g_stGame.clear();
    g_stOurs.clear();
    const std::vector<char> before(region, region + size);
    g_stThread = GetCurrentThreadId();
    g_stDepth = 0;
    g_stReplay = false;
    g_stNoting = true;
    const uint32_t theirs = game();
    g_stNoting = false;
    const std::vector<char> after(region, region + size);
    if (size)
        std::memcpy(region, before.data(), size);
    g_stReplay = true;
    const uint32_t mine = ours();
    g_stReplay = false;
    const std::vector<char> own(region, region + size);
    if (size)
        std::memcpy(region, after.data(), size);
    if (!(g_stGame == g_stOurs))
        StRecord(id, false, StNotesText(g_stGame, g_stOurs), StNotesText(g_stOurs, g_stGame));
    else
        StRecord(id, own == after && (!result || theirs == mine),
                 Hex(after.data(), size) + (result ? " " + Hex(&theirs, 4) : std::string()),
                 Hex(own.data(), size) + (result ? " " + Hex(&mine, 4) : std::string()));
    return theirs;
}

// The ends: ours first, its frees only noted, then the game's.
template <typename Game, typename Ours>
uint32_t StEndCompared(const char* id, Game&& game, Ours&& ours)
{
    StSaved saved;
    g_stGame.clear();
    g_stOurs.clear();
    g_stThread = GetCurrentThreadId();
    g_stDepth = 0;
    g_stNoting = false;
    g_stReplay = true;
    ours();
    g_stReplay = false;
    g_stNoting = true;
    const uint32_t theirs = game();
    g_stNoting = false;
    StRecord(id, g_stGame == g_stOurs, StNotesText(g_stGame, g_stOurs), StNotesText(g_stOurs, g_stGame));
    return theirs;
}

// What ours calls: in the replay the note, else ours or the game's.
char* CopyCompoundRecords(char* to, char* from);
char* CopyCompoundRecordsB(char* to, char* from);
void DestroyCompoundRecordsB(char* list);
void DestroyCompoundRecords(char* list);
char* CopyCompound(char* to, char* from);
char* CopyCompoundRange(char* first, char* last, char* place);
void DestroyCompoundRange(char* first, char* last);
void GrowInsertCompounds(char* store, char* at, char* object, uint32_t count, bool atEnd);
void InsertCompounds(char* store, char* at, uint32_t count, char* object);
void ResizeCompounds(char* store, uint32_t count, char* object);

char* StAllocate(uint32_t size)
{
    if (g_stReplay)
        return reinterpret_cast<char*>(StReplayed({addr::SC4_PoolAllocate, size, 0, 0, 0, 0, {}}, nullptr));
    return StlAllocate(size);
}

void StFree(char* storage)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4_Free, storage), nullptr);
    else
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(storage);
}

// The empty string's end byte; in the replay the storage is the game's,
// which its run has freed again.
void StClearText(char* text)
{
    if (!g_stReplay)
        *text = 0;
}

void StCopyRecord6C(char* to, char* from)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4EffectCompound_CopyConstructRecord, to, from), nullptr);
    else
        CallThis<void*>(addr::SC4EffectCompound_CopyConstructRecord, to, from);
}

char* StCopyRecords18(char* first, char* last, char* to)
{
    if (g_stReplay)
        return reinterpret_cast<char*>(
            StReplayed(StMake(addr::SC4EffectCompound_CopyRecordsBRange, first, last, to), nullptr));
    char tag = 0;
    using Range = char*(__cdecl*)(char*, char*, char*, char*);
    return reinterpret_cast<Range>(addr::SC4EffectCompound_CopyRecordsBRange)(first, last, to, &tag);
}

void StCopyString(char* to, char* first, char* last)
{
    if (g_stReplay) {
        StReplayed(StMake(addr::cSC4Advice_F408420, to, first, last), to);
        return;
    }
    char tag = 0;
    CallThis<void*>(addr::cSC4Advice_F408420, to, first, last, &tag);
}

char* StCopyCompoundRecords(char* to, char* from)
{
    if (g_stReplay)
        return reinterpret_cast<char*>(StReplayed(StMake(addr::SC4EffectCompound_CopyRecords, to, from), to));
    return CopyCompoundRecords(to, from);
}

char* StCopyCompoundRecordsB(char* to, char* from)
{
    if (g_stReplay)
        return reinterpret_cast<char*>(StReplayed(StMake(addr::SC4EffectCompound_CopyRecordsB, to, from), to));
    return CopyCompoundRecordsB(to, from);
}

void StDestroyCompoundRecordsB(char* list)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4EffectCompound_DestroyRecordsB, list), nullptr);
    else
        DestroyCompoundRecordsB(list);
}

void StDestroyCompoundRecords(char* list)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4EffectCompound_DestroyRecords, list), nullptr);
    else
        DestroyCompoundRecords(list);
}

char* StCopyCompound(char* to, char* from)
{
    if (g_stReplay)
        return reinterpret_cast<char*>(StReplayed(StMake(addr::SC4EffectCompound_CopyConstruct, to, from), to));
    return CopyCompound(to, from);
}

char* StCopyCompoundRange(char* first, char* last, char* to)
{
    if (g_stReplay)
        return reinterpret_cast<char*>(
            StReplayed(StMake(addr::SC4EffectCompounds_CopyRange, first, last, to), nullptr));
    return CopyCompoundRange(first, last, to);
}

void StDestroyCompoundRange(char* first, char* last)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4EffectCompounds_DestroyRange, first, last), nullptr);
    else
        DestroyCompoundRange(first, last);
}

void StGrowInsertCompounds(char* store, char* at, char* object, uint32_t count, bool atEnd)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4EffectCompounds_GrowInsert, store, at, object, count | (atEnd ? 0x80000000u : 0)),
                   store);
    else
        GrowInsertCompounds(store, at, object, count, atEnd);
}

void StInsertCompounds(char* store, char* at, uint32_t count, char* object)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4EffectCompounds_Insert, store, at, object, count), store);
    else
        InsertCompounds(store, at, count, object);
}

void StResizeCompounds(char* store, uint32_t count, char* object)
{
    if (g_stReplay)
        StReplayed({addr::SC4EffectCompounds_Resize, StArg(store), count, StArg(object), StObjectHash(object), 0, {}},
                   store);
    else
        ResizeCompounds(store, count, object);
}

using StRange3 = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t);
using StRange5 = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

void StFillNewCompounds(char* to, uint32_t count, char* object)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4EffectCompounds_FillNew, to, reinterpret_cast<void*>(count), object), nullptr);
    else
        reinterpret_cast<StRange3>(addr::SC4EffectCompounds_FillNew)(
            reinterpret_cast<uint32_t>(to), count, reinterpret_cast<uint32_t>(object));
}

void StMoveCompoundsBack(char* first, char* last, char* end)
{
    if (g_stReplay) {
        StReplayed(StMake(addr::SC4EffectCompounds_MoveBack, first, last, end), nullptr);
        return;
    }
    uint32_t tag = 0;
    reinterpret_cast<StRange5>(addr::SC4EffectCompounds_MoveBack)(reinterpret_cast<uint32_t>(first),
                                                                  reinterpret_cast<uint32_t>(last),
                                                                  reinterpret_cast<uint32_t>(end),
                                                                  reinterpret_cast<uint32_t>(&tag), 0);
}

void StAssignCompounds(char* first, char* last, char* object)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4EffectCompounds_Assign, first, last, object), nullptr);
    else
        reinterpret_cast<StRange3>(addr::SC4EffectCompounds_Assign)(
            reinterpret_cast<uint32_t>(first), reinterpret_cast<uint32_t>(last), reinterpret_cast<uint32_t>(object));
}

char* StCopyCompoundsDown(char* first, char* last, char* to)
{
    if (g_stReplay)
        return reinterpret_cast<char*>(
            StReplayed(StMake(addr::SC4EffectCompounds_CopyDown, first, last, to), nullptr));
    uint32_t tag = 0;
    return reinterpret_cast<char*>(reinterpret_cast<StRange5>(addr::SC4EffectCompounds_CopyDown)(
        reinterpret_cast<uint32_t>(first), reinterpret_cast<uint32_t>(last), reinterpret_cast<uint32_t>(to),
        reinterpret_cast<uint32_t>(&tag), 0));
}

#include "core/effects_store.h"

uint32_t StAllocateNoted(char* pool, uint32_t size)
{
    return StNoted({addr::SC4_PoolAllocate, size, 0, 0, 0, 0, {}}, nullptr, 0,
                   [&] { return g_SC4_PoolAllocate(pool, nullptr, size); });
}

// The game's side: each of the eleven by the group's mode (inside a noted
// run noted, and compared where it is called; ours too is noted there,
// when another group compares), the game's helpers, the allocation (in the
// pool allocator, MxAllocateNoted) and the free only noted. The lists of
// held pointers, the vectors and the tables share the engine with modes of
// their own.
template <typename Game, typename Ours, typename Compared>
uint32_t StByModeOf(StepMode mode, StNote note, const char* out, std::size_t size, Game&& game, Ours&& ours,
                    Compared&& compared)
{
    if (mode == StepMode::Ours)
        return StNoting() ? StNoted(std::move(note), out, size, ours) : ours();
    if (mode == StepMode::Original)
        return game();
    if (StNoting())
        return StNoted(std::move(note), out, size, compared);
    if (g_stNoting || g_stReplay)
        return game();
    return compared();
}

template <typename Game, typename Ours, typename Compared>
uint32_t StByMode(StNote note, const char* out, std::size_t size, Game&& game, Ours&& ours, Compared&& compared)
{
    return StByModeOf(g_stMode, std::move(note), out, size, game, ours, compared);
}

template <typename Game>
uint32_t StHelper(StNote note, const char* out, std::size_t size, Game&& game)
{
    if (StNoting())
        return StNoted(std::move(note), out, size, game);
    return game();
}

using StCdecl3 = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t);
using StCdecl4 = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t);
using StCdecl5 = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using StFreeFn = void(__cdecl*)(uint32_t);
Call1Fn g_SC4EffectCompound_CopyRecords = nullptr, g_SC4EffectCompound_CopyRecordsB = nullptr,
        g_SC4EffectCompound_CopyConstruct = nullptr, g_SC4EffectCompounds_ResizeEmpty = nullptr,
        g_SC4EffectCompound_CopyConstructRecord = nullptr;
Call0Fn g_SC4EffectCompound_DestroyRecordsB = nullptr, g_SC4EffectCompound_DestroyRecords = nullptr;
Call2Fn g_SC4EffectCompounds_Resize = nullptr;
Call3Fn g_SC4EffectCompounds_Insert = nullptr, g_cSC4Advice_F408420 = nullptr;
Call5Fn g_SC4EffectCompounds_GrowInsert = nullptr;
StCdecl3 g_SC4EffectCompounds_DestroyRange = nullptr, g_SC4EffectCompounds_FillNew = nullptr,
         g_SC4EffectCompounds_Assign = nullptr;
StCdecl4 g_SC4EffectCompounds_CopyRange = nullptr, g_SC4EffectCompound_CopyRecordsBRange = nullptr;
StCdecl5 g_SC4EffectCompounds_MoveBack = nullptr, g_SC4EffectCompounds_CopyDown = nullptr;
StFreeFn g_SC4_Free = nullptr;

char* Ptr(uint32_t value)
{
    return reinterpret_cast<char*>(value);
}

uint32_t Word(const void* pointer)
{
    return reinterpret_cast<uint32_t>(pointer);
}

uint32_t __fastcall StoreCopyRecordsByMode(char* to, void*, uint32_t from)
{
    const auto game = [&] { return g_SC4EffectCompound_CopyRecords(to, nullptr, from); };
    const auto ours = [&] { return Word(CopyCompoundRecords(to, Ptr(from))); };
    return StByMode(StMake(addr::SC4EffectCompound_CopyRecords, to, Ptr(from)), to, 0xC, game, ours,
                    [&] { return StCompared("effects.store.copy_records", to, 0xC, true, game, ours); });
}
OPENSC4_THUNK(TSC4EffectCompound_CopyRecords, StoreCopyRecordsByMode, 4)

uint32_t __fastcall StoreCopyRecordsBByMode(char* to, void*, uint32_t from)
{
    const auto game = [&] { return g_SC4EffectCompound_CopyRecordsB(to, nullptr, from); };
    const auto ours = [&] { return Word(CopyCompoundRecordsB(to, Ptr(from))); };
    return StByMode(StMake(addr::SC4EffectCompound_CopyRecordsB, to, Ptr(from)), to, 0xC, game, ours,
                    [&] { return StCompared("effects.store.copy_records_b", to, 0xC, true, game, ours); });
}
OPENSC4_THUNK(TSC4EffectCompound_CopyRecordsB, StoreCopyRecordsBByMode, 4)

uint32_t __fastcall StoreDestroyRecordsBByMode(char* list, void*)
{
    const auto game = [&] { return g_SC4EffectCompound_DestroyRecordsB(list, nullptr); };
    const auto ours = [&] {
        DestroyCompoundRecordsB(list);
        return 0u;
    };
    return StByMode(StMake(addr::SC4EffectCompound_DestroyRecordsB, list), nullptr, 0, game, ours,
                    [&] { return StEndCompared("effects.store.destroy_records_b", game, ours); });
}
OPENSC4_THUNK(TSC4EffectCompound_DestroyRecordsB, StoreDestroyRecordsBByMode, 0)

uint32_t __fastcall StoreDestroyRecordsByMode(char* list, void*)
{
    const auto game = [&] { return g_SC4EffectCompound_DestroyRecords(list, nullptr); };
    const auto ours = [&] {
        DestroyCompoundRecords(list);
        return 0u;
    };
    return StByMode(StMake(addr::SC4EffectCompound_DestroyRecords, list), nullptr, 0, game, ours,
                    [&] { return StEndCompared("effects.store.destroy_records", game, ours); });
}
OPENSC4_THUNK(TSC4EffectCompound_DestroyRecords, StoreDestroyRecordsByMode, 0)

uint32_t __fastcall StoreCopyCompoundByMode(char* to, void*, uint32_t from)
{
    const auto game = [&] { return g_SC4EffectCompound_CopyConstruct(to, nullptr, from); };
    const auto ours = [&] { return Word(CopyCompound(to, Ptr(from))); };
    return StByMode(StMake(addr::SC4EffectCompound_CopyConstruct, to, Ptr(from)), to, 0x38, game, ours,
                    [&] { return StCompared("effects.store.copy", to, 0x38, true, game, ours); });
}
OPENSC4_THUNK(TSC4EffectCompound_CopyConstruct, StoreCopyCompoundByMode, 4)

uint32_t __cdecl StoreCopyRangeByMode(uint32_t first, uint32_t last, uint32_t to, uint32_t tag)
{
    const auto game = [&] { return g_SC4EffectCompounds_CopyRange(first, last, to, tag); };
    const auto ours = [&] { return Word(CopyCompoundRange(Ptr(first), Ptr(last), Ptr(to))); };
    return StByMode(StMake(addr::SC4EffectCompounds_CopyRange, Ptr(first), Ptr(last), Ptr(to)), nullptr, 0, game,
                    ours, [&] { return StCompared("effects.store.copy_range", nullptr, 0, true, game, ours); });
}

void __cdecl StoreDestroyRangeByMode(uint32_t first, uint32_t last, uint32_t tag)
{
    const auto game = [&] {
        g_SC4EffectCompounds_DestroyRange(first, last, tag);
        return 0u;
    };
    const auto ours = [&] {
        DestroyCompoundRange(Ptr(first), Ptr(last));
        return 0u;
    };
    StByMode(StMake(addr::SC4EffectCompounds_DestroyRange, Ptr(first), Ptr(last)), nullptr, 0, game, ours,
             [&] { return StEndCompared("effects.store.destroy_range", game, ours); });
}

uint32_t __fastcall StoreGrowByMode(char* store, void*, uint32_t at, uint32_t object, uint32_t tag, uint32_t count,
                               uint32_t atEnd)
{
    const bool end = static_cast<uint8_t>(atEnd) != 0;
    const auto game = [&] { return g_SC4EffectCompounds_GrowInsert(store, nullptr, at, object, tag, count, atEnd); };
    const auto ours = [&] {
        GrowInsertCompounds(store, Ptr(at), Ptr(object), count, end);
        return 0u;
    };
    return StByMode(
        StMake(addr::SC4EffectCompounds_GrowInsert, store, Ptr(at), Ptr(object), count | (end ? 0x80000000u : 0)),
        store, 0xC, game, ours, [&] { return StCompared("effects.store.grow", store, 0xC, false, game, ours); });
}
OPENSC4_THUNK(TSC4EffectCompounds_GrowInsert, StoreGrowByMode, 20)

uint32_t __fastcall StoreInsertByMode(char* store, void*, uint32_t at, uint32_t count, uint32_t object)
{
    const auto game = [&] { return g_SC4EffectCompounds_Insert(store, nullptr, at, count, object); };
    const auto ours = [&] {
        InsertCompounds(store, Ptr(at), count, Ptr(object));
        return 0u;
    };
    return StByMode(StMake(addr::SC4EffectCompounds_Insert, store, Ptr(at), Ptr(object), count), store, 0xC, game,
                    ours, [&] { return StCompared("effects.store.insert", store, 0xC, false, game, ours); });
}
OPENSC4_THUNK(TSC4EffectCompounds_Insert, StoreInsertByMode, 12)

uint32_t __fastcall StoreResizeByMode(char* store, void*, uint32_t count, uint32_t object)
{
    const auto game = [&] { return g_SC4EffectCompounds_Resize(store, nullptr, count, object); };
    const auto ours = [&] {
        ResizeCompounds(store, count, Ptr(object));
        return 0u;
    };
    return StByMode({addr::SC4EffectCompounds_Resize, StArg(store), count, StArg(Ptr(object)),
                     StObjectHash(Ptr(object)), 0, {}},
                    store, 0xC, game, ours,
                    [&] { return StCompared("effects.store.resize", store, 0xC, false, game, ours); });
}
OPENSC4_THUNK(TSC4EffectCompounds_Resize, StoreResizeByMode, 8)

uint32_t __fastcall StoreResizeEmptyByMode(char* store, void*, uint32_t count)
{
    const auto game = [&] { return g_SC4EffectCompounds_ResizeEmpty(store, nullptr, count); };
    const auto ours = [&] {
        ResizeCompoundsEmpty(store, count);
        return 0u;
    };
    return StByMode(StMake(addr::SC4EffectCompounds_ResizeEmpty, store, Ptr(count)), store, 0xC, game, ours,
                    [&] { return StCompared("effects.store.resize_empty", store, 0xC, false, game, ours); });
}
OPENSC4_THUNK(TSC4EffectCompounds_ResizeEmpty, StoreResizeEmptyByMode, 4)

// The record copies and the copy down are the tables' (further down),
// noted by their detours here too.
bool HookCompoundRecordCopies();

// The game's helpers and the free, noted in a noted run.

uint32_t __fastcall CopyStringNoted(char* to, void*, uint32_t first, uint32_t last, uint32_t tag)
{
    return StHelper(StMake(addr::cSC4Advice_F408420, to, Ptr(first), Ptr(last)), to, 0xC,
                    [&] { return g_cSC4Advice_F408420(to, nullptr, first, last, tag); });
}
OPENSC4_THUNK(TcSC4Advice_F408420, CopyStringNoted, 12)

// The string copy has one detour, which the store's and the vectors'
// comparisons share.
bool HookCopyString()
{
    return g_cSC4Advice_F408420 || Detour(addr::cSC4Advice_F408420, &TcSC4Advice_F408420, &g_cSC4Advice_F408420);
}

uint32_t __cdecl FillNewNoted(uint32_t to, uint32_t count, uint32_t object)
{
    return StHelper(StMake(addr::SC4EffectCompounds_FillNew, Ptr(to), Ptr(count), Ptr(object)), nullptr, 0,
                    [&] { return g_SC4EffectCompounds_FillNew(to, count, object); });
}

uint32_t __cdecl MoveBackNoted(uint32_t first, uint32_t last, uint32_t end, uint32_t tag, uint32_t unused)
{
    return StHelper(StMake(addr::SC4EffectCompounds_MoveBack, Ptr(first), Ptr(last), Ptr(end)), nullptr, 0,
                    [&] { return g_SC4EffectCompounds_MoveBack(first, last, end, tag, unused); });
}

uint32_t __cdecl AssignNoted(uint32_t first, uint32_t last, uint32_t object)
{
    return StHelper(StMake(addr::SC4EffectCompounds_Assign, Ptr(first), Ptr(last), Ptr(object)), nullptr, 0,
                    [&] { return g_SC4EffectCompounds_Assign(first, last, object); });
}

// The frees a comparison keeps until it is done (the memory its game's
// run freed is put back and read again), each by the function it goes to.
std::vector<std::pair<uint32_t, uint32_t>>* g_stKept = nullptr;

void __cdecl FreeNoted(uint32_t storage)
{
    // The third class's destructor's own frees, held back (renderer.effects.dtor).
    if (g_dtorFrees && BNoting()) {
        BCallee(addr::SC4_Free, storage, 0, 0, [&] {
            g_dtorFreed.push_back(storage);
            return 0u;
        });
        return;
    }
    if (g_stKept && StNoting()) {
        g_stGame.push_back(StMake(addr::SC4_Free, Ptr(storage)));
        g_stKept->push_back({addr::SC4_Free, storage});
        return;
    }
    StHelper(StMake(addr::SC4_Free, Ptr(storage)), nullptr, 0, [&] {
        g_SC4_Free(storage);
        return 0u;
    });
}

template <typename Fn>
bool DetourCdecl(std::uintptr_t address, Fn replacement, Fn* original)
{
    return Detour(address, reinterpret_cast<void (*)()>(replacement), original);
}

// The free has one detour, which the store's, the lists of held pointers'
// and the vectors' comparisons share with the effect's (HookFrees).
bool HookFree()
{
    return HookFrees();
}

bool InstallStore(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !Detour(addr::SC4EffectCompound_CopyRecords, &TSC4EffectCompound_CopyRecords,
                &g_SC4EffectCompound_CopyRecords) ||
        !Detour(addr::SC4EffectCompound_CopyRecordsB, &TSC4EffectCompound_CopyRecordsB,
                &g_SC4EffectCompound_CopyRecordsB) ||
        !Detour(addr::SC4EffectCompound_DestroyRecordsB, &TSC4EffectCompound_DestroyRecordsB,
                &g_SC4EffectCompound_DestroyRecordsB) ||
        !Detour(addr::SC4EffectCompound_DestroyRecords, &TSC4EffectCompound_DestroyRecords,
                &g_SC4EffectCompound_DestroyRecords) ||
        !Detour(addr::SC4EffectCompound_CopyConstruct, &TSC4EffectCompound_CopyConstruct,
                &g_SC4EffectCompound_CopyConstruct) ||
        !DetourCdecl(addr::SC4EffectCompounds_CopyRange, &StoreCopyRangeByMode, &g_SC4EffectCompounds_CopyRange) ||
        !Detour(addr::SC4EffectCompounds_DestroyRange, reinterpret_cast<void (*)()>(&StoreDestroyRangeByMode),
                &g_SC4EffectCompounds_DestroyRange) ||
        !Detour(addr::SC4EffectCompounds_GrowInsert, &TSC4EffectCompounds_GrowInsert,
                &g_SC4EffectCompounds_GrowInsert) ||
        !Detour(addr::SC4EffectCompounds_Insert, &TSC4EffectCompounds_Insert, &g_SC4EffectCompounds_Insert) ||
        !Detour(addr::SC4EffectCompounds_Resize, &TSC4EffectCompounds_Resize, &g_SC4EffectCompounds_Resize) ||
        !Detour(addr::SC4EffectCompounds_ResizeEmpty, &TSC4EffectCompounds_ResizeEmpty,
                &g_SC4EffectCompounds_ResizeEmpty))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookPoolAllocate() && HookCompoundRecordCopies() && HookCopyString() &&
          DetourCdecl(addr::SC4EffectCompounds_FillNew, &FillNewNoted, &g_SC4EffectCompounds_FillNew) &&
          DetourCdecl(addr::SC4EffectCompounds_MoveBack, &MoveBackNoted, &g_SC4EffectCompounds_MoveBack) &&
          DetourCdecl(addr::SC4EffectCompounds_Assign, &AssignNoted, &g_SC4EffectCompounds_Assign) && HookFree()))
        return false;
    g_stMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the store of compound descriptions %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredStore = registry::Register({
    "renderer.effects.store",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectCompounds_ResizeEmpty,
    "The store of compound descriptions the effects file is read into: an object's copy and end, the two lists in "
    "it, a range copied and ended, the store's growth, insert and resizes (the in-place moves of an insert with "
    "room still the game's); compare mode runs the game's with its calls noted, each of these compared where it "
    "is called, and replays ours (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallStore,
});

// --- The lists of held pointers ------------------------------------------------------------
//
// The vectors of pointers in an effect description, of three kinds by how
// an element is held and let go: a list's copy, fill, assignment, end and
// growth (effects_pointers.h). The comparison is the store's, sharing
// its engine: the game's runs with each call it makes noted, these
// compared where they are called, ours replayed; the ends run ours first.
// A hold and a let-go are noted at the function the game reaches them by:
// the descriptions' two in their own detours, the other kinds' in detours
// on the shared functions their tables' slots hold (0x005BE3E0 and
// 0x005BE3F0 for the counted base at +4, 0x00968B60 and 0x0090D96B for
// the counted ones), which ours notes without reading the object, as the
// game's run may have let go of it last. A fourth kind, the effects
// manager's lists of held effects, is renderer.effects.hashes' (further
// down), by that group's mode.
enum class Held { CountedAt4, Description, Counted, Effect };

struct PtKind {
    uint32_t copy, fill, assign, destroy, grow, hold, letGo, holdSlot, letGoSlot;
    const char *copyId, *fillId, *assignId, *destroyId, *growId;
};

constexpr PtKind kPtKinds[] = {
    {addr::SC4CountedAt4Ptrs_CopyRange, addr::SC4CountedAt4Ptrs_Fill, 0, addr::SC4CountedAt4Ptrs_Destroy,
     addr::SC4CountedAt4Ptrs_GrowInsert, addr::SC4AdviceBase_AddRefThunk, addr::SC4CountedAt4_ReleaseThunk, 0x10,
     0x14, "effects.pointers.copy.counted_at4", "effects.pointers.fill.counted_at4", nullptr,
     "effects.pointers.destroy.counted_at4", "effects.pointers.grow.counted_at4"},
    {addr::SC4EffectDescriptionPtrs_CopyRange, addr::SC4EffectDescriptionPtrs_Fill,
     addr::SC4EffectDescriptionPtrs_Assign, addr::SC4EffectDescriptionPtrs_Destroy,
     addr::SC4EffectDescriptionPtrs_GrowInsert, addr::SC4Effect_Hold, addr::SC4EffectDescription_Release, 0, 0,
     "effects.pointers.copy.descriptions", "effects.pointers.fill.descriptions",
     "effects.pointers.assign.descriptions", "effects.pointers.destroy.descriptions",
     "effects.pointers.grow.descriptions"},
    {addr::SC4CountedPtrs_CopyRange, addr::SC4CountedPtrs_Fill, addr::SC4CountedPtrs_Assign,
     addr::SC4CountedPtrs_Destroy, addr::SC4CountedPtrs_GrowInsert, addr::cRZBaseRefCount_AddRef,
     addr::cRZBaseRefCount_Release, 8, 4, "effects.pointers.copy.counted", "effects.pointers.fill.counted",
     "effects.pointers.assign.counted", "effects.pointers.destroy.counted", "effects.pointers.grow.counted"},
    // The effects manager's lists of held effects (renderer.effects.hashes):
    // slots 0x1C and 0x20, which hand the call to the counted base at +4.
    {addr::SC4HeldEffects_CopyRange, addr::SC4HeldEffects_Fill, addr::SC4HeldEffects_Assign,
     addr::SC4HeldEffects_Destroy, addr::SC4HeldEffects_GrowInsert, addr::SC4EffectAt4_AddRefThunk,
     addr::SC4EffectAt4_ReleaseThunk, 0x1C, 0x20, "effects.hashes.copy.effects", "effects.hashes.fill.effects",
     "effects.hashes.assign.effects", "effects.hashes.destroy.effects", "effects.hashes.grow.effects"},
};

const PtKind& PtKindOf(Held kind)
{
    return kPtKinds[static_cast<int>(kind)];
}

StepMode g_ptMode = StepMode::Original, g_hsMode = StepMode::Original;

// The mode of a kind's group: the held effects' is the hashes'.
StepMode PtModeOf(Held kind)
{
    return kind == Held::Effect ? g_hsMode : g_ptMode;
}

// What ours calls: in the replay the note, else the call or ours.
char* CopyPointers(Held kind, char* first, char* last, char* place);
char* FillPointers(Held kind, char* place, uint32_t count, char* const* value);
void DestroyPointers(Held kind, char* list);

void PtCall(char* object, uint32_t function, uint32_t slot)
{
    if (g_stReplay)
        StReplayed(StMake(function, object), nullptr);
    else if (slot)
        CallThis<uint32_t>(At<uint32_t>(At<char*>(object, 0), slot), object);
    else
        CallThis<uint32_t>(function, object);
}

void PtHold(Held kind, char* object)
{
    PtCall(object, PtKindOf(kind).hold, PtKindOf(kind).holdSlot);
}

void PtLetGo(Held kind, char* object)
{
    PtCall(object, PtKindOf(kind).letGo, PtKindOf(kind).letGoSlot);
}

char* PtCopyPointers(Held kind, char* first, char* last, char* place)
{
    if (g_stReplay)
        return Ptr(StReplayed(StMake(PtKindOf(kind).copy, first, last, place), nullptr));
    return CopyPointers(kind, first, last, place);
}

char* PtFillPointers(Held kind, char* place, uint32_t count, char* const* value)
{
    if (g_stReplay)
        return Ptr(StReplayed(StMake(PtKindOf(kind).fill, place, Ptr(count), value), nullptr));
    return FillPointers(kind, place, count, value);
}

void PtDestroyPointers(Held kind, char* list)
{
    if (g_stReplay)
        StReplayed(StMake(PtKindOf(kind).destroy, list), nullptr);
    else
        DestroyPointers(kind, list);
}

#include "core/effects_pointers.h"

// The game's side: each of the fourteen by the group's mode (inside a noted
// run noted, and compared where it is called). What a copy, fill or
// assignment writes is the range it was given.
StCdecl4 g_ptCopy[4] = {}, g_ptFill[4] = {};
StCdecl3 g_ptAssign[4] = {};
Call0Fn g_ptDestroy[4] = {};
Call5Fn g_ptGrow[4] = {};

std::size_t PtRange(uint32_t place, uint32_t first, uint32_t last)
{
    return place && last > first ? last - first : 0;
}

template <Held kind>
uint32_t __cdecl PtCopyByMode(uint32_t first, uint32_t last, uint32_t place, uint32_t tag)
{
    const PtKind& k = PtKindOf(kind);
    const auto game = [&] { return g_ptCopy[static_cast<int>(kind)](first, last, place, tag); };
    const auto ours = [&] { return Word(CopyPointers(kind, Ptr(first), Ptr(last), Ptr(place))); };
    return StByModeOf(PtModeOf(kind), StMake(k.copy, Ptr(first), Ptr(last), Ptr(place)), nullptr, 0, game, ours, [&] {
        return StCompared(k.copyId, Ptr(place), PtRange(place, first, last), true, game, ours);
    });
}

template <Held kind>
uint32_t __cdecl PtFillByMode(uint32_t place, uint32_t count, uint32_t value, uint32_t tag)
{
    const PtKind& k = PtKindOf(kind);
    const auto game = [&] { return g_ptFill[static_cast<int>(kind)](place, count, value, tag); };
    const auto ours = [&] {
        return Word(FillPointers(kind, Ptr(place), count, reinterpret_cast<char* const*>(value)));
    };
    const std::size_t size = place && count < 0x100000 ? count * 4 : 0;
    return StByModeOf(PtModeOf(kind), StMake(k.fill, Ptr(place), Ptr(count), Ptr(value)), nullptr, 0, game, ours,
                      [&] { return StCompared(k.fillId, Ptr(place), size, true, game, ours); });
}

template <Held kind>
uint32_t __cdecl PtAssignByMode(uint32_t first, uint32_t last, uint32_t to)
{
    const PtKind& k = PtKindOf(kind);
    const auto game = [&] { return g_ptAssign[static_cast<int>(kind)](first, last, to); };
    const auto ours = [&] { return Word(AssignPointers(kind, Ptr(first), Ptr(last), Ptr(to))); };
    return StByModeOf(PtModeOf(kind), StMake(k.assign, Ptr(first), Ptr(last), Ptr(to)), nullptr, 0, game, ours,
                      [&] { return StCompared(k.assignId, Ptr(to), PtRange(to, first, last), true, game, ours); });
}

uint32_t PtDestroyByMode(Held kind, char* list)
{
    const PtKind& k = PtKindOf(kind);
    const auto game = [&] { return g_ptDestroy[static_cast<int>(kind)](list, nullptr); };
    const auto ours = [&] {
        DestroyPointers(kind, list);
        return 0u;
    };
    return StByModeOf(PtModeOf(kind), StMake(k.destroy, list), nullptr, 0, game, ours,
                      [&] { return StEndCompared(k.destroyId, game, ours); });
}

uint32_t PtGrowByMode(Held kind, char* list, uint32_t at, uint32_t value, uint32_t tag, uint32_t count,
                      uint32_t atEnd)
{
    const PtKind& k = PtKindOf(kind);
    const bool end = static_cast<uint8_t>(atEnd) != 0;
    const auto game = [&] { return g_ptGrow[static_cast<int>(kind)](list, nullptr, at, value, tag, count, atEnd); };
    const auto ours = [&] {
        GrowInsertPointers(kind, list, Ptr(at), reinterpret_cast<char* const*>(value), count, end);
        return 0u;
    };
    return StByModeOf(PtModeOf(kind), StMake(k.grow, list, Ptr(at), Ptr(value), count | (end ? 0x80000000u : 0)), list,
                      0xC, game, ours, [&] { return StCompared(k.growId, list, 0xC, false, game, ours); });
}

uint32_t __fastcall PtDestroyCountedAt4(char* list, void*)
{
    return PtDestroyByMode(Held::CountedAt4, list);
}
OPENSC4_THUNK(TPtDestroyCountedAt4, PtDestroyCountedAt4, 0)

uint32_t __fastcall PtDestroyDescriptions(char* list, void*)
{
    return PtDestroyByMode(Held::Description, list);
}
OPENSC4_THUNK(TPtDestroyDescriptions, PtDestroyDescriptions, 0)

uint32_t __fastcall PtDestroyCounted(char* list, void*)
{
    return PtDestroyByMode(Held::Counted, list);
}
OPENSC4_THUNK(TPtDestroyCounted, PtDestroyCounted, 0)

uint32_t __fastcall PtGrowCountedAt4(char* list, void*, uint32_t at, uint32_t value, uint32_t tag, uint32_t count,
                                     uint32_t atEnd)
{
    return PtGrowByMode(Held::CountedAt4, list, at, value, tag, count, atEnd);
}
OPENSC4_THUNK(TPtGrowCountedAt4, PtGrowCountedAt4, 20)

uint32_t __fastcall PtGrowDescriptions(char* list, void*, uint32_t at, uint32_t value, uint32_t tag, uint32_t count,
                                       uint32_t atEnd)
{
    return PtGrowByMode(Held::Description, list, at, value, tag, count, atEnd);
}
OPENSC4_THUNK(TPtGrowDescriptions, PtGrowDescriptions, 20)

uint32_t __fastcall PtGrowCounted(char* list, void*, uint32_t at, uint32_t value, uint32_t tag, uint32_t count,
                                  uint32_t atEnd)
{
    return PtGrowByMode(Held::Counted, list, at, value, tag, count, atEnd);
}
OPENSC4_THUNK(TPtGrowCounted, PtGrowCounted, 20)

uint32_t __fastcall PtDestroyEffects(char* list, void*)
{
    return PtDestroyByMode(Held::Effect, list);
}
OPENSC4_THUNK(TPtDestroyEffects, PtDestroyEffects, 0)

uint32_t __fastcall PtGrowEffects(char* list, void*, uint32_t at, uint32_t value, uint32_t tag, uint32_t count,
                                  uint32_t atEnd)
{
    return PtGrowByMode(Held::Effect, list, at, value, tag, count, atEnd);
}
OPENSC4_THUNK(TPtGrowEffects, PtGrowEffects, 20)

// The holds and let-goes, noted in a noted run.
uint32_t StCallNoted(uint32_t target, char* self, Call0Fn real)
{
    return StHelper(StMake(target, self), nullptr, 0, [&] { return real(self, nullptr); });
}

Call0Fn g_SC4AdviceBase_AddRefThunk = nullptr, g_SC4CountedAt4_ReleaseThunk = nullptr,
        g_cRZBaseRefCount_AddRef = nullptr, g_cRZBaseRefCount_Release = nullptr;

uint32_t __fastcall CountedAt4HoldNoted(char* self, void*)
{
    return StCallNoted(addr::SC4AdviceBase_AddRefThunk, self, g_SC4AdviceBase_AddRefThunk);
}
OPENSC4_THUNK(TCountedAt4HoldNoted, CountedAt4HoldNoted, 0)

uint32_t __fastcall CountedAt4LetGoNoted(char* self, void*)
{
    return StCallNoted(addr::SC4CountedAt4_ReleaseThunk, self, g_SC4CountedAt4_ReleaseThunk);
}
OPENSC4_THUNK(TCountedAt4LetGoNoted, CountedAt4LetGoNoted, 0)

// The let-go of the counted base at +4 has one detour, which the lists of
// held pointers' and the vectors' comparisons share.
bool HookCountedAt4LetGo()
{
    return g_SC4CountedAt4_ReleaseThunk ||
           Detour(addr::SC4CountedAt4_ReleaseThunk, &TCountedAt4LetGoNoted, &g_SC4CountedAt4_ReleaseThunk);
}

// So has its hold, which the lists of held pointers' and the tables'
// comparisons share.
bool HookCountedAt4Hold()
{
    return g_SC4AdviceBase_AddRefThunk ||
           Detour(addr::SC4AdviceBase_AddRefThunk, &TCountedAt4HoldNoted, &g_SC4AdviceBase_AddRefThunk);
}

uint32_t __fastcall CountedHoldNoted(char* self, void*)
{
    return StCallNoted(addr::cRZBaseRefCount_AddRef, self, g_cRZBaseRefCount_AddRef);
}
OPENSC4_THUNK(TCountedHoldNoted, CountedHoldNoted, 0)

uint32_t __fastcall CountedLetGoNoted(char* self, void*)
{
    return StCallNoted(addr::cRZBaseRefCount_Release, self, g_cRZBaseRefCount_Release);
}
OPENSC4_THUNK(TCountedLetGoNoted, CountedLetGoNoted, 0)

bool InstallPointers(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !DetourCdecl(addr::SC4CountedAt4Ptrs_CopyRange, &PtCopyByMode<Held::CountedAt4>, &g_ptCopy[0]) ||
        !DetourCdecl(addr::SC4CountedAt4Ptrs_Fill, &PtFillByMode<Held::CountedAt4>, &g_ptFill[0]) ||
        !Detour(addr::SC4CountedAt4Ptrs_Destroy, &TPtDestroyCountedAt4, &g_ptDestroy[0]) ||
        !Detour(addr::SC4CountedAt4Ptrs_GrowInsert, &TPtGrowCountedAt4, &g_ptGrow[0]) ||
        !DetourCdecl(addr::SC4EffectDescriptionPtrs_CopyRange, &PtCopyByMode<Held::Description>, &g_ptCopy[1]) ||
        !DetourCdecl(addr::SC4EffectDescriptionPtrs_Fill, &PtFillByMode<Held::Description>, &g_ptFill[1]) ||
        !DetourCdecl(addr::SC4EffectDescriptionPtrs_Assign, &PtAssignByMode<Held::Description>, &g_ptAssign[1]) ||
        !Detour(addr::SC4EffectDescriptionPtrs_Destroy, &TPtDestroyDescriptions, &g_ptDestroy[1]) ||
        !Detour(addr::SC4EffectDescriptionPtrs_GrowInsert, &TPtGrowDescriptions, &g_ptGrow[1]) ||
        !DetourCdecl(addr::SC4CountedPtrs_CopyRange, &PtCopyByMode<Held::Counted>, &g_ptCopy[2]) ||
        !DetourCdecl(addr::SC4CountedPtrs_Fill, &PtFillByMode<Held::Counted>, &g_ptFill[2]) ||
        !DetourCdecl(addr::SC4CountedPtrs_Assign, &PtAssignByMode<Held::Counted>, &g_ptAssign[2]) ||
        !Detour(addr::SC4CountedPtrs_Destroy, &TPtDestroyCounted, &g_ptDestroy[2]) ||
        !Detour(addr::SC4CountedPtrs_GrowInsert, &TPtGrowCounted, &g_ptGrow[2]))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookPoolAllocate() && HookFree() &&
          (g_SC4Effect_Hold || Detour(addr::SC4Effect_Hold, &TSC4Effect_Hold, &g_SC4Effect_Hold)) &&
          (g_SC4EffectDescription_Release || Detour(addr::SC4EffectDescription_Release,
                                                    &TSC4EffectDescription_Release,
                                                    &g_SC4EffectDescription_Release)) &&
          HookCountedAt4Hold() && HookCountedAt4LetGo() &&
          Detour(addr::cRZBaseRefCount_AddRef, &TCountedHoldNoted, &g_cRZBaseRefCount_AddRef) &&
          Detour(addr::cRZBaseRefCount_Release, &TCountedLetGoNoted, &g_cRZBaseRefCount_Release)))
        return false;
    g_ptMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the lists of held pointers %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredPointers = registry::Register({
    "renderer.effects.pointers",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDescriptionPtrs_GrowInsert,
    "The effect descriptions' lists of held pointers, three kinds (objects counted at +4, descriptions, counted "
    "objects): a list's copy into raw storage, fill, assignment, end and growth; compare mode runs the game's with "
    "its calls noted (the holds and let-goes at the functions the game reaches them by), each of these compared "
    "where it is called, and replays ours (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallPointers,
});

// --- The lists of records and texts --------------------------------------------------------
//
// The vectors the effects file reader fills: decal descriptions, records of
// 0x28 and 0x1C bytes, texts of 0x14 and 0x10 bytes; their copies, fills,
// copies down, ends, growth, inserts, the decal descriptions' resize, the
// texts' erase, the manager's emptying of its two texts' lists and the end
// of a range of name tables (effects_vectors.h). The comparison is the
// store's, sharing its engine: the game's runs with each call it makes
// noted (the lists' copies and assignments of floats, 12-byte items and
// strings among them), these compared where they are called, ours
// replayed; the ends run ours first.
enum class Ls { Decals, Records28, Records1C, Texts14, Texts10 };

struct LsKindInfo {
    uint32_t size, copy, fill, copyDown, destroy, grow, insert, erase, copyBack, assignFill, fillNew;
    const char *copyId, *fillId, *copyDownId, *destroyId, *growId, *insertId, *eraseId;
};

constexpr LsKindInfo kLsKinds[] = {
    {0x64, addr::SC4DecalDescriptions_CopyRange, 0, 0, addr::SC4DecalDescriptions_DestroyRange,
     addr::SC4DecalDescriptions_GrowInsert, addr::SC4DecalDescriptions_Insert, 0, addr::SC4DecalDescriptions_CopyBack,
     addr::SC4DecalDescriptions_AssignFill, addr::SC4DecalDescriptions_FillNew, "effects.vectors.copy.decals", nullptr,
     nullptr, "effects.vectors.destroy.decals", "effects.vectors.grow.decals", "effects.vectors.insert.decals", nullptr},
    {0x28, addr::SC4EffectRecords28_CopyRange, addr::SC4EffectRecords28_FillNew, addr::SC4EffectRecords28_CopyDown,
     addr::SC4EffectRecords28_DestroyRange, addr::SC4EffectRecords28_GrowInsert, addr::SC4EffectRecords28_Insert, 0,
     addr::SC4EffectRecords28_CopyBack, addr::SC4EffectRecords28_AssignFill, 0, "effects.vectors.copy.records28",
     "effects.vectors.fill.records28", "effects.vectors.copy_down.records28", "effects.vectors.destroy.records28",
     "effects.vectors.grow.records28", "effects.vectors.insert.records28", nullptr},
    {0x1C, addr::SC4EffectRecords1C_CopyRange, addr::SC4EffectRecords1C_FillNew, 0,
     addr::SC4EffectRecords1C_DestroyRange, addr::SC4EffectRecords1C_GrowInsert, addr::SC4EffectRecords1C_Insert, 0,
     addr::SC4EffectRecords1C_CopyBack, addr::SC4EffectRecords1C_AssignFill, 0, "effects.vectors.copy.records1c",
     "effects.vectors.fill.records1c", nullptr, "effects.vectors.destroy.records1c", "effects.vectors.grow.records1c",
     "effects.vectors.insert.records1c", nullptr},
    {0x14, addr::SC4EffectTexts14_CopyRange, addr::SC4EffectTexts14_FillNew, addr::SC4EffectTexts14_CopyDown,
     addr::SC4BitRowVector_Destroy, addr::SC4EffectTexts14_GrowInsert, 0, addr::SC4EffectTexts14_Erase, 0, 0, 0,
     "effects.vectors.copy.texts14", "effects.vectors.fill.texts14", "effects.vectors.copy_down.texts14", nullptr,
     "effects.vectors.grow.texts14", nullptr, "effects.vectors.erase.texts14"},
    {0x10, addr::SC4EffectTexts10_CopyRange, addr::SC4EffectTexts10_FillNew, addr::SC4EffectTexts10_CopyDown,
     addr::SC4EffectTexts10_Destroy, addr::SC4EffectTexts10_GrowInsert, 0, addr::SC4EffectTexts10_Erase, 0, 0, 0,
     "effects.vectors.copy.texts10", "effects.vectors.fill.texts10", "effects.vectors.copy_down.texts10",
     "effects.vectors.destroy.texts10", "effects.vectors.grow.texts10", nullptr, "effects.vectors.erase.texts10"},
};

const LsKindInfo& LsKindOf(Ls kind)
{
    return kLsKinds[static_cast<int>(kind)];
}

StepMode g_lsMode = StepMode::Original;

// What ours calls: in the replay the note, else the game's or ours.
char* CopyDecalDescription(char* to, char* from);
char* CopyElements(Ls kind, char* first, char* last, char* place);
char* FillElements(Ls kind, char* place, uint32_t count, char* value);
char* CopyElementsDown(Ls kind, char* first, char* last, char* to);
void DestroyElements(Ls kind, char* first, char* last);
void DestroyTexts10(char* list);
void GrowInsertElements(Ls kind, char* list, char* at, char* value, uint32_t count, bool atEnd);
void InsertElements(Ls kind, char* list, char* at, uint32_t count, char* value);
char* EraseTexts(Ls kind, char* list, char* first, char* last);

// One of the game's calls ours makes with `this` and a source; `out` gets
// the bytes the game's call wrote there in the replay.
void LsThis(uint32_t target, char* self, char* from, char* out)
{
    if (g_stReplay)
        StReplayed(StMake(target, self, from), out);
    else if (from)
        CallThis<void*>(target, self, from);
    else
        CallThis<void*>(target, self);
}

void LsCopyFloats(char* to, char* from)
{
    LsThis(addr::SC4FloatVector_CopyConstruct, to, from, to);
}

void LsCopyFloat3s(char* to, char* from)
{
    LsThis(addr::SC4Float3Vector_CopyConstruct, to, from, to);
}

void LsAssignFloats(char* to, char* from)
{
    LsThis(addr::SC4FloatVector_Assign, to, from, to);
}

void LsCopyRecord28(char* to, char* from)
{
    LsThis(addr::SC4EffectRecord28_CopyConstruct, to, from, to);
}

void LsDestroyDecal(char* description)
{
    LsThis(addr::SC4DecalDescription_Destroy, description, nullptr, nullptr);
}

void LsDestroyTexts14(char* list)
{
    LsThis(addr::SC4BitRowVector_Destroy, list, nullptr, nullptr);
}

void LsClearTable(char* table)
{
    LsThis(addr::SC4HashTable_Clear, table, nullptr, nullptr);
}

void LsAssignText(char* to, char* first, char* last)
{
    if (g_stReplay) {
        StReplayed(StMake(addr::SC4String_AssignRange, to, first, last), to);
        return;
    }
    char tag = 0;
    CallThis<void*>(addr::SC4String_AssignRange, to, first, last, &tag);
}

// The game's range helpers of three, four and five arguments (the last two
// a tag and a zero).
void LsGame3(uint32_t target, char* first, char* last, char* value)
{
    if (g_stReplay) {
        StReplayed(StMake(target, first, last, value), nullptr);
        return;
    }
    reinterpret_cast<StCdecl3>(target)(Word(first), Word(last), Word(value));
}

void LsGame4(uint32_t target, char* place, uint32_t count, char* value)
{
    if (g_stReplay) {
        StReplayed(StMake(target, place, Ptr(count), value), nullptr);
        return;
    }
    uint32_t tag = 0;
    reinterpret_cast<StCdecl4>(target)(Word(place), count, Word(value), Word(&tag));
}

uint32_t LsGame5(uint32_t target, char* first, char* last, char* to)
{
    if (g_stReplay)
        return StReplayed(StMake(target, first, last, to), nullptr);
    uint32_t tag = 0;
    return reinterpret_cast<StCdecl5>(target)(Word(first), Word(last), Word(to), Word(&tag), 0);
}

char* LsCopyDecal(char* to, char* from)
{
    if (g_stReplay)
        return Ptr(StReplayed(StMake(addr::SC4DecalDescription_CopyConstruct, to, from), to));
    return CopyDecalDescription(to, from);
}

char* LsCopyElements(Ls kind, char* first, char* last, char* place)
{
    if (g_stReplay)
        return Ptr(StReplayed(StMake(LsKindOf(kind).copy, first, last, place), nullptr));
    return CopyElements(kind, first, last, place);
}

char* LsFillElements(Ls kind, char* place, uint32_t count, char* value)
{
    if (g_stReplay)
        return Ptr(StReplayed(StMake(LsKindOf(kind).fill, place, Ptr(count), value), nullptr));
    return FillElements(kind, place, count, value);
}

char* LsCopyElementsDown(Ls kind, char* first, char* last, char* to)
{
    if (g_stReplay)
        return Ptr(StReplayed(StMake(LsKindOf(kind).copyDown, first, last, to), nullptr));
    return CopyElementsDown(kind, first, last, to);
}

void LsDestroyElements(Ls kind, char* first, char* last)
{
    if (g_stReplay)
        StReplayed(StMake(LsKindOf(kind).destroy, first, last), nullptr);
    else
        DestroyElements(kind, first, last);
}

void LsDestroyTexts10(char* list)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4EffectTexts10_Destroy, list), nullptr);
    else
        DestroyTexts10(list);
}

void LsGrowInsertElements(Ls kind, char* list, char* at, char* value, uint32_t count, bool atEnd)
{
    if (g_stReplay)
        StReplayed(StMake(LsKindOf(kind).grow, list, at, value, count | (atEnd ? 0x80000000u : 0)), list);
    else
        GrowInsertElements(kind, list, at, value, count, atEnd);
}

void LsInsertElements(Ls kind, char* list, char* at, uint32_t count, char* value)
{
    if (g_stReplay)
        StReplayed(StMake(LsKindOf(kind).insert, list, at, value, count), list);
    else
        InsertElements(kind, list, at, count, value);
}

char* LsEraseTexts(Ls kind, char* list, char* first, char* last)
{
    if (g_stReplay)
        return Ptr(StReplayed(StMake(LsKindOf(kind).erase, list, first, last), list));
    return EraseTexts(kind, list, first, last);
}

#include "core/effects_vectors.h"

// The game's side: each of the thirty by the group's mode (inside a noted
// run noted, and compared where it is called). What a copy, fill or copy
// down writes is the range it was given; a growth, insert, resize or erase
// the list.
StCdecl4 g_lsCopy[5] = {}, g_lsFill[5] = {};
StCdecl5 g_lsCopyDown[5] = {};
StCdecl3 g_lsDestroy[5] = {};
Call5Fn g_lsGrow[5] = {};
Call3Fn g_lsInsert[5] = {};
Call2Fn g_lsErase[5] = {};
Call1Fn g_SC4DecalDescription_CopyConstruct = nullptr;
Call2Fn g_SC4DecalDescriptions_Resize = nullptr;
Call0Fn g_SC4EffectTexts10_Destroy = nullptr, g_SC4EffectsManager_ClearTexts = nullptr;
using LsCdecl2 = uint32_t(__cdecl*)(uint32_t, uint32_t);
LsCdecl2 g_SC4EffectNameTables_DestroyRange = nullptr;

std::size_t LsRange(uint32_t place, uint32_t bytes)
{
    return place && bytes < 0x100000 ? bytes : 0;
}

uint32_t __fastcall LsCopyDecalByMode(char* to, void*, uint32_t from)
{
    const auto game = [&] { return g_SC4DecalDescription_CopyConstruct(to, nullptr, from); };
    const auto ours = [&] { return Word(CopyDecalDescription(to, Ptr(from))); };
    return StByModeOf(g_lsMode, StMake(addr::SC4DecalDescription_CopyConstruct, to, Ptr(from)), to, 0x64, game, ours,
                      [&] { return StCompared("effects.vectors.copy_construct.decals", to, 0x64, true, game, ours); });
}
OPENSC4_THUNK(TLsCopyDecal, LsCopyDecalByMode, 4)

template <Ls kind>
uint32_t __cdecl LsCopyByMode(uint32_t first, uint32_t last, uint32_t place, uint32_t tag)
{
    const LsKindInfo& k = LsKindOf(kind);
    const auto game = [&] { return g_lsCopy[static_cast<int>(kind)](first, last, place, tag); };
    const auto ours = [&] { return Word(CopyElements(kind, Ptr(first), Ptr(last), Ptr(place))); };
    return StByModeOf(g_lsMode, StMake(k.copy, Ptr(first), Ptr(last), Ptr(place)), nullptr, 0, game, ours, [&] {
        return StCompared(k.copyId, Ptr(place), LsRange(place, last > first ? last - first : 0), true, game, ours);
    });
}

template <Ls kind>
uint32_t __cdecl LsFillByMode(uint32_t place, uint32_t count, uint32_t value, uint32_t tag)
{
    const LsKindInfo& k = LsKindOf(kind);
    const auto game = [&] { return g_lsFill[static_cast<int>(kind)](place, count, value, tag); };
    const auto ours = [&] { return Word(FillElements(kind, Ptr(place), count, Ptr(value))); };
    const std::size_t size = count < 0x10000 ? LsRange(place, count * k.size) : 0;
    return StByModeOf(g_lsMode, StMake(k.fill, Ptr(place), Ptr(count), Ptr(value)), nullptr, 0, game, ours,
                      [&] { return StCompared(k.fillId, Ptr(place), size, true, game, ours); });
}

template <Ls kind>
uint32_t __cdecl LsCopyDownByMode(uint32_t first, uint32_t last, uint32_t to, uint32_t tag, uint32_t unused)
{
    const LsKindInfo& k = LsKindOf(kind);
    const auto game = [&] { return g_lsCopyDown[static_cast<int>(kind)](first, last, to, tag, unused); };
    const auto ours = [&] { return Word(CopyElementsDown(kind, Ptr(first), Ptr(last), Ptr(to))); };
    return StByModeOf(g_lsMode, StMake(k.copyDown, Ptr(first), Ptr(last), Ptr(to)), nullptr, 0, game, ours, [&] {
        return StCompared(k.copyDownId, Ptr(to), LsRange(to, last > first ? last - first : 0), true, game, ours);
    });
}

template <Ls kind>
uint32_t __cdecl LsDestroyByMode(uint32_t first, uint32_t last, uint32_t tag)
{
    const LsKindInfo& k = LsKindOf(kind);
    const auto game = [&] { return g_lsDestroy[static_cast<int>(kind)](first, last, tag); };
    const auto ours = [&] {
        DestroyElements(kind, Ptr(first), Ptr(last));
        return 0u;
    };
    return StByModeOf(g_lsMode, StMake(k.destroy, Ptr(first), Ptr(last)), nullptr, 0, game, ours,
                      [&] { return StEndCompared(k.destroyId, game, ours); });
}

uint32_t LsGrowByMode(Ls kind, char* list, uint32_t at, uint32_t value, uint32_t tag, uint32_t count, uint32_t atEnd)
{
    const LsKindInfo& k = LsKindOf(kind);
    const bool end = static_cast<uint8_t>(atEnd) != 0;
    const auto game = [&] { return g_lsGrow[static_cast<int>(kind)](list, nullptr, at, value, tag, count, atEnd); };
    const auto ours = [&] {
        GrowInsertElements(kind, list, Ptr(at), Ptr(value), count, end);
        return 0u;
    };
    return StByModeOf(g_lsMode, StMake(k.grow, list, Ptr(at), Ptr(value), count | (end ? 0x80000000u : 0)), list,
                      0xC, game, ours, [&] { return StCompared(k.growId, list, 0xC, false, game, ours); });
}

uint32_t LsInsertByMode(Ls kind, char* list, uint32_t at, uint32_t count, uint32_t value)
{
    const LsKindInfo& k = LsKindOf(kind);
    const auto game = [&] { return g_lsInsert[static_cast<int>(kind)](list, nullptr, at, count, value); };
    const auto ours = [&] {
        InsertElements(kind, list, Ptr(at), count, Ptr(value));
        return 0u;
    };
    return StByModeOf(g_lsMode, StMake(k.insert, list, Ptr(at), Ptr(value), count), list, 0xC, game, ours,
                      [&] { return StCompared(k.insertId, list, 0xC, false, game, ours); });
}

uint32_t LsEraseByMode(Ls kind, char* list, uint32_t first, uint32_t last)
{
    const LsKindInfo& k = LsKindOf(kind);
    const auto game = [&] { return g_lsErase[static_cast<int>(kind)](list, nullptr, first, last); };
    const auto ours = [&] { return Word(EraseTexts(kind, list, Ptr(first), Ptr(last))); };
    return StByModeOf(g_lsMode, StMake(k.erase, list, Ptr(first), Ptr(last)), list, 0xC, game, ours,
                      [&] { return StCompared(k.eraseId, list, 0xC, true, game, ours); });
}

uint32_t __fastcall LsGrowDecals(char* list, void*, uint32_t at, uint32_t value, uint32_t tag, uint32_t count,
                                 uint32_t atEnd)
{
    return LsGrowByMode(Ls::Decals, list, at, value, tag, count, atEnd);
}
OPENSC4_THUNK(TLsGrowDecals, LsGrowDecals, 20)

uint32_t __fastcall LsGrowRecords28(char* list, void*, uint32_t at, uint32_t value, uint32_t tag, uint32_t count,
                                    uint32_t atEnd)
{
    return LsGrowByMode(Ls::Records28, list, at, value, tag, count, atEnd);
}
OPENSC4_THUNK(TLsGrowRecords28, LsGrowRecords28, 20)

uint32_t __fastcall LsGrowRecords1C(char* list, void*, uint32_t at, uint32_t value, uint32_t tag, uint32_t count,
                                    uint32_t atEnd)
{
    return LsGrowByMode(Ls::Records1C, list, at, value, tag, count, atEnd);
}
OPENSC4_THUNK(TLsGrowRecords1C, LsGrowRecords1C, 20)

uint32_t __fastcall LsGrowTexts14(char* list, void*, uint32_t at, uint32_t value, uint32_t tag, uint32_t count,
                                  uint32_t atEnd)
{
    return LsGrowByMode(Ls::Texts14, list, at, value, tag, count, atEnd);
}
OPENSC4_THUNK(TLsGrowTexts14, LsGrowTexts14, 20)

uint32_t __fastcall LsGrowTexts10(char* list, void*, uint32_t at, uint32_t value, uint32_t tag, uint32_t count,
                                  uint32_t atEnd)
{
    return LsGrowByMode(Ls::Texts10, list, at, value, tag, count, atEnd);
}
OPENSC4_THUNK(TLsGrowTexts10, LsGrowTexts10, 20)

uint32_t __fastcall LsInsertDecals(char* list, void*, uint32_t at, uint32_t count, uint32_t value)
{
    return LsInsertByMode(Ls::Decals, list, at, count, value);
}
OPENSC4_THUNK(TLsInsertDecals, LsInsertDecals, 12)

uint32_t __fastcall LsInsertRecords28(char* list, void*, uint32_t at, uint32_t count, uint32_t value)
{
    return LsInsertByMode(Ls::Records28, list, at, count, value);
}
OPENSC4_THUNK(TLsInsertRecords28, LsInsertRecords28, 12)

uint32_t __fastcall LsInsertRecords1C(char* list, void*, uint32_t at, uint32_t count, uint32_t value)
{
    return LsInsertByMode(Ls::Records1C, list, at, count, value);
}
OPENSC4_THUNK(TLsInsertRecords1C, LsInsertRecords1C, 12)

uint32_t __fastcall LsEraseTexts14(char* list, void*, uint32_t first, uint32_t last)
{
    return LsEraseByMode(Ls::Texts14, list, first, last);
}
OPENSC4_THUNK(TLsEraseTexts14, LsEraseTexts14, 8)

uint32_t __fastcall LsEraseTexts10(char* list, void*, uint32_t first, uint32_t last)
{
    return LsEraseByMode(Ls::Texts10, list, first, last);
}
OPENSC4_THUNK(TLsEraseTexts10, LsEraseTexts10, 8)

uint32_t __fastcall LsResizeDecalsByMode(char* list, void*, uint32_t count, uint32_t value)
{
    const auto game = [&] { return g_SC4DecalDescriptions_Resize(list, nullptr, count, value); };
    const auto ours = [&] {
        ResizeDecalDescriptions(list, count, Ptr(value));
        return 0u;
    };
    return StByModeOf(g_lsMode, StMake(addr::SC4DecalDescriptions_Resize, list, Ptr(count), Ptr(value)), list, 0xC,
                      game, ours,
                      [&] { return StCompared("effects.vectors.resize.decals", list, 0xC, false, game, ours); });
}
OPENSC4_THUNK(TLsResizeDecals, LsResizeDecalsByMode, 8)

uint32_t __fastcall LsDestroyTexts10ByMode(char* list, void*)
{
    const auto game = [&] { return g_SC4EffectTexts10_Destroy(list, nullptr); };
    const auto ours = [&] {
        DestroyTexts10(list);
        return 0u;
    };
    return StByModeOf(g_lsMode, StMake(addr::SC4EffectTexts10_Destroy, list), nullptr, 0, game, ours,
                      [&] { return StEndCompared("effects.vectors.destroy.texts10", game, ours); });
}
OPENSC4_THUNK(TLsDestroyTexts10, LsDestroyTexts10ByMode, 0)

uint32_t __fastcall LsClearManagerTextsByMode(char* manager, void*)
{
    const auto game = [&] { return g_SC4EffectsManager_ClearTexts(manager, nullptr); };
    const auto ours = [&] {
        ClearManagerTexts(manager);
        return 0u;
    };
    // One detour, shared with renderer.effects.tail: noted whatever the mode.
    if (g_lsMode == StepMode::Original)
        return StHelper(StMake(addr::SC4EffectsManager_ClearTexts, manager), manager + 0xD0, 0x34, game);
    return StByModeOf(g_lsMode, StMake(addr::SC4EffectsManager_ClearTexts, manager), manager + 0xD0, 0x34, game, ours,
                      [&] { return StCompared("effects.vectors.clear_texts", manager + 0xD0, 0x34, false, game, ours); });
}
OPENSC4_THUNK(TLsClearManagerTexts, LsClearManagerTextsByMode, 0)

bool HookClearManagerTexts()
{
    return g_SC4EffectsManager_ClearTexts ||
           Detour(addr::SC4EffectsManager_ClearTexts, &TLsClearManagerTexts, &g_SC4EffectsManager_ClearTexts);
}

uint32_t __cdecl LsDestroyNameTablesByMode(uint32_t first, uint32_t last)
{
    const auto game = [&] { return g_SC4EffectNameTables_DestroyRange(first, last); };
    const auto ours = [&] {
        DestroyNameTables(Ptr(first), Ptr(last));
        return 0u;
    };
    return StByModeOf(g_lsMode, StMake(addr::SC4EffectNameTables_DestroyRange, Ptr(first), Ptr(last)), nullptr, 0,
                      game, ours, [&] { return StEndCompared("effects.vectors.destroy.name_tables", game, ours); });
}

// The game's helpers, noted in a noted run: the copies and assignments of
// a list of floats, of 12-byte items and of a string (the bytes they leave
// at `this` with them), the 0x28-byte record's copy, the ends of a decal
// description, of a 0x14-byte texts' list and of a hash table, and the
// range helpers of the inserts with room.
Call1Fn g_SC4FloatVector_CopyConstruct = nullptr, g_SC4Float3Vector_CopyConstruct = nullptr,
        g_SC4FloatVector_Assign = nullptr, g_SC4EffectRecord28_CopyConstruct = nullptr;
Call0Fn g_SC4DecalDescription_Destroy = nullptr, g_SC4BitRowVector_Destroy = nullptr, g_SC4HashTable_Clear = nullptr;
Call3Fn g_SC4String_AssignRange = nullptr;

template <Call1Fn* real, uint32_t target, std::size_t size>
uint32_t __fastcall LsCopyNoted(char* self, void*, uint32_t from)
{
    return StHelper(StMake(target, self, Ptr(from)), self, size, [&] { return (*real)(self, nullptr, from); });
}
uint32_t __fastcall CopyFloatsNoted(char* self, void* edx, uint32_t from)
{
    return LsCopyNoted<&g_SC4FloatVector_CopyConstruct, addr::SC4FloatVector_CopyConstruct, 0xC>(self, edx, from);
}
OPENSC4_THUNK(TCopyFloatsNoted, CopyFloatsNoted, 4)

uint32_t __fastcall CopyFloat3sNoted(char* self, void* edx, uint32_t from)
{
    return LsCopyNoted<&g_SC4Float3Vector_CopyConstruct, addr::SC4Float3Vector_CopyConstruct, 0xC>(self, edx, from);
}
OPENSC4_THUNK(TCopyFloat3sNoted, CopyFloat3sNoted, 4)

uint32_t __fastcall AssignFloatsNoted(char* self, void* edx, uint32_t from)
{
    return LsCopyNoted<&g_SC4FloatVector_Assign, addr::SC4FloatVector_Assign, 0xC>(self, edx, from);
}
OPENSC4_THUNK(TAssignFloatsNoted, AssignFloatsNoted, 4)

uint32_t __fastcall CopyRecord28Noted(char* self, void* edx, uint32_t from)
{
    return LsCopyNoted<&g_SC4EffectRecord28_CopyConstruct, addr::SC4EffectRecord28_CopyConstruct, 0x28>(self, edx,
                                                                                                         from);
}
OPENSC4_THUNK(TCopyRecord28Noted, CopyRecord28Noted, 4)

// The decal description's end has one detour, shared with
// renderer.effects.decal_parts (noted here whatever its mode).
bool HookDecalDescriptionDestroy();

uint32_t __fastcall DestroyTexts14Noted(char* self, void*)
{
    return StCallNoted(addr::SC4BitRowVector_Destroy, self, g_SC4BitRowVector_Destroy);
}
OPENSC4_THUNK(TDestroyTexts14Noted, DestroyTexts14Noted, 0)

uint32_t __fastcall ClearTableNoted(char* self, void*)
{
    return StCallNoted(addr::SC4HashTable_Clear, self, g_SC4HashTable_Clear);
}
OPENSC4_THUNK(TClearTableNoted, ClearTableNoted, 0)

uint32_t __fastcall AssignTextNoted(char* self, void*, uint32_t first, uint32_t last, uint32_t tag)
{
    return StHelper(StMake(addr::SC4String_AssignRange, self, Ptr(first), Ptr(last)), self, 0xC,
                    [&] { return g_SC4String_AssignRange(self, nullptr, first, last, tag); });
}
OPENSC4_THUNK(TAssignTextNoted, AssignTextNoted, 12)

// These four have one detour each, which the vectors' and the tables'
// comparisons share.
bool HookAssignFloats()
{
    return g_SC4FloatVector_Assign || Detour(addr::SC4FloatVector_Assign, &TAssignFloatsNoted, &g_SC4FloatVector_Assign);
}

bool HookAssignText()
{
    return g_SC4String_AssignRange || Detour(addr::SC4String_AssignRange, &TAssignTextNoted, &g_SC4String_AssignRange);
}

bool HookDestroyTexts14()
{
    return g_SC4BitRowVector_Destroy ||
           Detour(addr::SC4BitRowVector_Destroy, &TDestroyTexts14Noted, &g_SC4BitRowVector_Destroy);
}

bool HookClearTable()
{
    return g_SC4HashTable_Clear || Detour(addr::SC4HashTable_Clear, &TClearTableNoted, &g_SC4HashTable_Clear);
}

// The range helpers, by their place in this table.
constexpr uint32_t kLsRange3[] = {addr::SC4DecalDescriptions_AssignFill, addr::SC4EffectRecords28_AssignFill,
                                  addr::SC4EffectRecords1C_AssignFill};
constexpr uint32_t kLsRange5[] = {addr::SC4DecalDescriptions_CopyDown, addr::SC4DecalDescriptions_CopyBack,
                                  addr::SC4EffectRecords28_CopyBack, addr::SC4EffectRecords1C_CopyBack};
StCdecl3 g_lsRange3[3] = {};
StCdecl4 g_lsFillNewDecals = nullptr;
StCdecl5 g_lsRange5[4] = {};

template <int i>
uint32_t __cdecl LsRange3Noted(uint32_t first, uint32_t last, uint32_t value)
{
    return StHelper(StMake(kLsRange3[i], Ptr(first), Ptr(last), Ptr(value)), nullptr, 0,
                    [&] { return g_lsRange3[i](first, last, value); });
}

uint32_t __cdecl LsFillNewDecalsNoted(uint32_t place, uint32_t count, uint32_t value, uint32_t tag)
{
    return StHelper(StMake(addr::SC4DecalDescriptions_FillNew, Ptr(place), Ptr(count), Ptr(value)), nullptr, 0,
                    [&] { return g_lsFillNewDecals(place, count, value, tag); });
}

template <int i>
uint32_t __cdecl LsRange5Noted(uint32_t first, uint32_t last, uint32_t to, uint32_t tag, uint32_t unused)
{
    return StHelper(StMake(kLsRange5[i], Ptr(first), Ptr(last), Ptr(to)), nullptr, 0,
                    [&] { return g_lsRange5[i](first, last, to, tag, unused); });
}

bool InstallVectors(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !Detour(addr::SC4DecalDescription_CopyConstruct, &TLsCopyDecal, &g_SC4DecalDescription_CopyConstruct) ||
        !DetourCdecl(addr::SC4DecalDescriptions_CopyRange, &LsCopyByMode<Ls::Decals>, &g_lsCopy[0]) ||
        !DetourCdecl(addr::SC4EffectRecords28_CopyRange, &LsCopyByMode<Ls::Records28>, &g_lsCopy[1]) ||
        !DetourCdecl(addr::SC4EffectRecords1C_CopyRange, &LsCopyByMode<Ls::Records1C>, &g_lsCopy[2]) ||
        !DetourCdecl(addr::SC4EffectTexts14_CopyRange, &LsCopyByMode<Ls::Texts14>, &g_lsCopy[3]) ||
        !DetourCdecl(addr::SC4EffectTexts10_CopyRange, &LsCopyByMode<Ls::Texts10>, &g_lsCopy[4]) ||
        !DetourCdecl(addr::SC4EffectRecords28_FillNew, &LsFillByMode<Ls::Records28>, &g_lsFill[1]) ||
        !DetourCdecl(addr::SC4EffectRecords1C_FillNew, &LsFillByMode<Ls::Records1C>, &g_lsFill[2]) ||
        !DetourCdecl(addr::SC4EffectTexts14_FillNew, &LsFillByMode<Ls::Texts14>, &g_lsFill[3]) ||
        !DetourCdecl(addr::SC4EffectTexts10_FillNew, &LsFillByMode<Ls::Texts10>, &g_lsFill[4]) ||
        !DetourCdecl(addr::SC4EffectRecords28_CopyDown, &LsCopyDownByMode<Ls::Records28>, &g_lsCopyDown[1]) ||
        !DetourCdecl(addr::SC4EffectTexts14_CopyDown, &LsCopyDownByMode<Ls::Texts14>, &g_lsCopyDown[3]) ||
        !DetourCdecl(addr::SC4EffectTexts10_CopyDown, &LsCopyDownByMode<Ls::Texts10>, &g_lsCopyDown[4]) ||
        !DetourCdecl(addr::SC4DecalDescriptions_DestroyRange, &LsDestroyByMode<Ls::Decals>, &g_lsDestroy[0]) ||
        !DetourCdecl(addr::SC4EffectRecords28_DestroyRange, &LsDestroyByMode<Ls::Records28>, &g_lsDestroy[1]) ||
        !DetourCdecl(addr::SC4EffectRecords1C_DestroyRange, &LsDestroyByMode<Ls::Records1C>, &g_lsDestroy[2]) ||
        !Detour(addr::SC4EffectTexts10_Destroy, &TLsDestroyTexts10, &g_SC4EffectTexts10_Destroy) ||
        !Detour(addr::SC4DecalDescriptions_GrowInsert, &TLsGrowDecals, &g_lsGrow[0]) ||
        !Detour(addr::SC4EffectRecords28_GrowInsert, &TLsGrowRecords28, &g_lsGrow[1]) ||
        !Detour(addr::SC4EffectRecords1C_GrowInsert, &TLsGrowRecords1C, &g_lsGrow[2]) ||
        !Detour(addr::SC4EffectTexts14_GrowInsert, &TLsGrowTexts14, &g_lsGrow[3]) ||
        !Detour(addr::SC4EffectTexts10_GrowInsert, &TLsGrowTexts10, &g_lsGrow[4]) ||
        !Detour(addr::SC4DecalDescriptions_Insert, &TLsInsertDecals, &g_lsInsert[0]) ||
        !Detour(addr::SC4EffectRecords28_Insert, &TLsInsertRecords28, &g_lsInsert[1]) ||
        !Detour(addr::SC4EffectRecords1C_Insert, &TLsInsertRecords1C, &g_lsInsert[2]) ||
        !Detour(addr::SC4DecalDescriptions_Resize, &TLsResizeDecals, &g_SC4DecalDescriptions_Resize) ||
        !Detour(addr::SC4EffectTexts14_Erase, &TLsEraseTexts14, &g_lsErase[3]) ||
        !Detour(addr::SC4EffectTexts10_Erase, &TLsEraseTexts10, &g_lsErase[4]) ||
        !HookClearManagerTexts() ||
        !DetourCdecl(addr::SC4EffectNameTables_DestroyRange, &LsDestroyNameTablesByMode,
                     &g_SC4EffectNameTables_DestroyRange))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookPoolAllocate() && HookFree() && HookCopyString() && HookCountedAt4LetGo() &&
          Detour(addr::SC4FloatVector_CopyConstruct, &TCopyFloatsNoted, &g_SC4FloatVector_CopyConstruct) &&
          Detour(addr::SC4Float3Vector_CopyConstruct, &TCopyFloat3sNoted, &g_SC4Float3Vector_CopyConstruct) &&
          HookAssignFloats() && HookAssignText() &&
          Detour(addr::SC4EffectRecord28_CopyConstruct, &TCopyRecord28Noted, &g_SC4EffectRecord28_CopyConstruct) &&
          HookDecalDescriptionDestroy() &&
          HookDestroyTexts14() && HookClearTable() &&
          DetourCdecl(kLsRange3[0], &LsRange3Noted<0>, &g_lsRange3[0]) &&
          DetourCdecl(kLsRange3[1], &LsRange3Noted<1>, &g_lsRange3[1]) &&
          DetourCdecl(kLsRange3[2], &LsRange3Noted<2>, &g_lsRange3[2]) &&
          DetourCdecl(addr::SC4DecalDescriptions_FillNew, &LsFillNewDecalsNoted, &g_lsFillNewDecals) &&
          DetourCdecl(kLsRange5[0], &LsRange5Noted<0>, &g_lsRange5[0]) &&
          DetourCdecl(kLsRange5[1], &LsRange5Noted<1>, &g_lsRange5[1]) &&
          DetourCdecl(kLsRange5[2], &LsRange5Noted<2>, &g_lsRange5[2]) &&
          DetourCdecl(kLsRange5[3], &LsRange5Noted<3>, &g_lsRange5[3])))
        return false;
    g_lsMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the lists of records and texts %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredVectors = registry::Register({
    "renderer.effects.vectors",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4DecalDescriptions_Resize,
    "The effect descriptions' lists of decal descriptions, records and texts: an element's copy, a range copied, "
    "filled, copied down and ended, a list's growth, insert (the in-place moves still the game's), the decal "
    "descriptions' resize, the texts' erase, the manager's emptying of its texts and the end of a range of name "
    "tables; compare mode runs the game's with its calls noted, each of these compared where it is called, and "
    "replays ours (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallVectors,
});

// --- The effects tables and the manager's interface ---------------------------------------
//
// The tables the effects file reader fills and the manager holds: their
// constructor, end and take, the list of name tables and its fill, a name
// hash's copy and insert, the zoom distances, a compound description's
// record copies and copy down; and the manager's AddRef, QueryInterface and
// render states (effects_tables.h). The comparison is the store's,
// sharing its engine: the game's runs with each call it makes noted, these
// compared where they are called, ours replayed; the end runs ours first.
// Besides the bytes a function writes in place, a comparison may name
// memory it reads and writes elsewhere (put back before ours runs) and the
// storage the game's run allocated and ours fills (cleared before ours
// runs); both are compared.
StepMode g_tbMode = StepMode::Original;

struct TbSpan {
    char* at;
    std::size_t size;
    bool fill; // storage ours fills, cleared before it runs
};
using TbSpans = std::vector<TbSpan>;

TbSpans TbNone()
{
    return {};
}

// The storage the compared game's run allocated and did not free again.
TbSpans TbMade()
{
    TbSpans made;
    for (std::size_t i = 0; i < g_stGame.size(); ++i) {
        const StNote& note = g_stGame[i];
        if (note.target != addr::SC4_PoolAllocate || !note.result)
            continue;
        const bool freed = std::any_of(g_stGame.begin() + i, g_stGame.end(), [&](const StNote& later) {
            return later.target == addr::SC4_Free && later.a == note.result;
        });
        if (!freed)
            made.push_back({Ptr(note.result), note.a, true});
    }
    return made;
}

// StCompared with more memory: `kept` (before the game's run) names memory
// put back to how it was before ours runs, `after` (after the game's run)
// memory compared as ours leaves it, cleared first where it says so.
template <typename Game, typename Ours, typename Kept, typename After>
uint32_t TbCompared(const char* id, char* region, std::size_t size, bool result, Game&& game, Ours&& ours,
                    Kept&& kept, After&& after)
{
    StSaved saved;
    g_stGame.clear();
    g_stOurs.clear();
    TbSpans spans = kept();
    if (size)
        spans.insert(spans.begin(), {region, size, false});
    const std::size_t keptCount = spans.size();
    std::vector<std::vector<char>> before;
    for (const TbSpan& span : spans)
        before.emplace_back(span.at, span.at + span.size);
    g_stThread = GetCurrentThreadId();
    g_stDepth = 0;
    g_stReplay = false;
    g_stNoting = true;
    const uint32_t theirs = game();
    g_stNoting = false;
    for (const TbSpan& span : after())
        spans.push_back(span);
    std::vector<std::vector<char>> theirBytes, ownBytes;
    for (const TbSpan& span : spans)
        theirBytes.emplace_back(span.at, span.at + span.size);
    for (std::size_t i = 0; i < spans.size(); ++i) {
        if (i < keptCount)
            std::memcpy(spans[i].at, before[i].data(), spans[i].size);
        else if (spans[i].fill)
            std::memset(spans[i].at, 0xCD, spans[i].size);
    }
    g_stReplay = true;
    const uint32_t mine = ours();
    g_stReplay = false;
    for (const TbSpan& span : spans)
        ownBytes.emplace_back(span.at, span.at + span.size);
    for (std::size_t i = spans.size(); i--;)
        std::memcpy(spans[i].at, theirBytes[i].data(), spans[i].size);
    if (!(g_stGame == g_stOurs)) {
        StRecord(id, false, StNotesText(g_stGame, g_stOurs), StNotesText(g_stOurs, g_stGame));
        return theirs;
    }
    for (std::size_t i = 0; i < spans.size(); ++i) {
        if (ownBytes[i] == theirBytes[i])
            continue;
        std::size_t at = 0;
        while (ownBytes[i][at] == theirBytes[i][at])
            ++at;
        at &= ~std::size_t{0xF};
        const std::size_t n = std::min<std::size_t>(0x20, spans[i].size - at);
        char text[48];
        std::snprintf(text, sizeof text, "span %zu of %zu +0x%zX: ", i, spans.size(), at);
        StRecord(id, false, text + Hex(theirBytes[i].data() + at, n), text + Hex(ownBytes[i].data() + at, n));
        return theirs;
    }
    StRecord(id, !result || theirs == mine, Hex(&theirs, 4), Hex(&mine, 4));
    return theirs;
}

template <typename Game, typename Ours>
uint32_t TbCompared(const char* id, char* region, std::size_t size, bool result, Game&& game, Ours&& ours)
{
    return TbCompared(id, region, size, result, game, ours, TbNone, TbNone);
}

// The group's mode, or, while it is off, its detours shared with the
// store only noting the game's.
template <typename Game, typename Ours, typename Compared>
uint32_t TbByMode(StNote note, const char* out, std::size_t size, Game&& game, Ours&& ours, Compared&& compared)
{
    if (g_tbMode == StepMode::Original)
        return StHelper(std::move(note), out, size, game);
    return StByModeOf(g_tbMode, std::move(note), out, size, game, ours, compared);
}

// What ours calls: in the replay the note, else ours or the game's (a
// function of another group by its address, so by that group's mode).
char* CopyCompoundRecord(char* to, char* from);
void CopyNameHash(char* table, char* from);
char* FillNameTables(char* place, uint32_t count, char* value);
char* ConstructNameTables(char* list, uint32_t count);
char* ConstructZoomDistances(char* object);

char* TbCopyCompoundRecord(char* to, char* from)
{
    if (g_stReplay)
        return Ptr(StReplayed(StMake(addr::SC4EffectCompound_CopyConstructRecord, to, from), to));
    return CopyCompoundRecord(to, from);
}

void TbCopyNameHash(char* table, char* from)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4EffectNameHash_Copy, table, from), table);
    else
        CopyNameHash(table, from);
}

char* TbFillNameTables(char* place, uint32_t count, char* value)
{
    if (g_stReplay)
        return Ptr(StReplayed(StMake(addr::SC4EffectNameTables_FillNew, place, Ptr(count), value), nullptr));
    return FillNameTables(place, count, value);
}

void TbConstructNameTables(char* list, uint32_t count)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4EffectNameTables_Construct, list, Ptr(count)), list);
    else
        ConstructNameTables(list, count);
}

void TbConstructZoomDistances(char* object)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4EffectZoomDistances_Construct, object), object);
    else
        ConstructZoomDistances(object);
}

// The game's helpers: the hash's construction, a pointer vector's reserve
// and fill, a float appended, the record lists' assignments, a call of no
// arguments, a range end and a merge.
void TbConstructHash(char* table, uint32_t buckets)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4Uint32HashSet_Construct, table, Ptr(buckets)), table);
    else
        CallThis<void*>(addr::SC4Uint32HashSet_Construct, table, buckets);
}

void TbReserve(char* vector, uint32_t count)
{
    if (g_stReplay)
        StReplayed(StMake(addr::PtrVector_Reserve, vector, Ptr(count)), vector);
    else
        CallThis<void*>(addr::PtrVector_Reserve, vector, count);
}

void TbFillInsert(char* vector, char* at, uint32_t count, const uint32_t* value)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4PtrVector_FillInsert, vector, at, Ptr(count), *value), vector);
    else
        CallThis<void*>(addr::SC4PtrVector_FillInsert, vector, at, count, value);
}

void TbAppendFloat(char* vector, char* at, const float* value)
{
    if (g_stReplay) {
        uint32_t bits = 0;
        std::memcpy(&bits, value, 4);
        StReplayed(StMake(addr::SC4FloatVector_InsertN5, vector, at, nullptr, bits), vector);
        return;
    }
    char tag = 0;
    CallThis<void*>(addr::SC4FloatVector_InsertN5, vector, at, value, &tag, 1u, 1u);
}

void TbAssignCompoundRecords(char* to, char* from)
{
    LsThis(addr::SC4EffectCompoundRecords_Assign, to, from, to);
}

void TbAssignCompoundRecordsB(char* to, char* from)
{
    LsThis(addr::SC4EffectCompoundRecordsB_Assign, to, from, to);
}

void TbCall0(uint32_t target, char* self)
{
    LsThis(target, self, nullptr, nullptr);
}

void TbEndRange(uint32_t target, char* first, char* last)
{
    if (g_stReplay) {
        StReplayed(StMake(target, first, last), nullptr);
        return;
    }
    char tag = 0;
    reinterpret_cast<StCdecl3>(target)(Word(first), Word(last), Word(&tag));
}

uint32_t TbCall1(uint32_t target, char* self, char* other)
{
    if (g_stReplay)
        return StReplayed(StMake(target, self, other), nullptr);
    return CallThis<uint32_t>(target, self, other);
}

// The manager held through its own slot 4, and another object asked for
// an interface through its slot 0.
void TbAddRef(char* manager)
{
    const uint32_t addRef = At<uint32_t>(At<char*>(manager, 0), 4);
    if (g_stReplay)
        StReplayed(StMake(addRef, manager), nullptr);
    else
        CallThis<uint32_t>(addRef, manager);
}

uint32_t TbQueryOther(char* other, uint32_t id, char** out)
{
    const uint32_t query = At<uint32_t>(At<char*>(other, 0), 0);
    if (g_stReplay)
        return StReplayed(StMake(query, other, Ptr(id), out), reinterpret_cast<char*>(out));
    return CallThis<uint32_t>(query, other, id, out) & 0xFF;
}

#include "core/effects_tables.h"

// The game's side: each of the sixteen by the group's mode (inside a noted
// run noted, and compared where it is called).
Call1Fn g_SC4EffectNameHash_Copy = nullptr, g_SC4EffectNameTables_Construct = nullptr,
        g_SC4EffectTables_Take = nullptr;
Call0Fn g_SC4EffectZoomDistances_Construct = nullptr, g_SC4EffectTables_Construct = nullptr,
        g_SC4EffectTables_Destroy = nullptr, g_SC4EffectsManager_AddRef = nullptr,
        g_SC4EffectsManager_SetRenderStates = nullptr;
Call2Fn g_SC4EffectNameHash_InsertEqual = nullptr, g_SC4EffectsManager_QueryInterface = nullptr;
using TbCdecl2 = uint32_t(__cdecl*)(uint32_t, uint32_t);
TbCdecl2 g_SC4EffectCompound_ConstructRecordB = nullptr;
StCdecl3 g_SC4EffectCompound_CopyRecordsRange = nullptr;
StCdecl4 g_SC4EffectNameTables_FillNew = nullptr;

std::size_t TbRange(uint32_t place, uint32_t first, uint32_t last)
{
    return LsRange(place, last > first ? last - first : 0);
}

uint32_t __fastcall TbCopyRecordByMode(char* to, void*, uint32_t from)
{
    const auto game = [&] { return g_SC4EffectCompound_CopyConstructRecord(to, nullptr, from); };
    const auto ours = [&] { return Word(CopyCompoundRecord(to, Ptr(from))); };
    return TbByMode(StMake(addr::SC4EffectCompound_CopyConstructRecord, to, Ptr(from)), to, 0x6C, game, ours,
                    [&] { return TbCompared("effects.tables.copy_record", to, 0x6C, true, game, ours); });
}
OPENSC4_THUNK(TTbCopyRecord, TbCopyRecordByMode, 4)

uint32_t __cdecl TbConstructRecordBByMode(uint32_t place, uint32_t value)
{
    const auto game = [&] { return g_SC4EffectCompound_ConstructRecordB(place, value); };
    const auto ours = [&] { return ConstructCompoundRecordB(Ptr(place), Ptr(value)); };
    return TbByMode(StMake(addr::SC4EffectCompound_ConstructRecordB, Ptr(place), Ptr(value)), Ptr(place),
                    place ? 0x18 : 0, game, ours, [&] {
                        return TbCompared("effects.tables.construct_record_b", Ptr(place), place ? 0x18 : 0, true,
                                          game, ours);
                    });
}

uint32_t __cdecl TbCopyRecordsByMode(uint32_t first, uint32_t last, uint32_t place)
{
    const auto game = [&] { return g_SC4EffectCompound_CopyRecordsRange(first, last, place); };
    const auto ours = [&] { return Word(CopyCompoundRecords(Ptr(first), Ptr(last), Ptr(place))); };
    return TbByMode(StMake(addr::SC4EffectCompound_CopyRecordsRange, Ptr(first), Ptr(last), Ptr(place)), nullptr, 0,
                    game, ours, [&] {
                        return TbCompared("effects.tables.copy_records", Ptr(place), TbRange(place, first, last),
                                          true, game, ours);
                    });
}

uint32_t __cdecl TbCopyRecordsBByMode(uint32_t first, uint32_t last, uint32_t place, uint32_t tag)
{
    const auto game = [&] { return g_SC4EffectCompound_CopyRecordsBRange(first, last, place, tag); };
    const auto ours = [&] { return Word(CopyCompoundRecordsB(Ptr(first), Ptr(last), Ptr(place))); };
    return TbByMode(StMake(addr::SC4EffectCompound_CopyRecordsBRange, Ptr(first), Ptr(last), Ptr(place)), nullptr,
                    0, game, ours, [&] {
                        return TbCompared("effects.tables.copy_records_b", Ptr(place), TbRange(place, first, last),
                                          true, game, ours);
                    });
}

uint32_t __fastcall TbCopyNameHashByMode(char* table, void*, uint32_t from)
{
    const auto game = [&] { return g_SC4EffectNameHash_Copy(table, nullptr, from); };
    const auto ours = [&] {
        CopyNameHash(table, Ptr(from));
        return 0u;
    };
    // The buckets as the game's run leaves them (filled by its reserve and
    // fill, which ours replays), and the nodes and strings it made.
    const auto after = [&] {
        TbSpans spans = TbMade();
        if (char* const buckets = At<char*>(table, 4))
            spans.push_back({buckets, static_cast<std::size_t>(At<char*>(table, 8) - buckets), false});
        return spans;
    };
    return TbByMode(StMake(addr::SC4EffectNameHash_Copy, table, Ptr(from)), table, 0x14, game, ours, [&] {
        return TbCompared("effects.tables.copy_name_hash", table, 0x14, false, game, ours, TbNone, after);
    });
}
OPENSC4_THUNK(TTbCopyNameHash, TbCopyNameHashByMode, 4)

uint32_t __fastcall TbInsertNameByMode(char* table, void*, uint32_t out, uint32_t key)
{
    const auto game = [&] { return g_SC4EffectNameHash_InsertEqual(table, nullptr, out, key); };
    const auto ours = [&] { return Word(InsertNameEqual(table, Ptr(out), Ptr(key))); };
    // The table's count, its buckets and the links of the chain the name
    // goes into; the node made.
    const auto kept = [&] {
        TbSpans spans{{table, 0x14, false}};
        char* const buckets = At<char*>(table, 4);
        spans.push_back({buckets, static_cast<std::size_t>(At<char*>(table, 8) - buckets), false});
        const uint32_t bucket = CallThis<uint32_t>(addr::SC4EffectNameHash_Bucket, table, Ptr(key));
        for (char* at = At<char**>(table, 4)[bucket]; at; at = At<char*>(at, 0))
            spans.push_back({at, 4, false});
        return spans;
    };
    return TbByMode(StMake(addr::SC4EffectNameHash_InsertEqual, table, Ptr(out), Ptr(key)), Ptr(out), 8, game, ours,
                    [&] {
                        return TbCompared("effects.tables.insert_name", Ptr(out), 8, true, game, ours, kept,
                                          TbMade);
                    });
}
OPENSC4_THUNK(TTbInsertName, TbInsertNameByMode, 8)

uint32_t __cdecl TbCopyDownByMode(uint32_t first, uint32_t last, uint32_t to, uint32_t tag, uint32_t unused)
{
    const auto game = [&] { return g_SC4EffectCompounds_CopyDown(first, last, to, tag, unused); };
    const auto ours = [&] { return Word(CopyCompoundsDown(Ptr(first), Ptr(last), Ptr(to))); };
    return TbByMode(StMake(addr::SC4EffectCompounds_CopyDown, Ptr(first), Ptr(last), Ptr(to)), nullptr, 0, game,
                    ours, [&] {
                        return TbCompared("effects.tables.copy_down", Ptr(to), TbRange(to, first, last), true, game,
                                          ours);
                    });
}

uint32_t __cdecl TbFillNameTablesByMode(uint32_t place, uint32_t count, uint32_t value, uint32_t tag)
{
    const auto game = [&] { return g_SC4EffectNameTables_FillNew(place, count, value, tag); };
    const auto ours = [&] { return Word(FillNameTables(Ptr(place), count, Ptr(value))); };
    const std::size_t size = count < 0x10000 ? LsRange(place, count * 0x20) : 0;
    return TbByMode(StMake(addr::SC4EffectNameTables_FillNew, Ptr(place), Ptr(count), Ptr(value)), nullptr, 0, game,
                    ours, [&] {
                        return TbCompared("effects.tables.fill_name_tables", Ptr(place), size, true, game, ours,
                                          TbNone, TbMade);
                    });
}

uint32_t __fastcall TbConstructNameTablesByMode(char* list, void*, uint32_t count)
{
    const auto game = [&] { return g_SC4EffectNameTables_Construct(list, nullptr, count); };
    const auto ours = [&] { return Word(ConstructNameTables(list, count)); };
    return TbByMode(StMake(addr::SC4EffectNameTables_Construct, list, Ptr(count)), list, 0xC, game, ours,
                    [&] { return TbCompared("effects.tables.construct_name_tables", list, 0xC, true, game, ours); });
}
OPENSC4_THUNK(TTbConstructNameTables, TbConstructNameTablesByMode, 4)

uint32_t __fastcall TbConstructZoomByMode(char* object, void*)
{
    const auto game = [&] { return g_SC4EffectZoomDistances_Construct(object, nullptr); };
    const auto ours = [&] { return Word(ConstructZoomDistances(object)); };
    return TbByMode(StMake(addr::SC4EffectZoomDistances_Construct, object), object, 0x1C, game, ours,
                    [&] { return TbCompared("effects.tables.construct_zoom", object, 0x1C, true, game, ours); });
}
OPENSC4_THUNK(TTbConstructZoom, TbConstructZoomByMode, 0)

uint32_t __fastcall TbConstructByMode(char* tables, void*)
{
    const auto game = [&] { return g_SC4EffectTables_Construct(tables, nullptr); };
    const auto ours = [&] { return Word(ConstructTables(tables)); };
    // The string's end byte, in storage the game's run made.
    const auto after = [&] { return TbSpans{{At<char*>(tables, 0x10C), 1, true}}; };
    return TbByMode(StMake(addr::SC4EffectTables_Construct, tables), tables, 0x118, game, ours, [&] {
        return TbCompared("effects.tables.construct", tables, 0x118, true, game, ours, TbNone, after);
    });
}
OPENSC4_THUNK(TTbConstruct, TbConstructByMode, 0)

uint32_t __fastcall TbDestroyByMode(char* tables, void*)
{
    const auto game = [&] { return g_SC4EffectTables_Destroy(tables, nullptr); };
    const auto ours = [&] {
        DestroyTables(tables);
        return 0u;
    };
    return TbByMode(StMake(addr::SC4EffectTables_Destroy, tables), nullptr, 0, game, ours,
                    [&] { return StEndCompared("effects.tables.destroy", game, ours); });
}
OPENSC4_THUNK(TTbDestroy, TbDestroyByMode, 0)

uint32_t __fastcall TbTakeByMode(char* tables, void*, uint32_t other)
{
    const auto game = [&] { return g_SC4EffectTables_Take(tables, nullptr, other); };
    const auto ours = [&] { return TakeTables(tables, Ptr(other)); };
    const auto kept = [&] { return TbSpans{{Ptr(other), 0x118, false}}; };
    return TbByMode(StMake(addr::SC4EffectTables_Take, tables, Ptr(other)), tables, 0x118, game, ours, [&] {
        // The merge changes what the two hold beyond their own bytes.
        if (At<char*>(Ptr(other), 0x18) != At<char*>(Ptr(other), 0x1C)) {
            log::Write("water effects: effects.tables.take: a merge, not compared");
            return game();
        }
        return TbCompared("effects.tables.take", tables, 0x118, false, game, ours, kept, TbNone);
    });
}
OPENSC4_THUNK(TTbTake, TbTakeByMode, 4)

uint32_t __fastcall TbAddRefByMode(char* manager, void*)
{
    const auto game = [&] { return g_SC4EffectsManager_AddRef(manager, nullptr); };
    const auto ours = [&] { return AddRefManager(manager); };
    return TbByMode(StMake(addr::SC4EffectsManager_AddRef, manager), manager + 0x1B0, 4, game, ours, [&] {
        return TbCompared("effects.tables.add_ref", manager + 0x1B0, 4, true, game, ours);
    });
}
OPENSC4_THUNK(TTbAddRef, TbAddRefByMode, 0)

uint32_t __fastcall TbSetRenderStatesByMode(char* manager, void*)
{
    const auto game = [&] { return g_SC4EffectsManager_SetRenderStates(manager, nullptr); };
    const auto ours = [&] {
        SetManagerRenderStates(manager);
        return 0u;
    };
    // The render state's setter writes only the state; ours runs it in the
    // replay too.
    return TbByMode(StMake(addr::SC4EffectsManager_SetRenderStates, manager), manager + 0xD5C, 0x10, game, ours, [&] {
        return TbCompared("effects.tables.render_states", manager + 0xD5C, 0x10, false, game, ours);
    });
}
OPENSC4_THUNK(TTbSetRenderStates, TbSetRenderStatesByMode, 0)

uint32_t __fastcall TbQueryByMode(char* manager, void*, uint32_t id, uint32_t out)
{
    // The answer is AL.
    const auto game = [&] { return g_SC4EffectsManager_QueryInterface(manager, nullptr, id, out) & 0xFF; };
    const auto ours = [&] { return QueryManager(manager, id, reinterpret_cast<char**>(out)); };
    return TbByMode(StMake(addr::SC4EffectsManager_QueryInterface, manager, Ptr(id), Ptr(out)), Ptr(out), 4, game,
                    ours, [&] {
                        // For 0x0A5EAA1A the game jumps to the QueryInterface
                        // of the object at +0xD98: its run is noted as that
                        // call, with what it gives.
                        char* const other = At<char*>(manager, 0xD98);
                        if (id != 0x0A5EAA1A || !other)
                            return TbCompared("effects.tables.query", Ptr(out), 4, true, game, ours);
                        const auto asked = [&] {
                            return StHelper(StMake(At<uint32_t>(At<char*>(other, 0), 0), other, Ptr(id), Ptr(out)),
                                            Ptr(out), 4, game);
                        };
                        return TbCompared("effects.tables.query_other", Ptr(out), 4, true, asked, ours);
                    });
}
OPENSC4_THUNK(TTbQuery, TbQueryByMode, 8)

// The game's helpers, noted in a noted run: the hash's construction (the
// bytes it leaves at `this` with it), a pointer vector's reserve and fill,
// a float appended, the record lists' assignments; and, while their own
// group is off, the other groups' functions the tables end with.
Call1Fn g_SC4Uint32HashSet_Construct = nullptr, g_PtrVector_Reserve = nullptr,
        g_SC4EffectCompoundRecords_Assign = nullptr, g_SC4EffectCompoundRecordsB_Assign = nullptr;
Call3Fn g_SC4PtrVector_FillInsert = nullptr;
Call5Fn g_SC4FloatVector_InsertN5 = nullptr;

uint32_t __fastcall ConstructHashNoted(char* self, void*, uint32_t buckets)
{
    return StHelper(StMake(addr::SC4Uint32HashSet_Construct, self, Ptr(buckets)), self, 0x14,
                    [&] { return g_SC4Uint32HashSet_Construct(self, nullptr, buckets); });
}
OPENSC4_THUNK(TConstructHashNoted, ConstructHashNoted, 4)

uint32_t __fastcall ReserveNoted(char* self, void*, uint32_t count)
{
    return StHelper(StMake(addr::PtrVector_Reserve, self, Ptr(count)), self, 0xC,
                    [&] { return g_PtrVector_Reserve(self, nullptr, count); });
}
OPENSC4_THUNK(TReserveNoted, ReserveNoted, 4)

uint32_t __fastcall FillInsertNoted(char* self, void*, uint32_t at, uint32_t count, uint32_t value)
{
    return StHelper(StMake(addr::SC4PtrVector_FillInsert, self, Ptr(at), Ptr(count), At<uint32_t>(Ptr(value), 0)),
                    self, 0xC, [&] { return g_SC4PtrVector_FillInsert(self, nullptr, at, count, value); });
}
OPENSC4_THUNK(TFillInsertNoted, FillInsertNoted, 12)

uint32_t __fastcall AppendFloatNoted(char* self, void*, uint32_t at, uint32_t value, uint32_t tag, uint32_t count,
                                     uint32_t atEnd)
{
    return StHelper(StMake(addr::SC4FloatVector_InsertN5, self, Ptr(at), nullptr, At<uint32_t>(Ptr(value), 0)), self,
                    0xC, [&] { return g_SC4FloatVector_InsertN5(self, nullptr, at, value, tag, count, atEnd); });
}
OPENSC4_THUNK(TAppendFloatNoted, AppendFloatNoted, 20)

uint32_t __fastcall AssignRecordsNoted(char* self, void* edx, uint32_t from)
{
    return LsCopyNoted<&g_SC4EffectCompoundRecords_Assign, addr::SC4EffectCompoundRecords_Assign, 0xC>(self, edx,
                                                                                                      from);
}
OPENSC4_THUNK(TAssignRecordsNoted, AssignRecordsNoted, 4)

uint32_t __fastcall AssignRecordsBNoted(char* self, void* edx, uint32_t from)
{
    return LsCopyNoted<&g_SC4EffectCompoundRecordsB_Assign, addr::SC4EffectCompoundRecordsB_Assign, 0xC>(self, edx,
                                                                                                        from);
}
OPENSC4_THUNK(TAssignRecordsBNoted, AssignRecordsBNoted, 4)

// The ends of other groups, by the group, the function and how it is
// called (`self` alone or a range).
struct TbForeign {
    const char* group;
    uint32_t target;
    bool range;
};
constexpr TbForeign kTbForeign[] = {
    {"renderer.effects.vectors", addr::SC4EffectTexts10_Destroy, false},
    {"renderer.effects.pointers", addr::SC4CountedPtrs_Destroy, false},
    {"renderer.effects.pointers", addr::SC4CountedAt4Ptrs_Destroy, false},
    {"renderer.effects.vectors", addr::SC4EffectNameTables_DestroyRange, true},
    {"renderer.effects.vectors", addr::SC4EffectRecords1C_DestroyRange, true},
    {"renderer.effects.vectors", addr::SC4EffectRecords28_DestroyRange, true},
    {"renderer.effects.vectors", addr::SC4DecalDescriptions_DestroyRange, true},
    {"renderer.effects.store", addr::SC4EffectCompounds_DestroyRange, true},
};
Call0Fn g_tbForeign0[3] = {};
StCdecl3 g_tbForeign3[5] = {};

template <int i>
uint32_t __fastcall TbForeign0Noted(char* self, void*)
{
    return StCallNoted(kTbForeign[i].target, self, g_tbForeign0[i]);
}
uint32_t __fastcall TbForeignTexts10(char* self, void* edx)
{
    return TbForeign0Noted<0>(self, edx);
}
OPENSC4_THUNK(TTbForeignTexts10, TbForeignTexts10, 0)
uint32_t __fastcall TbForeignCounted(char* self, void* edx)
{
    return TbForeign0Noted<1>(self, edx);
}
OPENSC4_THUNK(TTbForeignCounted, TbForeignCounted, 0)
uint32_t __fastcall TbForeignCountedAt4(char* self, void* edx)
{
    return TbForeign0Noted<2>(self, edx);
}
OPENSC4_THUNK(TTbForeignCountedAt4, TbForeignCountedAt4, 0)

template <int i>
uint32_t __cdecl TbForeign3Noted(uint32_t first, uint32_t last, uint32_t tag)
{
    return StHelper(StMake(kTbForeign[i + 3].target, Ptr(first), Ptr(last)), nullptr, 0,
                    [&] { return g_tbForeign3[i](first, last, tag); });
}

bool HookTablesForeign()
{
    void (*const thunks0[])() = {&TTbForeignTexts10, &TTbForeignCounted, &TTbForeignCountedAt4};
    const StCdecl3 thunks3[] = {&TbForeign3Noted<0>, &TbForeign3Noted<1>, &TbForeign3Noted<2>, &TbForeign3Noted<3>,
                                &TbForeign3Noted<4>};
    for (int i = 0; i < 8; ++i) {
        const TbForeign& f = kTbForeign[i];
        if (registry::ModeOf(f.group) != registry::Mode::Original)
            continue;
        if (!(f.range ? DetourCdecl(f.target, thunks3[i - 3], &g_tbForeign3[i - 3])
                      : Detour(f.target, thunks0[i], &g_tbForeign0[i])))
            return false;
    }
    return true;
}

// The record copies and the copy down have one detour each, which the
// store's comparison shares (the game's only noted while the tables are
// off).
bool HookCompoundRecordCopies()
{
    return (g_SC4EffectCompound_CopyConstructRecord ||
            Detour(addr::SC4EffectCompound_CopyConstructRecord, &TTbCopyRecord,
                   &g_SC4EffectCompound_CopyConstructRecord)) &&
           (g_SC4EffectCompound_CopyRecordsBRange ||
            DetourCdecl(addr::SC4EffectCompound_CopyRecordsBRange, &TbCopyRecordsBByMode,
                        &g_SC4EffectCompound_CopyRecordsBRange)) &&
           (g_SC4EffectCompounds_CopyDown ||
            DetourCdecl(addr::SC4EffectCompounds_CopyDown, &TbCopyDownByMode, &g_SC4EffectCompounds_CopyDown));
}

bool InstallTables(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookCompoundRecordCopies() ||
        !DetourCdecl(addr::SC4EffectCompound_ConstructRecordB, &TbConstructRecordBByMode,
                     &g_SC4EffectCompound_ConstructRecordB) ||
        !DetourCdecl(addr::SC4EffectCompound_CopyRecordsRange, &TbCopyRecordsByMode,
                     &g_SC4EffectCompound_CopyRecordsRange) ||
        !Detour(addr::SC4EffectNameHash_Copy, &TTbCopyNameHash, &g_SC4EffectNameHash_Copy) ||
        !Detour(addr::SC4EffectNameHash_InsertEqual, &TTbInsertName, &g_SC4EffectNameHash_InsertEqual) ||
        !DetourCdecl(addr::SC4EffectNameTables_FillNew, &TbFillNameTablesByMode, &g_SC4EffectNameTables_FillNew) ||
        !Detour(addr::SC4EffectNameTables_Construct, &TTbConstructNameTables, &g_SC4EffectNameTables_Construct) ||
        !Detour(addr::SC4EffectZoomDistances_Construct, &TTbConstructZoom, &g_SC4EffectZoomDistances_Construct) ||
        !Detour(addr::SC4EffectTables_Construct, &TTbConstruct, &g_SC4EffectTables_Construct) ||
        !Detour(addr::SC4EffectTables_Destroy, &TTbDestroy, &g_SC4EffectTables_Destroy) ||
        !Detour(addr::SC4EffectTables_Take, &TTbTake, &g_SC4EffectTables_Take) ||
        !Detour(addr::SC4EffectsManager_AddRef, &TTbAddRef, &g_SC4EffectsManager_AddRef) ||
        !Detour(addr::SC4EffectsManager_SetRenderStates, &TTbSetRenderStates, &g_SC4EffectsManager_SetRenderStates) ||
        !Detour(addr::SC4EffectsManager_QueryInterface, &TTbQuery, &g_SC4EffectsManager_QueryInterface))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookPoolAllocate() && HookFree() && HookCopyString() && HookAssignText() && HookAssignFloats() &&
          HookClearTable() && HookDestroyTexts14() && HookCountedAt4Hold() && HookTablesForeign() &&
          (g_SC4EffectDescription_Release || Detour(addr::SC4EffectDescription_Release,
                                                    &TSC4EffectDescription_Release,
                                                    &g_SC4EffectDescription_Release)) &&
          Detour(addr::SC4Uint32HashSet_Construct, &TConstructHashNoted, &g_SC4Uint32HashSet_Construct) &&
          Detour(addr::PtrVector_Reserve, &TReserveNoted, &g_PtrVector_Reserve) &&
          Detour(addr::SC4PtrVector_FillInsert, &TFillInsertNoted, &g_SC4PtrVector_FillInsert) &&
          Detour(addr::SC4FloatVector_InsertN5, &TAppendFloatNoted, &g_SC4FloatVector_InsertN5) &&
          Detour(addr::SC4EffectCompoundRecords_Assign, &TAssignRecordsNoted, &g_SC4EffectCompoundRecords_Assign) &&
          Detour(addr::SC4EffectCompoundRecordsB_Assign, &TAssignRecordsBNoted,
                 &g_SC4EffectCompoundRecordsB_Assign)))
        return false;
    g_tbMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the effects tables and the manager's interface %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredTables = registry::Register({
    "renderer.effects.tables",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectTables_Construct,
    "The effects tables the effects file is read into: their constructor, end and take, the list of name tables "
    "and its fill, a name hash's copy and insert, the zoom distances, a compound description's record copies and "
    "copy down; and the effects manager's AddRef, QueryInterface and render states; compare mode runs the game's "
    "with its calls noted, each of these compared where it is called, and replays ours (docs/renderer.md, "
    "\"Water effects, sky and clouds\")",
    true,
    InstallTables,
});

// --- The effects manager's hash tables -----------------------------------------------------
//
// The manager's hash tables and small objects: a dword hash's look-up and
// end, a hash of key pairs' insert and growth, a hash of names' growth, the
// clear of a hash of lists and of a list of events, the 0x40-byte counted
// object's constructor and destructors, the character transform
// (effects_hashes.h); and the manager's lists of held effects (the
// fourth kind of the lists of held pointers, by this group's mode). The
// comparison is the store's, sharing its engine: the game's runs with each
// call it makes noted, these compared where they are called, ours
// replayed; the ends and clears run ours first. A growth's game's run
// frees the old buckets it relinks, and an end's what it read: their frees
// (and deletes) are kept until the comparison is done, so the memory can
// be put back and read again. Several of these are the whole game's, not
// only the manager's: they are compared only on the thread the group was
// installed on (another's runs the game's).
DWORD g_hsThread = 0;

// What ours calls: in the replay the note, else the call or ours.
void DestroyCounted40(char* object);
StFreeFn g_SC4_operator_delete = nullptr;

void HsLetGoEvent(char* object)
{
    const uint32_t letGo = At<uint32_t>(At<char*>(object, 0), 8);
    if (g_stReplay)
        StReplayed(StMake(letGo, object), nullptr);
    else
        CallThis<uint32_t>(letGo, object);
}

void HsDestroyCounted40(char* object)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4Counted40_Destroy, object), nullptr);
    else
        DestroyCounted40(object);
}

void HsDelete(char* object)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4_operator_delete, object), nullptr);
    else
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(object);
}

// The dword hash's clear (0x008EBA60), which renderer.effects.tail's
// comparison detours to note: with that detour on, the game's end of a
// hash is noted as the clear and the free, so ours' replay notes its
// inlined clear as that one call (its frees inside it are not noted, as
// the game's are not).
extern Call0Fn g_SC4Uint32HashSet_Clear;

bool HsClearNoted(char* table)
{
    if (!g_stReplay || !g_SC4Uint32HashSet_Clear)
        return false;
    StReplayed(StMake(addr::SC4Uint32HashSet_Clear, table), nullptr);
    return true;
}

#include "core/effects_hashes.h"

// A comparison whose game's run frees what is put back for ours: the
// frees kept until it is done (inside another's, for that one).
template <typename Compare>
uint32_t HsKeepingFrees(Compare&& compare)
{
    std::vector<std::pair<uint32_t, uint32_t>> kept;
    auto* const outer = g_stKept;
    if (!outer)
        g_stKept = &kept;
    const uint32_t result = compare();
    g_stKept = outer;
    for (const auto& [target, storage] : kept) {
        if (target == addr::SC4_Free)
            g_SC4_Free(storage);
        else
            g_SC4_operator_delete(storage);
    }
    return result;
}

// An end or clear: ours first (its frees, deletes and let-goes only
// noted), its bytes in `spans` kept, the memory put back; then the game's
// noted, its frees kept. The calls, the bytes and (with `result`) EAX are
// compared.
template <typename Game, typename Ours>
uint32_t HsEndCompared(const char* id, const TbSpans& spans, bool result, Game&& game, Ours&& ours)
{
    return HsKeepingFrees([&] {
        StSaved saved;
        g_stGame.clear();
        g_stOurs.clear();
        std::vector<std::vector<char>> before, own, theirs;
        for (const TbSpan& span : spans)
            before.emplace_back(span.at, span.at + span.size);
        g_stThread = GetCurrentThreadId();
        g_stDepth = 0;
        g_stNoting = false;
        g_stReplay = true;
        const uint32_t mine = ours();
        g_stReplay = false;
        for (std::size_t i = 0; i < spans.size(); ++i) {
            own.emplace_back(spans[i].at, spans[i].at + spans[i].size);
            std::memcpy(spans[i].at, before[i].data(), spans[i].size);
        }
        g_stNoting = true;
        const uint32_t answer = game();
        g_stNoting = false;
        for (const TbSpan& span : spans)
            theirs.emplace_back(span.at, span.at + span.size);
        if (!(g_stGame == g_stOurs)) {
            StRecord(id, false, StNotesText(g_stGame, g_stOurs), StNotesText(g_stOurs, g_stGame));
            return answer;
        }
        for (std::size_t i = 0; i < spans.size(); ++i) {
            if (own[i] == theirs[i])
                continue;
            std::size_t at = 0;
            while (own[i][at] == theirs[i][at])
                ++at;
            at &= ~std::size_t{0xF};
            const std::size_t n = std::min<std::size_t>(0x20, spans[i].size - at);
            char text[48];
            std::snprintf(text, sizeof text, "span %zu of %zu +0x%zX: ", i, spans.size(), at);
            StRecord(id, false, text + Hex(theirs[i].data() + at, n), text + Hex(own[i].data() + at, n));
            return answer;
        }
        StRecord(id, !result || answer == mine, Hex(&answer, 4), Hex(&mine, 4));
        return answer;
    });
}

// The group's mode; compared only on the group's thread.
template <typename Game, typename Ours, typename Compared>
uint32_t HsByMode(StNote note, const char* out, std::size_t size, Game&& game, Ours&& ours, Compared&& compared)
{
    // Some detours are shared with renderer.effects.tail: noted whatever the mode.
    if (g_hsMode == StepMode::Original)
        return StHelper(std::move(note), out, size, game);
    if (g_hsMode == StepMode::Compare && GetCurrentThreadId() != g_hsThread)
        return game();
    return StByModeOf(g_hsMode, std::move(note), out, size, game, ours, compared);
}

// A hash's table and bucket storage.
TbSpans HsTable(char* table)
{
    TbSpans spans{{table, 0x14, false}};
    if (char* const buckets = At<char*>(table, 4))
        spans.push_back({buckets, HsBuckets(table) * std::size_t{4}, false});
    return spans;
}

// The game's side: each of the eleven by the group's mode (inside a noted
// run noted, and compared where it is called). The lists of held effects
// are the lists of held pointers' code with this group's mode.
Call1Fn g_SC4Uint32HashSet_Find = nullptr, g_SC4PairHash_Grow = nullptr, g_SC4StringHash_Grow = nullptr,
        g_SC4Counted40_DeletingDestructor = nullptr;
Call0Fn g_SC4Uint32Hash_Destroy = nullptr, g_SC4PtrVectorHashMap_Clear = nullptr, g_SC4EventList_Clear = nullptr,
        g_SC4Counted40_Construct = nullptr, g_SC4Counted40_Destroy = nullptr;
Call2Fn g_SC4PairHash_InsertUnique = nullptr;
StCdecl4 g_SC4_TransformChars = nullptr;

uint32_t __fastcall HsFindByMode(char* table, void*, uint32_t key)
{
    const auto game = [&] { return g_SC4Uint32HashSet_Find(table, nullptr, key); };
    const auto ours = [&] { return Word(FindUint32(table, reinterpret_cast<const uint32_t*>(key))); };
    return HsByMode(StMake(addr::SC4Uint32HashSet_Find, table, Ptr(key)), nullptr, 0, game, ours,
                    [&] { return TbCompared("effects.hashes.find", nullptr, 0, true, game, ours); });
}
OPENSC4_THUNK(THsFind, HsFindByMode, 4)

// The pair an insert is given, folded into its note (the pointer alone is
// a place on the stack).
uint32_t PairBytesOf(uint32_t value)
{
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < 0xC; ++i)
        hash = (hash ^ reinterpret_cast<const uint8_t*>(value)[i]) * 16777619u;
    return hash;
}

uint32_t __fastcall HsInsertPairByMode(char* table, void*, uint32_t out, uint32_t value)
{
    const auto game = [&] { return g_SC4PairHash_InsertUnique(table, nullptr, out, value); };
    const auto ours = [&] {
        return Word(InsertPairUnique(table, Ptr(out), reinterpret_cast<const uint32_t*>(value)));
    };
    const auto kept = [&] { return HsTable(table); };
    return HsByMode(StMake(addr::SC4PairHash_InsertUnique, table, Ptr(out), Ptr(value), PairBytesOf(value)),
                    Ptr(out), 0xC, game, ours,
                    [&] {
                        return TbCompared("effects.hashes.insert_pair", Ptr(out), 0xC, true, game, ours, kept,
                                          TbMade);
                    });
}
OPENSC4_THUNK(THsInsertPair, HsInsertPairByMode, 8)

// A growth: the old buckets and each node's link, which the game's run
// changes and ours reads again (only when it grows); the new buckets it
// made.
template <typename Hash>
uint32_t HsGrowCompared(const char* id, char* table, uint32_t count, Call1Fn real, Hash&& hash)
{
    const auto game = [&] { return real(table, nullptr, count); };
    const auto ours = [&] {
        GrowHash(table, count, hash);
        return 0u;
    };
    const auto kept = [&] {
        TbSpans spans;
        const uint32_t old = HsBuckets(table);
        if (count <= old || HsPrimeAtLeast(count) <= old)
            return spans;
        char** const buckets = At<char**>(table, 4);
        spans.push_back({Ptr(Word(buckets)), old * std::size_t{4}, false});
        for (uint32_t i = 0; i < old; ++i) {
            for (char* node = buckets[i]; node; node = At<char*>(node, 0))
                spans.push_back({node, 4, false});
        }
        return spans;
    };
    return HsKeepingFrees([&] { return TbCompared(id, table, 0x14, false, game, ours, kept, TbMade); });
}

uint32_t __fastcall HsGrowPairsByMode(char* table, void*, uint32_t count)
{
    const auto game = [&] { return g_SC4PairHash_Grow(table, nullptr, count); };
    const auto ours = [&] {
        GrowPairHash(table, count);
        return 0u;
    };
    return HsByMode(StMake(addr::SC4PairHash_Grow, table, Ptr(count)), table, 0x14, game, ours, [&] {
        return HsGrowCompared("effects.hashes.grow_pairs", table, count, g_SC4PairHash_Grow, HashPair);
    });
}
OPENSC4_THUNK(THsGrowPairs, HsGrowPairsByMode, 4)

uint32_t __fastcall HsGrowNamesByMode(char* table, void*, uint32_t count)
{
    const auto game = [&] { return g_SC4StringHash_Grow(table, nullptr, count); };
    const auto ours = [&] {
        GrowNameHash(table, count);
        return 0u;
    };
    return HsByMode(StMake(addr::SC4StringHash_Grow, table, Ptr(count)), table, 0x14, game, ours, [&] {
        return HsGrowCompared("effects.hashes.grow_names", table, count, g_SC4StringHash_Grow, HashName);
    });
}
OPENSC4_THUNK(THsGrowNames, HsGrowNamesByMode, 4)

uint32_t __fastcall HsDestroyHashByMode(char* table, void*)
{
    const auto game = [&] { return g_SC4Uint32Hash_Destroy(table, nullptr); };
    const auto ours = [&] {
        DestroyUint32Hash(table);
        return 0u;
    };
    return HsByMode(StMake(addr::SC4Uint32Hash_Destroy, table), nullptr, 0, game, ours, [&] {
        return HsEndCompared("effects.hashes.destroy_hash", HsTable(table), false, game, ours);
    });
}
OPENSC4_THUNK(THsDestroyHash, HsDestroyHashByMode, 0)

uint32_t __fastcall HsClearListsByMode(char* table, void*)
{
    const auto game = [&] { return g_SC4PtrVectorHashMap_Clear(table, nullptr); };
    const auto ours = [&] {
        ClearListHash(table);
        return 0u;
    };
    return HsByMode(StMake(addr::SC4PtrVectorHashMap_Clear, table), nullptr, 0, game, ours, [&] {
        return HsEndCompared("effects.hashes.clear_lists", HsTable(table), false, game, ours);
    });
}
OPENSC4_THUNK(THsClearLists, HsClearListsByMode, 0)

uint32_t __fastcall HsClearEventsByMode(char* list, void*)
{
    const auto game = [&] { return g_SC4EventList_Clear(list, nullptr); };
    const auto ours = [&] {
        ClearEvents(list);
        return 0u;
    };
    return HsByMode(StMake(addr::SC4EventList_Clear, list), nullptr, 0, game, ours, [&] {
        return HsEndCompared("effects.hashes.clear_events", TbSpans{{list, 4, false}, {At<char*>(list, 0), 8, false}},
                             false, game, ours);
    });
}
OPENSC4_THUNK(THsClearEvents, HsClearEventsByMode, 0)

uint32_t __fastcall HsConstructCountedByMode(char* object, void*)
{
    const auto game = [&] { return g_SC4Counted40_Construct(object, nullptr); };
    const auto ours = [&] { return Word(ConstructCounted40(object)); };
    // The string's first byte, in storage the game's run made.
    const auto after = [&] {
        TbSpans spans;
        for (const TbSpan& made : TbMade())
            spans.push_back({made.at, 1, true});
        return spans;
    };
    return HsByMode(StMake(addr::SC4Counted40_Construct, object), object, 0x40, game, ours, [&] {
        return TbCompared("effects.hashes.construct_counted", object, 0x40, true, game, ours, TbNone, after);
    });
}
OPENSC4_THUNK(THsConstructCounted, HsConstructCountedByMode, 0)

uint32_t __fastcall HsDestroyCountedByMode(char* object, void*)
{
    const auto game = [&] { return g_SC4Counted40_Destroy(object, nullptr); };
    const auto ours = [&] {
        DestroyCounted40(object);
        return 0u;
    };
    return HsByMode(StMake(addr::SC4Counted40_Destroy, object), nullptr, 0, game, ours, [&] {
        return HsEndCompared("effects.hashes.destroy_counted", TbSpans{{object, 0x40, false}}, false, game, ours);
    });
}
OPENSC4_THUNK(THsDestroyCounted, HsDestroyCountedByMode, 0)

uint32_t __fastcall HsDeleteCountedByMode(char* object, void*, uint32_t flags)
{
    const auto game = [&] { return g_SC4Counted40_DeletingDestructor(object, nullptr, flags); };
    const auto ours = [&] { return Word(DeleteCounted40(object, flags)); };
    return HsByMode(StMake(addr::SC4Counted40_DeletingDestructor, object, Ptr(flags)), nullptr, 0, game, ours,
                    [&] { return HsEndCompared("effects.hashes.delete_counted", TbNone(), true, game, ours); });
}
OPENSC4_THUNK(THsDeleteCounted, HsDeleteCountedByMode, 4)

uint32_t __cdecl HsTransformByMode(uint32_t first, uint32_t last, uint32_t out, uint32_t fn)
{
    const auto game = [&] { return g_SC4_TransformChars(first, last, out, fn); };
    const auto ours = [&] { return Word(TransformChars(Ptr(first), Ptr(last), Ptr(out), fn)); };
    const std::size_t size = out && last > first && last - first < 0x100000 ? last - first : 0;
    // One detour, shared with renderer.effects.tail: noted whatever the mode.
    if (g_hsMode == StepMode::Original)
        return StHelper(StMake(addr::SC4_TransformChars, Ptr(first), Ptr(last), Ptr(out), fn), nullptr, 0, game);
    return HsByMode(StMake(addr::SC4_TransformChars, Ptr(first), Ptr(last), Ptr(out), fn), nullptr, 0, game, ours,
                    [&] { return TbCompared("effects.hashes.transform", Ptr(out), size, true, game, ours); });
}

// The game's helpers, noted in a noted run: operator delete (kept by a
// comparison that keeps its frees) and the held effects' hold and let-go,
// by the functions the two classes seen in the lists have in slots 0x1C
// and 0x20 (the effects of table 0x00AA0EA0: the thunks to their counted
// base at +4; the decals of 0x00A9EE78: the shared AddRef 0x005F47F0 and
// the decal's Release 0x005833A0), each noted as the hold or let-go ours
// notes.
Call0Fn g_SC4EffectAt4_AddRefThunk = nullptr, g_SC4EffectAt4_ReleaseThunk = nullptr, g_SC4Counted4_AddRef = nullptr,
        g_SC4EffectDecal_Release = nullptr;

void __cdecl DeleteNoted(uint32_t object)
{
    if (g_stKept && StNoting()) {
        g_stGame.push_back(StMake(addr::SC4_operator_delete, Ptr(object)));
        g_stKept->push_back({addr::SC4_operator_delete, object});
        return;
    }
    StHelper(StMake(addr::SC4_operator_delete, Ptr(object)), nullptr, 0, [&] {
        g_SC4_operator_delete(object);
        return 0u;
    });
}

uint32_t __fastcall EffectHoldNoted(char* self, void*)
{
    return StCallNoted(addr::SC4EffectAt4_AddRefThunk, self, g_SC4EffectAt4_AddRefThunk);
}
OPENSC4_THUNK(TEffectHoldNoted, EffectHoldNoted, 0)

uint32_t __fastcall EffectLetGoNoted(char* self, void*)
{
    return StCallNoted(addr::SC4EffectAt4_ReleaseThunk, self, g_SC4EffectAt4_ReleaseThunk);
}
OPENSC4_THUNK(TEffectLetGoNoted, EffectLetGoNoted, 0)

uint32_t __fastcall DecalHoldNoted(char* self, void*)
{
    return StCallNoted(addr::SC4EffectAt4_AddRefThunk, self, g_SC4Counted4_AddRef);
}
OPENSC4_THUNK(TDecalHoldNoted, DecalHoldNoted, 0)

// The decal's Release has one detour, shared with
// renderer.effects.decal_parts (noted here whatever its mode).
bool HookDecalRelease();

// The free and the delete have one detour each, which every comparison
// shares: the effect's record-and-replay holds back the drawing thread's
// (DFree, DDelete), the others' note theirs (FreeNoted, DeleteNoted).
// Two detours on one function would not install.
bool HookFrees()
{
    if (g_free)
        return true;
    const addr::Entry* const release = addr_check::Find(addr::SC4_Free);
    const addr::Entry* const destroy = addr_check::Find(addr::SC4_operator_delete);
    if (!(release && destroy && hook::Install(*release, &DFree, &g_free) &&
          hook::Install(*destroy, &DDelete, &g_delete)))
        return false;
    g_SC4_Free = reinterpret_cast<StFreeFn>(g_free);
    g_SC4_operator_delete = reinterpret_cast<StFreeFn>(g_delete);
    return true;
}

bool HookDelete()
{
    return HookFrees();
}

bool HookPairHashInsert()
{
    return g_SC4PairHash_InsertUnique ||
           Detour(addr::SC4PairHash_InsertUnique, &THsInsertPair, &g_SC4PairHash_InsertUnique);
}

bool HookPairHashGrow()
{
    return g_SC4PairHash_Grow || Detour(addr::SC4PairHash_Grow, &THsGrowPairs, &g_SC4PairHash_Grow);
}

bool HookTransformChars()
{
    return g_SC4_TransformChars || DetourCdecl(addr::SC4_TransformChars, &HsTransformByMode, &g_SC4_TransformChars);
}

bool InstallHashes(registry::Mode mode)
{
    constexpr int effect = static_cast<int>(Held::Effect);
    if (!game::IsBuild641() ||
        !Detour(addr::SC4Uint32HashSet_Find, &THsFind, &g_SC4Uint32HashSet_Find) ||
        !HookPairHashInsert() || !HookPairHashGrow() ||
        !Detour(addr::SC4StringHash_Grow, &THsGrowNames, &g_SC4StringHash_Grow) ||
        !Detour(addr::SC4Uint32Hash_Destroy, &THsDestroyHash, &g_SC4Uint32Hash_Destroy) ||
        !Detour(addr::SC4PtrVectorHashMap_Clear, &THsClearLists, &g_SC4PtrVectorHashMap_Clear) ||
        !Detour(addr::SC4EventList_Clear, &THsClearEvents, &g_SC4EventList_Clear) ||
        !Detour(addr::SC4Counted40_Construct, &THsConstructCounted, &g_SC4Counted40_Construct) ||
        !Detour(addr::SC4Counted40_Destroy, &THsDestroyCounted, &g_SC4Counted40_Destroy) ||
        !Detour(addr::SC4Counted40_DeletingDestructor, &THsDeleteCounted, &g_SC4Counted40_DeletingDestructor) ||
        !HookTransformChars() ||
        !DetourCdecl(addr::SC4HeldEffects_CopyRange, &PtCopyByMode<Held::Effect>, &g_ptCopy[effect]) ||
        !DetourCdecl(addr::SC4HeldEffects_Fill, &PtFillByMode<Held::Effect>, &g_ptFill[effect]) ||
        !DetourCdecl(addr::SC4HeldEffects_Assign, &PtAssignByMode<Held::Effect>, &g_ptAssign[effect]) ||
        !Detour(addr::SC4HeldEffects_Destroy, &TPtDestroyEffects, &g_ptDestroy[effect]) ||
        !Detour(addr::SC4HeldEffects_GrowInsert, &TPtGrowEffects, &g_ptGrow[effect]))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookPoolAllocate() && HookFree() &&
          HookDelete() &&
          Detour(addr::SC4EffectAt4_AddRefThunk, &TEffectHoldNoted, &g_SC4EffectAt4_AddRefThunk) &&
          Detour(addr::SC4EffectAt4_ReleaseThunk, &TEffectLetGoNoted, &g_SC4EffectAt4_ReleaseThunk) &&
          Detour(addr::SC4Counted4_AddRef, &TDecalHoldNoted, &g_SC4Counted4_AddRef) &&
          HookDecalRelease()))
        return false;
    g_hsThread = GetCurrentThreadId();
    g_hsMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the effects manager's hash tables %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredHashes = registry::Register({
    "renderer.effects.hashes",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4PairHash_InsertUnique,
    "The effects manager's hash tables and small objects: a dword hash's look-up and end, a hash of key pairs' "
    "insert and growth, a hash of names' growth, the clears of a hash of lists and of a list of events, the "
    "0x40-byte counted object's constructor and destructors, the character transform, and the manager's lists of "
    "held effects (copy, fill, assignment, end, growth); compare mode runs the game's with its calls noted, each of "
    "these compared where it is called (on the group's thread), and replays ours (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallHashes,
});

// --- The particles' height map -------------------------------------------------------------
//
// The height map a particle meeting the ground asks (effects_heightmap.h):
// its query, the height at a place, a vertex's height and the normal, the
// three changes of a layer, the constructor, the init, the shutdown and the
// deleting destructor with its thunk. The comparison runs both on what each
// may change. The four questions only read: the game's, then ours, the
// answers compared to the bit, the query's with the count at +8 it raised
// (put back after ours). A
// change of a layer: the layer and +0x48 put back after the game's, ours
// run, the two compared and the game's left. The constructor: ours on a
// copy of the memory, then the game's. The init, the shutdown and the
// destructor: ours on a copy whose layers are its own and whose terrain is
// held once more, then the game's; the bytes compared, the layers'
// pointers by what they hold (null alike, the same used and room, and
// after an init the same values). The destructor's game's run is made
// without the delete flag and the delete made after it; ours' delete is
// only noted. Inside another group's noted run only the game's runs.
StepMode g_hmMode = StepMode::Original;
using HmHeightFn = double(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t);
using HmCall4Fn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t, uint32_t);
Call0Fn g_SC4EffectHeightMap_Construct = nullptr, g_SC4EffectHeightMap_Shutdown = nullptr,
        g_SC4EffectHeightMap_Init = nullptr;
Call1Fn g_SC4EffectHeightMap_DeletingDestructor = nullptr, g_SC4EffectHeightMap_DeletingDestructorThunk = nullptr;
Call2Fn g_SC4EffectHeightMap_QueryInterface = nullptr, g_SC4EffectHeightMap_Clear = nullptr;
HmHeightFn g_SC4EffectHeightMap_Height = nullptr, g_SC4EffectHeightMap_CellHeight = nullptr;
HmCall4Fn g_SC4EffectHeightMap_Normal = nullptr, g_SC4EffectHeightMap_Raise = nullptr,
          g_SC4EffectHeightMap_Lift = nullptr;
char* g_hmDeleted = nullptr;
char* g_hmDestroyed = nullptr;
uint32_t g_hmDestroyedFlags = 0;

#include "core/effects_heightmap.h"

void HmDelete(void* object)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(object);
}

void HmNoteDelete(void* object)
{
    g_hmDeleted = static_cast<char*>(object);
}

bool HmGameOnly()
{
    return g_hmMode == StepMode::Original || g_stNoting || g_stReplay;
}

float HmFloat(uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, 4);
    return value;
}

void HmRecord(const char* id, const void* theirs, const void* mine, std::size_t size, bool same = true)
{
    same = same && std::memcmp(theirs, mine, size) == 0;
    StRecord(id, same, Hex(theirs, size), Hex(mine, size));
}

// The layers' storage of a map one can copy: none, or room for each.
bool HmCopyable(char* self)
{
    char* const begin = At<char*>(self, 0x14);
    if (!begin)
        return true;
    if (At<char*>(self, 0x1C) == begin)
        return false;
    for (char* layer = begin; layer < At<char*>(self, 0x18); layer += 0xC) {
        if (At<char*>(layer, 0) && At<char*>(layer, 8) == At<char*>(layer, 0))
            return false;
    }
    return true;
}

// A copy of the map whose layers are its own (storage of the same room,
// each layer's values in storage of its room), its terrain held once more.
std::vector<char> HmDeepCopy(char* self)
{
    std::vector<char> copy(self, self + kHeightMapSize);
    if (char* const begin = At<char*>(self, 0x14)) {
        const std::size_t used = At<char*>(self, 0x18) - begin, room = At<char*>(self, 0x1C) - begin;
        char* const layers = StlAllocate(room);
        for (std::size_t at = 0; at < used; at += 0xC) {
            char* const from = begin + at;
            char* const to = layers + at;
            std::memset(to, 0, 0xC);
            if (char* const values = At<char*>(from, 0)) {
                const std::size_t n = At<char*>(from, 4) - values, r = At<char*>(from, 8) - values;
                char* const storage = StlAllocate(r);
                std::memcpy(storage, values, n);
                At<char*>(to, 0) = storage;
                At<char*>(to, 4) = storage + n;
                At<char*>(to, 8) = storage + r;
            }
        }
        At<char*>(copy.data(), 0x14) = layers;
        At<char*>(copy.data(), 0x18) = layers + used;
        At<char*>(copy.data(), 0x1C) = layers + room;
    }
    if (char* const terrain = At<char*>(self, 0xC))
        HmSlot<uint32_t>(terrain, 4);
    return copy;
}

// A vector's three pointers by what they say: null alike, the same used and
// room.
bool HmSameShape(char* theirs, char* mine)
{
    char* const a = At<char*>(theirs, 0);
    char* const b = At<char*>(mine, 0);
    return (a == nullptr) == (b == nullptr) && At<char*>(theirs, 4) - a == At<char*>(mine, 4) - b &&
           At<char*>(theirs, 8) - a == At<char*>(mine, 8) - b;
}

// The game's map and ours: the bytes but the layers' pointers, those by what
// they say, and with `values` each layer's shape and values.
bool HmSame(const char* id, char* theirs, char* mine, bool values)
{
    char text[40];
    for (std::size_t at = 0; at < kHeightMapSize; at += 4) {
        if (at >= 0x14 && at < 0x20)
            continue;
        if (std::memcmp(theirs + at, mine + at, 4) != 0) {
            std::snprintf(text, sizeof text, "+%zX ", at);
            StRecord(id, false, text + Hex(theirs + at, 4), text + Hex(mine + at, 4));
            return false;
        }
    }
    bool same = HmSameShape(theirs + 0x14, mine + 0x14);
    std::snprintf(text, sizeof text, "layers ");
    std::size_t layer = 0;
    if (same && values && At<char*>(theirs, 0x14)) {
        char* a = At<char*>(theirs, 0x14);
        char* b = At<char*>(mine, 0x14);
        for (; same && a < At<char*>(theirs, 0x18); a += 0xC, b += 0xC, ++layer) {
            same = HmSameShape(a, b) && std::memcmp(At<char*>(a, 0), At<char*>(b, 0),
                                                    At<char*>(a, 4) - At<char*>(a, 0)) == 0;
            if (!same)
                std::snprintf(text, sizeof text, "layer %zu ", layer);
        }
    }
    StRecord(id, same, text + Hex(theirs + 0x14, 0xC), text + Hex(mine + 0x14, 0xC));
    return same;
}

uint32_t __fastcall HmQueryByMode(char* self, void*, uint32_t id, uint32_t out)
{
    void** const place = reinterpret_cast<void**>(out);
    if (g_hmMode == StepMode::Ours)
        return QueryHeightMap(self, id, place);
    if (HmGameOnly())
        return g_SC4EffectHeightMap_QueryInterface(self, nullptr, id, out);
    void* mine = *place;
    const uint32_t count = At<uint32_t>(self, 8);
    const uint32_t result = g_SC4EffectHeightMap_QueryInterface(self, nullptr, id, out);
    const uint32_t held = At<uint32_t>(self, 8);
    const uint8_t answer = static_cast<uint8_t>(QueryHeightMap(self, id, &mine));
    const uint32_t heldAgain = At<uint32_t>(self, 8);
    At<uint32_t>(self, 8) = held;
    const uint32_t theirs[3] = {Word(*place), static_cast<uint8_t>(result), held - count};
    const uint32_t own[3] = {Word(mine), answer, heldAgain - held};
    HmRecord("effects.heightmap.query", theirs, own, 0xC);
    return result;
}
OPENSC4_THUNK(THmQuery, HmQueryByMode, 8)

double __fastcall HmHeightByMode(char* self, void*, uint32_t layer, uint32_t x, uint32_t z)
{
    if (g_hmMode == StepMode::Ours)
        return HeightMapHeight(self, layer, HmFloat(x), HmFloat(z));
    const double result = g_SC4EffectHeightMap_Height(self, nullptr, layer, x, z);
    if (HmGameOnly())
        return result;
    const double mine = HeightMapHeight(self, layer, HmFloat(x), HmFloat(z));
    HmRecord("effects.heightmap.height", &result, &mine, 8);
    return result;
}
OPENSC4_THUNK(THmHeight, HmHeightByMode, 12)

double __fastcall HmCellByMode(char* self, void*, uint32_t layer, uint32_t x, uint32_t z)
{
    if (g_hmMode == StepMode::Ours)
        return HeightMapCell(self, layer, x, z);
    const double result = g_SC4EffectHeightMap_CellHeight(self, nullptr, layer, x, z);
    if (HmGameOnly())
        return result;
    const double mine = HeightMapCell(self, layer, x, z);
    HmRecord("effects.heightmap.cell", &result, &mine, 8);
    return result;
}
OPENSC4_THUNK(THmCell, HmCellByMode, 12)

uint32_t __fastcall HmNormalByMode(char* self, void*, uint32_t out, uint32_t layer, uint32_t x, uint32_t z)
{
    if (g_hmMode == StepMode::Ours)
        return Word(HeightMapNormal(self, reinterpret_cast<float*>(out), layer, HmFloat(x), HmFloat(z)));
    const uint32_t result = g_SC4EffectHeightMap_Normal(self, nullptr, out, layer, x, z);
    if (HmGameOnly())
        return result;
    float mine[3] = {};
    const bool answered = Word(HeightMapNormal(self, mine, layer, HmFloat(x), HmFloat(z))) == Word(mine);
    HmRecord("effects.heightmap.normal", Ptr(out), mine, 0xC, answered && result == out);
    return result;
}
OPENSC4_THUNK(THmNormal, HmNormalByMode, 16)

// A change of a layer: the layer's values and +0x48 put back after the
// game's, ours run, the two compared; the game's left.
template <typename Game, typename Ours>
void HmChangeCompared(const char* id, char* self, uint32_t layer, Game&& game, Ours&& ours)
{
    const uint32_t index = layer - 1;
    char* const values = index < HmLayers(self) ? reinterpret_cast<char*>(HmLayer(self, index)) : nullptr;
    const std::size_t size = values ? At<char*>(At<char*>(self, 0x14) + index * 0xC, 4) - values : 0;
    const std::vector<char> before(values, values + size);
    const uint8_t flag = At<uint8_t>(self, 0x48);
    game();
    const std::vector<char> after(values, values + size);
    const uint8_t changed = At<uint8_t>(self, 0x48);
    if (size)
        std::memcpy(values, before.data(), size);
    At<uint8_t>(self, 0x48) = flag;
    ours();
    char text[40];
    if (At<uint8_t>(self, 0x48) != changed) {
        StRecord(id, false, "+48 " + Hex(&changed, 1), "+48 " + Hex(self + 0x48, 1));
    } else if (size && std::memcmp(values, after.data(), size) != 0) {
        std::size_t at = 0;
        while (std::memcmp(values + at, after.data() + at, 4) == 0)
            at += 4;
        std::snprintf(text, sizeof text, "value %zu ", at / 4);
        StRecord(id, false, text + Hex(after.data() + at, 4), text + Hex(values + at, 4));
    } else {
        StRecord(id, true, std::string(), std::string());
    }
    if (size)
        std::memcpy(values, after.data(), size);
    At<uint8_t>(self, 0x48) = changed;
}

uint32_t __fastcall HmRaiseByMode(char* self, void*, uint32_t layer, uint32_t rect, uint32_t amount, uint32_t count)
{
    const auto ours = [&] {
        HeightMapRaise(self, layer, reinterpret_cast<const int32_t*>(rect), HmFloat(amount), count);
    };
    const auto game = [&] { return g_SC4EffectHeightMap_Raise(self, nullptr, layer, rect, amount, count); };
    if (g_hmMode == StepMode::Ours) {
        ours();
        return 0;
    }
    if (HmGameOnly())
        return game();
    uint32_t result = 0;
    HmChangeCompared("effects.heightmap.raise", self, layer, [&] { result = game(); }, ours);
    return result;
}
OPENSC4_THUNK(THmRaise, HmRaiseByMode, 16)

uint32_t __fastcall HmLiftByMode(char* self, void*, uint32_t layer, uint32_t rect, uint32_t height, uint32_t count)
{
    const auto ours = [&] {
        HeightMapLift(self, layer, reinterpret_cast<const int32_t*>(rect), HmFloat(height), count);
    };
    const auto game = [&] { return g_SC4EffectHeightMap_Lift(self, nullptr, layer, rect, height, count); };
    if (g_hmMode == StepMode::Ours) {
        ours();
        return 0;
    }
    if (HmGameOnly())
        return game();
    uint32_t result = 0;
    HmChangeCompared("effects.heightmap.lift", self, layer, [&] { result = game(); }, ours);
    return result;
}
OPENSC4_THUNK(THmLift, HmLiftByMode, 16)

uint32_t __fastcall HmClearByMode(char* self, void*, uint32_t layer, uint32_t rect)
{
    const auto ours = [&] { HeightMapClear(self, layer, reinterpret_cast<const int32_t*>(rect)); };
    const auto game = [&] { return g_SC4EffectHeightMap_Clear(self, nullptr, layer, rect); };
    if (g_hmMode == StepMode::Ours) {
        ours();
        return 0;
    }
    if (HmGameOnly())
        return game();
    uint32_t result = 0;
    HmChangeCompared("effects.heightmap.clear", self, layer, [&] { result = game(); }, ours);
    return result;
}
OPENSC4_THUNK(THmClear, HmClearByMode, 8)

uint32_t __fastcall HmConstructByMode(char* self, void*)
{
    if (g_hmMode == StepMode::Ours)
        return Word(ConstructHeightMap(self));
    if (HmGameOnly())
        return g_SC4EffectHeightMap_Construct(self, nullptr);
    std::vector<char> mine(self, self + kHeightMapSize);
    const bool answered = ConstructHeightMap(mine.data()) == mine.data();
    const uint32_t result = g_SC4EffectHeightMap_Construct(self, nullptr);
    std::size_t at = 0;
    while (at < kHeightMapSize && std::memcmp(self + at, mine.data() + at, 4) == 0)
        at += 4;
    const std::size_t shown = at < kHeightMapSize ? at : 0;
    char text[24];
    std::snprintf(text, sizeof text, "+%zX ", shown);
    StRecord("effects.heightmap.construct", answered && result == Word(self) && at == kHeightMapSize,
             text + Hex(self + shown, 4), text + Hex(mine.data() + shown, 4));
    return result;
}
OPENSC4_THUNK(THmConstruct, HmConstructByMode, 0)

uint32_t __fastcall HmInitByMode(char* self, void*)
{
    if (g_hmMode == StepMode::Ours)
        return InitHeightMap(self);
    if (HmGameOnly() || !HmCopyable(self))
        return g_SC4EffectHeightMap_Init(self, nullptr);
    std::vector<char> mine = HmDeepCopy(self);
    const uint8_t answer = static_cast<uint8_t>(InitHeightMap(mine.data()));
    const uint32_t result = g_SC4EffectHeightMap_Init(self, nullptr);
    if (answer != static_cast<uint8_t>(result))
        StRecord("effects.heightmap.init", false, Hex(&result, 1), Hex(&answer, 1));
    else
        HmSame("effects.heightmap.init", self, mine.data(), true);
    // Ours' copy undone: its layers freed, its terrain let go.
    CallThis<void>(addr::SC4ShortVectorVector_Destroy, mine.data() + 0x14);
    if (char* const terrain = At<char*>(mine.data(), 0xC))
        HmSlot<uint32_t>(terrain, 8);
    return result;
}
OPENSC4_THUNK(THmInit, HmInitByMode, 0)

uint32_t __fastcall HmShutdownByMode(char* self, void*)
{
    if (g_hmMode == StepMode::Ours)
        return ShutdownHeightMap(self);
    if (HmGameOnly() || !HmCopyable(self))
        return g_SC4EffectHeightMap_Shutdown(self, nullptr);
    std::vector<char> mine = HmDeepCopy(self);
    const uint8_t answer = static_cast<uint8_t>(ShutdownHeightMap(mine.data()));
    const uint32_t result = g_SC4EffectHeightMap_Shutdown(self, nullptr);
    if (answer != static_cast<uint8_t>(result))
        StRecord("effects.heightmap.shutdown", false, Hex(&result, 1), Hex(&answer, 1));
    else
        HmSame("effects.heightmap.shutdown", self, mine.data(), false);
    if (char* const layers = At<char*>(mine.data(), 0x14))
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(layers);
    return result;
}
OPENSC4_THUNK(THmShutdown, HmShutdownByMode, 0)

uint32_t HmDestroyRun(char* self, uint32_t flags)
{
    g_hmDestroyed = self;
    g_hmDestroyedFlags = flags;
    if (g_hmMode == StepMode::Ours)
        return Word(DestroyHeightMap(self, flags));
    // The line list must hold nothing: ours' copy would let it go again.
    if (HmGameOnly() || !HmCopyable(self) || At<uint32_t>(self, 0x4C) || At<uint32_t>(self, 0x50) ||
        At<uint32_t>(self, 0x54))
        return g_SC4EffectHeightMap_DeletingDestructor(self, nullptr, flags);
    std::vector<char> mine = HmDeepCopy(self);
    g_hmDeleted = nullptr;
    g_hmDelete = HmNoteDelete;
    const bool answered = DestroyHeightMap(mine.data(), flags) == mine.data();
    g_hmDelete = HmDelete;
    const bool deleted = g_hmDeleted == mine.data();
    const uint32_t result = g_SC4EffectHeightMap_DeletingDestructor(self, nullptr, flags & ~1u);
    if (!answered || result != Word(self) || deleted != ((flags & 1) != 0)) {
        const uint32_t theirs[2] = {result == Word(self), flags & 1}, own[2] = {answered, deleted};
        StRecord("effects.heightmap.destroy", false, Hex(theirs, 8), Hex(own, 8));
    } else {
        HmSame("effects.heightmap.destroy", self, mine.data(), false);
    }
    if (flags & 1)
        HmDelete(self);
    return result;
}

uint32_t __fastcall HmDestroyByMode(char* self, void*, uint32_t flags)
{
    return HmDestroyRun(self, flags);
}
OPENSC4_THUNK(THmDestroy, HmDestroyByMode, 4)

// The counted base's slot 3: in compare mode the game's thunk, whose jump
// goes to the destructor's detour; that it got the map and the flags is
// compared.
uint32_t __fastcall HmDestroyThunkByMode(char* base, void*, uint32_t flags)
{
    if (g_hmMode == StepMode::Ours)
        return HmDestroyRun(base - 4, flags);
    if (HmGameOnly())
        return g_SC4EffectHeightMap_DeletingDestructorThunk(base, nullptr, flags);
    g_hmDestroyed = nullptr;
    const uint32_t result = g_SC4EffectHeightMap_DeletingDestructorThunk(base, nullptr, flags);
    const uint32_t theirs[3] = {Word(g_hmDestroyed), g_hmDestroyedFlags, result};
    const uint32_t mine[3] = {Word(base - 4), flags, Word(base - 4)};
    HmRecord("effects.heightmap.destroy_thunk", theirs, mine, 0xC);
    return result;
}
OPENSC4_THUNK(THmDestroyThunk, HmDestroyThunkByMode, 4)

bool InstallHeightMap(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !Detour(addr::SC4EffectHeightMap_QueryInterface, &THmQuery, &g_SC4EffectHeightMap_QueryInterface) ||
        !Detour(addr::SC4EffectHeightMap_Height, &THmHeight, &g_SC4EffectHeightMap_Height) ||
        !Detour(addr::SC4EffectHeightMap_CellHeight, &THmCell, &g_SC4EffectHeightMap_CellHeight) ||
        !Detour(addr::SC4EffectHeightMap_Normal, &THmNormal, &g_SC4EffectHeightMap_Normal) ||
        !Detour(addr::SC4EffectHeightMap_Raise, &THmRaise, &g_SC4EffectHeightMap_Raise) ||
        !Detour(addr::SC4EffectHeightMap_Lift, &THmLift, &g_SC4EffectHeightMap_Lift) ||
        !Detour(addr::SC4EffectHeightMap_Clear, &THmClear, &g_SC4EffectHeightMap_Clear) ||
        !Detour(addr::SC4EffectHeightMap_Construct, &THmConstruct, &g_SC4EffectHeightMap_Construct) ||
        !Detour(addr::SC4EffectHeightMap_DeletingDestructorThunk, &THmDestroyThunk,
                &g_SC4EffectHeightMap_DeletingDestructorThunk) ||
        !Detour(addr::SC4EffectHeightMap_DeletingDestructor, &THmDestroy, &g_SC4EffectHeightMap_DeletingDestructor) ||
        !Detour(addr::SC4EffectHeightMap_Shutdown, &THmShutdown, &g_SC4EffectHeightMap_Shutdown) ||
        !Detour(addr::SC4EffectHeightMap_Init, &THmInit, &g_SC4EffectHeightMap_Init))
        return false;
    g_hmDelete = HmDelete;
    g_hmMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the particles' height map %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredHeightMap = registry::Register({
    "renderer.effects.heightmap",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectHeightMap_Height,
    "The particles' height map: its query, the height at a place, a vertex's height and the ground's normal, the "
    "three changes of a layer (raise, lift to at least, clear), the constructor, init, shutdown and deleting "
    "destructor with its thunk; compare mode runs the game's and ours on what each may change (ours on a copy for "
    "the constructor, init, shutdown and destructor) and compares (docs/renderer.md, \"Water effects, sky and "
    "clouds\")",
    true,
    InstallHeightMap,
});

// --- A decal's small functions ---------------------------------------------------------------
//
// A decal's parameter setter and Release, its description's end and
// assignment (effects_decal.h), and two of the manager's: its fill of held
// pointers and the AddRef thunks of two of its interfaces (the decal's
// update is still the game's). The setter only writes the decal: the
// game's, the decal put back, ours, the two compared and the game's left.
// The assignment, the end and Release are the store's kind, sharing its
// engine: the assignment's game's run noted (its list assignments noted
// with the bytes they leave) and ours replayed on the description put
// back; the end and Release ours first, then the game's noted, their
// frees and deletes kept (a Release at zero with an object at +0x120,
// whose let-go has no detour, runs the game's only). The three keep one
// detour each with the groups that note them (the decal's description,
// the vectors, the hash tables): by this group's mode, and noted in a
// noted run whatever its mode. The fill runs the game's, then ours on
// storage of its own: the pointers compared, ours' holds given back (slot
// 2). A thunk: the game's, the count at the manager's +0x1B0 put back,
// ours; the answers and the counts compared.
StepMode g_dcMode = StepMode::Original;
Call3Fn g_SC4EffectDecal_SetParameter = nullptr;
Call1Fn g_SC4Float3Vector_Assign = nullptr;
Call0Fn g_SC4TextureBinding_Release = nullptr, g_SC4EffectsManager_AddRefThunk4 = nullptr,
        g_SC4EffectsManager_AddRefThunk8 = nullptr;
using DcFillFn = char*(__cdecl*)(char*, uint32_t, char* const*);
DcFillFn g_SC4EffectsManager_FillHeld = nullptr;

// What ours calls: in the replay the note, else the call.
void DcAssignFloat3s(char* to, char* from)
{
    LsThis(addr::SC4Float3Vector_Assign, to, from, to);
}

void DcLetGo(char* object)
{
    const uint32_t letGo = At<uint32_t>(At<char*>(object, 0), 8);
    if (g_stReplay)
        StReplayed(StMake(letGo, object), nullptr);
    else
        CallThis<uint32_t>(letGo, object);
}

void DcReleaseTexture(char* texture)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4TextureBinding_Release, texture), nullptr);
    else
        CallThis<void>(addr::SC4TextureBinding_Release, texture);
}

void DcDelete(char* object)
{
    HsDelete(object);
}

#include "core/effects_decal.h"

bool DcGameOnly()
{
    return g_dcMode == StepMode::Original || g_stNoting || g_stReplay;
}

// The setter and the update: the decal's bytes after the game's and after
// ours (and with `result` AL).
template <typename Game, typename Ours>
uint32_t DcDecalCompared(const char* id, char* decal, bool result, Game&& game, Ours&& ours)
{
    char before[kDecalSize], after[kDecalSize];
    std::memcpy(before, decal, kDecalSize);
    const uint32_t theirs = game();
    std::memcpy(after, decal, kDecalSize);
    std::memcpy(decal, before, kDecalSize);
    const uint32_t mine = ours();
    std::size_t at = 0;
    while (at < kDecalSize && std::memcmp(decal + at, after + at, 4) == 0)
        at += 4;
    char text[24];
    if (at < kDecalSize) {
        std::snprintf(text, sizeof text, "+%zX ", at);
        StRecord(id, false, text + Hex(after + at, 4), text + Hex(decal + at, 4));
    } else {
        const uint8_t a = static_cast<uint8_t>(theirs), b = static_cast<uint8_t>(mine);
        StRecord(id, !result || a == b, "AL " + Hex(&a, 1), "AL " + Hex(&b, 1));
    }
    std::memcpy(decal, after, kDecalSize);
    return theirs;
}

uint32_t __fastcall DcSetByMode(char* decal, void*, uint32_t id, uint32_t value, uint32_t unused)
{
    const auto ours = [&] { return SetDecalParameter(decal, id, Ptr(value)); };
    const auto game = [&] { return g_SC4EffectDecal_SetParameter(decal, nullptr, id, value, unused); };
    if (g_dcMode == StepMode::Ours)
        return ours();
    if (DcGameOnly())
        return game();
    return DcDecalCompared("effects.decal_parts.parameter", decal, true, game, ours);
}
OPENSC4_THUNK(TDcSet, DcSetByMode, 12)

// The three shared with other groups' noting.
uint32_t __fastcall DcAssignByMode(char* to, void*, uint32_t from)
{
    if (g_dsNoting && GetCurrentThreadId() == g_dsThread && to == g_dsDecal + 0xC)
        return DecalDescriptionAssignNoted(to, nullptr, from);
    const auto game = [&] { return g_SC4DecalDescription_Assign(to, nullptr, from); };
    const auto ours = [&] { return Word(AssignDecalDescription(to, Ptr(from))); };
    return StByModeOf(g_dcMode, StMake(addr::SC4DecalDescription_Assign, to, Ptr(from)), to, 0x64, game, ours,
                      [&] { return StCompared("effects.decal_parts.assign", to, 0x64, true, game, ours); });
}
OPENSC4_THUNK(TDcAssign, DcAssignByMode, 4)

uint32_t __fastcall DcDestroyByMode(char* description, void*)
{
    const auto game = [&] { return g_SC4DecalDescription_Destroy(description, nullptr); };
    const auto ours = [&] {
        DestroyDecalDescription(description);
        return 0u;
    };
    const StNote note = StMake(addr::SC4DecalDescription_Destroy, description);
    if (g_dcMode == StepMode::Original)
        return StHelper(note, nullptr, 0, game);
    return StByModeOf(g_dcMode, note, nullptr, 0, game, ours,
                      [&] { return StEndCompared("effects.decal_parts.destroy_description", game, ours); });
}
OPENSC4_THUNK(TDcDestroy, DcDestroyByMode, 0)

// Noted as the held effects' let-go (renderer.effects.hashes).
uint32_t __fastcall DcReleaseByMode(char* decal, void*)
{
    const auto game = [&] { return g_SC4EffectDecal_Release(decal, nullptr); };
    const auto ours = [&] { return ReleaseDecal(decal); };
    const StNote note = StMake(addr::SC4EffectAt4_ReleaseThunk, decal);
    if (g_dcMode == StepMode::Original)
        return StHelper(note, nullptr, 0, game);
    return StByModeOf(g_dcMode, note, nullptr, 0, game, ours, [&] {
        if (At<uint32_t>(decal, 4) == 1 && At<char*>(decal, 0x120))
            return game();
        return HsEndCompared("effects.decal_parts.release", {{decal, kDecalSize, false}}, true, game, ours);
    });
}
OPENSC4_THUNK(TDcRelease, DcReleaseByMode, 0)

bool HookDecalDescriptionAssign()
{
    return g_SC4DecalDescription_Assign ||
           Detour(addr::SC4DecalDescription_Assign, &TDcAssign, &g_SC4DecalDescription_Assign);
}

bool HookDecalDescriptionDestroy()
{
    return g_SC4DecalDescription_Destroy ||
           Detour(addr::SC4DecalDescription_Destroy, &TDcDestroy, &g_SC4DecalDescription_Destroy);
}

bool HookDecalRelease()
{
    return g_SC4EffectDecal_Release || Detour(addr::SC4EffectDecal_Release, &TDcRelease, &g_SC4EffectDecal_Release);
}

// The game's helpers, noted in a noted run.
uint32_t __fastcall AssignFloat3sNoted(char* self, void* edx, uint32_t from)
{
    return LsCopyNoted<&g_SC4Float3Vector_Assign, addr::SC4Float3Vector_Assign, 0xC>(self, edx, from);
}
OPENSC4_THUNK(TAssignFloat3sNoted, AssignFloat3sNoted, 4)

bool HookAssignFloat3s()
{
    return g_SC4Float3Vector_Assign ||
           Detour(addr::SC4Float3Vector_Assign, &TAssignFloat3sNoted, &g_SC4Float3Vector_Assign);
}

uint32_t __fastcall TextureReleaseNoted(char* self, void*)
{
    return StCallNoted(addr::SC4TextureBinding_Release, self, g_SC4TextureBinding_Release);
}
OPENSC4_THUNK(TTextureReleaseNoted, TextureReleaseNoted, 0)

char* __cdecl DcFillByMode(char* place, uint32_t count, char* const* value)
{
    if (g_dcMode == StepMode::Ours)
        return FillHeldPointers(place, count, value);
    char* const result = g_SC4EffectsManager_FillHeld(place, count, value);
    if (DcGameOnly() || !place || !count)
        return result;
    std::vector<char> mine(count * std::size_t{4});
    char* const end = FillHeldPointers(mine.data(), count, value);
    const bool same = end - mine.data() == result - place && std::memcmp(place, mine.data(), mine.size()) == 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (char* const object = At<char*>(mine.data(), i * 4))
            CallThis<uint32_t>(At<uint32_t>(At<char*>(object, 0), 8), object);
    }
    StRecord("effects.decal_parts.fill", same, Hex(place, 4), Hex(mine.data(), 4));
    return result;
}

uint32_t DcAddRefByMode(const char* id, char* self, uint32_t offset, Call0Fn game)
{
    if (g_dcMode == StepMode::Ours)
        return AddRefManagerFrom(self, offset);
    if (DcGameOnly())
        return game(self, nullptr);
    char* const manager = self - offset;
    const uint32_t before = At<uint32_t>(manager, 0x1B0);
    const uint32_t theirs = game(self, nullptr);
    const uint32_t after = At<uint32_t>(manager, 0x1B0);
    At<uint32_t>(manager, 0x1B0) = before;
    const uint32_t mine = AddRefManagerFrom(self, offset);
    const uint32_t own = At<uint32_t>(manager, 0x1B0);
    At<uint32_t>(manager, 0x1B0) = after;
    const uint32_t a[2] = {theirs, after}, b[2] = {mine, own};
    HmRecord(id, a, b, 8);
    return theirs;
}

uint32_t __fastcall DcAddRef4ByMode(char* self, void*)
{
    return DcAddRefByMode("effects.decal_parts.addref4", self, 4, g_SC4EffectsManager_AddRefThunk4);
}
OPENSC4_THUNK(TDcAddRef4, DcAddRef4ByMode, 0)

uint32_t __fastcall DcAddRef8ByMode(char* self, void*)
{
    return DcAddRefByMode("effects.decal_parts.addref8", self, 8, g_SC4EffectsManager_AddRefThunk8);
}
OPENSC4_THUNK(TDcAddRef8, DcAddRef8ByMode, 0)

bool InstallDecalParts(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !Detour(addr::SC4EffectDecal_SetParameter, &TDcSet, &g_SC4EffectDecal_SetParameter) ||
        !HookDecalDescriptionAssign() || !HookDecalDescriptionDestroy() || !HookDecalRelease() ||
        !DetourCdecl(addr::SC4EffectsManager_FillHeld, &DcFillByMode, &g_SC4EffectsManager_FillHeld) ||
        !Detour(addr::SC4EffectsManager_AddRefThunk4, &TDcAddRef4, &g_SC4EffectsManager_AddRefThunk4) ||
        !Detour(addr::SC4EffectsManager_AddRefThunk8, &TDcAddRef8, &g_SC4EffectsManager_AddRefThunk8))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookPoolAllocate() && HookFree() && HookDelete() && HookAssignFloats() &&
          HookAssignFloat3s() &&
          (g_SC4TextureBinding_Release ||
           Detour(addr::SC4TextureBinding_Release, &TTextureReleaseNoted, &g_SC4TextureBinding_Release))))
        return false;
    g_dcMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: a decal's small functions %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDecalParts = registry::Register({
    "renderer.effects.decal_parts",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDecal_SetParameter,
    "A decal's parameter setter and Release (its update is still the game's), its description's end and assignment, and the manager's fill of "
    "held pointers and two AddRef thunks; compare mode runs the game's and ours on what each may change, the "
    "assignment, end and Release the store's way, noted and replayed (docs/renderer.md, \"Water effects, sky and "
    "clouds\")",
    true,
    InstallDecalParts,
});

// The reader of the 0x40-byte counted object (0x005837C0), compared as
// the compound descriptions' readers are: the game's reads with the
// stream's reads noted, ours reads the same into a copy of the object as
// it was (an empty string and list of its own), and the two must hold the
// same word, text, floats, word and IDs and answer the same.
using Counted40ReadFn = uint32_t(__cdecl*)(uint32_t, uint32_t);
Counted40ReadFn g_SC4Counted40_Read = nullptr;
StepMode g_drMode = StepMode::Original;

uint32_t __cdecl ReadCounted40ByMode(uint32_t stream, uint32_t object)
{
    if (g_drMode == StepMode::Original || g_listGame || g_listReplay)
        return g_SC4Counted40_Read(stream, object);
    if (g_drMode == StepMode::Ours)
        return ReadCounted40(stream, object);
    return ModelCompared("effects.decal_read", stream, object, 0x40,
                         {{ModelField::Words, 8, 4},
                          {ModelField::String, 0xC, 0},
                          {ModelField::Words, 0x18, 0x1C},
                          {ModelField::Plain, 0x34, 0}},
                         g_SC4Counted40_Read, &ReadCounted40);
}

bool InstallDecalRead(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !Detour(addr::SC4Counted40_Read, reinterpret_cast<void (*)()>(&ReadCounted40ByMode), &g_SC4Counted40_Read))
        return false;
    g_drMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the counted objects' reading %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDecalRead = registry::Register({
    "renderer.effects.decal_read",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Counted40_Read,
    "The reader of the 0x40-byte counted objects of an effect description's list at +0x5C, from the effects file; "
    "compare mode notes what the game's reads from the stream and has ours read the same into a copy of the object "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallDecalRead,
});

// The store's comparison over more than one span of memory.
// A comparison whose game's run may change what ours would then read: with
// `same` false after the game's run ours is not replayed, only counted.
std::map<std::string, uint64_t> g_stSkipped;

template <typename Game, typename Ours>
uint32_t StSpansCompared(const char* id, const std::vector<std::pair<char*, std::size_t>>& spans, bool result,
                         Game&& game, Ours&& ours, const std::function<bool()>& same = {})
{
    StSaved saved;
    g_stGame.clear();
    g_stOurs.clear();
    std::vector<std::vector<char>> before, after, own;
    for (const auto& [at, size] : spans)
        before.emplace_back(at, at + size);
    g_stThread = GetCurrentThreadId();
    g_stDepth = 0;
    g_stReplay = false;
    g_stNoting = true;
    const uint32_t theirs = game();
    g_stNoting = false;
    for (std::size_t i = 0; i < spans.size(); ++i)
        after.emplace_back(spans[i].first, spans[i].first + spans[i].second);
    if (same && !same()) {
        const uint64_t n = ++g_stSkipped[id];
        if ((n & (n - 1)) == 0)
            log::Write("water effects: %s: %llu not compared (the game's run changed what ours reads)", id,
                       static_cast<unsigned long long>(n));
        return theirs;
    }
    for (std::size_t i = 0; i < spans.size(); ++i)
        std::memcpy(spans[i].first, before[i].data(), spans[i].second);
    g_stReplay = true;
    const uint32_t mine = ours();
    g_stReplay = false;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        own.emplace_back(spans[i].first, spans[i].first + spans[i].second);
        std::memcpy(spans[i].first, after[i].data(), spans[i].second);
    }
    if (!(g_stGame == g_stOurs)) {
        StRecord(id, false, StNotesText(g_stGame, g_stOurs), StNotesText(g_stOurs, g_stGame));
        return theirs;
    }
    for (std::size_t i = 0; i < spans.size(); ++i) {
        if (own[i] != after[i]) {
            char text[24];
            std::snprintf(text, sizeof text, "span %zu: ", i);
            StRecord(id, false, text + Hex(after[i].data(), spans[i].second),
                     text + Hex(own[i].data(), spans[i].second));
            return theirs;
        }
    }
    StRecord(id, !result || theirs == mine, Hex(&theirs, 4), Hex(&mine, 4));
    return theirs;
}

// One detour, shared with renderer.effects.tail: noted in a noted run
// whatever the mode (the 16 bytes of the range).
uint32_t __fastcall EqualRangeByMode(char* map, void*, uint32_t out, uint32_t key)
{
    return StHelper(StMake(addr::SC4NameMap_EqualRange, map, reinterpret_cast<char*>(out), reinterpret_cast<char*>(key)),
                    reinterpret_cast<char*>(out), 0x10, [&] { return EqualRangeRun(map, out, key); });
}
OPENSC4_THUNK(TSC4NameMap_EqualRange, EqualRangeByMode, 8)

bool HookNameEqualRange()
{
    return g_SC4NameMap_EqualRange ||
           Detour(addr::SC4NameMap_EqualRange, &TSC4NameMap_EqualRange, &g_SC4NameMap_EqualRange);
}

// --- Calls through other objects' tables, and the manager's tail ------------------------------
//
// Most of what is left of the manager calls through the tables of other
// objects (scripts, the renderer, the effects it starts), which no detour
// can note. A comparison can: the objects the function will call are
// proxied for its length, each given a copy of its table in which the
// slots that function uses go to a thunk. In the game's noted run the
// thunk notes the call (the function the slot held, the object, the words
// passed, and the bytes the call left at an out pointer) and makes it; in
// ours' replay it answers from the note and writes those bytes; else it
// only makes the call. An object a noted call hands back through its out
// pointer is proxied too, as the kind the slot says. The engine is the
// store's (StCompared), so the calls and what the function writes are
// compared as for the others. The copy keeps the word before the table
// (the class's description) and 64 slots.
struct VrKind;

struct VrOut {
    int arg;       // the word that is an out pointer, or -1
    uint32_t size; // the bytes noted there
};

struct VrSlot {
    uint32_t slot;
    int args;
    VrOut outs[2];
    const VrKind* outKind;    // an object handed back at the first out pointer
    const VrKind* resultKind; // an object answered in EAX
    int in;                   // a word pointing at bytes passed in, or -1
    uint32_t inSize;
    uint32_t thunk;
    int in2 = -1;             // a second such word
    uint32_t in2Size = 0;
    bool outThrough = false;  // the first out is behind the pointer the word points at
    void (*after)(const uint32_t* args, uint32_t result) = nullptr; // after the call, noted or replayed
};

struct VrKind {
    std::vector<VrSlot> slots;
};

constexpr std::size_t kVrSlots = 64;

// The copies of tables, one for each table and set of kinds, kept for good:
// an object the game's run made by copying a proxied one's bytes keeps
// its copy, which then only hands its calls on to the table it was made
// from.
struct VrClone {
    uint32_t original;
    std::array<uint32_t, kVrSlots + 1> table;
};
std::map<std::vector<uint32_t>, std::unique_ptr<VrClone>> g_vrClones;
std::unordered_map<uint32_t, uint32_t> g_vrCloneOriginal;
// Other threads reach the thunks too (the objects are the game's): the
// proxies and copies are looked at and changed under this lock, never held
// across a call.
std::recursive_mutex g_vrLock;

// The table behind a copy (or the table itself).
uint32_t VrOriginalOf(uint32_t table)
{
    std::lock_guard<std::recursive_mutex> lock(g_vrLock);
    const auto at = g_vrCloneOriginal.find(table);
    return at == g_vrCloneOriginal.end() ? table : at->second;
}

uint32_t VrCloneOf(uint32_t original, const std::vector<const VrKind*>& kinds)
{
    std::lock_guard<std::recursive_mutex> lock(g_vrLock);
    std::vector<uint32_t> key{original};
    for (const VrKind* kind : kinds)
        key.push_back(reinterpret_cast<uint32_t>(kind));
    auto& clone = g_vrClones[key];
    if (!clone) {
        clone = std::make_unique<VrClone>();
        clone->original = original;
        std::memcpy(clone->table.data(), reinterpret_cast<const void*>(original - 4), clone->table.size() * 4);
        for (const VrKind* kind : kinds) {
            for (const VrSlot& slot : kind->slots)
                clone->table[1 + slot.slot / 4] = slot.thunk;
        }
        g_vrCloneOriginal[reinterpret_cast<uint32_t>(clone->table.data() + 1)] = original;
    }
    return reinterpret_cast<uint32_t>(clone->table.data() + 1);
}

struct VrProxy {
    char* object;
    uint32_t original;
    uint32_t clone;
    std::vector<const VrKind*> kinds;
};

std::vector<std::unique_ptr<VrProxy>> g_vrProxies;
int g_vrScopes = 0;

VrProxy* VrFind(char* object)
{
    std::lock_guard<std::recursive_mutex> lock(g_vrLock);
    for (const auto& proxy : g_vrProxies) {
        if (proxy->object == object)
            return proxy.get();
    }
    return nullptr;
}

// An object proxied as a kind (one proxied before gets the kind's slots
// too); only inside a comparison's scope.
void VrProxyObject(char* object, const VrKind& kind)
{
    if (!object || !g_vrScopes)
        return;
    std::lock_guard<std::recursive_mutex> lock(g_vrLock);
    VrProxy* proxy = VrFind(object);
    if (!proxy) {
        auto made = std::make_unique<VrProxy>();
        made->object = object;
        made->original = VrOriginalOf(At<uint32_t>(object, 0));
        made->clone = At<uint32_t>(object, 0);
        proxy = made.get();
        g_vrProxies.push_back(std::move(made));
    }
    for (const VrKind* at : proxy->kinds) {
        if (at == &kind)
            return;
    }
    proxy->kinds.push_back(&kind);
    if (At<uint32_t>(object, 0) != proxy->clone)
        return;
    proxy->clone = VrCloneOf(proxy->original, proxy->kinds);
    At<uint32_t>(object, 0) = proxy->clone;
}

// The proxies of one comparison, taken off at its end (an object freed or
// remade in the game's run no longer holds its copy).
struct VrScope {
    std::size_t first = g_vrProxies.size();
    VrScope()
    {
        ++g_vrScopes;
    }
    ~VrScope()
    {
        std::lock_guard<std::recursive_mutex> lock(g_vrLock);
        while (g_vrProxies.size() > first) {
            VrProxy& proxy = *g_vrProxies.back();
            if (At<uint32_t>(proxy.object, 0) == proxy.clone)
                At<uint32_t>(proxy.object, 0) = proxy.original;
            g_vrProxies.pop_back();
        }
        --g_vrScopes;
    }
};

// A call the game's makes, or ours in the replay, with the bytes it leaves
// in some buffers (out pointers, or a double it answers) noted or written
// back.
using VrBuffers = std::vector<std::pair<char*, uint32_t>>;

template <typename Call>
uint32_t VrNotedInto(StNote note, const VrBuffers& outs, Call&& call)
{
    if (g_stReplay && g_stDepth == 0 && GetCurrentThreadId() == g_stThread) {
        const std::size_t at = g_stOurs.size();
        const uint32_t result = StReplayed(std::move(note), nullptr);
        if (at < g_stGame.size() && g_stGame[at] == g_stOurs[at]) {
            const std::vector<char>& bytes = g_stGame[at].out;
            std::size_t from = 0;
            for (const auto& [place, size] : outs) {
                if (place && from + size <= bytes.size())
                    std::memcpy(place, bytes.data() + from, size);
                from += place ? size : 0;
            }
        }
        return result;
    }
    if (!StNoting())
        return call();
    const uint32_t result = StNoted(std::move(note), nullptr, 0, call);
    std::vector<char>& bytes = g_stGame.back().out;
    for (const auto& [place, size] : outs) {
        if (place)
            bytes.insert(bytes.end(), place, place + size);
    }
    return result;
}

template <typename Call>
uint32_t VrNoted(StNote note, const VrOut* outs, const uint32_t* args, Call&& call)
{
    VrBuffers buffers;
    for (int i = 0; i < 2; ++i)
        buffers.push_back({outs[i].arg >= 0 ? Ptr(args[outs[i].arg]) : nullptr, outs[i].size});
    return VrNotedInto(std::move(note), buffers, call);
}

uint32_t VrDispatch(char* self, uint32_t slot, const uint32_t* args, const std::function<uint32_t(uint32_t)>& call,
                    char* value = nullptr)
{
    uint32_t function = 0;
    const VrSlot* desc = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(g_vrLock);
        VrProxy* const proxy = GetCurrentThreadId() == g_stThread ? VrFind(self) : nullptr;
        function =
            At<uint32_t>(reinterpret_cast<char*>(proxy ? proxy->original : VrOriginalOf(At<uint32_t>(self, 0))), slot);
        if (proxy) {
            for (const VrKind* kind : proxy->kinds) {
                for (const VrSlot& at : kind->slots) {
                    if (at.slot == slot)
                        desc = &at;
                }
            }
        }
    }
    if (!desc)
        return call(function);
    uint32_t rest = desc->args > 2 ? StArg(Ptr(args[2])) : 0;
    for (int i = 3; i < desc->args; ++i)
        rest = (rest ^ StArg(Ptr(args[i]))) * 16777619u;
    for (const auto& [word, size] : {std::pair<int, uint32_t>{desc->in, desc->inSize}, {desc->in2, desc->in2Size}}) {
        if (word >= 0 && args[word]) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(args[word]);
            for (uint32_t i = 0; i < size; ++i)
                rest = (rest ^ bytes[i]) * 16777619u;
        }
    }
    VrBuffers buffers;
    if (value)
        buffers.push_back({value, 8});
    for (const VrOut& at : desc->outs) {
        char* place = at.arg >= 0 ? Ptr(args[at.arg]) : nullptr;
        if (place && desc->outThrough && &at == &desc->outs[0])
            place = At<char*>(place, 0);
        buffers.push_back({place, at.size});
    }
    const uint32_t result = VrNotedInto(StMake(function, self, desc->args > 0 ? Ptr(args[0]) : nullptr,
                                               desc->args > 1 ? Ptr(args[1]) : nullptr, rest),
                                        buffers, [&] { return call(function); });
    if (desc->outKind && desc->outs[0].arg >= 0)
        VrProxyObject(At<char*>(Ptr(args[desc->outs[0].arg]), 0), *desc->outKind);
    if (desc->resultKind)
        VrProxyObject(Ptr(result), *desc->resultKind);
    if (desc->after)
        desc->after(args, result);
    return result;
}

template <uint32_t Slot>
uint32_t __fastcall VrThunk0(char* self, void*)
{
    const uint32_t args[1] = {};
    return VrDispatch(self, Slot, args, [&](uint32_t f) { return reinterpret_cast<Call0Fn>(f)(self, nullptr); });
}

// A slot answering on the x87 stack: the double kept in a buffer.
template <uint32_t Slot>
double __fastcall VrThunk0D(char* self, void*)
{
    double value = 0;
    const uint32_t args[1] = {};
    VrDispatch(
        self, Slot, args,
        [&](uint32_t f) {
            value = reinterpret_cast<double(__fastcall*)(char*, void*)>(f)(self, nullptr);
            return 0u;
        },
        reinterpret_cast<char*>(&value));
    return value;
}

template <uint32_t Slot>
uint32_t __fastcall VrThunk1(char* self, void*, uint32_t a)
{
    const uint32_t args[1] = {a};
    return VrDispatch(self, Slot, args, [&](uint32_t f) { return reinterpret_cast<Call1Fn>(f)(self, nullptr, a); });
}

template <uint32_t Slot>
uint32_t __fastcall VrThunk2(char* self, void*, uint32_t a, uint32_t b)
{
    const uint32_t args[2] = {a, b};
    return VrDispatch(self, Slot, args,
                      [&](uint32_t f) { return reinterpret_cast<Call2Fn>(f)(self, nullptr, a, b); });
}

template <uint32_t Slot>
uint32_t __fastcall VrThunk3(char* self, void*, uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t args[3] = {a, b, c};
    return VrDispatch(self, Slot, args,
                      [&](uint32_t f) { return reinterpret_cast<Call3Fn>(f)(self, nullptr, a, b, c); });
}

template <uint32_t Slot>
uint32_t __fastcall VrThunk4(char* self, void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    const uint32_t args[4] = {a, b, c, d};
    using Fn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t, uint32_t);
    return VrDispatch(self, Slot, args, [&](uint32_t f) { return reinterpret_cast<Fn>(f)(self, nullptr, a, b, c, d); });
}

template <uint32_t Slot>
uint32_t __fastcall VrThunk5(char* self, void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    const uint32_t args[5] = {a, b, c, d, e};
    using Fn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    return VrDispatch(self, Slot, args,
                      [&](uint32_t f) { return reinterpret_cast<Fn>(f)(self, nullptr, a, b, c, d, e); });
}

template <uint32_t Slot>
uint32_t __fastcall VrThunk6(char* self, void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t g)
{
    const uint32_t args[6] = {a, b, c, d, e, g};
    using Fn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    return VrDispatch(self, Slot, args,
                      [&](uint32_t f) { return reinterpret_cast<Fn>(f)(self, nullptr, a, b, c, d, e, g); });
}

#define VR_SLOT(n, slot) reinterpret_cast<uint32_t>(&VrThunk##n<slot>)
constexpr VrOut kVrNoOut[2] = {{-1, 0}, {-1, 0}};

// A slot of a kind: its words, out pointers, kinds handed back, bytes in.
VrSlot VrPlain(uint32_t slot, int args, uint32_t thunk)
{
    return {slot, args, {{-1, 0}, {-1, 0}}, nullptr, nullptr, -1, 0, thunk};
}

// The kinds the tail's comparisons proxy.
const VrKind kVrUnknown{{VrPlain(8, 0, VR_SLOT(0, 8))}};
const VrKind kVrQueried{{{0, 2, {{1, 4}, {-1, 0}}, &kVrUnknown, nullptr, -1, 0, VR_SLOT(2, 0)}}};
const VrKind kVrScriptUnregister{{VrPlain(0x1C, 1, VR_SLOT(1, 0x1C))}};
const VrKind kVrScriptRegister{
    {VrPlain(0x54, 0, VR_SLOT(0, 0x54)), VrPlain(0x24, 1, VR_SLOT(1, 0x24)), VrPlain(0xC, 6, VR_SLOT(6, 0xC))}};
const VrKind kVrCompound{{VrPlain(0x20, 1, VR_SLOT(1, 0x20))}};
const VrKind kVrStarted{{VrPlain(0x20, 1, VR_SLOT(1, 0x20)), VrPlain(0x1C, 1, VR_SLOT(1, 0x1C)),
                         VrPlain(0xC, 1, VR_SLOT(1, 0xC)), VrPlain(8, 0, VR_SLOT(0, 8))}};
const VrKind kVrStarter{{{0x1C, 2, {{1, 4}, {-1, 0}}, &kVrStarted, nullptr, -1, 0, VR_SLOT(2, 0x1C)}}};
const VrKind kVrRenderer{{VrPlain(0xB0, 1, VR_SLOT(1, 0xB0))}};
const VrKind kVrOccupantType{{VrPlain(0xC, 1, VR_SLOT(1, 0xC))}};
const VrKind kVrOccupant{{{0x18, 0, {{-1, 0}, {-1, 0}}, nullptr, &kVrOccupantType, -1, 0, VR_SLOT(0, 0x18)},
                          {0x28, 2, {{0, 0xC}, {1, 0xC}}, nullptr, nullptr, -1, 0, VR_SLOT(2, 0x28)},
                          {0x2C, 1, {{0, 0x10}, {-1, 0}}, nullptr, nullptr, -1, 0, VR_SLOT(1, 0x2C)}}};
const VrKind kVrMessageServer{{VrPlain(0x18, 2, VR_SLOT(2, 0x18))}};
const VrKind kVrMessageListen{{VrPlain(0x14, 2, VR_SLOT(2, 0x14))}};
// The occupants a query put in its list (nodes of next, previous, object),
// proxied for their let-go.
void VrProxyListed(const uint32_t* args, uint32_t)
{
    char* const holder = At<char*>(Ptr(args[0]), 0);
    for (char* node = holder ? At<char*>(holder, 0) : nullptr; node && node != holder; node = At<char*>(node, 0))
        VrProxyObject(At<char*>(node, 8), kVrUnknown);
}

const VrKind kVrClearedMap{{{0x2C, 2, {{-1, 0}, {-1, 0}}, nullptr, nullptr, 1, 0x10, VR_SLOT(2, 0x2C)}}};
const VrKind kVrNodeAllocator{{VrPlain(0xC, 1, VR_SLOT(1, 0xC))}};
const VrKind kVrOccupantQuery{{{0xA4, 5, {{0, 8}, {-1, 0}}, nullptr, nullptr, 1, 8, VR_SLOT(5, 0xA4), 2, 8, true,
                                VrProxyListed}}};
// The options' objects.
const VrKind kVrAsked{{VrPlain(4, 0, VR_SLOT(0, 4)), VrPlain(8, 0, VR_SLOT(0, 8))}};
const VrKind kVrAskedSource{{{0, 2, {{1, 4}, {-1, 0}}, &kVrAsked, nullptr, -1, 0, VR_SLOT(2, 0)}}};
void VrProxyOwnerSource(const uint32_t*, uint32_t result)
{
    if (result)
        VrProxyObject(At<char*>(Ptr(result), 0x30), kVrAskedSource);
}
const VrKind kVrOptionsHost{{{0x28, 0, {{-1, 0}, {-1, 0}}, nullptr, nullptr, -1, 0, VR_SLOT(0, 0x28), -1, 0, false,
                              VrProxyOwnerSource},
                             VrPlain(0x50, 0, VR_SLOT(0, 0x50))}};
const VrKind kVrOptionsService{{VrPlain(0x44, 1, VR_SLOT(1, 0x44))}};
const VrKind kVrDetail{{VrPlain(0x38, 1, VR_SLOT(1, 0x38)), VrPlain(0x24, 1, VR_SLOT(1, 0x24)),
                        VrPlain(0x28, 1, VR_SLOT(1, 0x28))}};
const VrKind kVrDetailFramework{{{0x14, 3, {{2, 4}, {-1, 0}}, &kVrDetail, nullptr, -1, 0, VR_SLOT(3, 0x14)}}};
const VrKind kVrHeldEffect{{VrPlain(0x10, 1, VR_SLOT(1, 0x10)), VrPlain(8, 0, VR_SLOT(0, 8))}};
const VrKind kVrLiftedMap{{{0x28, 4, {{-1, 0}, {-1, 0}}, nullptr, nullptr, 1, 0x10, VR_SLOT(4, 0x28)}}};

// What ours calls besides the proxied tables: in the replay the note.
StepMode g_tlMode = StepMode::Original;
using TlQueryHeldFn = char**(__cdecl*)(char**, char*, uint32_t, uint32_t);
using TlPropertyFn = uint32_t(__cdecl*)(char*, uint32_t, uint8_t*);
TlQueryHeldFn g_SC4_QueryHeld = nullptr;
TlPropertyFn g_SC4_GetPropertyBool = nullptr;

void TlQueryHeld(char** out, char* object, uint32_t iid, uint32_t unused)
{
    const uint32_t args[4] = {Word(out), Word(object), iid, unused};
    const VrOut outs[2] = {{0, 4}, {-1, 0}};
    VrNoted(StMake(addr::SC4_QueryHeld, out, object, Ptr(iid), unused), outs, args, [&] {
        return Word(reinterpret_cast<TlQueryHeldFn>(addr::SC4_QueryHeld)(out, object, iid, unused));
    });
    VrProxyObject(*out, kVrUnknown);
}

uint32_t TlGetPropertyBool(char* object, uint32_t id, uint8_t* out)
{
    const uint32_t args[3] = {Word(object), id, Word(out)};
    const VrOut outs[2] = {{2, 1}, {-1, 0}};
    return VrNoted(StMake(addr::SC4_GetPropertyBool, object, Ptr(id), Ptr(Word(out))), outs, args, [&] {
               return reinterpret_cast<TlPropertyFn>(addr::SC4_GetPropertyBool)(object, id, out);
           }) & 0xFF;
}

uint32_t OccupantLifts(char* occupant);
void LiftUnderOccupant(char* manager, char* occupant);

// The name check's and the stop's helpers: the game's, called through
// these so that the replay answers them from the notes.
Call2Fn g_SC4StringBody_AssignChars = nullptr;
Call1Fn g_SC4StringHashSet_Find = nullptr;
Call0Fn g_SC4Uint32HashSet_Clear = nullptr;

void TlAssignChars(char* text, const char* first, const char* last)
{
    VrNotedInto(StMake(addr::SC4StringBody_AssignChars, text, Ptr(Word(first)), Ptr(Word(last))), {{text, 0xC}}, [&] {
        return CallThis<uint32_t>(addr::SC4StringBody_AssignChars, text, first, last);
    });
}

void TlLower(char* first, char* last)
{
    const uint32_t lower = 0x009F0543; // CRT::tolower
    VrNotedInto(StMake(addr::SC4_TransformChars, first, last, first, lower), {}, [&] {
        return reinterpret_cast<uint32_t(__cdecl*)(char*, char*, char*, uint32_t)>(addr::SC4_TransformChars)(
            first, last, first, lower);
    });
}

uint32_t TlFindName(char* hash, char* text)
{
    return VrNotedInto(StMake(addr::SC4StringHashSet_Find, hash, text), {},
                       [&] { return CallThis<uint32_t>(addr::SC4StringHashSet_Find, hash, text); });
}

void TlClearHash(char* hash)
{
    VrNotedInto(StMake(addr::SC4Uint32HashSet_Clear, hash), {{hash, 0x14}},
                [&] { return CallThis<uint32_t>(addr::SC4Uint32HashSet_Clear, hash); });
}

Call1Fn g_SC4Uint32HashMap_Lookup = nullptr;

void TlEqualRange(char* map, char* out, char* key)
{
    VrNotedInto(StMake(addr::SC4NameMap_EqualRange, map, out, key), {{out, 0x10}},
                [&] { return CallThis<uint32_t>(addr::SC4NameMap_EqualRange, map, out, key); });
}

void TlGrowPairs(char* table, uint32_t count)
{
    VrNotedInto(StMake(addr::SC4PairHash_Grow, table, Ptr(count)), {{table, 0x14}},
                [&] { return CallThis<uint32_t>(addr::SC4PairHash_Grow, table, count); });
}

// The insert's note keeps only its out bytes; in a replay the count it
// raises when it puts the pair in (out + 8) is raised here.
void TlInsertPair(char* table, char* out, uint32_t* pair)
{
    VrNotedInto(StMake(addr::SC4PairHash_InsertUnique, table, out, Ptr(Word(pair)), PairBytesOf(Word(pair))),
                {{out, 0xC}},
                [&] { return CallThis<uint32_t>(addr::SC4PairHash_InsertUnique, table, out, pair); });
    if (g_stReplay && g_stDepth == 0 && GetCurrentThreadId() == g_stThread && At<uint8_t>(out, 8))
        ++At<uint32_t>(table, 0x10);
}

uint32_t* TlLookup(char* map, char* key)
{
    return reinterpret_cast<uint32_t*>(VrNotedInto(StMake(addr::SC4Uint32HashMap_Lookup, map, key), {{map, 0x14}},
                                                   [&] { return CallThis<uint32_t>(addr::SC4Uint32HashMap_Lookup, map, key); }));
}

void TlReleaseRef(char** holder)
{
    CallThis<uint32_t>(addr::SC4_ReleaseRef, holder);
}

// The list's end: in the game's noted run of the occupant's going away
// it is noted and kept until the comparison is done (ours reads the list
// again), then made on a copy of the holder.
std::vector<char*>* g_tlKeptLists = nullptr;
Call0Fn g_SC4List_Destroy2 = nullptr;

void TlDestroyList(char** holder)
{
    VrNotedInto(StMake(addr::SC4List_Destroy2, holder), {},
                [&] { return CallThis<uint32_t>(addr::SC4List_Destroy2, holder); });
}

bool TlStNoting()
{
    return StNoting();
}

uint32_t TlNoted1(uint32_t target, char* self, uint32_t word, uint32_t (*run)(char*, uint32_t))
{
    return StNoted(StMake(target, self, Ptr(word)), nullptr, 0, [&] { return run(self, word); });
}

uint32_t TlNoted0(uint32_t target, char* self, uint32_t (*run)(char*))
{
    return StNoted(StMake(target, self), nullptr, 0, [&] { return run(self); });
}

void TlSetObject(char* effect, char* object)
{
    VrNotedInto(StMake(addr::SC4Effect_SetObject, effect, object), {},
                [&] { return CallThis<uint32_t>(addr::SC4Effect_SetObject, effect, object); });
}

void TlRestartInUse(char* manager)
{
    VrNotedInto(StMake(addr::SC4EffectsManager_RestartInUse, manager), {},
                [&] { return CallThis<uint32_t>(addr::SC4EffectsManager_RestartInUse, manager); });
}

void TlDestructHolder(char** holder)
{
    VrNotedInto(StMake(addr::SC4RefPtr_Destruct, holder), {},
                [&] { return CallThis<uint32_t>(addr::SC4RefPtr_Destruct, holder); });
}

// The manager's memory: operator new, which the pool allocator (noted in
// a noted run) gives; in a replay its note.
char* TlNew(uint32_t size)
{
    if (g_stReplay && g_stDepth == 0 && GetCurrentThreadId() == g_stThread)
        return Ptr(StReplayed({addr::SC4_PoolAllocate, size, 0, 0, 0, 0, {}}, nullptr));
    return reinterpret_cast<char*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(size);
}

char* TlConstructManager(char* memory)
{
    return Ptr(VrNotedInto(StMake(addr::SC4EffectsManager_Construct, memory), {},
                           [&] { return CallThis<uint32_t>(addr::SC4EffectsManager_Construct, memory); }));
}

uint32_t TlClearTexts(char* tables)
{
    return VrNotedInto(StMake(addr::SC4EffectsManager_ClearTexts, tables), {{tables + 0xD0, 0x34}},
                       [&] { return CallThis<uint32_t>(addr::SC4EffectsManager_ClearTexts, tables); });
}

uint32_t TlOccupantLifts(char* occupant)
{
    if (g_stReplay)
        return StReplayed(StMake(addr::SC4EffectsManager_OccupantLifts, occupant), nullptr) & 0xFF;
    return OccupantLifts(occupant);
}

void TlLiftUnderOccupant(char* manager, char* occupant)
{
    if (g_stReplay)
        StReplayed(StMake(addr::SC4EffectsManager_LiftUnderOccupant, manager, occupant), nullptr);
    else
        CallThis<void>(addr::SC4EffectsManager_LiftUnderOccupant, manager, occupant);
}

#include "core/effects_tail.h"

using TlRegisterFn = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, char*, uint32_t, uint32_t, char*);
using TlUnregisterFn = uint32_t(__cdecl*)(char*, char*);
TlRegisterFn g_SC4Script_RegisterFunction = nullptr;
TlUnregisterFn g_SC4Script_UnregisterTable = nullptr;
Call1Fn g_SC4EffectsManager_StartFollowUp = nullptr;
Call0Fn g_SC4EffectsManager_PlaceCompounds = nullptr, g_SC4EffectsManager_ReleaseThunk4 = nullptr,
        g_SC4EffectsManager_ReleaseThunk8 = nullptr;

// By the group's mode; the comparison with the objects proxied first.
template <typename Game, typename Ours, typename Proxy>
uint32_t TlByMode(const char* id, uint32_t target, char* region, std::size_t size, bool result, Game&& game,
                  Ours&& ours, Proxy&& proxy)
{
    return StByModeOf(g_tlMode, StMake(target, region), nullptr, 0, game, ours, [&] {
        VrScope scope;
        proxy();
        return StCompared(id, region, size, result, game, ours);
    });
}

uint32_t __cdecl TlRegisterByMode(uint32_t a, uint32_t b, uint32_t c, char* object, uint32_t flags, uint32_t f,
                                  char* script)
{
    const auto game = [&] { return g_SC4Script_RegisterFunction(a, b, c, object, flags, f, script); };
    const auto ours = [&] { return RegisterScriptFunction(a, b, c, object, flags, f, script); };
    return TlByMode("effects.tail.register", addr::SC4Script_RegisterFunction, nullptr, 0, true, game, ours, [&] {
        VrProxyObject(script, kVrScriptRegister);
        VrProxyObject(object, kVrQueried);
    });
}

uint32_t __cdecl TlUnregisterByMode(char* object, char* script)
{
    const auto game = [&] { return g_SC4Script_UnregisterTable(object, script) & 0xFF; };
    const auto ours = [&] { return UnregisterScriptTable(object, script); };
    return TlByMode("effects.tail.unregister", addr::SC4Script_UnregisterTable, nullptr, 0, true, game, ours, [&] {
        VrProxyObject(script, kVrScriptUnregister);
        VrProxyObject(object, kVrQueried);
    });
}

uint32_t __fastcall TlFollowUpByMode(char* manager, void*, uint32_t effect)
{
    const auto game = [&] { return g_SC4EffectsManager_StartFollowUp(manager, nullptr, effect); };
    const auto ours = [&] {
        StartFollowUp(manager, Ptr(effect));
        return 0u;
    };
    return TlByMode("effects.tail.follow_up", addr::SC4EffectsManager_StartFollowUp, Ptr(effect) + 0x144, 4, false,
                    game, ours, [&] {
                        VrProxyObject(M<char*>(0x00B43DD0), kVrRenderer);
                        VrProxyObject(manager, kVrStarter);
                    });
}
OPENSC4_THUNK(TTlFollowUp, TlFollowUpByMode, 4)

uint32_t __fastcall TlPlaceByMode(char* manager, void*)
{
    const auto game = [&] { return g_SC4EffectsManager_PlaceCompounds(manager, nullptr); };
    const auto ours = [&] {
        PlaceCompounds(manager);
        return 0u;
    };
    return TlByMode("effects.tail.place", addr::SC4EffectsManager_PlaceCompounds, nullptr, 0, false, game, ours,
                    [&] {
                        for (uint32_t* at = At<uint32_t*>(manager, 0xBAC); at != At<uint32_t*>(manager, 0xBB0); ++at)
                            VrProxyObject(At<char**>(manager, 0xB94)[*at], kVrCompound);
                    });
}
OPENSC4_THUNK(TTlPlace, TlPlaceByMode, 0)

// The two Release thunks only answer 1: the game's and ours run, the
// answers compared.
uint32_t TlReleaseByMode(const char* id, char* self, uint32_t offset, Call0Fn game)
{
    if (g_tlMode == StepMode::Ours)
        return ReleaseManagerFrom(self, offset);
    const uint32_t theirs = game(self, nullptr);
    if (g_tlMode == StepMode::Original || g_stNoting || g_stReplay)
        return theirs;
    const uint32_t mine = ReleaseManagerFrom(self, offset);
    StRecord(id, theirs == mine, Hex(&theirs, 4), Hex(&mine, 4));
    return theirs;
}

uint32_t __fastcall TlRelease4ByMode(char* self, void*)
{
    return TlReleaseByMode("effects.tail.release4", self, 4, g_SC4EffectsManager_ReleaseThunk4);
}
OPENSC4_THUNK(TTlRelease4, TlRelease4ByMode, 0)

uint32_t __fastcall TlRelease8ByMode(char* self, void*)
{
    return TlReleaseByMode("effects.tail.release8", self, 8, g_SC4EffectsManager_ReleaseThunk8);
}
OPENSC4_THUNK(TTlRelease8, TlRelease8ByMode, 0)

// The game's helpers the occupant's check calls, noted in a noted run
// (with the bytes they leave at their out pointer); the held unknown
// proxied for its let-go.
char** __cdecl TlQueryHeldNoted(char** out, char* object, uint32_t iid, uint32_t unused)
{
    const uint32_t args[4] = {Word(out), Word(object), iid, unused};
    const VrOut outs[2] = {{0, 4}, {-1, 0}};
    const uint32_t result = VrNoted(StMake(addr::SC4_QueryHeld, out, object, Ptr(iid), unused), outs, args,
                                    [&] { return Word(g_SC4_QueryHeld(out, object, iid, unused)); });
    if (GetCurrentThreadId() == g_stThread)
        VrProxyObject(*out, kVrUnknown);
    return reinterpret_cast<char**>(result);
}

uint32_t __cdecl TlGetPropertyBoolNoted(char* object, uint32_t id, uint8_t* out)
{
    const uint32_t args[3] = {Word(object), id, Word(out)};
    const VrOut outs[2] = {{2, 1}, {-1, 0}};
    return VrNoted(StMake(addr::SC4_GetPropertyBool, object, Ptr(id), Ptr(Word(out))), outs, args,
                   [&] { return g_SC4_GetPropertyBool(object, id, out); });
}

// The occupant's check takes its occupant in EDI (an LTCG convention): a
// naked thunk hands it on, and the game's is called with EDI set.
using TlEdiFn = void (*)();
TlEdiFn g_SC4EffectsManager_OccupantLifts = nullptr;

__declspec(naked) uint32_t __cdecl TlCallWithEdi(uint32_t function, char* occupant)
{
    __asm {
        push edi
        mov edi, [esp + 12]
        mov eax, [esp + 8]
        call eax
        pop edi
        ret
    }
}

uint32_t __cdecl TlOccupantByMode(char* occupant)
{
    const auto game = [&] {
        return TlCallWithEdi(reinterpret_cast<uint32_t>(g_SC4EffectsManager_OccupantLifts), occupant) & 0xFF;
    };
    const auto ours = [&] { return OccupantLifts(occupant); };
    return TlByMode("effects.tail.occupant", addr::SC4EffectsManager_OccupantLifts, occupant, 0, true, game, ours,
                    [&] { VrProxyObject(occupant, kVrOccupant); });
}

__declspec(naked) void TTlOccupant()
{
    __asm {
        push ecx
        push edx
        push edi
        call TlOccupantByMode
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

Call1Fn g_SC4EffectsManager_LiftUnderOccupant = nullptr;

uint32_t __fastcall TlLiftByMode(char* manager, void*, uint32_t occupant)
{
    const auto game = [&] { return g_SC4EffectsManager_LiftUnderOccupant(manager, nullptr, occupant); };
    const auto ours = [&] {
        LiftUnderOccupant(manager, Ptr(occupant));
        return 0u;
    };
    return StByModeOf(g_tlMode, StMake(addr::SC4EffectsManager_LiftUnderOccupant, manager, Ptr(occupant)), nullptr,
                      0, game, ours, [&] {
                          VrScope scope;
                          VrProxyObject(Ptr(occupant), kVrOccupant);
                          VrProxyObject(At<char*>(manager, 0xD98), kVrLiftedMap);
                          return StCompared("effects.tail.lift", manager + 0xDA0, 4, false, game, ours);
                      });
}
OPENSC4_THUNK(TTlLift, TlLiftByMode, 4)

using TlMessageFn = uint32_t(__cdecl*)(char*, char*);
TlMessageFn g_SC4EffectsManager_LiftMessage = nullptr;

uint32_t __cdecl TlLiftMessageByMode(char* occupant, char* manager)
{
    const auto game = [&] { return g_SC4EffectsManager_LiftMessage(occupant, manager) & 0xFF; };
    const auto ours = [&] { return LiftUnderOccupantMessage(occupant, manager); };
    return TlByMode("effects.tail.lift_message", addr::SC4EffectsManager_LiftMessage, nullptr, 0, true, game, ours,
                    [] {});
}

// The helpers' noted detours (compare mode), the bytes they leave noted.
uint32_t __fastcall TlAssignCharsNoted(char* text, void*, uint32_t first, uint32_t last)
{
    return VrNotedInto(StMake(addr::SC4StringBody_AssignChars, text, Ptr(first), Ptr(last)), {{text, 0xC}},
                       [&] { return g_SC4StringBody_AssignChars(text, nullptr, first, last); });
}
OPENSC4_THUNK(TTlAssignCharsNoted, TlAssignCharsNoted, 8)

uint32_t __fastcall TlFindNameNoted(char* hash, void*, uint32_t text)
{
    return VrNotedInto(StMake(addr::SC4StringHashSet_Find, hash, Ptr(text)), {},
                       [&] { return g_SC4StringHashSet_Find(hash, nullptr, text); });
}
OPENSC4_THUNK(TTlFindNameNoted, TlFindNameNoted, 4)

uint32_t __fastcall TlClearHashNoted(char* hash, void*)
{
    return VrNotedInto(StMake(addr::SC4Uint32HashSet_Clear, hash), {{hash, 0x14}},
                       [&] { return g_SC4Uint32HashSet_Clear(hash, nullptr); });
}
OPENSC4_THUNK(TTlClearHashNoted, TlClearHashNoted, 0)

uint32_t __fastcall TlLookupNoted(char* map, void*, uint32_t key)
{
    return VrNotedInto(StMake(addr::SC4Uint32HashMap_Lookup, map, Ptr(key)), {{map, 0x14}},
                       [&] { return g_SC4Uint32HashMap_Lookup(map, nullptr, key); });
}
OPENSC4_THUNK(TTlLookupNoted, TlLookupNoted, 4)

uint32_t __fastcall TlDestroyListNoted(char* holder, void*)
{
    if (g_tlKeptLists && StNoting()) {
        g_stGame.push_back(StMake(addr::SC4List_Destroy2, holder));
        g_tlKeptLists->push_back(At<char*>(holder, 0));
        return 0;
    }
    return g_SC4List_Destroy2(holder, nullptr);
}
OPENSC4_THUNK(TTlDestroyListNoted, TlDestroyListNoted, 0)

Call1Fn g_SC4EffectsManager_RelieveUnderOccupant = nullptr;

// A comparison with no allocator yet runs the game's only (its making is
// not compared).
uint32_t __fastcall TlRelieveByMode(char* manager, void*, uint32_t occupant)
{
    const auto game = [&] { return g_SC4EffectsManager_RelieveUnderOccupant(manager, nullptr, occupant); };
    const auto ours = [&] {
        RelieveUnderOccupant(manager, Ptr(occupant));
        return 0u;
    };
    return StByModeOf(g_tlMode, StMake(addr::SC4EffectsManager_RelieveUnderOccupant, manager, Ptr(occupant)), nullptr,
                      0, game, ours, [&] {
                          if (!M<char*>(0x00B207E4))
                              return game();
                          std::vector<char*> kept;
                          auto* const outer = g_tlKeptLists;
                          g_tlKeptLists = &kept;
                          uint32_t result = 0;
                          {
                              VrScope scope;
                              VrProxyObject(Ptr(occupant), kVrOccupant);
                              VrProxyObject(At<char*>(manager, 0xD98), kVrClearedMap);
                              VrProxyObject(M<char*>(0x00B207E4), kVrNodeAllocator);
                              VrProxyObject(M<char*>(0x00B43D0C), kVrOccupantQuery);
                              result = StSpansCompared("effects.tail.relieve", {}, false, game, ours);
                          }
                          g_tlKeptLists = outer;
                          for (char* list : kept)
                              g_SC4List_Destroy2(reinterpret_cast<char*>(&list), nullptr);
                          return result;
                      });
}
OPENSC4_THUNK(TTlRelieve, TlRelieveByMode, 4)

Call0Fn g_SC4EffectsManager_Construct = nullptr, g_SC4RefPtr_Destruct = nullptr,
        g_SC4EffectsManager_ApplyOptions = nullptr;
using TlMakeFn = char*(__cdecl*)();
TlMakeFn g_SC4EffectsManager_Make = nullptr;

uint32_t __fastcall TlConstructNoted(char* memory, void*)
{
    return VrNotedInto(StMake(addr::SC4EffectsManager_Construct, memory), {},
                       [&] { return g_SC4EffectsManager_Construct(memory, nullptr); });
}
OPENSC4_THUNK(TTlConstructNoted, TlConstructNoted, 0)

uint32_t __fastcall TlDestructHolderNoted(char* holder, void*)
{
    return VrNotedInto(StMake(addr::SC4RefPtr_Destruct, holder), {},
                       [&] { return g_SC4RefPtr_Destruct(holder, nullptr); });
}
OPENSC4_THUNK(TTlDestructHolderNoted, TlDestructHolderNoted, 0)

char* __cdecl TlMakeByMode()
{
    const auto game = [&] { return Word(g_SC4EffectsManager_Make()); };
    const auto ours = [&] { return Word(MakeEffectsManager()); };
    return Ptr(StByModeOf(g_tlMode, StMake(addr::SC4EffectsManager_Make, nullptr), nullptr, 0, game, ours,
                          [&] { return StSpansCompared("effects.tail.make", {}, true, game, ours); }));
}

uint32_t __fastcall TlOptionsByMode(char* manager, void*)
{
    const auto game = [&] { return g_SC4EffectsManager_ApplyOptions(manager, nullptr); };
    const auto ours = [&] {
        ApplyEffectOptions(manager);
        return 0u;
    };
    return StByModeOf(g_tlMode, StMake(addr::SC4EffectsManager_ApplyOptions, manager), nullptr, 0, game, ours, [&] {
        VrScope scope;
        VrProxyObject(M<char*>(0x00B43CA0), kVrOptionsService);
        VrProxyObject(At<char*>(manager, 0xDE0), kVrOptionsHost);
        VrProxyObject(reinterpret_cast<char*(__cdecl*)()>(addr::RZGetFramework)(), kVrDetailFramework);
        VrProxyObject(manager, kVrStarter);
        VrProxyObject(At<char*>(manager, 0xD90), kVrHeldEffect);
        VrProxyObject(At<char*>(manager, 0xD94), kVrHeldEffect);
        return StSpansCompared("effects.tail.options",
                               {{manager + 0xB8C, 8}, {manager + 0xC8C, 4}, {manager + 0xCA0, 8}, {manager + 0xCD4, 4},
                                {manager + 0xD90, 8}},
                               false, game, ours);
    });
}
OPENSC4_THUNK(TTlOptions, TlOptionsByMode, 0)

Call0Fn g_SC4EffectTables_StartListening = nullptr;

uint32_t __fastcall TlStartByMode(char* tables, void*)
{
    const auto game = [&] { return g_SC4EffectTables_StartListening(tables, nullptr); };
    const auto ours = [&] { return StartListening(tables); };
    return StByModeOf(g_tlMode, StMake(addr::SC4EffectTables_StartListening, tables), nullptr, 0, game, ours, [&] {
        VrScope scope;
        VrProxyObject(M<char*>(0x00B43CCC), kVrMessageListen);
        return StSpansCompared("effects.tail.start", {{tables + 0xC20, 0x20}, {tables + 0xC6C, 0x28}}, false, game,
                               ours);
    });
}
OPENSC4_THUNK(TTlStart, TlStartByMode, 0)

Call1Fn g_SC4EffectsManager_HasDescriptionNamed = nullptr;
Call0Fn g_SC4EffectTables_StopListening = nullptr;

uint32_t __fastcall TlNamedByMode(char* manager, void*, uint32_t name)
{
    const auto game = [&] { return g_SC4EffectsManager_HasDescriptionNamed(manager, nullptr, name) & 0xFF; };
    const auto ours = [&] { return HasDescriptionNamed(manager, reinterpret_cast<const char*>(name)); };
    return TlByMode("effects.tail.named", addr::SC4EffectsManager_HasDescriptionNamed, manager + 0x1A0, 0xC, true,
                    game, ours, [] {});
}
OPENSC4_THUNK(TTlNamed, TlNamedByMode, 4)

uint32_t __fastcall TlStopByMode(char* tables, void*)
{
    const auto game = [&] { return g_SC4EffectTables_StopListening(tables, nullptr); };
    const auto ours = [&] { return StopListening(tables); };
    return StByModeOf(g_tlMode, StMake(addr::SC4EffectTables_StopListening, tables), nullptr, 0, game, ours, [&] {
        VrScope scope;
        VrProxyObject(M<char*>(0x00B43CCC), kVrMessageServer);
        return StSpansCompared("effects.tail.stop", {{tables + 0xC6C, 0x28}, {tables + 0xD0, 0x34}}, false, game,
                               ours);
    });
}
OPENSC4_THUNK(TTlStop, TlStopByMode, 0)

bool InstallTail(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !DetourCdecl(addr::SC4Script_RegisterFunction, &TlRegisterByMode, &g_SC4Script_RegisterFunction) ||
        !DetourCdecl(addr::SC4Script_UnregisterTable, &TlUnregisterByMode, &g_SC4Script_UnregisterTable) ||
        !Detour(addr::SC4EffectsManager_StartFollowUp, &TTlFollowUp, &g_SC4EffectsManager_StartFollowUp) ||
        !Detour(addr::SC4EffectsManager_PlaceCompounds, &TTlPlace, &g_SC4EffectsManager_PlaceCompounds) ||
        !Detour(addr::SC4EffectsManager_ReleaseThunk4, &TTlRelease4, &g_SC4EffectsManager_ReleaseThunk4) ||
        !Detour(addr::SC4EffectsManager_ReleaseThunk8, &TTlRelease8, &g_SC4EffectsManager_ReleaseThunk8) ||
        !Detour(addr::SC4EffectsManager_OccupantLifts, &TTlOccupant, &g_SC4EffectsManager_OccupantLifts) ||
        !Detour(addr::SC4EffectsManager_LiftUnderOccupant, &TTlLift, &g_SC4EffectsManager_LiftUnderOccupant) ||
        !DetourCdecl(addr::SC4EffectsManager_LiftMessage, &TlLiftMessageByMode, &g_SC4EffectsManager_LiftMessage) ||
        !Detour(addr::SC4EffectsManager_HasDescriptionNamed, &TTlNamed, &g_SC4EffectsManager_HasDescriptionNamed) ||
        !Detour(addr::SC4EffectTables_StopListening, &TTlStop, &g_SC4EffectTables_StopListening) ||
        !Detour(addr::SC4EffectTables_StartListening, &TTlStart, &g_SC4EffectTables_StartListening) ||
        !Detour(addr::SC4EffectsManager_RelieveUnderOccupant, &TTlRelieve, &g_SC4EffectsManager_RelieveUnderOccupant) ||
        !DetourCdecl(addr::SC4EffectsManager_Make, &TlMakeByMode, &g_SC4EffectsManager_Make) ||
        !Detour(addr::SC4EffectsManager_ApplyOptions, &TTlOptions, &g_SC4EffectsManager_ApplyOptions))
        return false;
    if (mode == registry::Mode::Compare &&
        !(DetourCdecl(addr::SC4_QueryHeld, &TlQueryHeldNoted, &g_SC4_QueryHeld) &&
          DetourCdecl(addr::SC4_GetPropertyBool, &TlGetPropertyBoolNoted, &g_SC4_GetPropertyBool) &&
          Detour(addr::SC4StringBody_AssignChars, &TTlAssignCharsNoted, &g_SC4StringBody_AssignChars) &&
          Detour(addr::SC4StringHashSet_Find, &TTlFindNameNoted, &g_SC4StringHashSet_Find) &&
          Detour(addr::SC4Uint32HashSet_Clear, &TTlClearHashNoted, &g_SC4Uint32HashSet_Clear) &&
          HookTransformChars() && HookClearManagerTexts() && HookAssignFloats() && HookPairHashInsert() &&
          HookPairHashGrow() && HookNameEqualRange() &&
          Detour(addr::SC4Uint32HashMap_Lookup, &TTlLookupNoted, &g_SC4Uint32HashMap_Lookup) &&
          Detour(addr::SC4List_Destroy2, &TTlDestroyListNoted, &g_SC4List_Destroy2) && HookPoolAllocate() &&
          Detour(addr::SC4EffectsManager_Construct, &TTlConstructNoted, &g_SC4EffectsManager_Construct) &&
          Detour(addr::SC4RefPtr_Destruct, &TTlDestructHolderNoted, &g_SC4RefPtr_Destruct) &&
          (g_SC4Effect_SetObject || Detour(addr::SC4Effect_SetObject, &TSC4Effect_SetObject, &g_SC4Effect_SetObject)) &&
          (g_SC4EffectsManager_RestartInUse || Detour(addr::SC4EffectsManager_RestartInUse,
                                                      &TSC4EffectsManager_RestartInUse,
                                                      &g_SC4EffectsManager_RestartInUse))))
        return false;
    g_tlMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the manager's calls through other tables %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredTail = registry::Register({
    "renderer.effects.tail",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectsManager_PlaceCompounds,
    "The effects manager's script register and unregister, an effect's follow-up start, the place given to the "
    "compound effects, two Release thunks and the lift of the particles' floor under an occupant; compare mode proxies the objects they call through (their tables' "
    "slots noted in the game's run and answered in ours' replay) and compares the store's way (docs/renderer.md, "
    "\"Water effects, sky and clouds\")",
    true,
    InstallTail,
});

// --- The effects' time step and start actions -----------------------------------------------
//
// The manager's time step and an effect's start actions (effects_time.h),
// compared as the tail is: the objects they call through proxied (the
// speed service's slot answering a double kept in the note), the direct
// helpers noted by detours of this group's in compare mode (the city
// view's ray, a held reference's let-go, a shake at a place and a flash),
// the store's engine comparing the calls and what the function writes
// (the time at +0x1B8 and +0x1BC; the effect's +0x144 and the manager's
// shake at +0xD28 to +0xD4C).
StepMode g_tmMode = StepMode::Original;
Call1Fn g_SC4EffectsManager_StepTime = nullptr, g_SC4EffectsManager_StartActions = nullptr;
using TmRayFn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t, uint32_t);
TmRayFn g_SC4CameraView_ScreenRay = nullptr;
Call0Fn g_SC4RefPtr_Release = nullptr;
Call3Fn g_SC4EffectsManager_ShakeAt = nullptr;
Call1Fn g_SC4EffectsManager_Flash = nullptr;

const VrKind kVrSpeed{{{0x80, 0, {{-1, 0}, {-1, 0}}, nullptr, nullptr, -1, 0,
                        reinterpret_cast<uint32_t>(&VrThunk0D<0x80>)}}};
const VrKind kVrCursor{{{0x84, 2, {{0, 4}, {1, 4}}, nullptr, nullptr, -1, 0, VR_SLOT(2, 0x84)}}};
const VrKind kVrFramework{{{0x14, 3, {{2, 4}, {-1, 0}}, &kVrCursor, nullptr, -1, 0, VR_SLOT(3, 0x14)}}};
const VrKind kVrSounds{{{0xA8, 2, {{-1, 0}, {-1, 0}}, nullptr, nullptr, 1, 8, VR_SLOT(2, 0xA8)}}};

// What ours calls besides the proxied tables: in the replay the note.
double TmSlotDouble(char* object, uint32_t slot)
{
    return reinterpret_cast<double(__thiscall*)(void*)>(At<uint32_t>(At<char*>(object, 0), slot))(object);
}

void TmScreenRay(char* view, uint32_t x, uint32_t z, uint32_t* point, uint32_t* direction)
{
    VrNotedInto(StMake(addr::SC4CameraView_ScreenRay, view, Ptr(x), Ptr(z)),
                {{reinterpret_cast<char*>(point), 0xC}, {reinterpret_cast<char*>(direction), 0xC}}, [&] {
                    return CallThis<uint8_t>(addr::SC4CameraView_ScreenRay, view, x, z, point, direction);
                });
}

void TmReleaseHolder(char** holder)
{
    VrNotedInto(StMake(addr::SC4RefPtr_Release, holder), {},
                [&] { return CallThis<uint32_t>(addr::SC4RefPtr_Release, holder); });
}

void TmShakeAt(char* manager, char* shake, float* place, uint32_t value)
{
    VrNotedInto(StMake(addr::SC4EffectsManager_ShakeAt, manager, shake, Ptr(Word(place)), value), {},
                [&] { return CallThis<uint32_t>(addr::SC4EffectsManager_ShakeAt, manager, shake, place, value); });
}

void TmFlash(char* manager, char* shake)
{
    VrNotedInto(StMake(addr::SC4EffectsManager_Flash, manager, shake), {},
                [&] { return CallThis<uint32_t>(addr::SC4EffectsManager_Flash, manager, shake); });
}

#include "core/effects_time.h"

// The compound effects in use, in order (the step's comparison needs them
// the same after the game's run).
std::vector<char*> TmInUse(char* manager)
{
    std::vector<char*> effects;
    for (uint32_t* at = At<uint32_t*>(manager, 0xBAC); at != At<uint32_t*>(manager, 0xBB0); ++at)
        effects.push_back(At<char**>(manager, 0xB94)[*at]);
    return effects;
}

// The compound effect's three functions the step calls through its tables
// (its running flag 0x007D6680, a point 0x005BEA50 and its part's step
// 0x005BEA10), noted in a noted run by detours in compare mode: a proxy
// would follow the effects the step makes by copying others.
Call0Fn g_SC4Effect_IsRunning = nullptr;
Call3Fn g_SC4CompoundEffect_SetPoint = nullptr;
Call1Fn g_SC4CompoundEffectPart_Step = nullptr;

uint32_t __fastcall TmRunningNoted(char* self, void*)
{
    return VrNotedInto(StMake(addr::SC4Effect_IsRunning, self), {},
                       [&] { return g_SC4Effect_IsRunning(self, nullptr); });
}
OPENSC4_THUNK(TTmRunningNoted, TmRunningNoted, 0)

uint32_t __fastcall TmPointNoted(char* self, void*, uint32_t a, uint32_t point, uint32_t c)
{
    return VrNotedInto(StMake(addr::SC4CompoundEffect_SetPoint, self, Ptr(a), Ptr(point), StArg(Ptr(c))), {},
                       [&] { return g_SC4CompoundEffect_SetPoint(self, nullptr, a, point, c); });
}
OPENSC4_THUNK(TTmPointNoted, TmPointNoted, 12)

uint32_t __fastcall TmPartStepNoted(char* self, void*, uint32_t seconds)
{
    return VrNotedInto(StMake(addr::SC4CompoundEffectPart_Step, self, Ptr(seconds)), {},
                       [&] { return g_SC4CompoundEffectPart_Step(self, nullptr, seconds); });
}
OPENSC4_THUNK(TTmPartStepNoted, TmPartStepNoted, 4)

// The helpers' detours (compare mode): noted in a noted run.
uint32_t __fastcall TmScreenRayNoted(char* view, void*, uint32_t x, uint32_t z, uint32_t point, uint32_t direction)
{
    return VrNotedInto(StMake(addr::SC4CameraView_ScreenRay, view, Ptr(x), Ptr(z)),
                       {{Ptr(point), 0xC}, {Ptr(direction), 0xC}},
                       [&] { return g_SC4CameraView_ScreenRay(view, nullptr, x, z, point, direction); });
}
OPENSC4_THUNK(TTmScreenRayNoted, TmScreenRayNoted, 16)

uint32_t __fastcall TmReleaseNoted(char* holder, void*)
{
    return VrNotedInto(StMake(addr::SC4RefPtr_Release, holder), {},
                       [&] { return g_SC4RefPtr_Release(holder, nullptr); });
}
OPENSC4_THUNK(TTmReleaseNoted, TmReleaseNoted, 0)

uint32_t __fastcall TmShakeAtNoted(char* manager, void*, uint32_t shake, uint32_t place, uint32_t value)
{
    return VrNotedInto(StMake(addr::SC4EffectsManager_ShakeAt, manager, Ptr(shake), Ptr(place), value), {},
                       [&] { return g_SC4EffectsManager_ShakeAt(manager, nullptr, shake, place, value); });
}
OPENSC4_THUNK(TTmShakeAtNoted, TmShakeAtNoted, 12)

uint32_t __fastcall TmFlashNoted(char* manager, void*, uint32_t shake)
{
    return VrNotedInto(StMake(addr::SC4EffectsManager_Flash, manager, Ptr(shake)), {},
                       [&] { return g_SC4EffectsManager_Flash(manager, nullptr, shake); });
}
OPENSC4_THUNK(TTmFlashNoted, TmFlashNoted, 4)

uint32_t __fastcall TmStepByMode(char* manager, void*, uint32_t ms)
{
    const auto game = [&] { return g_SC4EffectsManager_StepTime(manager, nullptr, ms); };
    const auto ours = [&] {
        StepEffectsTime(manager, ms);
        return 0u;
    };
    return StByModeOf(g_tmMode, StMake(addr::SC4EffectsManager_StepTime, manager, Ptr(ms)), nullptr, 0, game, ours,
                      [&] {
                          VrScope scope;
                          VrProxyObject(M<char*>(0x00B43D5C), kVrSpeed);
                          VrProxyObject(reinterpret_cast<char*(__cdecl*)()>(addr::RZGetFramework)(), kVrFramework);
                          const std::vector<char*> before = TmInUse(manager);
                          return StSpansCompared("effects.time.step", {{manager + 0x1B8, 8}}, false, game, ours,
                                                 [&] { return TmInUse(manager) == before; });
                      });
}
OPENSC4_THUNK(TTmStep, TmStepByMode, 4)

uint32_t __fastcall TmStartByMode(char* manager, void*, uint32_t effect)
{
    const auto game = [&] { return g_SC4EffectsManager_StartActions(manager, nullptr, effect); };
    const auto ours = [&] {
        StartEffectActions(manager, Ptr(effect));
        return 0u;
    };
    return StByModeOf(g_tmMode, StMake(addr::SC4EffectsManager_StartActions, manager, Ptr(effect)), nullptr, 0, game,
                      ours, [&] {
                          VrScope scope;
                          VrProxyObject(M<char*>(0x00B43DD0), kVrSounds);
                          return StSpansCompared("effects.time.start", {{Ptr(effect) + 0x144, 4}, {manager + 0xD28, 0x28}},
                                                 false, game, ours);
                      });
}
OPENSC4_THUNK(TTmStart, TmStartByMode, 4)

bool InstallTime(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !Detour(addr::SC4EffectsManager_StepTime, &TTmStep, &g_SC4EffectsManager_StepTime) ||
        !Detour(addr::SC4EffectsManager_StartActions, &TTmStart, &g_SC4EffectsManager_StartActions))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookAssignFloats() && HookAssignFloat3s() &&
          Detour(addr::SC4CameraView_ScreenRay, &TTmScreenRayNoted, &g_SC4CameraView_ScreenRay) &&
          Detour(addr::SC4RefPtr_Release, &TTmReleaseNoted, &g_SC4RefPtr_Release) &&
          Detour(addr::SC4EffectsManager_ShakeAt, &TTmShakeAtNoted, &g_SC4EffectsManager_ShakeAt) &&
          Detour(addr::SC4EffectsManager_Flash, &TTmFlashNoted, &g_SC4EffectsManager_Flash) &&
          Detour(addr::SC4Effect_IsRunning, &TTmRunningNoted, &g_SC4Effect_IsRunning) &&
          Detour(addr::SC4CompoundEffect_SetPoint, &TTmPointNoted, &g_SC4CompoundEffect_SetPoint) &&
          Detour(addr::SC4CompoundEffectPart_Step, &TTmPartStepNoted, &g_SC4CompoundEffectPart_Step)))
        return false;
    g_tmMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the effects' time step and start actions %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredTime = registry::Register({
    "renderer.effects.time",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectsManager_StepTime,
    "The effects manager's time step (the speed, the cursor's ray, each running compound effect stepped) and an "
    "effect's start actions (a sound, a shake); compare mode proxies the objects they call through, notes the direct "
    "helpers and compares the store's way (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallTime,
});

// --- An effect's set-up ----------------------------------------------------------------------
//
// The base class's constructor (in renderer.effects.ctor above), an
// effect's transform, its particles' axes, its start and a new particle put
// on the ground (effects_setup.h). Each writes only the effect (and the
// particle) and draws random numbers from the generator at 0x00B22DF8; the
// terrain's answers it asks for are the same twice. So the comparison runs
// ours, keeps what it left of the effect, the particle and the generator,
// puts them back and runs the game's: the two must leave the same bytes and
// give the same answer (by its low byte).
#include "core/effects_setup.h"

constexpr std::uintptr_t kSetupGenerator = 0x00B22DF8;
constexpr std::size_t kSetupGeneratorSize = 0x10;

template <typename Ours, typename Game>
uint32_t SuCompared(const char* id, char* self, char* particle, Ours&& ours, Game&& game)
{
    char* const generator = reinterpret_cast<char*>(kSetupGenerator);
    constexpr std::size_t kGeneratorAt = kEffectSize, kParticleAt = kGeneratorAt + kSetupGeneratorSize,
                          kTotal = kParticleAt + kParticleBytes;
    char before[kTotal] = {}, mine[kTotal] = {}, theirs[kTotal] = {};
    const auto save = [&](char* to) {
        std::memcpy(to, self, kEffectSize);
        std::memcpy(to + kGeneratorAt, generator, kSetupGeneratorSize);
        if (particle)
            std::memcpy(to + kParticleAt, particle, kParticleBytes);
    };
    const auto restore = [&](const char* from) {
        std::memcpy(self, from, kEffectSize);
        std::memcpy(generator, from + kGeneratorAt, kSetupGeneratorSize);
        if (particle)
            std::memcpy(particle, from + kParticleAt, kParticleBytes);
    };
    save(before);
    const uint32_t ourResult = ours();
    save(mine);
    restore(before);
    const uint32_t result = game();
    save(theirs);
    const bool same = (result & 0xFF) == (ourResult & 0xFF) && std::memcmp(theirs, mine, kTotal) == 0;
    std::string gameText, ourText;
    if (!same) {
        std::size_t at = 0;
        while (at < kTotal && theirs[at] == mine[at])
            ++at;
        at = at < kTotal ? at & ~std::size_t{3} : 0;
        char text[48];
        std::snprintf(text, sizeof text, "%s+%zX ",
                      at < kGeneratorAt ? "effect" : at < kParticleAt ? "generator" : "particle",
                      at < kGeneratorAt ? at : at < kParticleAt ? at - kGeneratorAt : at - kParticleAt);
        char answer[24];
        std::snprintf(answer, sizeof answer, " answer %02X", result & 0xFF);
        gameText = text + Hex(theirs + at, 8) + answer;
        std::snprintf(answer, sizeof answer, " answer %02X", ourResult & 0xFF);
        ourText = text + Hex(mine + at, 8) + answer;
    }
    compare::Record(id, same, gameText, ourText);
    return result;
}

StepMode g_transformMode = StepMode::Original, g_axesMode = StepMode::Original, g_beginMode = StepMode::Original,
         g_groundMode = StepMode::Original;
Call2Fn g_SC4Effect_SetTransform = nullptr;
Call1Fn g_SC4Effect_BeforeParticles = nullptr, g_SC4Effect_Begin = nullptr, g_SC4Effect_PlaceOnGround = nullptr;

// The three that answer nothing (the game's answer is a left-over
// register) are compared on what they leave only.
uint32_t __fastcall SuTransformByMode(char* self, void*, uint32_t first, uint32_t second)
{
    const auto game = [&] { return g_SC4Effect_SetTransform(self, nullptr, first, second); };
    const auto ours = [&] {
        SetEffectTransform(self, Ptr(first), Ptr(second));
        return 0u;
    };
    if (g_transformMode == StepMode::Original)
        return game();
    if (g_transformMode == StepMode::Ours)
        return ours();
    return SuCompared("effects.transform", self, nullptr, ours, [&] {
        game();
        return 0u;
    });
}
OPENSC4_THUNK(TSuTransform, SuTransformByMode, 8)

uint32_t __fastcall SuAxesByMode(char* self, void*, uint32_t context)
{
    const auto game = [&] { return g_SC4Effect_BeforeParticles(self, nullptr, context); };
    const auto ours = [&] {
        SetEffectAxes(self, Ptr(context));
        return 0u;
    };
    if (g_effectRecord && g_drawRecording)
        return EffectRecorded(addr::SC4Effect_BeforeParticles, g_SC4Effect_BeforeParticles, self, context);
    if (g_axesMode == StepMode::Original)
        return game();
    if (g_axesMode == StepMode::Ours)
        return ours();
    return SuCompared("effects.axes", self, nullptr, ours, [&] {
        game();
        return 0u;
    });
}
OPENSC4_THUNK(TSuAxes, SuAxesByMode, 4)

uint32_t __fastcall SuBeginByMode(char* self, void*, uint32_t again)
{
    const auto game = [&] { return g_SC4Effect_Begin(self, nullptr, again); };
    const auto ours = [&] {
        BeginEffect(self, again);
        return 0u;
    };
    if (g_beginMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return game();
    if (g_beginMode == StepMode::Ours)
        return ours();
    return SuCompared("effects.begin", self, nullptr, ours, [&] {
        game();
        return 0u;
    });
}
OPENSC4_THUNK(TSuBegin, SuBeginByMode, 4)

uint32_t __fastcall SuGroundByMode(char* self, void*, uint32_t particle)
{
    const auto game = [&] { return g_SC4Effect_PlaceOnGround(self, nullptr, particle); };
    const auto ours = [&] { return PlaceParticleOnGround(self, Ptr(particle)); };
    if (g_groundMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return game();
    if (g_groundMode == StepMode::Ours)
        return ours();
    return SuCompared("effects.ground", self, Ptr(particle), ours, game);
}
OPENSC4_THUNK(TSuGround, SuGroundByMode, 4)

StepMode g_startMode = StepMode::Original;

uint32_t StartByMode(char* effect, uint32_t particle)
{
    const auto game = [&] {
        g_startParticle(effect, nullptr, particle);
        return 0u;
    };
    const auto ours = [&] {
        StartParticle(effect, Ptr(particle));
        return 0u;
    };
    if (g_startMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_startParticle(effect, nullptr, particle);
    if (g_startMode == StepMode::Ours)
        return ours();
    return SuCompared("effects.start", effect, Ptr(particle), ours, game);
}

template <typename Fn, typename Thunk>
bool SuInstall(registry::Mode mode, StepMode& target, uint32_t address, Thunk thunk, Fn* original, const char* what)
{
    if (!game::IsBuild641() || !(*original || Detour(address, thunk, original)))
        return false;
    target = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: %s %s", what, mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallSetupTransform(registry::Mode mode)
{
    return SuInstall(mode, g_transformMode, addr::SC4Effect_SetTransform, &TSuTransform, &g_SC4Effect_SetTransform,
                     "an effect's transform");
}

bool InstallSetupAxes(registry::Mode mode)
{
    return SuInstall(mode, g_axesMode, addr::SC4Effect_BeforeParticles, &TSuAxes, &g_SC4Effect_BeforeParticles,
                     "an effect's particle axes");
}

bool InstallSetupBegin(registry::Mode mode)
{
    return SuInstall(mode, g_beginMode, addr::SC4Effect_Begin, &TSuBegin, &g_SC4Effect_Begin, "an effect's start");
}

bool InstallSetupGround(registry::Mode mode)
{
    return SuInstall(mode, g_groundMode, addr::SC4Effect_PlaceOnGround, &TSuGround, &g_SC4Effect_PlaceOnGround,
                     "a new particle put on the ground");
}

bool InstallSetupStart(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookStart())
        return false;
    g_startMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: a new particle's start %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredSetupStart = registry::Register({
    "renderer.effects.start",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StartParticle,
    "A new particle's start (0x005B7580): its life, position, velocity, size and spin from the effect, its "
    "description and random numbers; compare mode runs ours and the game's on the same effect, particle and "
    "generator (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallSetupStart,
});

const bool g_registeredSetupTransform = registry::Register({
    "renderer.effects.transform",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_SetTransform,
    "An effect's transform (0x005B3430): the two blocks kept and the effect's centre and box moved by them; "
    "compare mode runs ours and the game's on the same effect (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallSetupTransform,
});

const bool g_registeredSetupAxes = registry::Register({
    "renderer.effects.axes",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_BeforeParticles,
    "The two axes an effect's particles are drawn along (0x005B3B90), taken again when the view turns; compare mode "
    "runs ours and the game's on the same effect (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallSetupAxes,
});

const bool g_registeredSetupBegin = registry::Register({
    "renderer.effects.begin",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_Begin,
    "An effect started (0x005B3000): its clocks set back, its pull, and the random drift, speed and delay its "
    "description asks for; compare mode runs ours and the game's on the same effect and generator "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallSetupBegin,
});

const bool g_registeredSetupGround = registry::Register({
    "renderer.effects.ground",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_PlaceOnGround,
    "A new particle put on the ground (0x005B71E0) or ended there; compare mode runs ours and the game's on the "
    "same effect, particle and generator (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallSetupGround,
});

// --- A description's constructor and its read ------------------------------------------------
//
// The inner constructor (0x005B0FE0) and the read from the effects file
// (0x005B1DC0), written by hand in effects_setup.h. Both leave lists and a
// string behind, so two descriptions are compared field by field: the
// words as they are, and each list's or string's three pointers by its
// length, its room and its bytes (the third list's records string by
// string, with their value). The bytes no function writes (+0xD6, +0xD7,
// +0x1C8) and the count of holders (+0x204) are left out.
constexpr std::size_t kDescriptionSize = 0x208;
struct SuVector {
    uint32_t at, item;
};
constexpr SuVector kDescriptionVectors[] = {{0x88, 4},     {0x94, 12},    {0xA0, 4},     {0xAC, 4},  {0xB8, 4},
                                            {0xC4, 4},     {0x108, 0x1C}, {0x12C, 4},    {0x160, 1}, {0x1A4, 4},
                                            {0x1B8, 0x20}, {0x1CC, 0x10}, {0x1E0, 4},    {0x1F4, 4}};

// The first place the two differ, or -1.
int32_t SuDescriptionsDiffer(char* a, char* b)
{
    for (uint32_t at = 0; at < 0x204;) {
        const SuVector* vector = nullptr;
        for (const SuVector& v : kDescriptionVectors)
            vector = v.at == at ? &v : vector;
        if (vector) {
            char* const first = At<char*>(a, at);
            char* const second = At<char*>(b, at);
            const uint32_t used = At<char*>(a, at + 4) - first, room = At<char*>(a, at + 8) - first;
            if (used != static_cast<uint32_t>(At<char*>(b, at + 4) - second) ||
                room != static_cast<uint32_t>(At<char*>(b, at + 8) - second))
                return static_cast<int32_t>(at);
            if (at == 0x1CC) {
                for (uint32_t record = 0; record < used; record += 0x10) {
                    char* const x = first + record;
                    char* const y = second + record;
                    const uint32_t length = At<char*>(x, 4) - At<char*>(x, 0);
                    if (length != static_cast<uint32_t>(At<char*>(y, 4) - At<char*>(y, 0)) ||
                        At<uint32_t>(x, 0xC) != At<uint32_t>(y, 0xC) ||
                        (length && std::memcmp(At<char*>(x, 0), At<char*>(y, 0), length) != 0))
                        return static_cast<int32_t>(at);
                }
            } else if (used && std::memcmp(first, second, used) != 0) {
                return static_cast<int32_t>(at);
            }
            at += 12;
            continue;
        }
        if (at == 0xD4) {
            if (At<uint16_t>(a, at) != At<uint16_t>(b, at))
                return static_cast<int32_t>(at);
        } else if (at != 0x1C8 && At<uint32_t>(a, at) != At<uint32_t>(b, at)) {
            return static_cast<int32_t>(at);
        }
        at += 4;
    }
    return -1;
}

// What ours' description holds freed: the lists, the third list's
// strings and the string.
void SuFreeDescriptionParts(char* description)
{
    const auto release = [](char* storage) {
        if (storage)
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(storage);
    };
    for (char* record = At<char*>(description, 0x1CC); record && record < At<char*>(description, 0x1D0);
         record += 0x10)
        release(At<char*>(record, 0));
    for (const SuVector& v : kDescriptionVectors)
        release(At<char*>(description, v.at));
}

StepMode g_descCtorMode = StepMode::Original, g_descReadMode = StepMode::Original;
Call0Fn g_SC4EffectDescription_Construct = nullptr;
using SuReadFn = uint32_t(__cdecl*)(uint32_t, uint32_t);
SuReadFn g_SC4EffectDescription_Read = nullptr;

void SuRecordDescription(const char* id, int32_t at)
{
    char text[32];
    std::snprintf(text, sizeof text, "first at +%X", at < 0 ? 0 : at);
    compare::Record(id, at < 0, at < 0 ? std::string() : std::string("the game's"), at < 0 ? std::string() : text);
}

// The constructor's comparison: ours on the memory, what it left kept, the
// memory put back and the game's; then ours' lists and string freed.
uint32_t __fastcall SuDescriptionCtorByMode(char* self, void*)
{
    if (g_descCtorMode == StepMode::Original)
        return g_SC4EffectDescription_Construct(self, nullptr);
    if (g_descCtorMode == StepMode::Ours)
        return ConstructDescription(self);
    std::vector<char> before(self, self + kDescriptionSize);
    const uint32_t ourResult = ConstructDescription(self);
    std::vector<char> mine(self, self + kDescriptionSize);
    std::memcpy(self, before.data(), kDescriptionSize);
    const uint32_t result = g_SC4EffectDescription_Construct(self, nullptr);
    const int32_t at = result == ourResult ? SuDescriptionsDiffer(self, mine.data()) : 0;
    SuFreeDescriptionParts(mine.data());
    SuRecordDescription("effects.description_ctor", at);
    return result;
}
OPENSC4_THUNK(TSuDescriptionCtor, SuDescriptionCtorByMode, 0)

// The read's comparison: a description of ours made new by the game's
// whole constructor (0x005B1660) and checked to be the same as the one the
// game reads into (it always is new); the game's reads with each read of
// the stream noted through a copy of its table of functions (the lists'
// engine, ListStreamNoted), ours reads the same values into ours, and the
// two must be the same; ours is then destroyed (0x0059ECB0) and freed.
uint32_t __cdecl SuDescriptionReadByMode(uint32_t stream, uint32_t description)
{
    char* const self = Ptr(description);
    if (g_descReadMode == StepMode::Original || g_listGame || g_listReplay)
        return g_SC4EffectDescription_Read(stream, description);
    if (g_descReadMode == StepMode::Ours)
        return ReadDescription(stream, self);
    char* const mine = reinterpret_cast<char*(__cdecl*)(std::size_t)>(0x005E55E0)(kDescriptionSize);
    CallThis<uint32_t>(addr::SC4EffectDescription_ConstructWhole, mine);
    uint32_t result = 0;
    if (const int32_t fresh = SuDescriptionsDiffer(self, mine); fresh >= 0) {
        compare::Record("effects.description_read.not_new", true, std::string(), std::string());
        result = g_SC4EffectDescription_Read(stream, description);
    } else {
        const std::vector<uint32_t>& table = ListStreamNoted(stream);
        M<const uint32_t*>(stream) = table.data();
        g_listGame = true;
        result = g_SC4EffectDescription_Read(stream, description);
        g_listGame = false;
        g_listReplay = true;
        const uint32_t ourResult = ReadDescription(stream, mine);
        g_listReplay = false;
        M<uint32_t>(stream) = g_listStreamReal;
        const bool reads = !g_listWrong && g_listReplayed == g_listReads.size() && result == ourResult;
        SuRecordDescription("effects.description_read", reads ? SuDescriptionsDiffer(self, mine) : 0);
    }
    CallThis<uint32_t>(0x0059ECB0, mine);
    reinterpret_cast<void(__cdecl*)(void*)>(0x005E5620)(mine);
    return result;
}

bool InstallDescriptionCtor(registry::Mode mode)
{
    return SuInstall(mode, g_descCtorMode, addr::SC4EffectDescription_Construct, &TSuDescriptionCtor,
                     &g_SC4EffectDescription_Construct, "a description's constructor");
}

bool InstallDescriptionRead(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !DetourCdecl(addr::SC4EffectDescription_Read, &SuDescriptionReadByMode, &g_SC4EffectDescription_Read))
        return false;
    g_descReadMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: a description's read %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDescriptionCtor = registry::Register({
    "renderer.effects.description_ctor",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDescription_Construct,
    "A description's inner constructor (0x005B0FE0): its fields' first values, an empty string and one value in "
    "each of seven lists; compare mode runs ours and the game's on the same memory and compares the lists by their "
    "contents (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallDescriptionCtor,
});

const bool g_registeredDescriptionRead = registry::Register({
    "renderer.effects.description_read",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDescription_Read,
    "A description read from the effects file (0x005B1DC0); compare mode notes what the game's reads from the stream "
    "and has ours read the same into a new description of its own (docs/renderer.md, \"Water effects, sky and "
    "clouds\")",
    true,
    InstallDescriptionRead,
});

// --- An effect given its description ---------------------------------------------------------
//
// 0x005B7C90 (effects_setup.h) holds and lets go of descriptions, loads a
// counted texture and sets up the batch, which cannot run twice. Its
// comparison is the store's: the game's runs with those four calls noted
// (the hold and the let-go by their detours, the texture and the batch's
// set-up (0x005B7170, slot 0x40) by detours of this group's in compare mode, with the
// bytes they leave in the effect, +0x68 and +0x28 to +0x67), the effect is
// put back, and ours runs with the four answered from the notes; the rest
// (the render state, 0x005B7030, the terrain's height, the names asked of
// the manager) runs for real in both. The calls and the effect must be the
// same.
StepMode g_descriptionMode = StepMode::Original;
Call1Fn g_SC4Effect_SetDescription = nullptr, g_SC4Effect_SetUpBatch = nullptr;

uint32_t SuTextureNoted(char* self, uint32_t which)
{
    return VrNotedInto(StMake(addr::SC4Effect_LoadTexture, self, Ptr(which)), {{self + 0x68, 8}},
                       [&] { return LoadTextureByMode(self, which); });
}

uint32_t __fastcall SuBatchNoted(char* self, void*, uint32_t batch)
{
    return VrNotedInto(StMake(addr::SC4Effect_SortBatch, self, Ptr(batch)), {{Ptr(batch), 0x40}},
                       [&] { return g_SC4Effect_SetUpBatch(self, nullptr, batch); });
}
OPENSC4_THUNK(TSuBatchNoted, SuBatchNoted, 4)

// Noted as one call in the second class's noted run (renderer.effects.description_b).
bool BdNoting(char* self);
uint32_t BdBaseNoted(char* self, uint32_t description);

uint32_t __fastcall SuDescriptionByMode(char* self, void*, uint32_t description)
{
    if (BdNoting(self))
        return BdBaseNoted(self, description);
    const auto game = [&] { return g_SC4Effect_SetDescription(self, nullptr, description); };
    const auto ours = [&] {
        SetEffectDescription(self, Ptr(description));
        return 0u;
    };
    if (g_descriptionMode == StepMode::Original || g_stNoting || g_stReplay)
        return game();
    if (g_descriptionMode == StepMode::Ours)
        return ours();
    return StSpansCompared("effects.description", {{self, kEffectSize}}, false, game, ours);
}
OPENSC4_THUNK(TSuDescription, SuDescriptionByMode, 4)

bool InstallDescription(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4Effect_SetDescription ||
          Detour(addr::SC4Effect_SetDescription, &TSuDescription, &g_SC4Effect_SetDescription)))
        return false;
    if (mode == registry::Mode::Compare &&
        !((g_SC4Effect_Hold || Detour(addr::SC4Effect_Hold, &TSC4Effect_Hold, &g_SC4Effect_Hold)) &&
          (g_SC4EffectDescription_Release || Detour(addr::SC4EffectDescription_Release,
                                                    &TSC4EffectDescription_Release, &g_SC4EffectDescription_Release)) &&
          (g_SC4Effect_LoadTexture ||
           Detour(addr::SC4Effect_LoadTexture, &TSC4Effect_LoadTexture, &g_SC4Effect_LoadTexture)) &&
          Detour(addr::SC4Effect_SortBatch, &TSuBatchNoted, &g_SC4Effect_SetUpBatch)))
        return false;
    g_descriptionMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: an effect given its description %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDescription = registry::Register({
    "renderer.effects.description",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_SetDescription,
    "An effect given its description (0x005B7C90): held, its render state, texture and batch, and the values that "
    "come from it; compare mode notes the hold, let-go, texture and batch in the game's run and answers ours from "
    "the notes (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallDescription,
});

// --- The second class's model and description ------------------------------------------------
//
// The second class loading its model (0x005AC610) and taking a description
// (0x005AEC90), effects_model.h. The load is compared by running both for
// real on an effect that has no model yet (as its one caller leaves it):
// ours first, its model's materials read and its model released twice, as
// the class lets go of one; the effect put back and the game's run. The
// effect but for the two pointers, whether there is a model, and the
// materials must be the same.
//
// The description's comparison is the store's (StSpansCompared on the
// effect and the generator at 0x00B22DF8): the game's runs noted, the
// effect and the generator are put back, and ours runs with its calls
// answered from the notes. Noted are the base class's whole run
// (0x005B7C90, by its detour, with the bytes it leaves in the effect), the
// releases of the instance (+0x220) and of the model's counting part and
// the end of the object at +0x278 (their tables proxied: slot 8, slot
// 0x20), the load (slot 0x54, by its detour, with +0x220 and +0x274), the
// instance's tint (slot 0x40, its block by its set bytes) and the part's
// three functions (0x008038D0, 0x00802DB0, 0x005AC110, by detours, with
// the part's bytes). The pick among the description's models is made by
// ours from the generator; the tint block, the part's +8 and the names
// asked of the manager run for real in both. A game's run that changed
// the bytes of the model ours reads the counting part from (it was let go
// of for good) is only counted.

StepMode g_modelBMode = StepMode::Original, g_descriptionBMode = StepMode::Original;
Call1Fn g_SC4EffectB_LoadModel = nullptr, g_SC4EffectB_SetDescription = nullptr, g_SC4EffectPartB_Attach = nullptr,
        g_SC4EffectPartB_Place = nullptr;
char* g_bdSelf = nullptr;

bool BdNoting(char* self)
{
    return g_bdSelf && self == g_bdSelf && StNoting();
}

// The function a slot of the object's table holds: a proxied object's from
// its own table (in a replay the object may be gone); in a replay nothing
// else is read.
uint32_t BdOriginalSlot(char* object, uint32_t slot)
{
    if (VrProxy* const proxy = VrFind(object))
        return At<uint32_t>(Ptr(proxy->original), slot);
    if (g_stReplay && g_stDepth == 0 && GetCurrentThreadId() == g_stThread)
        return 0;
    return At<uint32_t>(Ptr(VrOriginalOf(At<uint32_t>(object, 0))), slot);
}

uint32_t BdSlot0(char* object, uint32_t slot)
{
    const uint32_t function = BdOriginalSlot(object, slot);
    return VrNotedInto(StMake(function, object), {}, [&] { return CallThis<uint32_t>(function, object); });
}

uint32_t BdBytes(const char* bytes, uint32_t size, uint32_t skip = 0, uint32_t skipped = 0)
{
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < size; ++i) {
        if (i < skip || i >= skip + skipped)
            hash = (hash ^ static_cast<uint8_t>(bytes[i])) * 16777619u;
    }
    return hash;
}

// The tint block's bytes 2 and 3 are left as the stack held them.
uint32_t BdTint(char* instance, const char* block)
{
    const uint32_t function = BdOriginalSlot(instance, 0x40);
    return VrNotedInto(StMake(function, instance, nullptr, nullptr, BdBytes(block, 0x14, 2, 2)), {},
                       [&] { return CallThis<uint32_t>(function, instance, block); });
}

uint32_t __fastcall BdTintSlot(char* instance, void*, uint32_t block)
{
    return BdTint(instance, Ptr(block));
}

const VrKind kBdEnded{{VrPlain(0x20, 0, VR_SLOT(0, 0x20))}};
const VrKind kBdInstance{{VrPlain(8, 0, VR_SLOT(0, 8)),
                          {0x40, 1, {{-1, 0}, {-1, 0}}, nullptr, nullptr, -1, 0,
                           reinterpret_cast<uint32_t>(&BdTintSlot)}}};

uint32_t BdBaseNoted(char* self, uint32_t description)
{
    return VrNotedInto(StMake(addr::SC4Effect_SetDescription, self, Ptr(description)), {{self, kEffectSize}},
                       [&] { return g_SC4Effect_SetDescription(self, nullptr, description); });
}

void BdBase(char* self, char* description)
{
    VrNotedInto(StMake(addr::SC4Effect_SetDescription, self, description), {{self, kEffectSize}},
                [&] { return CallThis<uint32_t>(addr::SC4Effect_SetDescription, self, description); });
}

uint32_t BdPartDescriptionNoted(char* part, uint32_t description)
{
    return VrNotedInto(StMake(addr::SC4EffectPartB_SetDescription, part, Ptr(description)), {{part, 0x44}},
                       [&] { return g_SC4EffectPartB_SetDescription(part, nullptr, description); });
}

void BdPartDescription(char* part, char* description)
{
    VrNotedInto(StMake(addr::SC4EffectPartB_SetDescription, part, description), {{part, 0x44}},
                [&] { return CallThis<uint32_t>(addr::SC4EffectPartB_SetDescription, part, description); });
}

uint8_t BdAttach(char* part, char* instance)
{
    return VrNotedInto(StMake(addr::SC4EffectPartB_Attach, part, instance), {{part, 0x44}},
                       [&] { return CallThis<uint32_t>(addr::SC4EffectPartB_Attach, part, instance); }) &
           0xFF;
}

void BdPlace(char* part, const float* place)
{
    VrNotedInto(
        StMake(addr::SC4EffectPartB_Place, part, nullptr, nullptr, BdBytes(reinterpret_cast<const char*>(place), 12)),
        {{part, 0x44}}, [&] { return CallThis<uint32_t>(addr::SC4EffectPartB_Place, part, place); });
}

uint32_t __fastcall BdAttachByMode(char* part, void*, uint32_t instance)
{
    if (BdNoting(part - kEffectPartB))
        return VrNotedInto(StMake(addr::SC4EffectPartB_Attach, part, Ptr(instance)), {{part, 0x44}},
                           [&] { return g_SC4EffectPartB_Attach(part, nullptr, instance); });
    return g_SC4EffectPartB_Attach(part, nullptr, instance);
}
OPENSC4_THUNK(TBdAttach, BdAttachByMode, 4)

uint32_t __fastcall BdPlaceByMode(char* part, void*, uint32_t place)
{
    if (BdNoting(part - kEffectPartB))
        return VrNotedInto(StMake(addr::SC4EffectPartB_Place, part, nullptr, nullptr, BdBytes(Ptr(place), 12)),
                           {{part, 0x44}}, [&] { return g_SC4EffectPartB_Place(part, nullptr, place); });
    return g_SC4EffectPartB_Place(part, nullptr, place);
}
OPENSC4_THUNK(TBdPlace, BdPlaceByMode, 4)

void BdLoadModel(char* self, uint32_t which);
#include "core/effects_model.h"

// The materials' flags of a model's instance (eight bytes each, from +4).
std::vector<char> BdMaterials(char* instance)
{
    std::vector<char> bytes;
    if (!instance)
        return bytes;
    char* materials = nullptr;
    SuSlot<uint32_t>(instance, 0, 0x26A68448u, &materials);
    if (!materials)
        return bytes;
    const uint32_t count = SuSlot<uint32_t>(materials, 0xC);
    for (uint32_t i = 0; i < count; ++i) {
        const char* const material = reinterpret_cast<const char*>(SuSlot<uint32_t>(materials, 0x18, i));
        bytes.insert(bytes.end(), material + 4, material + 12);
    }
    SuSlot<uint32_t>(materials, 8);
    return bytes;
}

uint32_t BdModelCompared(char* self, uint32_t which)
{
    const auto game = [&] { return g_SC4EffectB_LoadModel(self, nullptr, which); };
    if (At<char*>(self, 0x220) || At<char*>(self, 0x224)) {
        StRecord("effects.model_b.skipped", true, std::string(), std::string());
        return game();
    }
    std::vector<char> before(self, self + kEffectSizeB);
    LoadEffectModelB(self, which);
    std::vector<char> mine(self, self + kEffectSizeB);
    char* const ourInstance = At<char*>(self, 0x220);
    char* const ourModel = At<char*>(self, 0x224);
    const std::vector<char> ourMaterials = BdMaterials(ourInstance);
    if (ourInstance)
        SuSlot<uint32_t>(ourInstance, 8);
    if (ourModel)
        SuSlot<uint32_t>(BdCounted(ourModel), 8);
    std::memcpy(self, before.data(), kEffectSizeB);
    const uint32_t result = game();
    std::vector<char> theirs(self, self + kEffectSizeB);
    const std::vector<char> theirMaterials = BdMaterials(At<char*>(self, 0x220));
    const bool sameModel = (ourInstance != nullptr) == (At<char*>(self, 0x220) != nullptr) &&
                           (ourModel != nullptr) == (At<char*>(self, 0x224) != nullptr);
    std::memset(mine.data() + 0x220, 0, 8);
    std::memset(theirs.data() + 0x220, 0, 8);
    const char* const id = theirMaterials.empty() ? "effects.model_b.none" : "effects.model_b";
    char text[64];
    if (!sameModel) {
        StRecord(id, false, At<char*>(self, 0x220) ? "a model" : "no model", ourInstance ? "a model" : "no model");
    } else if (theirMaterials != ourMaterials) {
        std::snprintf(text, sizeof text, "%zu materials ", theirMaterials.size() / 8);
        const std::string game = text + Hex(theirMaterials.data(), std::min<std::size_t>(theirMaterials.size(), 32));
        std::snprintf(text, sizeof text, "%zu materials ", ourMaterials.size() / 8);
        StRecord(id, false, game, text + Hex(ourMaterials.data(), std::min<std::size_t>(ourMaterials.size(), 32)));
    } else if (theirs != mine) {
        std::size_t at = 0;
        while (theirs[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "effect+%zX ", at);
        StRecord(id, false, text + Hex(theirs.data() + at, 8), text + Hex(mine.data() + at, 8));
    } else {
        StRecord(id, true, std::string(), std::string());
    }
    return result;
}

uint32_t LoadModelBRun(char* self, uint32_t which)
{
    if (g_modelBMode == StepMode::Original || g_stNoting || g_stReplay)
        return g_SC4EffectB_LoadModel(self, nullptr, which);
    if (g_modelBMode == StepMode::Ours) {
        LoadEffectModelB(self, which);
        return 0;
    }
    if (g_effectRecord || g_effectReplay || BBusy())
        return g_SC4EffectB_LoadModel(self, nullptr, which);
    return BdModelCompared(self, which);
}

// In the description's noted run the load is one noted call, its instance
// proxied for its tint.
void BdLoadModel(char* self, uint32_t which)
{
    VrNotedInto(StMake(addr::SC4EffectB_LoadModel, self, Ptr(which)), {{self + 0x220, 8}, {self + 0x274, 4}},
                [&] { return SuSlot<uint32_t>(self, 0x54, which); });
    VrProxyObject(At<char*>(self, 0x220), kBdInstance);
}

uint32_t __fastcall BdLoadModelByMode(char* self, void*, uint32_t which)
{
    if (!BdNoting(self))
        return LoadModelBRun(self, which);
    const uint32_t result = VrNotedInto(StMake(addr::SC4EffectB_LoadModel, self, Ptr(which)),
                                        {{self + 0x220, 8}, {self + 0x274, 4}}, [&] {
                                            // The game's or ours, or both compared, one level down.
                                            const bool noting = g_stNoting;
                                            g_stNoting = false;
                                            const uint32_t loaded = LoadModelBRun(self, which);
                                            g_stNoting = noting;
                                            return loaded;
                                        });
    VrProxyObject(At<char*>(self, 0x220), kBdInstance);
    return result;
}
OPENSC4_THUNK(TBdLoadModel, BdLoadModelByMode, 4)

uint32_t BdDescriptionCompared(char* self, uint32_t description)
{
    const auto game = [&] { return g_SC4EffectB_SetDescription(self, nullptr, description); };
    const auto ours = [&] {
        SetEffectDescriptionB(self, Ptr(description));
        return 0u;
    };
    VrScope scope;
    VrProxyObject(At<char*>(self, 0x220), kVrUnknown);
    char* const model = At<char*>(self, 0x224);
    char modelBytes[8] = {};
    if (model) {
        std::memcpy(modelBytes, model, 8);
        VrProxyObject(BdCounted(model), kVrUnknown);
    }
    VrProxyObject(At<char*>(self, 0x278), kBdEnded);
    char* const outer = g_bdSelf;
    g_bdSelf = self;
    const uint32_t result = StSpansCompared(
        At<uint8_t>(Ptr(description), 8) & 1 ? "effects.description_b" : "effects.description_b.none",
        {{self, kEffectSizeB}, {Ptr(kSetupGenerator), kSetupGeneratorSize}}, false, game, ours,
        [&] {
            if (!model || std::memcmp(model, modelBytes, 8) == 0)
                return true;
            StRecord("effects.description_b.skipped", true, std::string(), std::string());
            return false;
        });
    g_bdSelf = outer;
    return result;
}

uint32_t __fastcall BdDescriptionByMode(char* self, void*, uint32_t description)
{
    if (g_descriptionBMode == StepMode::Original || g_stNoting || g_stReplay)
        return g_SC4EffectB_SetDescription(self, nullptr, description);
    if (g_descriptionBMode == StepMode::Ours) {
        SetEffectDescriptionB(self, Ptr(description));
        return 0;
    }
    // Not inside a record and replay of the effects' (another comparison's run).
    if (g_effectRecord || g_effectReplay || BBusy())
        return g_SC4EffectB_SetDescription(self, nullptr, description);
    return BdDescriptionCompared(self, description);
}
OPENSC4_THUNK(TBdDescription, BdDescriptionByMode, 4)

bool HookLoadModelB()
{
    return g_SC4EffectB_LoadModel || Detour(addr::SC4EffectB_LoadModel, &TBdLoadModel, &g_SC4EffectB_LoadModel);
}

bool InstallModelB(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookLoadModelB())
        return false;
    g_modelBMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the second class's model load %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallDescriptionB(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !Detour(addr::SC4EffectB_SetDescription, &TBdDescription, &g_SC4EffectB_SetDescription))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookLoadModelB() &&
          (g_SC4Effect_SetDescription ||
           Detour(addr::SC4Effect_SetDescription, &TSuDescription, &g_SC4Effect_SetDescription)) &&
          (g_SC4EffectPartB_SetDescription || Detour(addr::SC4EffectPartB_SetDescription,
                                                     &TSC4EffectPartB_SetDescription,
                                                     &g_SC4EffectPartB_SetDescription)) &&
          Detour(addr::SC4EffectPartB_Attach, &TBdAttach, &g_SC4EffectPartB_Attach) &&
          Detour(addr::SC4EffectPartB_Place, &TBdPlace, &g_SC4EffectPartB_Place)))
        return false;
    g_descriptionBMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the second class taking a description %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredModelB = registry::Register({
    "renderer.effects.model_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectB_LoadModel,
    "The second class loading its model (0x005AC610): the resource asked for, the model and its counting part "
    "kept, its box's largest side and its materials' flags; compare mode runs both for real on an effect without a "
    "model (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallModelB,
});

const bool g_registeredDescriptionB = registry::Register({
    "renderer.effects.description_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectB_SetDescription,
    "The second class taking a description (0x005AEC90): the base class's, its model let go of and another loaded, "
    "the instance and the part set up; compare mode notes the game's calls (the base class's run, the releases, "
    "the load, the tint and the part's functions) and answers ours from the notes (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallDescriptionB,
});

// --- A particle's life -----------------------------------------------------------------------
//
// A particle's step, an effect's emit and the box of an effect not drawn
// (effects_life.h). The step and the box write only the particle or the
// effect, so they are compared as the set-up is (SuCompared). The emit's
// only call is to one of the three emitters, which take and step
// particles and cannot run twice: ours runs with that call only noted
// (which emitter, the time and the effect as it was then), the effect is
// put back, and the game's runs with detours on the three emitters noting
// the same at their entry; the two notes (or, when neither emits, the
// effects) must be the same.
//
// The three game functions those ported callers still called by address
// (0x005B8020, 0x005B8290, 0x005AC070) are ours when their switches are
// (renderer.effects.step_b1, .start_named and .move_place, at the end of
// this file, which set these).
bool g_stepB1Ours = false, g_startNamedOurs = false, g_movePlaceOurs = false;

#include "core/effects_life.h"

float LfFloat(uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, 4);
    return value;
}

StepMode g_stepMode = StepMode::Original, g_emitMode = StepMode::Original, g_boxMode = StepMode::Original;
Call2Fn g_SC4Effect_StepParticle = nullptr;
Call0Fn g_SC4Effect_Emit = nullptr, g_SC4Effect_NotDrawn = nullptr;

uint32_t __fastcall LfStepByMode(char* self, void*, uint32_t particle, uint32_t step)
{
    const auto game = [&] { return g_SC4Effect_StepParticle(self, nullptr, particle, step); };
    const auto ours = [&] { return StepParticle(self, Ptr(particle), LfFloat(step)); };
    if (g_effectRecord && (g_emitterRecording || g_drawRecording))
        return EffectRecorded(addr::SC4Effect_StepParticle, g_SC4Effect_StepParticle, self, particle, step);
    if (g_stepMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return game();
    if (g_stepMode == StepMode::Ours)
        return ours();
    // The other step has its own comparison.
    if (At<uint32_t>(At<char*>(self, 0x24), 0) & M<uint32_t>(0x00B22E14))
        return game();
    return SuCompared("effects.step", self, Ptr(particle), ours, game);
}
OPENSC4_THUNK(TLfStep, LfStepByMode, 8)

uint32_t __fastcall LfBoxByMode(char* self, void*)
{
    const auto game = [&] { return g_SC4Effect_NotDrawn(self, nullptr); };
    const auto ours = [&] {
        KeepEffectBox(self);
        return 0u;
    };
    if (g_effectRecord && g_drawRecording)
        return EffectRecorded(addr::SC4Effect_NotDrawn, g_SC4Effect_NotDrawn, self);
    if (g_boxMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return game();
    if (g_boxMode == StepMode::Ours)
        return ours();
    return SuCompared("effects.box", self, nullptr, ours, [&] {
        game();
        return 0u;
    });
}
OPENSC4_THUNK(TLfBox, LfBoxByMode, 0)

// One emitter's call as noted: which, the time's bits and the effect then.
struct LfEmitNote {
    uintptr_t emitter;
    uint32_t time;
    char effect[kEffectSize];
};
LfEmitNote g_emitOurs = {}, g_emitGame = {};
bool g_emitWatch = false;
Call1Fn g_SC4Effect_EmitToCount = nullptr, g_SC4Effect_EmitBurst = nullptr, g_SC4Effect_EmitStream = nullptr;

void LfNoteEmitter(LfEmitNote& note, char* self, uintptr_t emitter, uint32_t time)
{
    note.emitter = emitter;
    note.time = time;
    std::memcpy(note.effect, self, kEffectSize);
}

// The emitters by their own switches (effects_emit.h, at the end of the file).
uint32_t EmitterByMode(uintptr_t emitter, Call1Fn original, char* self, uint32_t time);

uint32_t LfEmitterNoted(uintptr_t emitter, Call1Fn original, char* self, uint32_t time)
{
    if (g_emitWatch) {
        g_emitWatch = false;
        LfNoteEmitter(g_emitGame, self, emitter, time);
    }
    return EmitterByMode(emitter, original, self, time);
}

uint32_t __fastcall LfEmitToCountNoted(char* self, void*, uint32_t time)
{
    return LfEmitterNoted(addr::SC4Effect_EmitToCount, g_SC4Effect_EmitToCount, self, time);
}
OPENSC4_THUNK(TLfEmitToCount, LfEmitToCountNoted, 4)

uint32_t __fastcall LfEmitBurstNoted(char* self, void*, uint32_t time)
{
    return LfEmitterNoted(addr::SC4Effect_EmitBurst, g_SC4Effect_EmitBurst, self, time);
}
OPENSC4_THUNK(TLfEmitBurst, LfEmitBurstNoted, 4)

uint32_t __fastcall LfEmitStreamNoted(char* self, void*, uint32_t time)
{
    return LfEmitterNoted(addr::SC4Effect_EmitStream, g_SC4Effect_EmitStream, self, time);
}
OPENSC4_THUNK(TLfEmitStream, LfEmitStreamNoted, 4)

uint32_t __fastcall LfEmitByMode(char* self, void*)
{
    if (g_effectRecord && g_drawRecording)
        return EffectRecorded(addr::SC4Effect_Emit, g_SC4Effect_Emit, self);
    if (g_emitMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4Effect_Emit(self, nullptr);
    if (g_emitMode == StepMode::Ours) {
        EmitParticles(self, [](char* effect, uintptr_t emitter, float time) {
            CallThis<uint32_t>(emitter, effect, time);
        });
        return 0;
    }
    std::vector<char> before(self, self + kEffectSize);
    g_emitOurs.emitter = 0;
    EmitParticles(self, [](char* effect, uintptr_t emitter, float time) {
        uint32_t bits = 0;
        std::memcpy(&bits, &time, 4);
        LfNoteEmitter(g_emitOurs, effect, emitter, bits);
    });
    if (!g_emitOurs.emitter)
        LfNoteEmitter(g_emitOurs, self, 0, 0);
    std::memcpy(self, before.data(), kEffectSize);
    g_emitGame.emitter = 0;
    g_emitWatch = true;
    const uint32_t result = g_SC4Effect_Emit(self, nullptr);
    const bool emitted = !g_emitWatch;
    g_emitWatch = false;
    if (!emitted)
        LfNoteEmitter(g_emitGame, self, 0, 0);
    const bool same = g_emitGame.emitter == g_emitOurs.emitter && g_emitGame.time == g_emitOurs.time &&
                      std::memcmp(g_emitGame.effect, g_emitOurs.effect, kEffectSize) == 0;
    std::string gameText, ourText;
    if (!same) {
        std::size_t at = 0;
        while (at < kEffectSize && g_emitGame.effect[at] == g_emitOurs.effect[at])
            ++at;
        at = at < kEffectSize ? at & ~std::size_t{3} : 0;
        char text[64];
        std::snprintf(text, sizeof text, "emitter %08X time %08X effect+%zX ",
                      static_cast<uint32_t>(g_emitGame.emitter), g_emitGame.time, at);
        gameText = text + Hex(g_emitGame.effect + at, 8);
        std::snprintf(text, sizeof text, "emitter %08X time %08X effect+%zX ",
                      static_cast<uint32_t>(g_emitOurs.emitter), g_emitOurs.time, at);
        ourText = text + Hex(g_emitOurs.effect + at, 8);
    }
    compare::Record("effects.emit", same, gameText, ourText);
    return result;
}
OPENSC4_THUNK(TLfEmit, LfEmitByMode, 0)

bool InstallLifeStep(registry::Mode mode)
{
    return SuInstall(mode, g_stepMode, addr::SC4Effect_StepParticle, &TLfStep, &g_SC4Effect_StepParticle,
                     "a particle's step");
}

bool InstallLifeBox(registry::Mode mode)
{
    return SuInstall(mode, g_boxMode, addr::SC4Effect_NotDrawn, &TLfBox, &g_SC4Effect_NotDrawn,
                     "the box of an effect not drawn");
}

bool InstallLifeEmit(registry::Mode mode)
{
    if (mode == registry::Mode::Compare && game::IsBuild641() &&
        !((g_SC4Effect_EmitToCount ||
           Detour(addr::SC4Effect_EmitToCount, &TLfEmitToCount, &g_SC4Effect_EmitToCount)) &&
          (g_SC4Effect_EmitBurst || Detour(addr::SC4Effect_EmitBurst, &TLfEmitBurst, &g_SC4Effect_EmitBurst)) &&
          (g_SC4Effect_EmitStream || Detour(addr::SC4Effect_EmitStream, &TLfEmitStream, &g_SC4Effect_EmitStream))))
        return false;
    return SuInstall(mode, g_emitMode, addr::SC4Effect_Emit, &TLfEmit, &g_SC4Effect_Emit, "an effect's emit");
}

const bool g_registeredLifeStep = registry::Register({
    "renderer.effects.step",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StepParticle,
    "A particle's step (0x005B9BA0): its age against its life, its velocity and position; the other step's "
    "descriptions go on to 0x005B9790; compare mode runs ours and the game's on the same particle "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallLifeStep,
});

const bool g_registeredLifeEmit = registry::Register({
    "renderer.effects.emit",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_Emit,
    "An effect's emit (0x005B96E0): its countdown and the time handed to one of the three emitters; compare mode "
    "notes the emitter's call in both runs (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallLifeEmit,
});

const bool g_registeredLifeBox = registry::Register({
    "renderer.effects.box",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_NotDrawn,
    "The box around the particles of an effect not drawn (0x005B6390); compare mode runs ours and the game's on "
    "the same effect (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallLifeBox,
});

// The wandering and the pull write only the effect (and the generator);
// the pull's service is asked twice. The other step writes the particle,
// and the game's functions it calls are run in both: the move along a
// path (0x005B6600) and the meeting with the ground (0x005B8DC0) write the
// particle and the effect and draw random numbers, which are put back. A
// step that could start another effect (an effect with +0x218 or +0x219
// set, or a description with bit 0 of its +4 for 0x005B8020) is not
// compared: such a start cannot run twice.
StepMode g_wanderMode = StepMode::Original, g_pullMode = StepMode::Original, g_stepFullMode = StepMode::Original;
Call1Fn g_SC4Effect_Wander = nullptr;
Call0Fn g_SC4Effect_PathVelocity = nullptr;
Call2Fn g_SC4Effect_StepParticleFull = nullptr;

uint32_t __fastcall LfWanderByMode(char* self, void*, uint32_t step)
{
    const auto game = [&] { return g_SC4Effect_Wander(self, nullptr, step); };
    const auto ours = [&] {
        WanderEffect(self, LfFloat(step));
        return 0u;
    };
    if (g_effectRecord && g_drawRecording)
        return EffectRecorded(addr::SC4Effect_StepFlag800000, g_SC4Effect_Wander, self, step);
    if (g_wanderMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return game();
    if (g_wanderMode == StepMode::Ours)
        return ours();
    return SuCompared("effects.wander", self, nullptr, ours, [&] {
        game();
        return 0u;
    });
}
OPENSC4_THUNK(TLfWander, LfWanderByMode, 4)

uint32_t __fastcall LfPullByMode(char* self, void*)
{
    const auto game = [&] { return g_SC4Effect_PathVelocity(self, nullptr); };
    const auto ours = [&] {
        PullEffect(self);
        return 0u;
    };
    if (g_effectRecord && g_drawRecording)
        return EffectRecorded(addr::SC4Effect_PathVelocity, g_SC4Effect_PathVelocity, self);
    if (g_pullMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return game();
    if (g_pullMode == StepMode::Ours)
        return ours();
    return SuCompared("effects.pull", self, nullptr, ours, [&] {
        game();
        return 0u;
    });
}
OPENSC4_THUNK(TLfPull, LfPullByMode, 0)

uint32_t __fastcall LfStepFullByMode(char* self, void*, uint32_t particle, uint32_t step)
{
    const auto game = [&] { return g_SC4Effect_StepParticleFull(self, nullptr, particle, step); };
    const auto ours = [&] { return StepParticleFull(self, Ptr(particle), LfFloat(step)); };
    if (g_stepFullMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return game();
    if (g_stepFullMode == StepMode::Ours)
        return ours();
    if (At<uint8_t>(self, 0x218) || At<uint8_t>(self, 0x219) || (At<uint8_t>(At<char*>(self, 0x24), 4) & 1)) {
        compare::Record("effects.step_full.not_compared", true, std::string(), std::string());
        return game();
    }
    return SuCompared("effects.step_full", self, Ptr(particle), ours, game);
}
OPENSC4_THUNK(TLfStepFull, LfStepFullByMode, 8)

bool InstallLifeWander(registry::Mode mode)
{
    return SuInstall(mode, g_wanderMode, addr::SC4Effect_StepFlag800000, &TLfWander, &g_SC4Effect_Wander,
                     "an effect's wandering");
}

bool InstallLifePull(registry::Mode mode)
{
    return SuInstall(mode, g_pullMode, addr::SC4Effect_PathVelocity, &TLfPull, &g_SC4Effect_PathVelocity,
                     "the pull on an effect's particles");
}

bool InstallLifeStepFull(registry::Mode mode)
{
    return SuInstall(mode, g_stepFullMode, addr::SC4Effect_StepParticleFull, &TLfStepFull,
                     &g_SC4Effect_StepParticleFull, "a particle's other step");
}

const bool g_registeredLifeWander = registry::Register({
    "renderer.effects.wander",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StepFlag800000,
    "An effect's wandering (0x005B2730): its timer and its own velocity turned by random amounts; compare mode runs "
    "ours and the game's on the same effect and generator (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallLifeWander,
});

const bool g_registeredLifePull = registry::Register({
    "renderer.effects.pull",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_PathVelocity,
    "The pull on an effect's particles from outside (0x005B6C40); compare mode runs ours and the game's on the same "
    "effect (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallLifePull,
});

const bool g_registeredLifeStepFull = registry::Register({
    "renderer.effects.step_full",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StepParticleFull,
    "A particle's other step (0x005B9790): in parts, with the pull towards the effect, the damping, the map's "
    "edges and the meeting with the ground (still the game's); compare mode runs ours and the game's on the same "
    "particle, effect and generator (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallLifeStepFull,
});

// The start edge (0x005B7030) writes only the effect and is compared as
// the pull. The starts at set ages (0x005B8410) start other effects
// (0x005B8290), which cannot run twice: ours runs first with each start
// only noted (the effect, the suffix, the place's address and the place),
// then the game's with 0x005B8290's calls noted at its entry (a detour in
// compare mode) and passed on. The notes must be the same, and ours must
// leave the effect as it was.
//
// No description in the test cities has flag 0x2000 or 0x80000000, so
// neither is called there. With OPENSC4_EFFECT_FORCE_STEPS=1 (compare mode
// only) both run on every running effect before its draw (LfForceSteps),
// and what they change is put back after. The start edge: one choice per
// draw in turn of the description's flag 0x2000 (set or cleared), the
// vector at +0x1E8 (the effect's own or one of seven made up: each order
// of its two floats against each other and zero, equal ones, a NaN) and
// the word (0 to 4, or -1). The starts at set ages: the description's
// table swapped for eight made-up entries with ages around the first
// running particle's (its age, half a step and a step on, the floats just
// below and above it, zero, far off, a NaN), whose starts the game's run
// only notes. They are recorded as effects.start_edge.forced and
// effects.age_starts.forced.
StepMode g_edgeMode = StepMode::Original, g_agesMode = StepMode::Original;
Call1Fn g_SC4Effect_StartEdge = nullptr;
Call0Fn g_SC4Effect_StartAtAges = nullptr;
Call2Fn g_SC4Effect_StartNamed = nullptr;

uint32_t __fastcall LfEdgeByMode(char* self, void*, uint32_t which)
{
    const auto game = [&] { return g_SC4Effect_StartEdge(self, nullptr, which); };
    const auto ours = [&] {
        SetStartEdge(self, which);
        return 0u;
    };
    if (g_effectRecord && g_drawRecording)
        return EffectRecorded(addr::SC4Effect_StartEdge, g_SC4Effect_StartEdge, self, which);
    if (g_edgeMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return game();
    if (g_edgeMode == StepMode::Ours)
        return ours();
    return SuCompared("effects.start_edge", self, nullptr, ours, [&] {
        game();
        return 0u;
    });
}
OPENSC4_THUNK(TLfEdge, LfEdgeByMode, 4)

// One start as noted.
struct LfStartNote {
    uint32_t effect, suffix, place, at[3];
    bool operator==(const LfStartNote& other) const { return std::memcmp(this, &other, sizeof *this) == 0; }
};
std::vector<LfStartNote> g_agesOurs, g_agesGame;
bool g_agesWatch = false, g_agesForced = false;
int g_agesDepth = 0;
DWORD g_agesThread = 0;

LfStartNote LfNoteStart(char* effect, uint32_t suffix, char* place)
{
    LfStartNote note{Word(effect), suffix, Word(place), {}};
    std::memcpy(note.at, place, sizeof note.at);
    return note;
}

// The start itself by renderer.effects.start_named's mode (defined with it).
uint32_t EnNamedRun(char* self, uint32_t suffix, uint32_t place);

uint32_t __fastcall LfStartNamedNoted(char* self, void*, uint32_t suffix, uint32_t place)
{
    if (!g_agesWatch || g_agesDepth > 0 || GetCurrentThreadId() != g_agesThread)
        return EnNamedRun(self, suffix, place);
    g_agesGame.push_back(LfNoteStart(self, suffix, Ptr(place)));
    if (g_agesForced)
        return 0;
    ++g_agesDepth;
    const uint32_t result = EnNamedRun(self, suffix, place);
    --g_agesDepth;
    return result;
}
OPENSC4_THUNK(TLfStartNamed, LfStartNamedNoted, 8)

std::string LfStartText(const std::vector<LfStartNote>& notes, std::size_t at)
{
    char text[112];
    if (at >= notes.size()) {
        std::snprintf(text, sizeof text, "%zu starts", notes.size());
        return text;
    }
    const LfStartNote& n = notes[at];
    std::snprintf(text, sizeof text, "%zu starts, #%zu effect %08X suffix %08X place %08X ", notes.size(), at, n.effect,
                  n.suffix, n.place);
    return text + Hex(reinterpret_cast<const char*>(n.at), sizeof n.at);
}

// Ours and the game's run with their starts noted (the game's through
// 0x005B8290's detour); the effect must be left as it was.
template <typename Ours, typename Game>
void LfStartsCompared(const char* id, char* self, Ours&& ours, Game&& game)
{
    const std::vector<char> before(self, self + kEffectSize);
    g_agesOurs.clear();
    ours([](char* effect, uint32_t suffix, char* place) { g_agesOurs.push_back(LfNoteStart(effect, suffix, place)); });
    std::size_t wrote = 0;
    while (wrote < kEffectSize && before[wrote] == self[wrote])
        ++wrote;
    g_agesGame.clear();
    g_agesThread = GetCurrentThreadId();
    g_agesWatch = true;
    game();
    g_agesWatch = false;
    const bool same = wrote == kEffectSize && g_agesOurs == g_agesGame;
    std::string gameText, ourText;
    if (!same) {
        std::size_t at = 0;
        while (at < g_agesOurs.size() && at < g_agesGame.size() && g_agesOurs[at] == g_agesGame[at])
            ++at;
        gameText = LfStartText(g_agesGame, at);
        ourText = LfStartText(g_agesOurs, at);
        if (wrote < kEffectSize) {
            char text[40];
            std::snprintf(text, sizeof text, ", wrote effect+%zX", wrote);
            ourText += text;
        }
    }
    compare::Record(id, same, gameText, ourText);
}

void LfAgesCompared(const char* id, char* self)
{
    LfStartsCompared(
        id, self, [&](auto note) { StartAtAges(self, note); }, [&] { g_SC4Effect_StartAtAges(self, nullptr); });
}

uint32_t __fastcall LfAgesByMode(char* self, void*)
{
    if (g_effectRecord && g_drawRecording)
        return EffectRecorded(addr::SC4Effect_StartAtAges, g_SC4Effect_StartAtAges, self);
    if (g_agesMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4Effect_StartAtAges(self, nullptr);
    if (g_agesMode == StepMode::Ours) {
        StartAtAges(self, [](char* effect, uint32_t suffix, char* place) {
            LfStartNamed(effect, suffix, place);
        });
        return 0;
    }
    LfAgesCompared("effects.age_starts", self);
    return 0;
}
OPENSC4_THUNK(TLfAges, LfAgesByMode, 0)

// The starts at birth (0x005B84C0) are compared as the starts at set ages.
// The emitters' take-and-step reaches them only for descriptions with flag
// 0x80000000, which no test city has; OPENSC4_EFFECT_FORCE_STEPS=1 runs
// them in LfForceSteps too (effects.birth_starts.forced). The pool's and
// the draw's comparisons record them through this detour.
StepMode g_birthMode = StepMode::Original;

void LfBirthCompared(const char* id, char* self, char* particle)
{
    LfStartsCompared(
        id, self, [&](auto note) { StartAtBirth(self, particle, note); },
        [&] { g_SC4Effect_StartAtBirth(self, nullptr, Word(particle)); });
}

uint32_t __fastcall LfBirthByMode(char* self, void*, uint32_t particle)
{
    if (g_effectRecord && Recorded(addr::SC4Effect_StartAtBirth))
        return EffectRecorded(addr::SC4Effect_StartAtBirth, g_SC4Effect_StartAtBirth, self, particle);
    if (g_birthMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4Effect_StartAtBirth(self, nullptr, particle);
    if (g_birthMode == StepMode::Ours) {
        StartAtBirth(self, Ptr(particle), [](char* effect, uint32_t suffix, char* place) {
            LfStartNamed(effect, suffix, place);
        });
        return 0;
    }
    LfBirthCompared("effects.birth_starts", self, Ptr(particle));
    return 0;
}
OPENSC4_THUNK(TLfBirth, LfBirthByMode, 4)

bool g_forceSteps = false;

void LfForceSteps(char* self)
{
    if (!g_forceSteps || g_effectRecord || g_effectReplay || !At<uint8_t>(self, 9) || !At<char*>(self, 0x24))
        return;
    char* const description = At<char*>(self, 0x24);
    const uint32_t flags = At<uint32_t>(description, 0);
    if (g_edgeMode == StepMode::Compare) {
        static uint32_t turn = 0;
        static const uint32_t words[] = {0, 1, 2, 3, 4, 0xFFFFFFFFu};
        static const uint32_t pairs[][2] = {
            {0x3F800000u, 0},           {0xBF800000u, 0xC0000000u}, {0, 0xBF800000u}, {0xBF800000u, 0x3F800000u},
            {0xC0000000u, 0xBF800000u}, {0x3F000000u, 0x3F000000u}, {0x7FC00000u, 0},
        };
        const uint32_t k = turn++ % 96;
        const uint32_t vector = k / 2 % 8;
        const uint32_t word = words[k / 16];
        uint32_t made[5] = {};
        char saved[kEffectSize];
        std::memcpy(saved, self, kEffectSize);
        if (vector) {
            made[2] = pairs[vector - 1][0];
            made[4] = pairs[vector - 1][1];
            At<uint32_t*>(self, 0x1E8) = made;
        }
        At<uint32_t>(description, 0) = (k & 1) && At<char*>(self, 0x1E8) ? flags | 0x2000u : flags & ~0x2000u;
        SuCompared(
            "effects.start_edge.forced", self, nullptr,
            [&] {
                SetStartEdge(self, word);
                return 0u;
            },
            [&] {
                g_SC4Effect_StartEdge(self, nullptr, word);
                return 0u;
            });
        std::memcpy(self, saved, kEffectSize);
        At<uint32_t>(description, 0) = flags;
    }
    if (g_birthMode == StepMode::Compare) {
        char* particle = nullptr;
        for (char* node = At<char*>(At<char*>(self, 0x14), 0); node != At<char*>(self, 0x14) && !particle;
             node = At<char*>(node, 0)) {
            if (At<uint8_t>(At<char*>(node, 8), 0))
                particle = At<char*>(node, 8);
        }
        if (particle) {
            // Ages in turn: all zero, zero and -0 then the smallest float,
            // a NaN first, one first, seven zeros and -1, no entries.
            static uint32_t turn = 0;
            static const char suffixes[8][8] = {"_e6a", "_e6b", "_e6c", "_e6d", "_e6e", "_e6f", "_e6g", "_e6h"};
            static const uint32_t ages[5][8] = {
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0x80000000u, 1, 0, 0, 0, 0, 0},
                {0x7FC00000u, 0, 0, 0, 0, 0, 0, 0},
                {0x3F800000u, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0xBF800000u},
            };
            const uint32_t k = turn++ % 6;
            uint32_t table[8][4] = {};
            for (uint32_t i = 0; i < 8; ++i) {
                table[i][0] = Word(const_cast<char*>(suffixes[i]));
                table[i][3] = k < 5 ? ages[k][i] : 0;
            }
            // A copy of the particle that does not run starts nothing.
            char stopped[0x18];
            std::memcpy(stopped, particle, sizeof stopped);
            stopped[0] = 0;
            const uint32_t begin = At<uint32_t>(description, 0x1CC), end = At<uint32_t>(description, 0x1D0);
            At<uint32_t*>(description, 0x1CC) = table[0];
            At<uint32_t*>(description, 0x1D0) = k < 5 ? table[0] + 32 : table[0];
            g_agesForced = true;
            LfBirthCompared("effects.birth_starts.forced", self, particle);
            LfBirthCompared("effects.birth_starts.forced", self, stopped);
            g_agesForced = false;
            At<uint32_t>(description, 0x1CC) = begin;
            At<uint32_t>(description, 0x1D0) = end;
        }
    }
    if (g_agesMode == StepMode::Compare) {
        char* particle = nullptr;
        for (char* node = At<char*>(At<char*>(self, 0x14), 0); node != At<char*>(self, 0x14) && !particle;
             node = At<char*>(node, 0)) {
            if (At<uint8_t>(At<char*>(node, 8), 0))
                particle = At<char*>(node, 8);
        }
        if (!particle)
            return;
        static const char suffixes[8][8] = {"_e5a", "_e5b", "_e5c", "_e5d", "_e5e", "_e5f", "_e5g", "_e5h"};
        const float age = At<float>(particle, 4);
        const float step = At<float>(self, 0x108);
        const uint32_t nan = 0x7FC00000u;
        float notANumber = 0.0f;
        std::memcpy(&notANumber, &nan, 4);
        const float ages[8] = {age,
                               age + step * 0.5f,
                               age + step,
                               std::nextafter(age, -1.0e30f),
                               std::nextafter(age, 1.0e30f),
                               0.0f,
                               age + 1000.0f,
                               notANumber};
        uint32_t table[8][4] = {};
        for (uint32_t i = 0; i < 8; ++i) {
            table[i][0] = Word(const_cast<char*>(suffixes[i]));
            std::memcpy(&table[i][3], &ages[i], 4);
        }
        const uint32_t begin = At<uint32_t>(description, 0x1CC), end = At<uint32_t>(description, 0x1D0);
        At<uint32_t*>(description, 0x1CC) = table[0];
        At<uint32_t*>(description, 0x1D0) = table[0] + 32;
        g_agesForced = true;
        LfAgesCompared("effects.age_starts.forced", self);
        g_agesForced = false;
        At<uint32_t>(description, 0x1CC) = begin;
        At<uint32_t>(description, 0x1D0) = end;
    }
}

// Forcing hooks the effect's draw (DwHookEffectDraw, below).
bool DwHookEffectDraw();

bool LfHookForce()
{
    const char* v = std::getenv("OPENSC4_EFFECT_FORCE_STEPS");
    if (!v || *v != '1')
        return true;
    if (!DwHookEffectDraw())
        return false;
    if (!g_forceSteps)
        log::Write("water effects: the start edge and the starts at set ages and at birth forced before each effect's draw");
    g_forceSteps = true;
    return true;
}

bool InstallLifeEdge(registry::Mode mode)
{
    return SuInstall(mode, g_edgeMode, addr::SC4Effect_StartEdge, &TLfEdge, &g_SC4Effect_StartEdge,
                     "the edge where an effect's particles start") &&
           (mode != registry::Mode::Compare || LfHookForce());
}

bool InstallLifeAges(registry::Mode mode)
{
    if (mode == registry::Mode::Compare && game::IsBuild641() &&
        !(g_SC4Effect_StartNamed || Detour(addr::SC4Effect_StartNamed, &TLfStartNamed, &g_SC4Effect_StartNamed)))
        return false;
    return SuInstall(mode, g_agesMode, addr::SC4Effect_StartAtAges, &TLfAges, &g_SC4Effect_StartAtAges,
                     "the effects started at set ages of particles") &&
           (mode != registry::Mode::Compare || LfHookForce());
}

bool InstallLifeBirth(registry::Mode mode)
{
    if (mode == registry::Mode::Compare && game::IsBuild641() &&
        !(g_SC4Effect_StartNamed || Detour(addr::SC4Effect_StartNamed, &TLfStartNamed, &g_SC4Effect_StartNamed)))
        return false;
    return SuInstall(mode, g_birthMode, addr::SC4Effect_StartAtBirth, &TLfBirth, &g_SC4Effect_StartAtBirth,
                     "the effects started where particles are born") &&
           (mode != registry::Mode::Compare || LfHookForce());
}

const bool g_registeredLifeEdge = registry::Register({
    "renderer.effects.start_edge",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StartEdge,
    "The limits of where an effect's particles start put on one edge of the map (0x005B7030), for descriptions with "
    "flag 0x2000; compare mode runs ours and the game's on the same effect, OPENSC4_EFFECT_FORCE_STEPS=1 forces it "
    "on every running effect (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallLifeEdge,
});

const bool g_registeredLifeAges = registry::Register({
    "renderer.effects.age_starts",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StartAtAges,
    "Effects started where particles reach set ages (0x005B8410), for descriptions with flag 0x80000000; compare "
    "mode notes the starts in ours and the game's, OPENSC4_EFFECT_FORCE_STEPS=1 forces it on every running effect "
    "with made-up ages (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallLifeAges,
});

const bool g_registeredLifeBirth = registry::Register({
    "renderer.effects.birth_starts",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StartAtBirth,
    "Effects started where the emitters' particles are born (0x005B84C0), for descriptions with flag 0x80000000; "
    "compare mode notes the starts in ours and the game's, OPENSC4_EFFECT_FORCE_STEPS=1 forces it on every running "
    "effect with made-up tables (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallLifeBirth,
});

// The meeting with the ground writes the particle and the effect, draws
// random numbers and asks the terrain, all of which can be done twice; the
// effects it may start ("_water", "_terrain", 0x005B8290) cannot, so an
// effect with +0x218 or +0x219 set is not compared.
StepMode g_hitMode = StepMode::Original;
Call3Fn g_SC4Effect_MeetGround = nullptr;

uint32_t __fastcall LfHitByMode(char* self, void*, uint32_t particle, uint32_t grid, uint32_t part)
{
    const auto game = [&] { return g_SC4Effect_MeetGround(self, nullptr, particle, grid, part); };
    const auto ours = [&] {
        return ParticleMeetsGround(self, Ptr(particle), reinterpret_cast<float*>(Ptr(grid)), LfFloat(part));
    };
    if (g_hitMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return game();
    if (g_hitMode == StepMode::Ours)
        return ours();
    if (At<uint8_t>(self, 0x218) || At<uint8_t>(self, 0x219)) {
        compare::Record("effects.hit.not_compared", true, std::string(), std::string());
        return game();
    }
    return SuCompared("effects.hit", self, Ptr(particle), ours, game);
}
OPENSC4_THUNK(TLfHit, LfHitByMode, 12)

bool InstallLifeHit(registry::Mode mode)
{
    return SuInstall(mode, g_hitMode, addr::SC4Effect_StepFlag100, &TLfHit, &g_SC4Effect_MeetGround,
                     "a particle meeting the water or the ground");
}

const bool g_registeredLifeHit = registry::Register({
    "renderer.effects.hit",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StepFlag100,
    "A particle meeting the water or the ground (0x005B8DC0): ended by chance in the water, pushed, put on the "
    "ground, sliding or bouncing; compare mode runs ours and the game's on the same particle, effect and generator "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallLifeHit,
});

// --- An effect's emitters --------------------------------------------------------------------
//
// The three emitters (effects_emit.h) take, step, add and give back
// particles, which cannot run twice: they are compared by the record and
// replay of the effect's draw (RecordAndReplay), the pool's functions and
// the particle's step recorded in the game's run and answered from the
// record in ours. The stream's and the burst's own draws from the
// generator are recorded too (a detour on 0x00913761 that notes only the
// draws made by the emitter itself, on the emitter's thread) and handed
// to ours in the replay. The emit's comparison detours the same three
// functions; both go through one thunk each (TLfEmitToCount and its two),
// which notes for the emit and then goes by the emitter's own switch.
using UniformFn = double(__fastcall*)(void*, void*, double, double);
UniformFn g_SC4Random_Uniform = nullptr;
std::vector<double> g_emitterDraws;
std::size_t g_emitterDraw = 0;

uint32_t LfFloatBits(double value)
{
    const float f = static_cast<float>(value);
    uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    return bits;
}

double __fastcall LfUniformNoted(void* random, void*, double low, double high)
{
    const double value = g_SC4Random_Uniform(random, nullptr, low, high);
    if (g_emitterRecording && g_effectRecord && g_effectNested == 0 && GetCurrentThreadId() == g_effectThread) {
        g_effectGameCalls.push_back(
            EffectCall{addr::SC4Random_Uniform, reinterpret_cast<uint32_t>(random), {LfFloatBits(low), LfFloatBits(high)}});
        g_effectChanges.emplace_back();
        g_emitterDraws.push_back(value);
    }
    return value;
}
OPENSC4_THUNK(TLfUniform, LfUniformNoted, 16)

// The emitter's draw between two of the description's floats: in the
// replay the game's draw, noted as a call.
double LfEmitterUniform(double low, double high)
{
    if (!g_effectReplay)
        return SuUniform(low, high);
    g_effectOurCalls.push_back(EffectCall{addr::SC4Random_Uniform, 0x00B22DF8u, {LfFloatBits(low), LfFloatBits(high)}});
    return g_emitterDraw < g_emitterDraws.size() ? g_emitterDraws[g_emitterDraw++] : 0.0;
}

#include "core/effects_emit.h"

StepMode g_emitCountMode = StepMode::Original, g_emitStreamMode = StepMode::Original,
         g_emitBurstMode = StepMode::Original;

struct LfEmitterCall {
    char* self;
    float time;
    void (*ours)(char*, float);
};

uint32_t EmitterByMode(uintptr_t emitter, Call1Fn original, char* self, uint32_t time)
{
    StepMode mode = StepMode::Original;
    const char* id = nullptr;
    void (*ours)(char*, float) = nullptr;
    if (emitter == addr::SC4Effect_EmitToCount) {
        mode = g_emitCountMode;
        id = "effects.emit_count";
        ours = &EmitToCount;
    } else if (emitter == addr::SC4Effect_EmitStream) {
        mode = g_emitStreamMode;
        id = "effects.emit_stream";
        ours = &EmitStream;
    } else if (emitter == addr::SC4Effect_EmitBurst) {
        mode = g_emitBurstMode;
        id = "effects.emit_burst";
        ours = &EmitBurst;
    }
    if (mode == StepMode::Original || g_effectRecord || g_effectReplay)
        return original(self, nullptr, time);
    if (mode == StepMode::Ours) {
        ours(self, LfFloat(time));
        return 0;
    }
    g_emitterDraws.clear();
    g_emitterDraw = 0;
    LfEmitterCall call{self, LfFloat(time), ours};
    g_emitterRecording = true;
    const uint32_t result = RecordAndReplay(
        id, self, false, [&] { return original(self, nullptr, time); },
        [](void* c) {
            const auto* const call = static_cast<LfEmitterCall*>(c);
            call->ours(call->self, call->time);
            return 0u;
        },
        &call);
    g_emitterRecording = false;
    return result;
}

bool InstallEmitter(registry::Mode mode, StepMode& target, uintptr_t address, void (*thunk)(), Call1Fn* original,
                    const char* what)
{
    if (!game::IsBuild641() || !(*original || Detour(address, thunk, original)))
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookFrees() && HookPool() &&
          (g_SC4Effect_StepParticle || Detour(addr::SC4Effect_StepParticle, &TLfStep, &g_SC4Effect_StepParticle)) &&
          (g_SC4Random_Uniform || Detour(addr::SC4Random_Uniform, &TLfUniform, &g_SC4Random_Uniform))))
        return false;
    target = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: %s %s", what, mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallEmitCount(registry::Mode mode)
{
    return InstallEmitter(mode, g_emitCountMode, addr::SC4Effect_EmitToCount, &TLfEmitToCount,
                          &g_SC4Effect_EmitToCount, "the count emitter");
}

bool InstallEmitStream(registry::Mode mode)
{
    return InstallEmitter(mode, g_emitStreamMode, addr::SC4Effect_EmitStream, &TLfEmitStream,
                          &g_SC4Effect_EmitStream, "the stream emitter");
}

bool InstallEmitBurst(registry::Mode mode)
{
    return InstallEmitter(mode, g_emitBurstMode, addr::SC4Effect_EmitBurst, &TLfEmitBurst, &g_SC4Effect_EmitBurst,
                          "the burst emitter");
}

const bool g_registeredEmitCount = registry::Register({
    "renderer.effects.emit_count",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_EmitToCount,
    "The count emitter (0x005B3CF0): the time times the rate added up, and particles taken until the effect has "
    "the number its description wants; compare mode records the game's run and replays ours (docs/renderer.md, "
    "\"Water effects, sky and clouds\")",
    true,
    InstallEmitCount,
});

const bool g_registeredEmitStream = registry::Register({
    "renderer.effects.emit_stream",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_EmitStream,
    "The stream emitter (0x005B87B0): particles taken and stepped slice by slice at the description's rate, with "
    "pauses drawn between them; compare mode records the game's run, its steps and draws, and replays ours "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallEmitStream,
});

const bool g_registeredEmitBurst = registry::Register({
    "renderer.effects.emit_burst",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_EmitBurst,
    "The burst emitter (0x005B8B50): bursts of the description's size taken and stepped, with pauses drawn "
    "between them; compare mode records the game's run, its steps and draws, and replays ours (docs/renderer.md, "
    "\"Water effects, sky and clouds\")",
    true,
    InstallEmitBurst,
});

// --- The second class's take, give back and step out of view -----------------------------------
//
// The second effect class's take of a particle, its give back and its step
// when it is not drawn (effects_second.h) ask the effects manager to make
// effects, call the particles' own effects and the object at the effect's
// +0x278, none of which can run twice. Their comparison is the notes'
// engine's (as the holders'), the game's first: the manager, each
// particle's own effect, the effect the manager makes and the object at
// +0x278 get copies of their tables of functions whose slots note each
// call (on this thread, outside other notes; the restart of a particle,
// 0x005AF060, by a detour of its own), and the game's runs with the calls
// passed on. The effect, the class's free list, the generator and the
// particles are put back and ours runs with every such call answered from
// the notes; the calls and those bytes must be the same, and what the
// game's left is put back. The particle's start and the block (0x005ACA30)
// run in both, from the same bytes.
#include "core/effects_second.h"

constexpr uint32_t kSdOwnRole = 0xE000, kSdManagerRole = 0xE100, kSdHeldRole = 0xE200;

StepMode g_poolBMode = StepMode::Original, g_giveBackBMode = StepMode::Original,
         g_notDrawnBMode = StepMode::Original;
Call1Fn g_SC4EffectB_RestartParticle = nullptr;
Call0Fn g_SC4EffectB_NotDrawn = nullptr;

// The noting slots of a particle's own effect: with one word (0xC, 0x10),
// none (0x14), a block (0x1C, 0x20), or a kind, a pointer and a flag (0x30,
// 0x34), noted as the calls of effects_second.h note them.
template <uint32_t Slot>
uint32_t __fastcall SdOwnWordStub(char* object, void*, uint32_t value)
{
    const uint32_t real = BRealSlot(object, Slot);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, value);
    return BCallee(kSdOwn + Slot, Word(object), Word(&value), 4,
                   [&] { return CallThis<uint32_t>(real, object, value); });
}

uint32_t __fastcall SdOwnAskStub(char* object, void*)
{
    const uint32_t real = BRealSlot(object, 0x14);
    if (!BNoting())
        return CallThis<uint32_t>(real, object);
    return BCallee(kSdOwn + 0x14, Word(object), 0, 0, [&] { return CallThis<uint32_t>(real, object); });
}

template <uint32_t Slot>
uint32_t __fastcall SdOwnBlockStub(char* object, void*, uint32_t block)
{
    const uint32_t real = BRealSlot(object, Slot);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, block);
    return BCallee(kSdOwn + Slot, Word(object), block, kBBlock, [&] { return CallThis<uint32_t>(real, object, block); });
}

template <uint32_t Slot>
uint32_t __fastcall SdOwnValueStub(char* object, void*, uint32_t kind, uint32_t value, uint32_t flag)
{
    const uint32_t real = BRealSlot(object, Slot);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, kind, value, flag);
    const uint32_t noted[4] = {kind, M<uint32_t>(value), flag, Slot == 0x34 ? value : 0u};
    return BCallee(kSdOwn + Slot, Word(object), Word(noted), sizeof noted,
                   [&] { return CallThis<uint32_t>(real, object, kind, value, flag); });
}

void SdWatch(char* object)
{
    BSwapRole(object, kSdOwnRole,
              {{0x8, reinterpret_cast<uint32_t>(&BRelease)},
               {0xC, reinterpret_cast<uint32_t>(&SdOwnWordStub<0xC>)},
               {0x10, reinterpret_cast<uint32_t>(&SdOwnWordStub<0x10>)},
               {0x14, reinterpret_cast<uint32_t>(&SdOwnAskStub)},
               {0x1C, reinterpret_cast<uint32_t>(&SdOwnBlockStub<0x1C>)},
               {0x20, reinterpret_cast<uint32_t>(&SdOwnBlockStub<0x20>)},
               {0x30, reinterpret_cast<uint32_t>(&SdOwnValueStub<0x30>)},
               {0x34, reinterpret_cast<uint32_t>(&SdOwnValueStub<0x34>)}});
}

// The manager's make: noted with the name, and what it made watched.
uint32_t __fastcall SdMakeStub(char* manager, void*, const char* name, uint32_t out)
{
    const uint32_t real = BRealSlot(manager, 0x1C);
    if (!BNoting())
        return CallThis<uint32_t>(real, manager, name, out);
    const uint32_t made = BCallee(kSdMake, 0, Word(name), kBString,
                                  [&] { return CallThis<uint32_t>(real, manager, name, out); }, out);
    SdWatch(M<char*>(out));
    return made;
}

uint32_t __fastcall SdHeldStub(char* object, void*, uint32_t value)
{
    const uint32_t real = BRealSlot(object, 0x4);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, value);
    return BCallee(kSdHeld, Word(object), Word(&value), 4, [&] { return CallThis<uint32_t>(real, object, value); });
}

uint32_t __fastcall SdRestartNoted(char* self, void*, uint32_t particle)
{
    if (!BNoting())
        return g_SC4EffectB_RestartParticle(self, nullptr, particle);
    return BCallee(addr::SC4EffectB_RestartParticle, particle, 0, 0,
                   [&] { return g_SC4EffectB_RestartParticle(self, nullptr, particle); });
}
OPENSC4_THUNK(TSdRestart, SdRestartNoted, 4)

// The bytes the noted calls may change: the effect, the class's free list,
// the generator and the particles.
std::vector<BRegion> SdRegions(char* self, const std::vector<char*>& particles)
{
    std::vector<BRegion> regions{{self, kEffectSizeB},
                                 {Ptr(kParticleFreeB), 4},
                                 {Ptr(kSetupGenerator), kSetupGeneratorSize}};
    for (char* const particle : particles)
        regions.push_back({particle, kParticleBytesB});
    return regions;
}

void SdRestore(const std::vector<char>& bytes)
{
    std::size_t at = 0;
    for (const BRegion& region : g_bRegions) {
        std::memcpy(region.at, bytes.data() + at, region.size);
        at += region.size;
    }
}

// Where a byte of the regions is: the effect, the free list, the
// generator or the particle (by its place in the regions).
std::string SdWhere(std::size_t offset)
{
    static const char* const kNames[] = {"effect", "free", "generator"};
    char text[40];
    for (std::size_t i = 0; i < g_bRegions.size(); ++i) {
        if (offset < g_bRegions[i].size) {
            if (i < 3)
                std::snprintf(text, sizeof text, "%s+%zX ", kNames[i], offset);
            else
                std::snprintf(text, sizeof text, "particle %zu+%zX ", i - 3, offset);
            return text;
        }
        offset -= g_bRegions[i].size;
    }
    return "? ";
}

// The calls the game's runs made, by what they were, logged for each
// comparison at every power of two of the runs compared.
void SdCount(const char* id)
{
    struct Counts {
        uint64_t runs = 0;
        std::map<uint32_t, uint64_t> calls;
    };
    static std::map<std::string, Counts> counts;
    Counts& mine = counts[id];
    for (const BCall& call : g_bGameCalls)
        ++mine.calls[call.target];
    if ((++mine.runs & (mine.runs - 1)) != 0)
        return;
    std::string text;
    char entry[32];
    for (const auto& [target, count] : mine.calls) {
        std::snprintf(entry, sizeof entry, " %X:%llu", target, static_cast<unsigned long long>(count));
        text += entry;
    }
    log::Write("water effects: %s: %llu compared, calls noted:%s", id, static_cast<unsigned long long>(mine.runs),
               text.c_str());
}

template <typename Game, typename Ours>
uint32_t SdCompared(const char* id, std::vector<BRegion> regions, bool checkResult, Game&& game, Ours&& ours)
{
    g_bSelf = nullptr;
    g_bRegions = std::move(regions);
    g_bThread = GetCurrentThreadId();
    const std::vector<char> before = BSaveRegions();
    g_bGameCalls.clear();
    g_bRecord = true;
    const uint32_t result = game();
    g_bRecord = false;
    const std::vector<char> after = BSaveRegions();
    SdRestore(before);
    g_bOurCalls.clear();
    g_bReplay = true;
    const uint32_t ourResult = ours();
    g_bReplay = false;
    const std::vector<char> mine = BSaveRegions();
    SdRestore(after);
    BGiveBackOwn();
    const bool sameCalls = g_bGameCalls == g_bOurCalls, sameBytes = after == mine,
               sameResult = !checkResult || result == ourResult;
    std::string theirs, text;
    if (!sameCalls) {
        std::size_t at = 0;
        while (at < g_bGameCalls.size() && at < g_bOurCalls.size() && g_bGameCalls[at] == g_bOurCalls[at])
            ++at;
        const auto describe = [&](const std::vector<BCall>& calls) {
            char line[96];
            if (at < calls.size())
                std::snprintf(line, sizeof line, "call %zu of %zu: %X on %08X ", at, calls.size(), calls[at].target,
                              calls[at].a);
            else
                std::snprintf(line, sizeof line, "%zu calls", calls.size());
            return at < calls.size() ? line + Hex(calls[at].data, 16) : std::string(line);
        };
        theirs = describe(g_bGameCalls);
        text = describe(g_bOurCalls);
    } else if (!sameBytes) {
        std::size_t at = 0;
        while (after[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        const std::size_t size = std::min<std::size_t>(8, after.size() - at);
        theirs = SdWhere(at) + Hex(after.data() + at, size);
        text = SdWhere(at) + Hex(mine.data() + at, size);
    } else if (!sameResult) {
        char line[40];
        std::snprintf(line, sizeof line, "answer %08X", result);
        theirs = line;
        std::snprintf(line, sizeof line, "answer %08X", ourResult);
        text = line;
    }
    compare::Record(id, sameCalls && sameBytes && sameResult, theirs, text);
    SdCount(id);
    return result;
}

void SdWatchManager()
{
    BSwapRole(M<char*>(kEffectsManager), kSdManagerRole, {{0x1C, reinterpret_cast<uint32_t>(&SdMakeStub)}});
}

// A take that would grow the pool is not compared.
uint32_t __fastcall TakeBByMode(char* base, void*)
{
    const uint32_t first = Global<uint32_t>(kParticleFreeB);
    if (g_poolBMode == StepMode::Original || g_effectRecord || g_effectReplay || BBusy() ||
        (g_poolBMode == StepMode::Compare && !first))
        return g_SC4EffectBaseB_TakeParticle(base, nullptr);
    if (g_poolBMode == StepMode::Ours)
        return TakeParticleB(base);
    SdWatchManager();
    return SdCompared(
        "effects.pool_b", SdRegions(base - 4, {Ptr(first)}), true,
        [&] { return g_SC4EffectBaseB_TakeParticle(base, nullptr); }, [&] { return TakeParticleB(base); });
}

uint32_t __fastcall GiveBackBByMode(char* base, void*, uint32_t particle)
{
    if (g_giveBackBMode == StepMode::Original || g_effectRecord || g_effectReplay || BBusy() || !particle)
        return g_SC4EffectBaseB_GiveBackParticle(base, nullptr, particle);
    if (g_giveBackBMode == StepMode::Ours) {
        GiveBackParticleB(base, particle);
        return 0;
    }
    char* const self = base - 4;
    SdWatchManager();
    SdWatch(At<char*>(Ptr(particle), 0x44));
    BSwapRole(At<char*>(self, 0x278), kSdHeldRole, {{0x4, reinterpret_cast<uint32_t>(&SdHeldStub)}});
    return SdCompared(
        "effects.giveback_b", SdRegions(self, {Ptr(particle)}), false,
        [&] { return g_SC4EffectBaseB_GiveBackParticle(base, nullptr, particle); },
        [&] {
            GiveBackParticleB(base, particle);
            return 0u;
        });
}

// An effect whose list is broken is not compared.
uint32_t __fastcall NotDrawnBByMode(char* self, void*)
{
    if (g_effectRecord && g_drawRecording)
        return EffectRecorded(addr::SC4EffectB_NotDrawn, g_SC4EffectB_NotDrawn, self);
    if (g_notDrawnBMode == StepMode::Original || g_effectRecord || g_effectReplay || BBusy())
        return g_SC4EffectB_NotDrawn(self, nullptr);
    if (g_notDrawnBMode == StepMode::Ours) {
        StepNotDrawnB(self);
        return 0;
    }
    const std::vector<char*> nodes = EffectNodes(self);
    if (nodes.empty())
        return g_SC4EffectB_NotDrawn(self, nullptr);
    std::vector<char*> particles;
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        particles.push_back(At<char*>(nodes[i], 8));
        SdWatch(At<char*>(particles.back(), 0x44));
    }
    return SdCompared(
        "effects.notdrawn_b", SdRegions(self, particles), false, [&] { return g_SC4EffectB_NotDrawn(self, nullptr); },
        [&] {
            StepNotDrawnB(self);
            return 0u;
        });
}
OPENSC4_THUNK(TSdNotDrawn, NotDrawnBByMode, 0)

// The block only does arithmetic (and its four callees, which only read
// and write what it hands them), so its comparison runs ours, keeps the
// effect, the particle, the place and the block it left, puts them back
// and runs the game's.
StepMode g_blockBMode = StepMode::Original;
Call5Fn g_SC4EffectB_ParticleBlock = nullptr;

uint32_t __fastcall BlockBByMode(char* self, void*, uint32_t particle, uint32_t t, uint32_t scale, uint32_t place,
                                 uint32_t block)
{
    const auto game = [&] { return g_SC4EffectB_ParticleBlock(self, nullptr, particle, t, scale, place, block); };
    if (g_blockBMode == StepMode::Original)
        return game();
    const auto ours = [&] {
        ParticleBlockB(self, Ptr(particle), LfFloat(t), LfFloat(scale), reinterpret_cast<float*>(place), Ptr(block));
    };
    if (g_blockBMode == StepMode::Ours) {
        ours();
        return 0;
    }
    const std::pair<char*, std::size_t> regions[] = {
        {self, kEffectSizeB}, {Ptr(particle), kParticleBytesB}, {Ptr(place), 12}, {Ptr(block), 0x38}};
    const auto save = [&](std::vector<char>& to) {
        to.clear();
        for (const auto& [at, size] : regions)
            to.insert(to.end(), at, at + size);
    };
    const auto restore = [&](const std::vector<char>& from) {
        std::size_t offset = 0;
        for (const auto& [at, size] : regions) {
            std::memcpy(at, from.data() + offset, size);
            offset += size;
        }
    };
    std::vector<char> before, mine, theirs;
    save(before);
    ours();
    save(mine);
    restore(before);
    const uint32_t result = game();
    save(theirs);
    // The block's two unset bytes are left out.
    const std::size_t blockAt = kEffectSizeB + kParticleBytesB + 12;
    for (const std::size_t unset : {blockAt + 2, blockAt + 3})
        mine[unset] = theirs[unset];
    const bool same = mine == theirs;
    std::string gameText, ourText;
    if (!same) {
        std::size_t at = 0;
        while (theirs[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        static const char* const kNames[] = {"effect", "particle", "place", "block"};
        std::size_t region = 0, offset = at;
        while (offset >= regions[region].second)
            offset -= regions[region++].second;
        char text[40];
        std::snprintf(text, sizeof text, "%s+%zX ", kNames[region], offset);
        const std::size_t size = std::min<std::size_t>(8, theirs.size() - at);
        gameText = text + Hex(theirs.data() + at, size);
        ourText = text + Hex(mine.data() + at, size);
    }
    compare::Record("effects.block_b", same, gameText, ourText);
    return result;
}
OPENSC4_THUNK(TSdBlock, BlockBByMode, 20)

bool InstallBlockB(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4EffectB_ParticleBlock ||
          Detour(addr::SC4EffectB_ParticleBlock, &TSdBlock, &g_SC4EffectB_ParticleBlock)))
        return false;
    g_blockBMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the second class's block %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallPoolB(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookPool())
        return false;
    g_poolBMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the second class's take %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallGiveBackB(registry::Mode mode)
{
    if (!game::IsBuild641() || !HookPool())
        return false;
    if (mode == registry::Mode::Compare &&
        !(g_SC4EffectB_RestartParticle ||
          Detour(addr::SC4EffectB_RestartParticle, &TSdRestart, &g_SC4EffectB_RestartParticle)))
        return false;
    g_giveBackBMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the second class's give back %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallNotDrawnB(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4EffectB_NotDrawn || Detour(addr::SC4EffectB_NotDrawn, &TSdNotDrawn, &g_SC4EffectB_NotDrawn)))
        return false;
    g_notDrawnBMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the second class's step out of view %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredPoolB = registry::Register({
    "renderer.effects.pool_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectBaseB_TakeParticle,
    "The second effect class's take of a particle (0x005AEF20): the first of its own free list, cleared and "
    "started, turned, and given an effect of its own made by the effects manager when the description names one; "
    "compare mode runs the game's with the manager's make and the calls on the effect it makes noted, and replays "
    "ours (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallPoolB,
});

const bool g_registeredGiveBackB = registry::Register({
    "renderer.effects.giveback_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectBaseB_GiveBackParticle,
    "The second effect class's give back of a particle (0x005AF390): the particle started again, or its own effect "
    "stopped and let go, a \"_death\" effect made where it was, and the particle put back on the class's free "
    "list; compare mode runs the game's with those calls noted and replays ours (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallGiveBackB,
});

const bool g_registeredNotDrawnB = registry::Register({
    "renderer.effects.notdrawn_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectB_NotDrawn,
    "The second effect class's step when it is not drawn (0x005AE6E0): its box taken again and each particle's own "
    "effect told where it is (a block from 0x005ACA30 and a value of a curve); compare mode runs the game's with "
    "the calls on those effects noted and replays ours (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallNotDrawnB,
});

const bool g_registeredBlockB = registry::Register({
    "renderer.effects.block_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectB_ParticleBlock,
    "The second effect class's block of a particle (0x005ACA30): its turn (facing the way it goes for kinds 2 to 4, "
    "else by its angle), spin and size as a matrix and its place, through the effect's second block, and the "
    "effect's box widened; compare mode runs ours and the game's on the same bytes (docs/renderer.md, \"Water "
    "effects, sky and clouds\")",
    true,
    InstallBlockB,
});

// --- The particle draws ----------------------------------------------------------------------
//
// The second class's draw, the draw into the dynamic vertex buffer and the
// base class's draw without it (effects_particles_draw.h).
//
// None of them can run twice as a whole: they draw, and the second class's
// also moves the model on, steps the part and calls the particles' own
// effects. Their comparison is the notes' engine's (as the second class's
// step out of view): the objects they call get copies of their tables of
// functions whose slots note each call (the model instance, the model,
// the dynamic vertex buffer and the particles' own effects), and detours
// of compare mode note the game's functions they call (the context's
// texture bind and colour reset, the batch's draw, the check whether a
// particle is in view and the part's two). The game's runs with the calls
// passed on; the effect (but for its batch, +0x28 to +0x68, whose storage
// the game's run may have moved; the batch's own functions run in both
// and leave it the same), the class's free list, the generator and the
// particles are put back, and ours runs with every such call answered from
// the notes. The calls and those bytes must be the same, and the vertices
// handed over: the buffer's, read at the unlock (the game's, of the locked
// memory; ours, of memory the replay lends it), and the batch's, read at
// its draw. The block (0x005ACA30), 0x005AC070 and the batch's functions
// run in both, from the same bytes.
//
// An effect's draw (renderer.effects.draw_effect) records the three while
// it is compared (g_drawRecording), through the same detours.
#include "core/effects_particles_draw.h"

// The second class's table of functions.
constexpr uint32_t kEffectTableB = 0x00AA0C58;

StepMode g_dbMode = StepMode::Original, g_bfMode = StepMode::Original, g_plMode = StepMode::Original;
Call1Fn g_DwSC4Effect_DrawParticlesTextured = nullptr, g_DwSC4Effect_DrawParticles = nullptr,
        g_DwSC4EffectB_DrawParticles = nullptr;
Call0Fn g_DwSC4RenderContext_ResetColour = nullptr;
Call2Fn g_PdBindTexture = nullptr, g_PdPartStart = nullptr;
Call1Fn g_PdBatchDraw = nullptr, g_PdPartStep = nullptr;
Call5Fn g_PdInView = nullptr;

// The context's colour reset: recorded for an effect's draw, noted for the
// second class's draw.
uint32_t __fastcall DDwSC4RenderContext_ResetColour(char* self, void*)
{
    if (g_drawRecording && g_effectRecord)
        return EffectRecorded(addr::SC4RenderContext_ResetColour, g_DwSC4RenderContext_ResetColour, self);
    if (!BNoting())
        return g_DwSC4RenderContext_ResetColour(self, nullptr);
    return BCallee(addr::SC4RenderContext_ResetColour, Word(self), 0, 0,
                   [&] { return g_DwSC4RenderContext_ResetColour(self, nullptr); });
}
OPENSC4_THUNK(TDwSC4RenderContext_ResetColour, DDwSC4RenderContext_ResetColour, 0)

uint32_t __fastcall PdBindTextureNoted(char* self, void*, uint32_t texture, uint32_t stage)
{
    if (!BNoting())
        return g_PdBindTexture(self, nullptr, texture, stage);
    return BCallee(addr::SC4RenderContext_BindTexture, Word(self), Word(&texture), 4,
                   [&] { return g_bDry ? 0u : g_PdBindTexture(self, nullptr, texture, stage); });
}
OPENSC4_THUNK(TPdBindTexture, PdBindTextureNoted, 8)

// The batch's draw by its own switch's mode ("The index batch's draw" at the
// end); this detour is that switch's too.
uint32_t IbDrawByMode(char* batch, uint32_t context);

uint32_t __fastcall PdBatchDrawNoted(char* self, void*, uint32_t context)
{
    if (!BNoting())
        return IbDrawByMode(self, context);
    PlKeepVertices(self, g_bfGameVertices);
    return BCallee(addr::SC4IndexBatch_Draw, Word(self), Word(&context), 4,
                   [&] { return g_bDry ? 0u : IbDrawByMode(self, context); });
}
OPENSC4_THUNK(TPdBatchDraw, PdBatchDrawNoted, 4)

uint32_t __fastcall PdInViewNoted(char* self, void*, uint32_t depth, uint32_t camera, uint32_t particle,
                                  uint32_t place, uint32_t scale)
{
    if (!BNoting())
        return g_PdInView(self, nullptr, depth, camera, particle, place, scale);
    float* const where = reinterpret_cast<float*>(place);
    const uint32_t noted[6] = {depth, camera, LfBits(where[0]), LfBits(where[1]), LfBits(where[2]), M<uint32_t>(scale)};
    const uint32_t answer = BCallee(addr::SC4EffectB_ParticleInView, particle, Word(noted), sizeof noted, [&] {
        return g_PdInView(self, nullptr, depth, camera, particle, place, scale);
    });
    DbLeft left{};
    std::memcpy(left.place, where, 12);
    left.scale = M<uint32_t>(scale);
    g_dbInView.push_back(left);
    return answer;
}
OPENSC4_THUNK(TPdInView, PdInViewNoted, 20)

uint32_t __fastcall PdPartStartNoted(char* self, void*, uint32_t a, uint32_t b)
{
    if (!BNoting())
        return g_PdPartStart(self, nullptr, a, b);
    const uint32_t noted[2] = {a, b};
    return BCallee(addr::SC4EffectPartB_Start, Word(self), Word(noted), sizeof noted,
                   [&] { return g_PdPartStart(self, nullptr, a, b); });
}
OPENSC4_THUNK(TPdPartStart, PdPartStartNoted, 8)

uint32_t __fastcall PdPartStepNoted(char* self, void*, uint32_t step)
{
    if (!BNoting())
        return g_PdPartStep(self, nullptr, step);
    return BCallee(addr::SC4EffectPartB_Step, Word(self), Word(&step), 4,
                   [&] { return g_PdPartStep(self, nullptr, step); });
}
OPENSC4_THUNK(TPdPartStep, PdPartStepNoted, 4)

// The noting slots of the model instance (0x28 the block, 0x30 the draw,
// 0x3C the colour it answers, 0x40 the colour given), the model (0x28 the
// frame) and the dynamic vertex buffer (0x18 the lock, 0x20 the unlock,
// 0x28 the draw), noted as the calls of effects_particles_draw.h note them.
uint32_t __fastcall DbInstanceBlockStub(char* object, void*, uint32_t block)
{
    const uint32_t real = BRealSlot(object, 0x28);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, block);
    return BCallee(kDbInstance + 0x28, Word(object), block, kBBlock, [&] { return CallThis<uint32_t>(real, object, block); });
}

uint32_t __fastcall DbInstanceDrawStub(char* object, void*, uint32_t context, uint32_t flag)
{
    const uint32_t real = BRealSlot(object, 0x30);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, context, flag);
    const uint32_t noted[2] = {context, flag};
    return BCallee(kDbInstance + 0x30, Word(object), Word(noted), sizeof noted,
                   [&] { return CallThis<uint32_t>(real, object, context, flag); });
}

uint32_t __fastcall DbInstanceColourStub(char* object, void*, uint32_t scratch)
{
    const uint32_t real = BRealSlot(object, 0x3C);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, scratch);
    const uint32_t answer =
        BCallee(kDbInstance + 0x3C, Word(object), 0, 0, [&] { return CallThis<uint32_t>(real, object, scratch); });
    std::memcpy(g_dbColour, Ptr(answer), kDbColour);
    return answer;
}

uint32_t __fastcall DbInstanceTintStub(char* object, void*, uint32_t colour)
{
    const uint32_t real = BRealSlot(object, 0x40);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, colour);
    return BCallee(kDbInstance + 0x40, Word(object), colour, kDbColour,
                   [&] { return CallThis<uint32_t>(real, object, colour); });
}

uint32_t __fastcall DbModelFrameStub(char* object, void*, uint32_t frame)
{
    const uint32_t real = BRealSlot(object, 0x28);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, frame);
    return BCallee(kDbModel + 0x28, Word(object), Word(&frame), 4, [&] { return CallThis<uint32_t>(real, object, frame); });
}

char* g_bfLocked = nullptr;
uint32_t g_bfLockedCount = 0;

uint32_t __fastcall BfLockStub(char* object, void*, uint32_t from, uint32_t count)
{
    const uint32_t real = BRealSlot(object, 0x18);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, from, count);
    const uint32_t noted[2] = {from, count};
    const uint32_t answer = BCallee(kBfBuffer + 0x18, Word(object), Word(noted), sizeof noted,
                                    [&] { return CallThis<uint32_t>(real, object, from, count); });
    g_bfLocked = Ptr(answer);
    g_bfLockedCount = count;
    return answer;
}

uint32_t __fastcall BfUnlockStub(char* object, void*, uint32_t value)
{
    const uint32_t real = BRealSlot(object, 0x20);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, value);
    if (g_bfLocked)
        BfKeep(g_bfLocked, g_bfLockedCount, g_bfGameVertices);
    g_bfLocked = nullptr;
    return BCallee(kBfBuffer + 0x20, Word(object), Word(&value), 4, [&] { return CallThis<uint32_t>(real, object, value); });
}

uint32_t __fastcall BfDrawStub(char* object, void*, uint32_t a, uint32_t b, uint32_t count, uint32_t indices)
{
    const uint32_t real = BRealSlot(object, 0x28);
    if (!BNoting())
        return CallThis<uint32_t>(real, object, a, b, count, indices);
    const uint32_t noted[4] = {a, b, count, indices};
    return BCallee(kBfBuffer + 0x28, Word(object), Word(noted), sizeof noted,
                   [&] { return CallThis<uint32_t>(real, object, a, b, count, indices); });
}

// The comparison of the two base class draws: the notes' engine's with the
// effect but for its batch, and the vertices handed over. `size` is the
// effect's.
template <typename Game, typename Ours>
uint32_t BfComparedSized(const char* id, char* self, std::size_t size, Game&& game, Ours&& ours)
{
    g_bSelf = nullptr;
    g_bRegions = {{self, 0x28}, {self + 0x68, size - 0x68}};
    g_bThread = GetCurrentThreadId();
    g_bfGameVertices.clear();
    g_bfOurVertices.clear();
    g_bfLocked = nullptr;
    const std::vector<char> before = BSaveRegions();
    g_bGameCalls.clear();
    g_bRecord = true;
    const uint32_t result = game();
    g_bRecord = false;
    const std::vector<char> after = BSaveRegions();
    SdRestore(before);
    g_bOurCalls.clear();
    g_bReplay = true;
    ours();
    g_bReplay = false;
    const std::vector<char> mine = BSaveRegions();
    SdRestore(after);
    BGiveBackOwn();
    std::string theirs, text;
    char line[96];
    if (!(g_bGameCalls == g_bOurCalls)) {
        std::size_t at = 0;
        while (at < g_bGameCalls.size() && at < g_bOurCalls.size() && g_bGameCalls[at] == g_bOurCalls[at])
            ++at;
        const auto describe = [&](const std::vector<BCall>& calls) {
            if (at < calls.size())
                std::snprintf(line, sizeof line, "call %zu of %zu: %X on %08X ", at, calls.size(), calls[at].target,
                              calls[at].a);
            else
                std::snprintf(line, sizeof line, "%zu calls", calls.size());
            return at < calls.size() ? line + Hex(calls[at].data, 16) : std::string(line);
        };
        theirs = describe(g_bGameCalls);
        text = describe(g_bOurCalls);
    } else if (after != mine) {
        std::size_t at = 0;
        while (after[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(line, sizeof line, "effect+%zX ", at < 0x28 ? at : at + 0x40);
        theirs = line + Hex(after.data() + at, std::min<std::size_t>(8, after.size() - at));
        text = line + Hex(mine.data() + at, std::min<std::size_t>(8, mine.size() - at));
    } else if (g_bfGameVertices != g_bfOurVertices) {
        std::size_t at = 0;
        while (at < g_bfGameVertices.size() && at < g_bfOurVertices.size() && g_bfGameVertices[at] == g_bfOurVertices[at])
            ++at;
        at &= ~std::size_t{3};
        const auto describe = [&](const std::vector<char>& bytes) {
            std::snprintf(line, sizeof line, "vertex bytes %zu of %zu: ", at, bytes.size());
            return at < bytes.size() ? line + Hex(bytes.data() + at, std::min<std::size_t>(8, bytes.size() - at))
                                     : std::string(line);
        };
        theirs = describe(g_bfGameVertices);
        text = describe(g_bfOurVertices);
    }
    compare::Record(id, theirs.empty() && text.empty(), theirs, text);
    SdCount(id);
    return result;
}

template <typename Game, typename Ours>
uint32_t BfCompared(const char* id, char* self, Game&& game, Ours&& ours)
{
    return BfComparedSized(id, self, At<uint32_t>(self, 0) == kEffectTableB ? kEffectSizeB : kEffectSize, game, ours);
}

// With OPENSC4_EFFECT_FORCE_DRAWS=1 (compare mode of the two base class
// draws only) each compared draw is made in turn with one of eight
// changes, put back after both runs: none; the description's turn about
// y (+0x104) 0.002; its velocity axes (+0xDC) 0.5, so that fast particles
// are stretched, or 1000, so that none is; both texture flips (flags
// 0x40000000 and 0x20000000; the plain draw has none); the most size (the
// vector at +0x1E8's +0x34) 0.5, so that the size is clamped; flip
// 0x20000000 alone; no texture (+0x68), so that nothing is drawn. They are
// recorded as effects.draw_buffered.forced and effects.draw_plain.forced.
// The game's run draws them too.
bool g_pdForce = false;

template <typename Compare>
uint32_t PdForced(char* self, Compare&& compare)
{
    static uint32_t turn = 0;
    const uint32_t k = turn++ % 8;
    char* const description = At<char*>(self, 0x24);
    char* const most = At<char*>(self, 0x1E8);
    const uint32_t flags = At<uint32_t>(description, 0), spin = At<uint32_t>(description, 0x104),
                   axes = At<uint32_t>(description, 0xDC), texture = At<uint32_t>(self, 0x68),
                   size = most ? At<uint32_t>(most, 0x34) : 0;
    switch (k) {
    case 1:
        At<float>(description, 0x104) = 0.002f;
        break;
    case 2:
        At<float>(description, 0xDC) = 0.5f;
        break;
    case 3:
        At<float>(description, 0xDC) = 1000.0f;
        break;
    case 4:
        At<uint32_t>(description, 0) = flags | 0x60000000u;
        break;
    case 5:
        if (most)
            At<float>(most, 0x34) = 0.5f;
        break;
    case 6:
        At<uint32_t>(description, 0) = (flags | 0x20000000u) & ~0x40000000u;
        break;
    case 7:
        At<uint32_t>(self, 0x68) = 0;
        break;
    default:
        break;
    }
    const uint32_t result = compare();
    At<uint32_t>(description, 0) = flags;
    At<uint32_t>(description, 0x104) = spin;
    At<uint32_t>(description, 0xDC) = axes;
    At<uint32_t>(self, 0x68) = texture;
    if (most)
        At<uint32_t>(most, 0x34) = size;
    return result;
}

uint32_t __fastcall BfBufferedByMode(char* self, void*, uint32_t context)
{
    const auto game = [&] { return g_DwSC4Effect_DrawParticlesTextured(self, nullptr, context); };
    if (g_drawRecording && g_effectRecord)
        return EffectRecorded(addr::SC4Effect_DrawParticlesTextured, g_DwSC4Effect_DrawParticlesTextured, self,
                              context);
    if (g_bfMode == StepMode::Original || g_effectRecord || g_effectReplay || BBusy())
        return game();
    if (g_bfMode == StepMode::Ours) {
        DrawParticlesBuffered(self, Ptr(context));
        return 0;
    }
    BSwapRole(At<char*>(self, 0x1EC), kBfBuffer,
              {{0x18, reinterpret_cast<uint32_t>(&BfLockStub)},
               {0x20, reinterpret_cast<uint32_t>(&BfUnlockStub)},
               {0x28, reinterpret_cast<uint32_t>(&BfDrawStub)}});
    if (g_pdForce)
        return PdForced(self, [&] {
            return BfCompared("effects.draw_buffered.forced", self, game,
                              [&] { DrawParticlesBuffered(self, Ptr(context)); });
        });
    return BfCompared("effects.draw_buffered", self, game, [&] { DrawParticlesBuffered(self, Ptr(context)); });
}
OPENSC4_THUNK(TDwSC4Effect_DrawParticlesTextured, BfBufferedByMode, 4)

uint32_t __fastcall PlPlainByMode(char* self, void*, uint32_t context)
{
    const auto game = [&] { return g_DwSC4Effect_DrawParticles(self, nullptr, context); };
    if (g_drawRecording && g_effectRecord)
        return EffectRecorded(addr::SC4Effect_DrawParticles, g_DwSC4Effect_DrawParticles, self, context);
    if (g_plMode == StepMode::Original || g_effectRecord || g_effectReplay || BBusy())
        return game();
    if (g_plMode == StepMode::Ours) {
        DrawParticlesPlain(self, Ptr(context));
        return 0;
    }
    if (EffectNodes(self).empty())
        return game();
    if (g_pdForce)
        return PdForced(self, [&] {
            return BfCompared("effects.draw_plain.forced", self, game, [&] { DrawParticlesPlain(self, Ptr(context)); });
        });
    return BfCompared("effects.draw_plain", self, game, [&] { DrawParticlesPlain(self, Ptr(context)); });
}
OPENSC4_THUNK(TDwSC4Effect_DrawParticles, PlPlainByMode, 4)

// With OPENSC4_EFFECT_FORCE_DRAWS=1 the second class's compared draws are
// made in turn with one of six changes too, put back after both runs:
// none; the description's flag 1 (the colour's bit 4); its turn about y
// (+0x104) 0.002; flag 2 of its third word (each particle asked whether it
// is in view); a list of three colours (+0x94) in place of its own, so that
// the colour is read between two; flags 8 and 0x10 of its third word (the
// particle's own effect given the opacity and a placed block). Not forced:
// the frames of a model of more than one (no test city has one; flags
// 0x20 and 0x40 and +0x1DC count only there), and the part's start and
// step (flags 4 and 0x100), as the game's step (0x005AC200) reads through
// a null pointer for a part that was not made for them. Recorded as
// effects.draw_b.forced.
constexpr float kDbForcedColours[9] = {1.0f, 0.25f, 0.5f, 0.0f, 0.75f, 0.125f, 0.5f, 0.5f, 1.0f};

template <typename Compare>
uint32_t DbForced(char* self, Compare&& compare)
{
    static uint32_t turn = 0;
    const uint32_t k = turn++ % 6;
    char* const description = At<char*>(self, 0x24);
    const uint32_t flags = At<uint32_t>(description, 0), third = At<uint32_t>(description, 8),
                   spin = At<uint32_t>(description, 0x104), first = At<uint32_t>(description, 0x94),
                   last = At<uint32_t>(description, 0x98);
    switch (k) {
    case 1:
        At<uint32_t>(description, 0) = flags | 1u;
        break;
    case 2:
        At<float>(description, 0x104) = 0.002f;
        break;
    case 3:
        At<uint32_t>(description, 8) = third | 2u;
        break;
    case 4:
        At<uint32_t>(description, 0x94) = Word(kDbForcedColours);
        At<uint32_t>(description, 0x98) = Word(kDbForcedColours + 9);
        break;
    case 5:
        At<uint32_t>(description, 8) = third | 0x18u;
        break;
    default:
        break;
    }
    const uint32_t result = compare();
    At<uint32_t>(description, 0) = flags;
    At<uint32_t>(description, 8) = third;
    At<uint32_t>(description, 0x104) = spin;
    At<uint32_t>(description, 0x94) = first;
    At<uint32_t>(description, 0x98) = last;
    return result;
}

// An effect whose list is broken is not compared.
uint32_t __fastcall DbDrawByMode(char* self, void*, uint32_t context)
{
    const auto game = [&] { return g_DwSC4EffectB_DrawParticles(self, nullptr, context); };
    if (g_drawRecording && g_effectRecord)
        return EffectRecorded(addr::SC4EffectB_DrawParticles, g_DwSC4EffectB_DrawParticles, self, context);
    if (g_dbMode == StepMode::Original || g_effectRecord || g_effectReplay || BBusy())
        return game();
    if (g_dbMode == StepMode::Ours) {
        DrawParticlesB(self, Ptr(context));
        return 0;
    }
    const std::vector<char*> nodes = EffectNodes(self);
    if (nodes.empty())
        return game();
    std::vector<char*> particles;
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        particles.push_back(At<char*>(nodes[i], 8));
        SdWatch(At<char*>(particles.back(), 0x44));
    }
    BSwapRole(At<char*>(self, 0x220), kDbInstance,
              {{0x28, reinterpret_cast<uint32_t>(&DbInstanceBlockStub)},
               {0x30, reinterpret_cast<uint32_t>(&DbInstanceDrawStub)},
               {0x3C, reinterpret_cast<uint32_t>(&DbInstanceColourStub)},
               {0x40, reinterpret_cast<uint32_t>(&DbInstanceTintStub)}});
    BSwapRole(At<char*>(self, 0x224), kDbModel, {{0x28, reinterpret_cast<uint32_t>(&DbModelFrameStub)}});
    g_dbInView.clear();
    g_dbInViewAt = 0;
    if (g_pdForce)
        return DbForced(self, [&] {
            return SdCompared("effects.draw_b.forced", SdRegions(self, particles), false, game, [&] {
                DrawParticlesB(self, Ptr(context));
                return 0u;
            });
        });
    return SdCompared("effects.draw_b", SdRegions(self, particles), false, game, [&] {
        DrawParticlesB(self, Ptr(context));
        return 0u;
    });
}
OPENSC4_THUNK(TDwSC4EffectB_DrawParticles, DbDrawByMode, 4)

// The three draws have one detour each, which an effect's draw's
// comparison and their own switches share; so has the colour reset.
bool PdHookBuffered()
{
    return g_DwSC4Effect_DrawParticlesTextured ||
           Detour(addr::SC4Effect_DrawParticlesTextured, &TDwSC4Effect_DrawParticlesTextured,
                  &g_DwSC4Effect_DrawParticlesTextured);
}

bool PdHookPlain()
{
    return g_DwSC4Effect_DrawParticles ||
           Detour(addr::SC4Effect_DrawParticles, &TDwSC4Effect_DrawParticles, &g_DwSC4Effect_DrawParticles);
}

bool PdHookB()
{
    return g_DwSC4EffectB_DrawParticles ||
           Detour(addr::SC4EffectB_DrawParticles, &TDwSC4EffectB_DrawParticles, &g_DwSC4EffectB_DrawParticles);
}

bool PdHookResetColour()
{
    return g_DwSC4RenderContext_ResetColour ||
           Detour(addr::SC4RenderContext_ResetColour, &TDwSC4RenderContext_ResetColour,
                  &g_DwSC4RenderContext_ResetColour);
}

bool PdHookBindTexture()
{
    return g_PdBindTexture || Detour(addr::SC4RenderContext_BindTexture, &TPdBindTexture, &g_PdBindTexture);
}

void PdReadForce(registry::Mode mode)
{
    const char* v = std::getenv("OPENSC4_EFFECT_FORCE_DRAWS");
    if (mode != registry::Mode::Compare || !v || *v != '1' || g_pdForce)
        return;
    g_pdForce = true;
    log::Write("water effects: the particles' draws forced with made-up turns, axes, flips, sizes and textures");
}

bool InstallDrawBuffered(registry::Mode mode)
{
    if (!game::IsBuild641() || !PdHookBuffered() || (mode == registry::Mode::Compare && !PdHookBindTexture()))
        return false;
    g_bfMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    PdReadForce(mode);
    log::Write("water effects: the particles' draw into the dynamic buffer %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallDrawPlain(registry::Mode mode)
{
    if (!game::IsBuild641() || !PdHookPlain())
        return false;
    if (mode == registry::Mode::Compare &&
        !(PdHookBindTexture() &&
          (g_PdBatchDraw || Detour(addr::SC4IndexBatch_Draw, &TPdBatchDraw, &g_PdBatchDraw))))
        return false;
    g_plMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    PdReadForce(mode);
    log::Write("water effects: the particles' draw without the dynamic buffer %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallDrawB(registry::Mode mode)
{
    if (!game::IsBuild641() || !PdHookB())
        return false;
    if (mode == registry::Mode::Compare &&
        !(PdHookResetColour() &&
          (g_PdInView || Detour(addr::SC4EffectB_ParticleInView, &TPdInView, &g_PdInView)) &&
          (g_PdPartStart || Detour(addr::SC4EffectPartB_Start, &TPdPartStart, &g_PdPartStart)) &&
          (g_PdPartStep || Detour(addr::SC4EffectPartB_Step, &TPdPartStep, &g_PdPartStep))))
        return false;
    g_dbMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    PdReadForce(mode);
    log::Write("water effects: the second class's particle draw %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDrawBuffered = registry::Register({
    "renderer.effects.draw_buffered",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_DrawParticlesTextured,
    "The particles' draw into the dynamic vertex buffer (0x005B4B90): a quad of four vertices for each particle, "
    "in runs of the buffer's size, and the box around them; compare mode notes the game's calls (the texture, the "
    "buffer's lock, unlock and draw, the vertices handed over) and answers ours from the notes (docs/renderer.md, "
    "\"Water effects, sky and clouds\")",
    true,
    InstallDrawBuffered,
});

const bool g_registeredDrawPlain = registry::Register({
    "renderer.effects.draw_plain",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_DrawParticles,
    "The base class's particle draw without the dynamic buffer (0x005B3DA0): a quad of the effect's batch for each "
    "particle, the batch drawn, and the box around them; compare mode notes the game's calls (the texture, the "
    "batch's draw with its vertices) and answers ours from the notes (docs/renderer.md, \"Water effects, sky and "
    "clouds\")",
    true,
    InstallDrawPlain,
});

const bool g_registeredDrawB = registry::Register({
    "renderer.effects.draw_b",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectB_DrawParticles,
    "The second class's particle draw (0x005ADD50): each particle drawn as the effect's model, its block, colour "
    "and frame given to the model instance, its own effect placed, and the box around them; compare mode notes the "
    "game's calls (the instance's, the model's, the own effects', the part's, the check whether a particle is in "
    "view and the colour reset) and answers ours from the notes (docs/renderer.md, \"Water effects, sky and "
    "clouds\")",
    true,
    InstallDrawB,
});

// --- The draws without particles -------------------------------------------------------------
//
// The manager's draw, an effect's draw, a decal's step, its update and
// its draw (effects_draw.h).
//
// The effect's draw is compared by the record and replay of the emitters:
// the game's run with its calls recorded (the particle's step answered),
// ours replayed. Its calls of functions with detours of their own (the
// step, the emit, the axes, the box, the wandering, the pull and the second
// class's step out of view) are recorded by those detours while
// g_drawRecording is set; the rest get detours here (OPENSC4_DRAW_HOOK*),
// which record only then.
//
// The manager's draw cannot run twice either: its effects and decals move
// on when drawn. Ours runs first with the calls that draw or step
// something only noted (DwManagerCall), the manager is put back, and the
// game's runs with the same calls noted at their entry (the effect's draw
// and the decal's step through their own detours, the three rarer ones and
// the boxes' draw through detours here) and made. The two vectors are
// given room first, so that neither run moves them. Compared: the notes
// (the clean-up, which the game's run reaches through the clean-up's own
// detour, ours' last note), what ours wrote of the manager (the particle
// count against the counts the game's draws found before drawing), that
// ours wrote nothing else, and the two vectors' contents. An effect's draw
// may add, take away or stop decals of the fourth list: when the list the
// game's run steps (and which of it runs) is not the one it started with,
// the decals' notes and their count are left out.
//
// The decal's step only calls its owner and its own slot 4: it is given a
// stand-in owner and a copy of its table with slot 4 replaced, which note
// each call (the slot, the handle and the values behind the pointers);
// ours runs with the calls only noted, the decal is put back, and the
// game's runs with them noted and passed on. The update only writes the
// decal (DcDecalCompared), but for its calls on the owner, which run the
// game's only.
//
// The decal's draw puts it on the terrain through the terrain's view
// (0x00602160) and an overlay manager. The comparison gives it stand-ins
// for both (the view through a detour of 0x00602160 in compare mode, the
// manager as its owner): the game's runs first with each call noted, made
// on the real object and its answer kept; the decal is put back and ours
// runs with the calls noted and answered from the game's. Compared: the
// calls and the decal.
#include "core/effects_draw.h"
#include "core/effects_flash.h"

bool DwRecorded(uint32_t target)
{
    for (const uint32_t address :
         {addr::SC4Effect_StartEdge, addr::SC4Effect_PathVelocity, addr::SC4Effect_StartAtAges,
          addr::SC4Effect_StepParticle, addr::SC4Effect_Emit, addr::SC4Effect_BeforeParticles,
          addr::SC4Effect_DrawParticlesTextured, addr::SC4Effect_DrawParticles, addr::SC4EffectB_DrawParticles,
          addr::SC4Effect_NotDrawn, addr::SC4EffectB_NotDrawn, addr::SC4Effect_StepFlag800000,
          addr::SC4RenderContext_SetColourAlt, addr::SC4RenderContext_UpdateViewMatrix,
          addr::SC4RenderContext_SetRenderState, addr::SC4RenderContext_ResetColour}) {
        if (target == address)
            return true;
    }
    return false;
}

#define OPENSC4_DRAW_HOOK0(Name)                                                                                       \
    Call0Fn g_Dw##Name = nullptr;                                                                                      \
    uint32_t __fastcall DDw##Name(char* self, void*)                                                                   \
    {                                                                                                                  \
        return g_drawRecording && g_effectRecord ? EffectRecorded(addr::Name, g_Dw##Name, self)                        \
                                                 : g_Dw##Name(self, nullptr);                                          \
    }                                                                                                                  \
    OPENSC4_THUNK(TDw##Name, DDw##Name, 0)
#define OPENSC4_DRAW_HOOK1(Name)                                                                                       \
    Call1Fn g_Dw##Name = nullptr;                                                                                      \
    uint32_t __fastcall DDw##Name(char* self, void*, uint32_t a)                                                       \
    {                                                                                                                  \
        return g_drawRecording && g_effectRecord ? EffectRecorded(addr::Name, g_Dw##Name, self, a)                     \
                                                 : g_Dw##Name(self, nullptr, a);                                       \
    }                                                                                                                  \
    OPENSC4_THUNK(TDw##Name, DDw##Name, 4)

OPENSC4_DRAW_HOOK1(SC4RenderContext_SetColourAlt)
OPENSC4_DRAW_HOOK0(SC4RenderContext_UpdateViewMatrix)
OPENSC4_DRAW_HOOK1(SC4RenderContext_SetRenderState)

// The manager's notes: which call (DwKind), on what, with what.
struct DwNote {
    uint32_t kind, self, a, b;
    bool operator==(const DwNote& other) const
    {
        return kind == other.kind && self == other.self && a == other.a && b == other.b;
    }
};
enum class DwTrace { Off, Ours, Game };
DwTrace g_dwTrace = DwTrace::Off;
int g_dwDepth = 0;
DWORD g_dwThread = 0;
std::vector<DwNote> g_dwOurs, g_dwGame;
uint32_t g_dwCounted = 0;
std::vector<uint32_t> g_dwFourth;
bool g_dwFourthTaken = false;

// The fourth list as the manager has it: its indices, what they point at
// and whether that runs (its slot 8; an effect's draw may stop a decal).
std::vector<uint32_t> DwFourth(char* self)
{
    std::vector<uint32_t> words;
    for (uint32_t* it = At<uint32_t*>(self, 0xBCC); it != At<uint32_t*>(self, 0xBD0); ++it) {
        char* const decal = At<char**>(self, 0xBC0)[*it];
        words.push_back(*it);
        words.push_back(Word(decal));
        words.push_back(SuSlot<uint32_t>(decal, 8) & 0xFF);
    }
    return words;
}

char* g_dwManager = nullptr;

bool DwNoting()
{
    return g_dwTrace == DwTrace::Game && g_dwDepth == 0 && GetCurrentThreadId() == g_dwThread;
}

// A box's draw is given the box and the colour on each run's own stack:
// its note holds a hash of the colour's four floats. The box is not
// compared: it is the effect's, which the game's run has drawn (and so
// moved) before the boxes and ours, whose effects' draws are only noted,
// has not.
DwNote DwMakeNote(uint32_t kind, char* self, uint32_t a, uint32_t b)
{
    if (kind == kDwBox) {
        const auto hash = [](uint32_t at, std::size_t size) {
            uint32_t h = 2166136261u;
            for (std::size_t i = 0; i < size; ++i)
                h = (h ^ static_cast<uint8_t>(Ptr(at)[i])) * 16777619u;
            return h;
        };
        return DwNote{kind, Word(self), 0, hash(b, 16)};
    }
    return DwNote{kind, Word(self), a, b};
}

void DwAddNote(uint32_t kind, char* self, uint32_t a, uint32_t b)
{
    if (kind >= kDwDecal && !g_dwFourthTaken) {
        g_dwFourth = DwFourth(g_dwManager);
        g_dwFourthTaken = true;
    }
    g_dwGame.push_back(DwMakeNote(kind, self, a, b));
}

uint32_t DwManagerCall(uint32_t kind, uint32_t target, char* self, uint32_t a, uint32_t b)
{
    if (g_dwTrace == DwTrace::Ours) {
        g_dwOurs.push_back(DwMakeNote(kind, self, a, b));
        return 0;
    }
    switch (kind) {
    case kDwBeforeDraw:
    case kDwDecal:
    case kDwShake:
        return CallThis<uint32_t>(target, self, a);
    case kDwAfter:
        return CallThis<uint32_t>(target, self);
    default:
        return CallThis<uint32_t>(target, self, a, b);
    }
}

// The rarer calls of the manager's draw, noted in the game's run. The flash
// and the shake have modes of their own (ours / compare / original): ours
// runs the hand-written function at the top level, anything else is noted in
// the game's run (compare's own run is the forcing's).
Call1Fn g_DwBeforeDraw = nullptr, g_DwStepShake = nullptr;
Call2Fn g_DwDrawFlash = nullptr, g_DwDrawBox = nullptr;
StepMode g_flashMode = StepMode::Original, g_shakeMode = StepMode::Original;

template <typename Fn, typename... Args>
uint32_t DwNoted(uint32_t kind, Fn original, char* self, Args... args)
{
    if (!DwNoting())
        return original(self, nullptr, args...);
    uint32_t words[2] = {0, 0};
    std::size_t n = 0;
    ((words[n++] = args), ...);
    DwAddNote(kind, self, words[0], words[1]);
    ++g_dwDepth;
    const uint32_t result = original(self, nullptr, args...);
    --g_dwDepth;
    return result;
}

// --- The height map's rest ---------------------------------------------------------------
//
// The particles' height map's own lines (0x00586720, a debug view), the
// clear of its values (0x00586700) and its plain destructor (0x00586900),
// written by hand in effects_heightmap.h. The lines' draw has one detour,
// which the manager's draw's comparison notes it through (DwNoted) and
// this group's mode runs otherwise. Compare mode runs ours and then the
// game's on the same map and context, both drawing, with the calls the
// draw is made of noted at their entry (the lines made anew, the render
// state's two words and fourteen values, the line list asked for its
// mesh: detours of compare mode) and passed on; between the two runs the
// byte +0x48 is put back. The notes and the map's bytes before the line
// list (+0x4C, which the game's functions keep) must be the same. The
// context's two calls (the state set and the mesh drawn) are not noted:
// other switches detour them. The clear and the destructor run ours on a
// copy whose layers are its own (HmDeepCopy) and the game's on the map,
// compared as the height map's destructor; the destructor's answer (the
// base's end's) is not compared, and a map whose line list holds anything
// is left to the game's.
//
// No test city reaches any of the three (the manager's +0xD98 byte is
// never set, the clear and the manager's destructor never called). With
// OPENSC4_HEIGHTMAP_FORCE=1 (compare mode only) the manager's draw runs
// them before its own on the manager's map when it has layers: the lines
// each frame (every fourth with +0x48 set, so that they are made anew;
// +0x48 put back after) and on a copy without layers, and every eighth
// frame the clear and the destructor, each on two copies of the map whose
// layers are their own (the destructor's with line lists made anew), the
// game's on one and ours on the other. They are recorded as
// effects.heightmap_rest.*.forced.
StepMode g_hrMode = StepMode::Original;
Call0Fn g_SC4EffectHeightMap_MakeLines = nullptr, g_SC4EffectHeightMap_ClearValues = nullptr,
        g_SC4EffectHeightMap_Destructor = nullptr, g_SC4HeightMapLines_Mesh = nullptr;
using HrStateFn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                        uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
HrStateFn g_SC4RenderState_Init = nullptr;
bool g_hrForce = false;

// One call noted: 1 the lines made anew (the map), 2 the render state (its
// two words at the call, then the fourteen values), 3 the mesh asked for
// (the line list).
struct HrNote {
    uint32_t kind, self, words[16];
    bool operator==(const HrNote& other) const { return std::memcmp(this, &other, sizeof *this) == 0; }
};
std::vector<HrNote>* g_hrNotes = nullptr;
DWORD g_hrThread = 0;
int g_hrDepth = 0;

bool HrGameOnly()
{
    return g_hrMode == StepMode::Original || g_stNoting || g_stReplay;
}

bool HrNoting()
{
    return g_hrNotes && g_hrDepth == 0 && GetCurrentThreadId() == g_hrThread;
}

// The lines' making (0x00585C20) has one detour: noted for the lines'
// draw's comparison (above), and run by this group's mode. Its comparison
// runs ours with the line list's calls (the clear, its +0xC, the colour,
// the normal, each box) only noted, then the game's with the same calls
// noted at their entry by detours of compare mode and passed on. The notes,
// each with what it is given (three floats; the colour's three; a box's
// eight corners), must be the same. The reads (the terrain's height, the
// map's height and normal) run in both.
struct MlNote {
    uint32_t kind, self, words[24];
    bool operator==(const MlNote& other) const { return std::memcmp(this, &other, sizeof *this) == 0; }
};
std::vector<MlNote>* g_mlNotes = nullptr;
bool g_mlPass = false;
DWORD g_mlThread = 0;
Call0Fn g_SC4HeightMapLines_Clear = nullptr;
Call1Fn g_SC4HeightMapLines_SetVectorC = nullptr, g_SC4HeightMapLines_SetColour = nullptr,
        g_SC4HeightMapLines_SetNormal = nullptr;
using MlAddBoxFn = void(__cdecl*)(char*, const float*);
MlAddBoxFn g_SC4HeightMapLines_AddBox = nullptr;

// Whether a call is to be noted; one that is not passed on is not made.
bool MlAdd(uint32_t kind, char* self, const void* data, std::size_t words, bool& pass)
{
    pass = true;
    if (!g_mlNotes || GetCurrentThreadId() != g_mlThread)
        return false;
    MlNote note{kind, Word(self), {}};
    if (data)
        std::memcpy(note.words, data, words * 4);
    g_mlNotes->push_back(note);
    pass = g_mlPass;
    return true;
}

uint32_t __fastcall MlClearNoted(char* self, void*)
{
    bool pass = true;
    MlAdd(1, self, nullptr, 0, pass);
    return pass ? g_SC4HeightMapLines_Clear(self, nullptr) : 0;
}
OPENSC4_THUNK(TMlClear, MlClearNoted, 0)

uint32_t __fastcall MlVectorCNoted(char* self, void*, uint32_t value)
{
    bool pass = true;
    MlAdd(2, self, Ptr(value), 3, pass);
    return pass ? g_SC4HeightMapLines_SetVectorC(self, nullptr, value) : 0;
}
OPENSC4_THUNK(TMlVectorC, MlVectorCNoted, 4)

uint32_t __fastcall MlColourNoted(char* self, void*, uint32_t rgb)
{
    bool pass = true;
    MlAdd(3, self, Ptr(rgb), 3, pass);
    return pass ? g_SC4HeightMapLines_SetColour(self, nullptr, rgb) : 0;
}
OPENSC4_THUNK(TMlColour, MlColourNoted, 4)

uint32_t __fastcall MlNormalNoted(char* self, void*, uint32_t normal)
{
    bool pass = true;
    MlAdd(4, self, Ptr(normal), 3, pass);
    return pass ? g_SC4HeightMapLines_SetNormal(self, nullptr, normal) : 0;
}
OPENSC4_THUNK(TMlNormal, MlNormalNoted, 4)

void __cdecl MlAddBoxNoted(char* lines, const float* corners)
{
    bool pass = true;
    MlAdd(5, lines, corners, 24, pass);
    if (pass)
        g_SC4HeightMapLines_AddBox(lines, corners);
}
OPENSC4_CDECL_THUNK(TMlAddBox, MlAddBoxNoted, 8)

std::string MlNotesText(const std::vector<MlNote>& notes, std::size_t at)
{
    char text[64];
    if (at >= notes.size()) {
        std::snprintf(text, sizeof text, "%zu calls", notes.size());
        return text;
    }
    std::snprintf(text, sizeof text, "%zu calls, #%zu kind %u on %08X ", notes.size(), at, notes[at].kind,
                  notes[at].self);
    return text + Hex(notes[at].words, sizeof notes[at].words);
}

void MlCompared(const char* id, char* self)
{
    std::vector<MlNote> ours, game;
    g_mlThread = GetCurrentThreadId();
    g_mlPass = false;
    g_mlNotes = &ours;
    MakeHeightMapLines(self);
    g_mlPass = true;
    g_mlNotes = &game;
    g_SC4EffectHeightMap_MakeLines(self, nullptr);
    g_mlNotes = nullptr;
    std::size_t at = 0;
    while (at < ours.size() && at < game.size() && ours[at] == game[at])
        ++at;
    const bool same = ours == game;
    StRecord(id, same, same ? std::string() : MlNotesText(game, at), same ? std::string() : MlNotesText(ours, at));
}

uint32_t MlByMode(char* self)
{
    if (g_hrMode == StepMode::Ours) {
        MakeHeightMapLines(self);
        return 0;
    }
    if (g_hrMode == StepMode::Original || HrGameOnly() || g_mlNotes)
        return g_SC4EffectHeightMap_MakeLines(self, nullptr);
    MlCompared("effects.heightmap_rest.make_lines", self);
    return 0;
}

uint32_t __fastcall HrMakeLinesNoted(char* self, void*)
{
    if (!HrNoting())
        return MlByMode(self);
    g_hrNotes->push_back(HrNote{1, Word(self), {}});
    ++g_hrDepth;
    const uint32_t result = MlByMode(self);
    --g_hrDepth;
    return result;
}
OPENSC4_THUNK(THrMakeLines, HrMakeLinesNoted, 0)

uint32_t __fastcall HrStateNoted(char* self, void*, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                 uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11,
                                 uint32_t a12, uint32_t a13, uint32_t a14)
{
    if (HrNoting()) {
        // The state is a local of each run: its place is not noted.
        g_hrNotes->push_back(HrNote{2,
                                    0,
                                    {At<uint32_t>(self, 0), At<uint32_t>(self, 4), a1, a2, a3, a4, a5, a6, a7, a8, a9,
                                     a10, a11, a12, a13, a14}});
    }
    ++g_hrDepth;
    const uint32_t result =
        g_SC4RenderState_Init(self, nullptr, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
    --g_hrDepth;
    return result;
}
OPENSC4_THUNK(THrState, HrStateNoted, 56)

uint32_t __fastcall HrMeshNoted(char* self, void*)
{
    if (!HrNoting())
        return g_SC4HeightMapLines_Mesh(self, nullptr);
    g_hrNotes->push_back(HrNote{3, Word(self), {}});
    ++g_hrDepth;
    const uint32_t result = g_SC4HeightMapLines_Mesh(self, nullptr);
    --g_hrDepth;
    return result;
}
OPENSC4_THUNK(THrMesh, HrMeshNoted, 0)

std::string HrNotesText(const std::vector<HrNote>& notes, std::size_t at)
{
    char text[64];
    if (at >= notes.size()) {
        std::snprintf(text, sizeof text, "%zu calls", notes.size());
        return text;
    }
    std::snprintf(text, sizeof text, "%zu calls, #%zu kind %u on %08X ", notes.size(), at, notes[at].kind,
                  notes[at].self);
    return text + Hex(notes[at].words, sizeof notes[at].words);
}

void HrLinesCompared(const char* id, char* self, char* context)
{
    const uint8_t changed = At<uint8_t>(self, 0x48);
    std::vector<HrNote> ours, game;
    g_hrThread = GetCurrentThreadId();
    g_hrNotes = &ours;
    DrawHeightMapLines(self, context);
    g_hrNotes = nullptr;
    const std::vector<char> mine(self, self + 0x4C);
    At<uint8_t>(self, 0x48) = changed;
    g_hrNotes = &game;
    g_DwBeforeDraw(self, nullptr, Word(context));
    g_hrNotes = nullptr;
    std::size_t wrote = 0;
    while (wrote < 0x4C && mine[wrote] == self[wrote])
        ++wrote;
    if (wrote < 0x4C) {
        char text[16];
        std::snprintf(text, sizeof text, "+%zX ", wrote);
        StRecord(id, false, text + Hex(self + wrote, 1), text + Hex(mine.data() + wrote, 1));
        return;
    }
    std::size_t at = 0;
    while (at < ours.size() && at < game.size() && ours[at] == game[at])
        ++at;
    const bool same = ours == game;
    StRecord(id, same, same ? std::string() : HrNotesText(game, at), same ? std::string() : HrNotesText(ours, at));
}

uint32_t __fastcall HrLinesByMode(char* self, void*, uint32_t context)
{
    if (DwNoting())
        return DwNoted(kDwBeforeDraw, g_DwBeforeDraw, self, context);
    if (g_hrMode == StepMode::Ours) {
        DrawHeightMapLines(self, Ptr(context));
        return 0;
    }
    if (g_hrMode == StepMode::Original || HrGameOnly() || g_hrNotes)
        return g_DwBeforeDraw(self, nullptr, context);
    HrLinesCompared("effects.heightmap_rest.lines", self, Ptr(context));
    return 0;
}
OPENSC4_THUNK(TDwBeforeDraw, HrLinesByMode, 4)

// A copy of the map from HmDeepCopy done with: its layers freed, its
// terrain let go.
void HrFreeCopy(std::vector<char>& copy)
{
    CallThis<void>(addr::SC4ShortVectorVector_Destroy, copy.data() + 0x14);
    if (char* const terrain = At<char*>(copy.data(), 0xC))
        HmSlot<uint32_t>(terrain, 8);
}

uint32_t __fastcall HrClearByMode(char* self, void*)
{
    if (g_hrMode == StepMode::Ours) {
        ClearHeightMapValues(self);
        return 0;
    }
    if (g_hrMode == StepMode::Original || HrGameOnly() || !HmCopyable(self) ||
        At<char*>(self, 0x14) == At<char*>(self, 0x18))
        return g_SC4EffectHeightMap_ClearValues(self, nullptr);
    std::vector<char> mine = HmDeepCopy(self);
    ClearHeightMapValues(mine.data());
    const uint32_t result = g_SC4EffectHeightMap_ClearValues(self, nullptr);
    HmSame("effects.heightmap_rest.clear", self, mine.data(), true);
    HrFreeCopy(mine);
    return result;
}
OPENSC4_THUNK(THrClear, HrClearByMode, 0)

uint32_t __fastcall HrEndByMode(char* self, void*)
{
    if (g_hrMode == StepMode::Ours)
        return EndHeightMap(self);
    if (g_hrMode == StepMode::Original || HrGameOnly() || !HmCopyable(self) || At<uint32_t>(self, 0x4C) ||
        At<uint32_t>(self, 0x50) || At<uint32_t>(self, 0x54))
        return g_SC4EffectHeightMap_Destructor(self, nullptr);
    std::vector<char> mine = HmDeepCopy(self);
    EndHeightMap(mine.data());
    const uint32_t result = g_SC4EffectHeightMap_Destructor(self, nullptr);
    HmSame("effects.heightmap_rest.end", self, mine.data(), false);
    return result;
}
OPENSC4_THUNK(THrEnd, HrEndByMode, 0)

// The forcing, before the manager's draw (`self` the interface).
void HrForce(char* self, char* context)
{
    if (!g_hrForce)
        return;
    char* const map = At<char*>(self, 0xD94);
    if (!map || !context || At<char*>(map, 0x14) == At<char*>(map, 0x18) || !HmCopyable(map) || HrGameOnly() ||
        g_hrNotes)
        return;
    static uint32_t turn = 0;
    const uint32_t k = turn++;
    const uint8_t changed = At<uint8_t>(map, 0x48);
    if (k % 4 == 0)
        At<uint8_t>(map, 0x48) = 1;
    HrLinesCompared("effects.heightmap_rest.lines.forced", map, context);
    At<uint8_t>(map, 0x48) = changed;
    std::vector<char> empty(map, map + kHeightMapSize);
    At<char*>(empty.data(), 0x18) = At<char*>(empty.data(), 0x14);
    HrLinesCompared("effects.heightmap_rest.lines.forced", empty.data(), context);
    if (k % 8 == 4) {
        // The lines made on a copy whose first layer's values are negated
        // (the map's height below the terrain's) and whose cell size is 13
        // (products that are not exact as floats), its line list its own.
        std::vector<char> odd = HmDeepCopy(map);
        char* const layer = At<char*>(odd.data(), 0x14);
        for (char* at = At<char*>(layer, 0); at != At<char*>(layer, 4); at += 4)
            At<float>(at, 0) = -At<float>(at, 0);
        At<uint32_t>(odd.data(), 0x30) = 13;
        CallThis<void*>(addr::SC4HeightMapLines_Construct, odd.data() + 0x4C);
        MlCompared("effects.heightmap_rest.make_lines.forced", odd.data());
        CallThis<void>(addr::SC4HeightMapLines_Destroy, odd.data() + 0x4C);
        HrFreeCopy(odd);
    }
    if (k % 8 != 0)
        return;
    std::vector<char> theirs = HmDeepCopy(map), mine = HmDeepCopy(map);
    g_SC4EffectHeightMap_ClearValues(theirs.data(), nullptr);
    ClearHeightMapValues(mine.data());
    HmSame("effects.heightmap_rest.clear.forced", theirs.data(), mine.data(), true);
    HrFreeCopy(theirs);
    HrFreeCopy(mine);
    theirs = HmDeepCopy(map);
    mine = HmDeepCopy(map);
    CallThis<void*>(addr::SC4HeightMapLines_Construct, theirs.data() + 0x4C);
    CallThis<void*>(addr::SC4HeightMapLines_Construct, mine.data() + 0x4C);
    g_SC4EffectHeightMap_Destructor(theirs.data(), nullptr);
    EndHeightMap(mine.data());
    HmSame("effects.heightmap_rest.end.forced", theirs.data(), mine.data(), false);
}

uint32_t __fastcall DwDrawFlashNoted(char* self, void*, uint32_t context, uint32_t step)
{
    if (g_flashMode == StepMode::Ours && g_dwTrace == DwTrace::Off && g_dwDepth == 0) {
        DrawFlash(self, Ptr(context), LfFloat(step));
        return 0;
    }
    return DwNoted(kDwFlash, g_DwDrawFlash, self, context, step);
}
OPENSC4_THUNK(TDwDrawFlash, DwDrawFlashNoted, 8)

uint32_t __fastcall DwStepShakeNoted(char* self, void*, uint32_t step)
{
    if (g_shakeMode == StepMode::Ours && g_dwTrace == DwTrace::Off && g_dwDepth == 0) {
        StepShake(self, LfFloat(step));
        return 0;
    }
    return DwNoted(kDwShake, g_DwStepShake, self, step);
}
OPENSC4_THUNK(TDwStepShake, DwStepShakeNoted, 4)

uint32_t __fastcall DwDrawBoxNoted(char* self, void*, uint32_t box, uint32_t colour)
{
    return DwNoted(kDwBox, g_DwDrawBox, self, box, colour);
}
OPENSC4_THUNK(TDwDrawBox, DwDrawBoxNoted, 8)

// --- An effect's draw by its mode

StepMode g_dwEffectMode = StepMode::Original;
Call2Fn g_SC4Effect_Draw = nullptr;

struct DwEffectCall {
    char* self;
    uint32_t step, context;
};

uint32_t DwEffectDrawRun(char* self, uint32_t step, uint32_t context)
{
    const auto game = [&] { return g_SC4Effect_Draw(self, nullptr, step, context); };
    if (g_dwEffectMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return game();
    if (g_dwEffectMode == StepMode::Ours)
        return DrawEffect(self, LfFloat(step), Ptr(context));
    DwEffectCall call{self, step, context};
    g_drawRecording = true;
    const uint32_t result = RecordAndReplay(
        "effects.draw_effect", self, true, game,
        [](void* c) {
            const auto* const call = static_cast<DwEffectCall*>(c);
            return DrawEffect(call->self, LfFloat(call->step), Ptr(call->context));
        },
        &call);
    g_drawRecording = false;
    return result;
}

void EnForce(char* self);
void PlForce(char* self);
void IbForce(char* self);

uint32_t __fastcall DwEffectDrawByMode(char* self, void*, uint32_t step, uint32_t context)
{
    LfForceSteps(self);
    EnForce(self);
    PlForce(self);
    IbForce(self);
    if (!DwNoting())
        return DwEffectDrawRun(self, step, context);
    DwAddNote(kDwEffect, self, step, context);
    if (context)
        g_dwCounted += At<uint32_t>(self, 0x18);
    ++g_dwDepth;
    const uint32_t result = DwEffectDrawRun(self, step, context);
    --g_dwDepth;
    return result;
}
OPENSC4_THUNK(TDwEffectDraw, DwEffectDrawByMode, 8)

// --- A decal's step by its mode

StepMode g_dwDecalMode = StepMode::Original;
Call1Fn g_SC4EffectDecal_Draw = nullptr;
constexpr std::uintptr_t kDwDecalTable = 0x00A9EE78;
constexpr std::size_t kDwDecalSlots = 64;

struct DwDecalCall {
    uint32_t slot, handle;
    char data[0x24];
    bool operator==(const DwDecalCall& other) const
    {
        return slot == other.slot && handle == other.handle && std::memcmp(data, other.data, sizeof data) == 0;
    }
};
std::vector<DwDecalCall> g_dwDecalOurs, g_dwDecalGame;
bool g_dwDecalForward = false, g_dwDecalEnded = false;
char* g_dwDecal = nullptr;
char* g_dwDecalOwner = nullptr;
char g_dwDecalAtEnd[kDecalSize];
uint32_t g_dwStandInTable[kDwDecalSlots], g_dwDecalTable[kDwDecalSlots];
uint32_t* g_dwStandIn = g_dwStandInTable;

void DwNoteDecalCall(uint32_t slot, uint32_t handle, const void* data, std::size_t size)
{
    DwDecalCall call{slot, handle, {}};
    std::memcpy(call.data, data, size);
    (g_dwDecalForward ? g_dwDecalGame : g_dwDecalOurs).push_back(call);
}

void DwGiveDecalBack()
{
    At<uint32_t>(g_dwDecal, 0) = kDwDecalTable;
    At<char*>(g_dwDecal, 0x120) = g_dwDecalOwner;
}

uint32_t DwPassToOwner(uint32_t slot, uint32_t handle, uint32_t value)
{
    return g_dwDecalForward ? SuSlot<uint32_t>(g_dwDecalOwner, slot, handle, value) : 0;
}

uint32_t __fastcall DwOwnerBlock(void*, void*, uint32_t handle, uint32_t block)
{
    DwNoteDecalCall(0x34, handle, Ptr(block), 0x24);
    return DwPassToOwner(0x34, handle, block);
}

uint32_t __fastcall DwOwnerOpacity(void*, void*, uint32_t handle, uint32_t value)
{
    DwNoteDecalCall(0x44, handle, &value, 4);
    return DwPassToOwner(0x44, handle, value);
}

uint32_t __fastcall DwOwnerPlace(void*, void*, uint32_t handle, uint32_t place)
{
    DwNoteDecalCall(0x48, handle, Ptr(place), 12);
    return DwPassToOwner(0x48, handle, place);
}

uint32_t __fastcall DwDecalEnd(char* self, void*, uint32_t value)
{
    DwNoteDecalCall(4, value, &value, 0);
    std::memcpy(g_dwDecalAtEnd, self, kDecalSize);
    g_dwDecalEnded = true;
    if (!g_dwDecalForward)
        return 0;
    // The decal may be gone when this returns: it is given its own back
    // first, and not touched again.
    DwGiveDecalBack();
    return CallThis<uint32_t>(M<uint32_t>(kDwDecalTable + 4), self, value);
}

uint32_t DwDecalCompared(char* self, uint32_t step)
{
    g_dwDecal = self;
    g_dwDecalOwner = At<char*>(self, 0x120);
    std::memcpy(g_dwDecalTable, reinterpret_cast<const void*>(kDwDecalTable), sizeof g_dwDecalTable);
    g_dwDecalTable[1] = Word(reinterpret_cast<void*>(&DwDecalEnd));
    std::memset(g_dwStandInTable, 0, sizeof g_dwStandInTable);
    g_dwStandInTable[0x34 / 4] = Word(reinterpret_cast<void*>(&DwOwnerBlock));
    g_dwStandInTable[0x44 / 4] = Word(reinterpret_cast<void*>(&DwOwnerOpacity));
    g_dwStandInTable[0x48 / 4] = Word(reinterpret_cast<void*>(&DwOwnerPlace));
    At<uint32_t*>(self, 0) = g_dwDecalTable;
    At<uint32_t**>(self, 0x120) = &g_dwStandIn;
    char before[kDecalSize], mine[kDecalSize], theirs[kDecalSize];
    std::memcpy(before, self, kDecalSize);
    g_dwDecalOurs.clear();
    g_dwDecalGame.clear();
    g_dwDecalForward = false;
    g_dwDecalEnded = false;
    StepDecal(self, LfFloat(step));
    std::memcpy(mine, g_dwDecalEnded ? g_dwDecalAtEnd : self, kDecalSize);
    std::memcpy(self, before, kDecalSize);
    g_dwDecalForward = true;
    g_dwDecalEnded = false;
    const uint32_t result = g_SC4EffectDecal_Draw(self, nullptr, step);
    if (g_dwDecalEnded) {
        std::memcpy(theirs, g_dwDecalAtEnd, kDecalSize);
    } else {
        std::memcpy(theirs, self, kDecalSize);
        DwGiveDecalBack();
    }
    g_dwDecal = nullptr;
    char text[96];
    if (!(g_dwDecalGame == g_dwDecalOurs)) {
        std::size_t at = 0;
        while (at < g_dwDecalGame.size() && at < g_dwDecalOurs.size() && g_dwDecalGame[at] == g_dwDecalOurs[at])
            ++at;
        const auto describe = [&](const std::vector<DwDecalCall>& calls) {
            if (at >= calls.size()) {
                std::snprintf(text, sizeof text, "%zu calls", calls.size());
                return std::string(text);
            }
            std::snprintf(text, sizeof text, "call %zu of %zu: slot %X (%08X) ", at, calls.size(), calls[at].slot,
                          calls[at].handle);
            return text + Hex(calls[at].data, sizeof calls[at].data);
        };
        const std::string game = describe(g_dwDecalGame);
        compare::Record("effects.decal_step", false, game, describe(g_dwDecalOurs));
    } else if (std::memcmp(theirs, mine, kDecalSize) != 0) {
        std::size_t at = 0;
        while (theirs[at] == mine[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "decal+%zX ", at);
        compare::Record("effects.decal_step", false, text + Hex(theirs + at, 4), text + Hex(mine + at, 4));
    } else {
        compare::Record(g_dwDecalEnded ? "effects.decal_step.ended" : "effects.decal_step", true, std::string(),
                        std::string());
    }
    return result;
}

uint32_t DwDecalStepRun(char* self, uint32_t step)
{
    if (g_dwDecalMode == StepMode::Original)
        return g_SC4EffectDecal_Draw(self, nullptr, step);
    if (g_dwDecalMode == StepMode::Ours) {
        StepDecal(self, LfFloat(step));
        return 0;
    }
    if (At<uint32_t>(self, 0) != kDwDecalTable || g_dwDecal)
        return g_SC4EffectDecal_Draw(self, nullptr, step);
    return DwDecalCompared(self, step);
}

uint32_t __fastcall DwDecalStepByMode(char* self, void*, uint32_t step)
{
    if (!DwNoting())
        return DwDecalStepRun(self, step);
    DwAddNote(kDwDecal, self, step, 0);
    ++g_dwDepth;
    const uint32_t result = DwDecalStepRun(self, step);
    --g_dwDepth;
    return result;
}
OPENSC4_THUNK(TDwDecalStep, DwDecalStepByMode, 4)

// --- A decal's update by its mode

StepMode g_dwUpdateMode = StepMode::Original;
Call2Fn g_SC4EffectDecal_Update = nullptr;

uint32_t __fastcall DwUpdateByMode(char* decal, void*, uint32_t first, uint32_t second)
{
    const auto game = [&] { return g_SC4EffectDecal_Update(decal, nullptr, first, second); };
    const auto ours = [&] {
        UpdateDecal(decal, Ptr(first), Ptr(second));
        return 0u;
    };
    if (g_dwUpdateMode == StepMode::Original || g_stNoting || g_stReplay)
        return game();
    if (g_dwUpdateMode == StepMode::Ours)
        return ours();
    if (!At<uint8_t>(decal, 0x110) && (At<uint32_t>(decal, 0xC) >> 6 & 1) && At<int32_t>(decal, 0x70) != -1) {
        compare::Record("effects.decal_update.not_compared", true, std::string(), std::string());
        return game();
    }
    return DcDecalCompared("effects.decal_update", decal, false, game, ours);
}
OPENSC4_THUNK(TDwUpdate, DwUpdateByMode, 8)

// --- A decal's draw by its mode

StepMode g_ddMode = StepMode::Original;
Call1Fn g_SC4EffectDecal_Start = nullptr;
using DdPickerFn = char*(__cdecl*)();
DdPickerFn g_DdTerrainPicker = nullptr;

// A call as the draw's comparison notes it: the slot (0 the terrain's view
// asked for, 0x148 the view's slot 0x48, else the overlay manager's), the
// words and the bytes behind a pointer.
struct DdCall {
    uint32_t slot, words[4];
    char data[0x24];
    bool operator==(const DdCall& other) const
    {
        return slot == other.slot && std::memcmp(words, other.words, sizeof words) == 0 &&
               std::memcmp(data, other.data, sizeof data) == 0;
    }
};
// The game's answer to a call: the word, and the bytes a block's query
// (slot 0x38) wrote.
struct DdAnswer {
    uint32_t value;
    char block[0x24];
    bool written[0x24];
};
enum class DdRun { Off, Game, Ours };
DdRun g_ddRun = DdRun::Off;
DWORD g_ddThread = 0;
int g_ddDepth = 0;
std::vector<DdCall> g_ddGame, g_ddOurs;
std::vector<DdAnswer> g_ddAnswers;
std::size_t g_ddAnswered = 0;
char* g_ddView = nullptr;
char* g_ddOwner = nullptr;
uint32_t g_ddViewTable[kDwDecalSlots], g_ddOwnerTable[kDwDecalSlots];
uint32_t* g_ddViewStandIn = g_ddViewTable;
uint32_t* g_ddOwnerStandIn = g_ddOwnerTable;

// Notes a call. In the game's run `made` makes it on the real object and
// the answer is kept; in ours' the game's answer of the same turn is given.
template <typename Made>
DdAnswer DdCalled(uint32_t slot, std::initializer_list<uint32_t> words, const void* data, std::size_t size,
                  Made&& made)
{
    DdCall call{slot, {}, {}};
    std::copy(words.begin(), words.end(), call.words);
    if (size)
        std::memcpy(call.data, data, size);
    if (g_ddRun == DdRun::Game) {
        g_ddGame.push_back(call);
        DdAnswer answer{};
        ++g_ddDepth;
        answer.value = made(answer);
        --g_ddDepth;
        g_ddAnswers.push_back(answer);
        return answer;
    }
    g_ddOurs.push_back(call);
    return g_ddAnswered < g_ddAnswers.size() ? g_ddAnswers[g_ddAnswered++] : DdAnswer{};
}

// The terrain's view, in a compared run the stand-in.
char* __cdecl DdPicker()
{
    if (g_ddRun == DdRun::Off || g_ddDepth != 0 || GetCurrentThreadId() != g_ddThread)
        return g_DdTerrainPicker();
    const DdAnswer answer = DdCalled(0, {}, nullptr, 0, [](DdAnswer&) {
        g_ddView = g_DdTerrainPicker();
        return Word(g_ddView);
    });
    return answer.value ? reinterpret_cast<char*>(&g_ddViewStandIn) : nullptr;
}
OPENSC4_CDECL_THUNK(TDdPicker, DdPicker, 0)

// The stand-in view's slot 0x48: the overlay manager by number, the
// stand-in owner for it.
uint32_t __fastcall DdViewManager(void*, void*, uint32_t number)
{
    const DdAnswer answer = DdCalled(0x148, {number}, nullptr, 0, [&](DdAnswer&) {
        g_ddOwner = SuSlot<char*>(g_ddView, 0x48, number);
        return Word(g_ddOwner);
    });
    return answer.value ? Word(&g_ddOwnerStandIn) : 0;
}

// The stand-in owner's slots.
template <uint32_t Slot>
uint32_t __fastcall DdOwnerCount(void*, void*)
{
    return DdCalled(Slot, {}, nullptr, 0, [](DdAnswer&) { return SuSlot<uint32_t>(g_ddOwner, Slot); }).value;
}

template <uint32_t Slot>
uint32_t __fastcall DdOwnerAdd(void*, void*, uint32_t key, uint32_t place, uint32_t size, uint32_t turn)
{
    return DdCalled(Slot, {key, place, size, turn}, Ptr(place), 12, [&](DdAnswer&) {
               return SuSlot<uint32_t>(g_ddOwner, Slot, key, place, size, turn);
           }).value;
}

template <uint32_t Slot>
uint32_t __fastcall DdOwnerValue(void*, void*, uint32_t handle, uint32_t value)
{
    return DdCalled(Slot, {handle, value}, nullptr, 0, [&](DdAnswer&) {
               return SuSlot<uint32_t>(g_ddOwner, Slot, handle, value);
           }).value;
}

template <uint32_t Slot, std::size_t Size>
uint32_t __fastcall DdOwnerData(void*, void*, uint32_t handle, uint32_t data)
{
    return DdCalled(Slot, {handle}, Ptr(data), Size, [&](DdAnswer&) {
               return SuSlot<uint32_t>(g_ddOwner, Slot, handle, data);
           }).value;
}

// The block's query: noted by its first two words (the rest is what the
// caller's stack held); ours is given the bytes the game's query wrote.
// Which those are is learnt by asking it twice more, on blocks of zero
// bytes and of 0xFF bytes: it only reads the manager.
uint32_t __fastcall DdOwnerInfo(void*, void*, uint32_t handle, uint32_t block)
{
    char* const out = Ptr(block);
    const DdAnswer answer = DdCalled(0x38, {handle}, out, 8, [&](DdAnswer& a) {
        const uint32_t value = SuSlot<uint32_t>(g_ddOwner, 0x38, handle, block);
        std::memcpy(a.block, out, sizeof a.block);
        char zeros[0x24] = {}, ones[0x24];
        std::memset(ones, 0xFF, sizeof ones);
        SuSlot<uint32_t>(g_ddOwner, 0x38, handle, zeros);
        SuSlot<uint32_t>(g_ddOwner, 0x38, handle, ones);
        for (std::size_t at = 0; at < sizeof zeros; ++at)
            a.written[at] = zeros[at] == ones[at];
        return value;
    });
    if (g_ddRun == DdRun::Ours) {
        for (std::size_t at = 0; at < sizeof answer.block; ++at) {
            if (answer.written[at])
                out[at] = answer.block[at];
        }
    }
    return answer.value;
}

std::string DdDescribe(const std::vector<DdCall>& calls, std::size_t at)
{
    char text[96];
    if (at >= calls.size()) {
        std::snprintf(text, sizeof text, "%zu calls", calls.size());
        return text;
    }
    const DdCall& c = calls[at];
    std::snprintf(text, sizeof text, "call %zu of %zu: slot %X (%08X %08X %08X %08X) ", at, calls.size(), c.slot,
                  c.words[0], c.words[1], c.words[2], c.words[3]);
    return text + Hex(c.data, sizeof c.data);
}

uint32_t DdCompared(char* self, uint32_t atEnd)
{
    std::memset(g_ddViewTable, 0, sizeof g_ddViewTable);
    g_ddViewTable[0x48 / 4] = Word(reinterpret_cast<void*>(&DdViewManager));
    std::memset(g_ddOwnerTable, 0, sizeof g_ddOwnerTable);
    g_ddOwnerTable[1] = Word(reinterpret_cast<void*>(&DdOwnerCount<4>));
    g_ddOwnerTable[2] = Word(reinterpret_cast<void*>(&DdOwnerCount<8>));
    g_ddOwnerTable[0x10 / 4] = Word(reinterpret_cast<void*>(&DdOwnerAdd<0x10>));
    g_ddOwnerTable[0x18 / 4] = Word(reinterpret_cast<void*>(&DdOwnerAdd<0x18>));
    g_ddOwnerTable[0x1C / 4] = Word(reinterpret_cast<void*>(&DdOwnerAdd<0x1C>));
    g_ddOwnerTable[0x34 / 4] = Word(reinterpret_cast<void*>(&DdOwnerData<0x34, 0x24>));
    g_ddOwnerTable[0x38 / 4] = Word(reinterpret_cast<void*>(&DdOwnerInfo));
    g_ddOwnerTable[0x44 / 4] = Word(reinterpret_cast<void*>(&DdOwnerValue<0x44>));
    g_ddOwnerTable[0x48 / 4] = Word(reinterpret_cast<void*>(&DdOwnerData<0x48, 12>));
    g_ddOwnerTable[0x4C / 4] = Word(reinterpret_cast<void*>(&DdOwnerValue<0x4C>));
    g_ddOwnerTable[0x50 / 4] = Word(reinterpret_cast<void*>(&DdOwnerValue<0x50>));
    g_ddOwner = At<char*>(self, 0x120);
    g_ddView = nullptr;
    if (g_ddOwner)
        At<uint32_t**>(self, 0x120) = &g_ddOwnerStandIn;
    char before[kDecalSize], theirs[kDecalSize];
    std::memcpy(before, self, kDecalSize);
    g_ddGame.clear();
    g_ddOurs.clear();
    g_ddAnswers.clear();
    g_ddAnswered = 0;
    g_ddThread = GetCurrentThreadId();
    g_ddRun = DdRun::Game;
    const uint32_t result = g_SC4EffectDecal_Start(self, nullptr, atEnd);
    std::memcpy(theirs, self, kDecalSize);
    std::memcpy(self, before, kDecalSize);
    g_ddRun = DdRun::Ours;
    StartDecal(self, atEnd);
    g_ddRun = DdRun::Off;
    char text[96];
    if (!(g_ddGame == g_ddOurs)) {
        std::size_t at = 0;
        while (at < g_ddGame.size() && at < g_ddOurs.size() && g_ddGame[at] == g_ddOurs[at])
            ++at;
        compare::Record("effects.decal_draw", false, DdDescribe(g_ddGame, at), DdDescribe(g_ddOurs, at));
    } else if (std::memcmp(theirs, self, kDecalSize) != 0) {
        std::size_t at = 0;
        while (theirs[at] == self[at])
            ++at;
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "decal+%zX ", at);
        compare::Record("effects.decal_draw", false, text + Hex(theirs + at, 4), text + Hex(self + at, 4));
    } else {
        compare::Record(At<uint8_t>(before, 8) ? "effects.decal_draw.started" : "effects.decal_draw", true,
                        std::string(), std::string());
    }
    std::memcpy(self, theirs, kDecalSize);
    if (At<uint32_t**>(self, 0x120) == &g_ddOwnerStandIn)
        At<char*>(self, 0x120) = g_ddOwner;
    return result;
}

uint32_t __fastcall DdStartByMode(char* self, void*, uint32_t atEnd)
{
    CountEffectCall(addr::SC4EffectDecal_Start);
    if (g_ddMode == StepMode::Original || g_stNoting || g_stReplay || g_ddRun != DdRun::Off)
        return g_SC4EffectDecal_Start(self, nullptr, atEnd);
    if (g_ddMode == StepMode::Ours) {
        StartDecal(self, atEnd);
        return 0;
    }
    return DdCompared(self, atEnd);
}
OPENSC4_THUNK(TDdStart, DdStartByMode, 4)

// --- The manager's draw by its mode

StepMode g_dwManagerMode = StepMode::Original;
Call1Fn g_SC4EffectsManager_Draw = nullptr;

// Gives the two vectors the draw fills room for every effect (from the
// game's allocator, as their own growth would), so that neither compared
// run moves them: a copy cannot undo that.
void DwReserve(char* self)
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

// What ours writes of the manager (offsets from the interface).
constexpr std::size_t kDwWritten[][2] = {{0x1B4, 0x1BC}, {0xC8C, 0xC90}, {0xC94, 0xCA0}, {0xDA0, 0xDB8}};

std::string DwDescribe(const std::vector<DwNote>& notes, std::size_t at)
{
    char text[96];
    if (at < notes.size())
        std::snprintf(text, sizeof text, "note %zu of %zu: %u on %08X (%08X, %08X)", at, notes.size(), notes[at].kind,
                      notes[at].self, notes[at].a, notes[at].b);
    else
        std::snprintf(text, sizeof text, "%zu notes", notes.size());
    return text;
}

extern bool g_forceMgrSteps;
void FsForceAfterDraw(char* self, char* context);

bool g_dwForce = false, g_dwForcing = false;

uint32_t DwManagerCompared(char* self, char* context)
{
    const char* const id = g_dwForcing ? "effects.draw_manager.forced" : "effects.draw_manager";
    DwReserve(self);
    char* const manager = self - 4;
    const std::vector<char> before(manager, manager + kManagerSize);
    const std::vector<uint32_t> fourth = DwFourth(self);
    const bool early = At<uint8_t>(At<char*>(At<char*>(context, 0x38), 0xC), 0x39C) != 0;
    g_dwOurs.clear();
    g_dwTrace = DwTrace::Ours;
    DrawEffects(self, context);
    g_dwTrace = DwTrace::Off;
    const std::vector<char> mine(manager, manager + kManagerSize);
    const std::vector<char> ourShown(At<char*>(self, 0xDA0), At<char*>(self, 0xDA4));
    const std::vector<char> ourHidden(At<char*>(self, 0xDAC), At<char*>(self, 0xDB0));
    std::memcpy(manager, before.data(), kManagerSize);
    g_dwGame.clear();
    g_dwCounted = 0;
    g_dwFourthTaken = false;
    g_dwManager = self;
    g_dwThread = GetCurrentThreadId();
    g_dwTrace = DwTrace::Game;
    const uint32_t result = g_SC4EffectsManager_Draw(self, nullptr, Word(context));
    g_dwTrace = DwTrace::Off;
    if (!g_dwFourthTaken)
        g_dwFourth = DwFourth(self);
    const bool sameFourth = g_dwFourth == fourth;
    std::vector<DwNote> ours = g_dwOurs, game = g_dwGame;
    char text[96];
    if (!early) {
        const DwNote last{kDwAfter, Word(manager), 0, 0};
        if (ours.empty() || !(ours.back() == last)) {
            compare::Record(id, false, "the clean-up last", DwDescribe(ours, ours.size() - (ours.empty() ? 0 : 1)));
            return result;
        }
        ours.pop_back();
    }
    if (!sameFourth) {
        compare::Record("effects.draw_manager.fourth_changed", true, std::string(), std::string());
        const auto drop = [](std::vector<DwNote>& notes) {
            notes.erase(std::remove_if(notes.begin(), notes.end(), [](const DwNote& n) { return n.kind == kDwDecal; }),
                        notes.end());
        };
        drop(ours);
        drop(game);
    }
    if (!(ours == game)) {
        std::size_t at = 0;
        while (at < ours.size() && at < game.size() && ours[at] == game[at])
            ++at;
        compare::Record(id, false, DwDescribe(game, at), DwDescribe(ours, at));
        return result;
    }
    for (const auto& [from, to] : kDwWritten) {
        for (std::size_t at = from; at < to; at += 4) {
            if (at == 0xC8C || (at == 0xC98 && !sameFourth))
                continue;
            if (std::memcmp(self + at, mine.data() + 4 + at, 4) != 0) {
                std::snprintf(text, sizeof text, "manager+%zX ", at);
                compare::Record(id, false, text + Hex(self + at, 4), text + Hex(mine.data() + 4 + at, 4));
                return result;
            }
        }
    }
    if (!early && std::memcmp(mine.data() + 4 + 0xC8C, &g_dwCounted, 4) != 0) {
        compare::Record(id, false, "particles counted " + Hex(&g_dwCounted, 4),
                        "particles counted " + Hex(mine.data() + 4 + 0xC8C, 4));
        return result;
    }
    for (std::size_t at = 0; at < kManagerSize; ++at) {
        bool written = false;
        for (const auto& [from, to] : kDwWritten)
            written = written || (at >= from + 4 && at < to + 4);
        if (!written && mine[at] != before[at]) {
            std::snprintf(text, sizeof text, "manager+%zX ", (at & ~std::size_t{3}) - 4);
            compare::Record(id, false, std::string(text) + "as it was", std::string(text) + "written by ours");
            return result;
        }
    }
    if (ourShown != std::vector<char>(At<char*>(self, 0xDA0), At<char*>(self, 0xDA4)) ||
        ourHidden != std::vector<char>(At<char*>(self, 0xDAC), At<char*>(self, 0xDB0))) {
        compare::Record(id, false, "the effects in view, sorted, and those out of view", "another order or set");
        return result;
    }
    compare::Record(early ? (g_dwForcing ? "effects.draw_manager.off.forced" : "effects.draw_manager.off") : id, true,
                    std::string(), std::string());
    FsForceAfterDraw(self, context);
    return result;
}

// With OPENSC4_EFFECT_FORCE_MANAGER=1 (compare mode of the manager's draw
// only) each compared draw is made in turn with one of seven changes, put
// back after both runs: none; the render's property at +0x39C set (the
// draw does nothing); its +0x1BC cleared (no effect drawn); its +0x35C set
// (the boxes drawn); the blend (the render's +0x24's +0xBC) 0.5 with the
// last frame's count (+0xC8C) 6,000, above the most; the blend 0.5 with the
// budget (+0xC9C) 0.5, below the target; every description of the three
// lists given flag 0x10 (no frustum test). The budget the draw leaves is
// put back too, so that the forcing does not change later frames. They
// are recorded as effects.draw_manager.forced (.off.forced for +0x39C).
uint32_t DwManagerForced(char* self, char* context)
{
    static uint32_t turn = 0;
    const uint32_t k = turn++ % 7;
    char* const properties = At<char*>(At<char*>(context, 0x38), 0xC);
    char* const options = At<char*>(At<char*>(context, 0x38), 0x24);
    const uint8_t off = At<uint8_t>(properties, 0x39C), effects = At<uint8_t>(properties, 0x1BC),
                  boxes = At<uint8_t>(properties, 0x35C);
    const uint32_t blend = At<uint32_t>(options, 0xBC), budget = At<uint32_t>(self, 0xC9C);
    std::vector<std::pair<char*, uint32_t>> flagged;
    switch (k) {
    case 1:
        At<uint8_t>(properties, 0x39C) = 1;
        break;
    case 2:
        At<uint8_t>(properties, 0x1BC) = 0;
        break;
    case 3:
        At<uint8_t>(properties, 0x35C) = 1;
        break;
    case 4:
        At<float>(options, 0xBC) = 0.5f;
        At<uint32_t>(self, 0xC8C) = 6000;
        break;
    case 5:
        At<float>(options, 0xBC) = 0.5f;
        At<float>(self, 0xC9C) = 0.5f;
        break;
    case 6:
        for (const auto& [indices, table] : {std::pair{std::size_t{0xBF0}, std::size_t{0xBE4}},
                                             std::pair{std::size_t{0xC38}, std::size_t{0xC2C}},
                                             std::pair{std::size_t{0xC5C}, std::size_t{0xC50}}}) {
            for (uint32_t* it = At<uint32_t*>(self, indices); it != At<uint32_t*>(self, indices + 4); ++it) {
                char* const description = At<char*>(At<char**>(self, table)[*it], 0x24);
                if (!description)
                    continue;
                bool seen = false;
                for (const auto& entry : flagged)
                    seen = seen || entry.first == description;
                if (!seen) {
                    flagged.push_back({description, At<uint32_t>(description, 0)});
                    At<uint32_t>(description, 0) |= 0x10u;
                }
            }
        }
        break;
    default:
        break;
    }
    g_dwForcing = true;
    const uint32_t result = DwManagerCompared(self, context);
    g_dwForcing = false;
    At<uint8_t>(properties, 0x39C) = off;
    At<uint8_t>(properties, 0x1BC) = effects;
    At<uint8_t>(properties, 0x35C) = boxes;
    At<uint32_t>(options, 0xBC) = blend;
    if (k == 4 || k == 5)
        At<uint32_t>(self, 0xC9C) = budget;
    for (const auto& [description, flags] : flagged)
        At<uint32_t>(description, 0) = flags;
    return result;
}

void TcForce(char* self);
void McForce(char* self);
void PcForce(char* self);
void EcForce(char* self);

uint32_t __fastcall DwManagerByMode(char* self, void*, uint32_t context)
{
    HrForce(self, Ptr(context));
    TcForce(self);
    McForce(self);
    PcForce(self);
    EcForce(self);
    if (g_dwManagerMode == StepMode::Original || g_dwTrace != DwTrace::Off || g_depth > 0) {
        const uint32_t result = g_SC4EffectsManager_Draw(self, nullptr, context);
        // The flash and the shake are forced after the manager's draw (the
        // compared manager's draw does it in DwManagerCompared).
        if (g_forceMgrSteps && g_dwManagerMode == StepMode::Original && g_dwTrace == DwTrace::Off && g_depth == 0)
            FsForceAfterDraw(self, Ptr(context));
        return result;
    }
    if (g_dwManagerMode == StepMode::Ours) {
        const uint32_t result = DrawEffects(self, Ptr(context));
        if (g_forceMgrSteps && g_dwTrace == DwTrace::Off && g_depth == 0)
            FsForceAfterDraw(self, Ptr(context));
        return result;
    }
    if (g_dwForce)
        return DwManagerForced(self, Ptr(context));
    return DwManagerCompared(self, Ptr(context));
}
OPENSC4_THUNK(TDwManager, DwManagerByMode, 4)

// The effect's draw and the decal's step have one detour each, which the
// manager's comparison and their own switches share.
bool DwHookEffectDraw()
{
    return g_SC4Effect_Draw || Detour(addr::SC4Effect_Draw, &TDwEffectDraw, &g_SC4Effect_Draw);
}

bool DwHookDecalStep()
{
    return g_SC4EffectDecal_Draw || Detour(addr::SC4EffectDecal_Draw, &TDwDecalStep, &g_SC4EffectDecal_Draw);
}

bool InstallDrawEffect(registry::Mode mode)
{
    if (!game::IsBuild641() || !DwHookEffectDraw())
        return false;
    if (mode == registry::Mode::Compare &&
        !(HookFrees() && HookPool() && HookFinish() &&
          (g_SC4Effect_StepParticle || Detour(addr::SC4Effect_StepParticle, &TLfStep, &g_SC4Effect_StepParticle)) &&
          (g_SC4Effect_Emit || Detour(addr::SC4Effect_Emit, &TLfEmit, &g_SC4Effect_Emit)) &&
          (g_SC4Effect_BeforeParticles ||
           Detour(addr::SC4Effect_BeforeParticles, &TSuAxes, &g_SC4Effect_BeforeParticles)) &&
          (g_SC4Effect_NotDrawn || Detour(addr::SC4Effect_NotDrawn, &TLfBox, &g_SC4Effect_NotDrawn)) &&
          (g_SC4Effect_Wander || Detour(addr::SC4Effect_StepFlag800000, &TLfWander, &g_SC4Effect_Wander)) &&
          (g_SC4Effect_PathVelocity ||
           Detour(addr::SC4Effect_PathVelocity, &TLfPull, &g_SC4Effect_PathVelocity)) &&
          (g_SC4Effect_StartEdge || Detour(addr::SC4Effect_StartEdge, &TLfEdge, &g_SC4Effect_StartEdge)) &&
          (g_SC4Effect_StartAtAges || Detour(addr::SC4Effect_StartAtAges, &TLfAges, &g_SC4Effect_StartAtAges)) &&
          PdHookBuffered() && PdHookPlain() && PdHookB() &&
          (g_SC4EffectB_NotDrawn || Detour(addr::SC4EffectB_NotDrawn, &TSdNotDrawn, &g_SC4EffectB_NotDrawn)) &&
          Detour(addr::SC4RenderContext_SetColourAlt, &TDwSC4RenderContext_SetColourAlt,
                 &g_DwSC4RenderContext_SetColourAlt) &&
          Detour(addr::SC4RenderContext_UpdateViewMatrix, &TDwSC4RenderContext_UpdateViewMatrix,
                 &g_DwSC4RenderContext_UpdateViewMatrix) &&
          Detour(addr::SC4RenderContext_SetRenderState, &TDwSC4RenderContext_SetRenderState,
                 &g_DwSC4RenderContext_SetRenderState) &&
          PdHookResetColour()))
        return false;
    g_dwEffectMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: an effect's draw %s", mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallDrawManager(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    if (mode == registry::Mode::Compare &&
        !(DwHookEffectDraw() && DwHookDecalStep() &&
          (g_DwBeforeDraw || Detour(addr::SC4EffectHeightMap_DrawLines, &TDwBeforeDraw, &g_DwBeforeDraw)) &&
          Detour(addr::SC4EffectsManager_DrawFlash, &TDwDrawFlash, &g_DwDrawFlash) &&
          Detour(addr::SC4EffectsManager_StepShake, &TDwStepShake, &g_DwStepShake) &&
          Detour(addr::SC4RenderContext_DrawBox, &TDwDrawBox, &g_DwDrawBox)))
        return false;
    if (!g_SC4EffectsManager_Draw && !Detour(addr::SC4EffectsManager_Draw, &TDwManager, &g_SC4EffectsManager_Draw))
        return false;
    g_dwManagerMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    if (const char* v = std::getenv("OPENSC4_EFFECT_FORCE_MANAGER"); mode == registry::Mode::Compare && v && *v == '1') {
        g_dwForce = true;
        log::Write("water effects: the effects manager's draw forced with made-up properties, budgets and flags");
    }
    log::Write("water effects: the effects manager's draw %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallHeightMapRest(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_DwBeforeDraw || Detour(addr::SC4EffectHeightMap_DrawLines, &TDwBeforeDraw, &g_DwBeforeDraw)) ||
        !Detour(addr::SC4EffectHeightMap_ClearValues, &THrClear, &g_SC4EffectHeightMap_ClearValues) ||
        !Detour(addr::SC4EffectHeightMap_Destructor, &THrEnd, &g_SC4EffectHeightMap_Destructor) ||
        !Detour(addr::SC4EffectHeightMap_MakeLines, &THrMakeLines, &g_SC4EffectHeightMap_MakeLines))
        return false;
    if (mode == registry::Mode::Compare) {
        if (!Detour(addr::SC4RenderState_Init, &THrState, &g_SC4RenderState_Init) ||
            !Detour(addr::SC4HeightMapLines_Mesh, &THrMesh, &g_SC4HeightMapLines_Mesh) ||
            !Detour(addr::SC4HeightMapLines_Clear, &TMlClear, &g_SC4HeightMapLines_Clear) ||
            !Detour(addr::SC4HeightMapLines_SetVectorC, &TMlVectorC, &g_SC4HeightMapLines_SetVectorC) ||
            !Detour(addr::SC4HeightMapLines_SetColour, &TMlColour, &g_SC4HeightMapLines_SetColour) ||
            !Detour(addr::SC4HeightMapLines_SetNormal, &TMlNormal, &g_SC4HeightMapLines_SetNormal) ||
            !Detour(addr::SC4HeightMapLines_AddBox, &TMlAddBox, &g_SC4HeightMapLines_AddBox))
            return false;
        const char* v = std::getenv("OPENSC4_HEIGHTMAP_FORCE");
        if (v && *v == '1') {
            if (!g_SC4EffectsManager_Draw &&
                !Detour(addr::SC4EffectsManager_Draw, &TDwManager, &g_SC4EffectsManager_Draw))
                return false;
            g_hrForce = true;
            log::Write("water effects: the height map's lines, clear and destructor forced before the manager's draw");
        }
    }
    g_hrMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the height map's lines, their making, clear and destructor %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredHeightMapRest = registry::Register({
    "renderer.effects.heightmap_rest",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectHeightMap_DrawLines,
    "The particles' height map's own lines (a debug view the manager's draw asks for: their draw and their making), "
    "the clear of its values and its plain destructor (0x00586720, 0x00585C20, 0x00586700, 0x00586900); compare "
    "mode runs ours and the game's with the calls they are made of noted, or ours on a copy, "
    "OPENSC4_HEIGHTMAP_FORCE=1 forces them before the manager's draw "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallHeightMapRest,
});

bool InstallDecalStep(registry::Mode mode)
{
    if (!game::IsBuild641() || !DwHookDecalStep())
        return false;
    g_dwDecalMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: a decal's step %s", mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallDecalUpdate(registry::Mode mode)
{
    return SuInstall(mode, g_dwUpdateMode, addr::SC4EffectDecal_Update, &TDwUpdate, &g_SC4EffectDecal_Update,
                     "a decal's update");
}

bool InstallDecalDraw(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    if (mode == registry::Mode::Compare &&
        !(g_DdTerrainPicker || Detour(addr::SC4_TerrainPicker, &TDdPicker, &g_DdTerrainPicker)))
        return false;
    return SuInstall(mode, g_ddMode, addr::SC4EffectDecal_Start, &TDdStart, &g_SC4EffectDecal_Start,
                     "a decal's draw");
}

const bool g_registeredDrawManager = registry::Register({
    "renderer.effects.draw_manager",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectsManager_Draw,
    "The effects manager's draw (0x00593120): the effects in view collected from three lists, the particle budget "
    "steered, the effects sorted by depth and drawn, the fourth list's decals stepped, the clean-up; compare mode "
    "runs ours with the calls that draw only noted, then the game's, and compares the notes and the manager "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallDrawManager,
});

const bool g_registeredDrawEffect = registry::Register({
    "renderer.effects.draw_effect",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_Draw,
    "An effect's draw (0x005B8540): its clock and velocity, its particles stepped and the ended ones given back, "
    "new ones emitted, drawn or kept as not drawn; compare mode records the game's run and replays ours "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallDrawEffect,
});

const bool g_registeredDecalStep = registry::Register({
    "renderer.effects.decal_step",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDecal_Draw,
    "A decal's step (0x00582920): its time, and the curves read there given to its owner; compare mode gives the "
    "decal a stand-in owner that notes the calls, runs ours and the game's and compares the notes and the decal "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallDecalStep,
});

const bool g_registeredDecalUpdate = registry::Register({
    "renderer.effects.decal_update",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDecal_Update,
    "A decal's update (0x00582600): its place, scale and turn from two transforms; compare mode runs the game's "
    "and ours on the same decal (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallDecalUpdate,
});

const bool g_registeredDecalDraw = registry::Register({
    "renderer.effects.decal_draw",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectDecal_Start,
    "A decal's draw (0x00582090): the decal put on the terrain once, through an overlay manager of the terrain's "
    "view, with its block, flags, opacity, colour and draw mode; compare mode gives it a stand-in view and owner "
    "that note the calls, runs the game's and then ours with the game's answers, and compares the calls and the "
    "decal (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallDecalDraw,
});

// --- The flash and the camera shake after the draw ------------------------------------------

// The flash (0x0058E870) and the camera shake (0x0058EC40), the manager's
// two steps after the draw. No test city runs them (nothing sets +0xD44
// above 0 with the property at +0x3FC, nor +0xD88 above 0), so they are
// compared only when forced: OPENSC4_EFFECT_FORCE_MGR_STEPS=1 sets up
// +0xD44/+0xD88 and their point lists and curves with made-up but valid
// data, runs both ours and the game's, compares the bytes each writes and
// the calls each makes, and puts everything back.
//
// Both calls are made from the flash and the shake on objects of their
// own: the context's +0x34 (slot 0x2C gives a pair, slot 0xC4 draws) and
// the object at 0x00B43DDC (slot 0x1C shakes, slot 0x20 stops). The
// forcing stands a table of its own in front of each, so ours and the
// game's are handed the same pair and both draws are noted, never made.

bool g_forceMgrSteps = false;

// One call noted by a stand-in, with the words it was given (the draw's
// data and block, or the shake's direction) and one further word.
struct FsCall {
    uint32_t kind;
    uint32_t last;
    uint32_t words[8];
    bool operator==(const FsCall& other) const
    {
        return kind == other.kind && last == other.last && std::memcmp(words, other.words, sizeof words) == 0;
    }
};

std::vector<FsCall> g_fsOurs, g_fsGame;
bool g_fsForward = false;
uint32_t g_fsPair[2] = {0, 0};

void FsNote(uint32_t kind, const void* first, std::size_t firstSize, uint32_t last)
{
    FsCall call{kind, last, {}};
    if (first)
        std::memcpy(call.words, first, firstSize);
    (g_fsForward ? g_fsGame : g_fsOurs).push_back(call);
}

uint32_t __fastcall FsPairOf(void*, void*, uint32_t first, uint32_t second)
{
    At<uint32_t>(Ptr(first), 0) = g_fsPair[0];
    At<uint32_t>(Ptr(second), 0) = g_fsPair[1];
    return 0;
}

uint32_t __fastcall FsDrawSprite(void*, void*, uint32_t data, uint32_t block, uint32_t, uint32_t)
{
    uint32_t words[8] = {};
    std::memcpy(words, Ptr(data), 16);
    std::memcpy(words + 4, Ptr(block), 16);
    FsNote(0xC4, words, sizeof words, 0);
    return 0;
}

uint32_t __fastcall FsShakeSet(void*, void*, uint32_t direction, uint32_t strength)
{
    FsNote(0x1C, Ptr(direction), 12, strength);
    return 0;
}

uint32_t __fastcall FsShakeStop(void*, void*)
{
    FsNote(0x20, nullptr, 0, 0);
    return 0;
}

// The two functions and the manager's draw have one detour each, which the
// manager's comparison, the switches and the forcing share: never hook one
// twice (a second Install would undo the first).
bool FsHookFlash()
{
    return g_DwDrawFlash || Detour(addr::SC4EffectsManager_DrawFlash, &TDwDrawFlash, &g_DwDrawFlash);
}

bool FsHookShake()
{
    return g_DwStepShake || Detour(addr::SC4EffectsManager_StepShake, &TDwStepShake, &g_DwStepShake);
}

bool FsHookForce()
{
    const char* v = std::getenv("OPENSC4_EFFECT_FORCE_MGR_STEPS");
    if (!v || *v != '1')
        return true;
    if (!(FsHookFlash() && FsHookShake() &&
          (g_SC4EffectsManager_Draw || Detour(addr::SC4EffectsManager_Draw, &TDwManager, &g_SC4EffectsManager_Draw))))
        return false;
    if (!g_forceMgrSteps)
        log::Write("water effects: the flash and the shake forced after the manager's draw");
    g_forceMgrSteps = true;
    return true;
}

// Four point entries and four curve floats for the flash and the shake.
constexpr float kFsPoints[4][3] = {{0.1f, 0.2f, 0.3f}, {0.4f, 0.5f, 0.6f}, {0.7f, 0.8f, 0.9f}, {1.0f, 1.0f, 1.0f}};
constexpr float kFsCurve[4] = {0.25f, 0.5f, 0.75f, 1.0f};

// Runs ours then the game's on the same state: the two floats at `at` and
// the calls each noted are compared. The manager's two floats are put back
// to their start before the game's run.
template <typename FnOurs, typename FnGame>
void FsCompareRun(const char* id, char* iface, std::size_t at, FnOurs&& ours, FnGame&& game)
{
    char before[8], mine[8], theirs[8];
    std::memcpy(before, iface + at, sizeof before);
    g_fsOurs.clear();
    g_fsForward = false;
    ours();
    std::memcpy(mine, iface + at, sizeof mine);
    std::vector<FsCall> ourCalls = g_fsOurs;
    std::memcpy(iface + at, before, sizeof before);
    g_fsGame.clear();
    g_fsForward = true;
    game();
    std::memcpy(theirs, iface + at, sizeof theirs);
    std::vector<FsCall> gameCalls = g_fsGame;
    std::memcpy(iface + at, before, sizeof before);
    const bool callsSame = ourCalls == gameCalls;
    const bool bytesSame = std::memcmp(mine, theirs, sizeof mine) == 0;
    std::string gameText, ourText;
    if (!bytesSame) {
        char text[48];
        std::snprintf(text, sizeof text, "interface+%zX ", at);
        gameText = text + Hex(theirs, 8);
        ourText = text + Hex(mine, 8);
    } else if (!callsSame) {
        std::size_t n = 0;
        while (n < ourCalls.size() && n < gameCalls.size() && ourCalls[n] == gameCalls[n])
            ++n;
        char text[64];
        std::snprintf(text, sizeof text, "call %zu of %zu/%zu ", n, ourCalls.size(), gameCalls.size());
        gameText = std::string(text) + (n < gameCalls.size() ? Hex(&gameCalls[n], sizeof(FsCall)) : "none");
        ourText = std::string(text) + (n < ourCalls.size() ? Hex(&ourCalls[n], sizeof(FsCall)) : "none");
    }
    compare::Record(id, bytesSame && callsSame, gameText, ourText);
}

// The forcing: called after the manager draw (compared or not) when
// OPENSC4_EFFECT_FORCE_MGR_STEPS=1. Sets up the flash and the shake with
// made-up data, runs ours and the game's, and puts the manager back.
void FsForceAfterDraw(char* self, char* context)
{
    if (!g_forceMgrSteps)
        return;
    // The manager's draw calls the flash and the shake with the object at
    // its interface less four (0x00593958), so both use that as theirs.
    char* const object = self - 4;
    // Save what the two functions may write and what they read.
    constexpr std::size_t kSavedAt = 0xC80, kSavedLen = 0x320;
    char saved[kSavedLen];
    std::memcpy(saved, object + kSavedAt, kSavedLen);

    // Stand-in tables: the context's +0x34 object and the camera. Only the
    // slots the two functions call are given, the rest are zero: a call the
    // two do not make would be found at once.
    uint32_t standIn[0xCC / 4] = {};
    char* const obj34 = At<char*>(context, 0x34);
    const uint32_t savedObj34 = obj34 ? At<uint32_t>(obj34, 0) : 0;
    if (obj34) {
        standIn[0x2C / 4] = Word(reinterpret_cast<void*>(&FsPairOf));
        standIn[0xC4 / 4] = Word(reinterpret_cast<void*>(&FsDrawSprite));
        At<uint32_t>(obj34, 0) = Word(standIn);
    }
    uint32_t cameraStandIn[0x24 / 4] = {};
    char* const camera = M<char*>(0x00B43DDC);
    const uint32_t savedCamera = camera ? At<uint32_t>(camera, 0) : 0;
    if (camera) {
        cameraStandIn[0x1C / 4] = Word(reinterpret_cast<void*>(&FsShakeSet));
        cameraStandIn[0x20 / 4] = Word(reinterpret_cast<void*>(&FsShakeStop));
        At<uint32_t>(camera, 0) = Word(cameraStandIn);
    }
    const float step = 1.0f;
    if ((g_flashMode == StepMode::Compare || g_shakeMode == StepMode::Compare) && obj34) {
        static uint32_t turn = 0;
        const uint32_t k = turn++ % 144;
        const uint32_t pointCount = 1 + k % 4;
        g_fsPair[0] = 0x12345678u + k;
        g_fsPair[1] = 0x9ABCDEF0u - k;
        At<float>(object, 0xD44) = k % 4 == 0 ? 0.3f : k % 3 == 0 ? 1.5f : 0.9f;
        At<float>(object, 0xD48) = k % 2 ? -0.3f : -0.1f;
        At<uint8_t>(object, 0xD4C) = static_cast<uint8_t>((k / 4) & 1);
        At<float*>(object, 0xD28) = const_cast<float*>(kFsPoints[0]);
        At<float*>(object, 0xD2C) = const_cast<float*>(kFsPoints[0] + pointCount * 3);
        At<float*>(object, 0xD34) = const_cast<float*>(kFsCurve);
        At<float*>(object, 0xD38) = const_cast<float*>(kFsCurve + 2 + k % 3);
        At<float>(object, 0xD58) = 1.0f;
        At<int32_t>(object, 0xC80) = static_cast<int32_t>((k / 12) % 5);
        // The flash's place, set near the render properties' origin so that
        // the attenuation is in (0, 1) (and past it for some choices, ending
        // the flash): dx = part * scale, dz = part * scale / 2.
        char* const props = At<char*>(context, 0x24);
        const double scale = DwF(props, 0x150) * DwF(object, 0xD58);
        const double part = (k / 3) % 2 ? 2.5 : 0.45 + (k % 3) * 0.05;
        At<float>(object, 0xD54) = static_cast<float>(DwF(props, 0x3DC) + scale * part);
        At<float>(object, 0xD50) = static_cast<float>(DwF(props, 0x3E4) + scale * part * 0.5);
    }
    if (g_flashMode == StepMode::Compare && obj34) {
        // The camera's place (+0x3DC, +0x3E4) is the same on both axes in
        // the square test maps, which hides a swap of the two; made unequal
        // for the run and put back.
        char* const props = At<char*>(context, 0x24);
        const float savedX = At<float>(props, 0x3DC), savedZ = At<float>(props, 0x3E4);
        At<float>(props, 0x3DC) = savedX + 37.25f;
        At<float>(props, 0x3E4) = savedZ - 11.5f;
        // The flash's place near the camera and the properties' range
        // (+0x150, zero in the test cities, which makes the factor minus
        // infinity) set, so that the rest of that path runs.
        const float savedRange = At<float>(props, 0x150);
        At<float>(props, 0x150) = 2.5f;
        At<float>(object, 0xD50) = At<float>(props, 0x3DC) + At<float>(object, 0xD50) / 40.0f;
        At<float>(object, 0xD54) = At<float>(props, 0x3E4) - At<float>(object, 0xD54) / 25.0f;
        FsCompareRun("effects.flash.forced", object, 0xD44,
                     [&] { DrawFlash(object, context, step); },
                     [&] { g_DwDrawFlash(self - 4, nullptr, Word(context), LfBits(step)); });
        At<float>(props, 0x3DC) = savedX;
        At<float>(props, 0x3E4) = savedZ;
        At<float>(props, 0x150) = savedRange;
    }
    if (g_shakeMode == StepMode::Compare && camera) {
        static uint32_t turn = 0;
        const uint32_t k = turn++ % 72;
        const uint32_t pointCount = 1 + k % 4;
        At<float>(object, 0xD88) = k % 3 == 0 ? 0.3f : k % 2 == 0 ? 1.5f : 0.9f;
        At<float>(object, 0xD8C) = k % 2 ? -0.3f : -0.1f;
        At<float*>(object, 0xD6C) = const_cast<float*>(kFsPoints[0]);
        At<float*>(object, 0xD70) = const_cast<float*>(kFsPoints[0] + pointCount * 3);
        At<float*>(object, 0xD78) = const_cast<float*>(kFsCurve);
        At<float*>(object, 0xD7C) = const_cast<float*>(kFsCurve + 2 + k % 3);
        FsCompareRun("effects.shake.forced", object, 0xD88,
                     [&] { StepShake(object, step); },
                     [&] { g_DwStepShake(self - 4, nullptr, LfBits(step)); });
    }

    if (camera)
        At<uint32_t>(camera, 0) = savedCamera;
    if (obj34)
        At<uint32_t>(obj34, 0) = savedObj34;
    std::memcpy(object + kSavedAt, saved, kSavedLen);
}

bool InstallFlash(registry::Mode mode)
{
    if (!game::IsBuild641() || !FsHookFlash())
        return false;
    if (mode == registry::Mode::Compare && !FsHookForce())
        return false;
    g_flashMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the flash after the draw %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallShake(registry::Mode mode)
{
    if (!game::IsBuild641() || !FsHookShake())
        return false;
    if (mode == registry::Mode::Compare && !FsHookForce())
        return false;
    g_shakeMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the camera shake after the draw %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredFlash = registry::Register({
    "renderer.effects.flash",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectsManager_DrawFlash,
    "The full-screen flash after the effects manager's draw (0x0058E870): a colour from a point list, strength "
    "from a curve, drawn as a sprite over the view; compare mode only with OPENSC4_EFFECT_FORCE_MGR_STEPS=1, "
    "which sets up the manager's +0xD44 and the lists with made-up data, runs ours and the game's and compares "
    "every byte each writes (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallFlash,
});

const bool g_registeredShake = registry::Register({
    "renderer.effects.shake",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectsManager_StepShake,
    "The camera shake after the effects manager's draw (0x0058EC40): a direction from a point list and strength "
    "from a curve applied to the object at 0x00B43DDC; compare mode only with "
    "OPENSC4_EFFECT_FORCE_MGR_STEPS=1, which sets up the manager's +0xD88 and the lists with made-up data, "
    "runs ours and the game's and compares every byte each writes (docs/renderer.md, \"Water effects, sky and "
    "clouds\")",
    true,
    InstallShake,
});

// --- The third effect class ---------------------------------------------------------------

#include "core/effects_third.h"

// Its letting go, its giving back of a particle and its setting of a value
// (renderer.effects.third; the maker, the constructor and the destructor
// are in renderer.effects.makers, .ctor and .dtor). No test city makes an
// effect of the class, so they are compared only when forced (below).
//
// The letting go is compared as the second class's is: the effect's draw's
// record and replay, the model's release noted through a copy of its table
// of functions, the field past the bytes the record keeps put back for
// ours and checked. The giving back: ours on the list's head and the
// particle's first word, put back, then the game's. The setting of a
// value: the game's first, then ours on the effect's bytes as they were,
// the two compared with the answer's low byte; the base class's function
// (ids ours does not know) runs again for ours, and the call for id 9
// (0x005AA700) is only noted, never made twice: ours must make that one
// call with the value and the value + 0xC and change no byte.
StepMode g_thirdMode = StepMode::Original;
Call0Fn g_SC4EffectBaseC_LetGo = nullptr;
Call1Fn g_SC4EffectBaseC_GiveBackParticle = nullptr;
Call3Fn g_SC4EffectC_SetValue = nullptr;

struct LetGoCReplay {
    char* base;
    char* model;
    bool cleared;
};

uint32_t __fastcall LetGoCByMode(char* base, void*)
{
    if (g_thirdMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4EffectBaseC_LetGo(base, nullptr);
    if (g_thirdMode == StepMode::Ours)
        return LetGoEffectC(base);
    g_heldGame.clear();
    g_heldOurs.clear();
    g_effectThread = GetCurrentThreadId();
    LetGoCReplay replay{base, At<char*>(base, 0x250), false};
    const bool used = At<uint8_t>(base, 4) != 0;
    if (used)
        SwapHeld(replay.model);
    const uint32_t result = RecordAndReplay(
        used ? "effects.letgo_c" : "effects.letgo_c.unused", base - 4, false,
        [&] { return g_SC4EffectBaseC_LetGo(base, nullptr); },
        [](void* r) {
            LetGoCReplay& replay = *static_cast<LetGoCReplay*>(r);
            char* const modelAfter = At<char*>(replay.base, 0x250);
            At<char*>(replay.base, 0x250) = replay.model;
            const uint32_t result = LetGoEffectC(replay.base);
            replay.cleared = At<char*>(replay.base, 0x250) == modelAfter;
            At<char*>(replay.base, 0x250) = modelAfter;
            return result;
        },
        &replay);
    for (const auto& [object, real] : g_heldReal)
        At<uint32_t>(object, 0) = real;
    g_heldReal.clear();
    char text[64];
    std::snprintf(text, sizeof text, "%zu released", g_heldGame.size());
    const std::string game = text;
    std::snprintf(text, sizeof text, "%zu released, field %s", g_heldOurs.size(), replay.cleared ? "same" : "differs");
    const bool same = g_heldGame == g_heldOurs && replay.cleared;
    compare::Record(g_heldGame.empty() ? "effects.letgo_c.released.none" : "effects.letgo_c.released", same,
                    same ? std::string() : game, same ? std::string() : std::string(text));
    return result;
}

uint32_t __fastcall DSC4EffectBaseC_LetGo(char* base, void*)
{
    return EffectRecorded(addr::SC4EffectBaseC_LetGo, &LetGoCByMode, base);
}
OPENSC4_THUNK(TSC4EffectBaseC_LetGo, DSC4EffectBaseC_LetGo, 0)

uint32_t __fastcall GiveBackCByMode(char* base, void*, uint32_t particle)
{
    if (g_thirdMode == StepMode::Original || g_effectReplay || !particle)
        return g_SC4EffectBaseC_GiveBackParticle(base, nullptr, particle);
    if (g_thirdMode == StepMode::Ours)
        return GiveBackParticleC(particle);
    const uint32_t freeBefore = Global<uint32_t>(kParticleFreeC), linkBefore = M<uint32_t>(particle);
    const uint32_t ourResult = GiveBackParticleC(particle);
    const uint32_t ourFree = Global<uint32_t>(kParticleFreeC), ourLink = M<uint32_t>(particle);
    Global<uint32_t>(kParticleFreeC) = freeBefore;
    M<uint32_t>(particle) = linkBefore;
    const uint32_t result = g_SC4EffectBaseC_GiveBackParticle(base, nullptr, particle);
    char text[64];
    std::snprintf(text, sizeof text, "free %08X, link %08X, answer %08X", Global<uint32_t>(kParticleFreeC),
                  M<uint32_t>(particle), result);
    const std::string game = text;
    std::snprintf(text, sizeof text, "free %08X, link %08X, answer %08X", ourFree, ourLink, ourResult);
    compare::Record("effects.giveback_c", game == text, game == text ? std::string() : game,
                    game == text ? std::string() : std::string(text));
    return result;
}

uint32_t __fastcall DSC4EffectBaseC_GiveBackParticle(char* base, void*, uint32_t particle)
{
    return EffectRecorded(addr::SC4EffectBaseC_GiveBackParticle, &GiveBackCByMode, base, particle);
}
OPENSC4_THUNK(TSC4EffectBaseC_GiveBackParticle, DSC4EffectBaseC_GiveBackParticle, 4)

uint32_t SetValueCReal(char* self, uint32_t target, uint32_t a, uint32_t b, uint32_t c)
{
    return target == 0x005AA700u ? CallThis<uint32_t>(target, self, a, b) : CallThis<uint32_t>(target, self, a, b, c);
}

uint32_t __fastcall SetValueCByMode(char* self, void*, uint32_t id, uint32_t value, uint32_t third)
{
    if (g_thirdMode == StepMode::Original)
        return g_SC4EffectC_SetValue(self, nullptr, id, value, third);
    const auto real = [&](uint32_t target, uint32_t a, uint32_t b, uint32_t c) {
        return SetValueCReal(self, target, a, b, c);
    };
    const uint32_t* const words = reinterpret_cast<const uint32_t*>(value);
    if (g_thirdMode == StepMode::Ours)
        return SetEffectValueC(self, static_cast<int32_t>(id), words, third, real);
    const std::vector<char> before(self, self + kEffectSizeC);
    const uint32_t result = g_SC4EffectC_SetValue(self, nullptr, id, value, third);
    const std::vector<char> after(self, self + kEffectSizeC);
    std::memcpy(self, before.data(), kEffectSizeC);
    std::vector<std::array<uint32_t, 4>> noted;
    const uint32_t mine = SetEffectValueC(self, static_cast<int32_t>(id), words, third,
                                          [&](uint32_t target, uint32_t a, uint32_t b, uint32_t c) {
                                              if (target == 0x005AA700u) {
                                                  noted.push_back({target, a, b, 0});
                                                  return 0u;
                                              }
                                              return real(target, a, b, c);
                                          });
    const std::vector<char> ours(self, self + kEffectSizeC);
    std::memcpy(self, after.data(), kEffectSizeC);
    const bool nine = id == 9;
    const std::vector<std::array<uint32_t, 4>> expected =
        nine ? std::vector<std::array<uint32_t, 4>>{{0x005AA700u, value, value + 0xC, 0}}
             : std::vector<std::array<uint32_t, 4>>{};
    const bool same = (result & 0xFF) == (mine & 0xFF) && noted == expected && (nine ? ours == before : ours == after);
    char text[96];
    std::string game, own;
    if (!same) {
        std::size_t at = 0;
        const std::vector<char>& theirs = nine ? before : after;
        while (at < kEffectSizeC && theirs[at] == ours[at])
            ++at;
        std::snprintf(text, sizeof text, "id %X, answer %u, %zu calls, bytes from +%zX", id, result & 0xFF,
                      expected.size(), at);
        game = text;
        std::snprintf(text, sizeof text, "id %X, answer %u, %zu calls", id, mine & 0xFF, noted.size());
        own = text;
    }
    compare::Record(nine ? "effects.set_value_c.9" : "effects.set_value_c", same, game, own);
    return result;
}
OPENSC4_THUNK(TSC4EffectC_SetValue, SetValueCByMode, 12)

// The forcing (compare mode only, OPENSC4_EFFECT_FORCE_THIRD=1): before
// the manager's draw, one turn a frame of six, each on an effect of the
// class made for it:
// 0. one made by the manager's maker (0x00590EA0, so the makers'
//    comparison sees the making; a new one the first time, one taken from
//    the free ones after), its index moved at once from those in use back
//    to the free ones, so that nothing else ever sees it;
// 1. that one's values set (ids 8, 0x101, 0, 0x102 and 0x20 with made-up
//    words), its bytes put back after;
// 2. that one let go, in use (+8 set, a made-up model at +0x254) and not
//    in use, its bytes put back after;
// 3. a made-up particle given back to it, and a null one, the list's head
//    put back after;
// 4. a new one allocated, constructed and deleted (through its
//    sub-object's slot 0xC, the thunk to 0x005A9080) as it is;
// 5. the same with its vectors filled: a block at +0x220, a vector of
//    words at +0x23C, two batches copied from its own (+0x28, 0x005EC750)
//    at +0x230, at most two textures held by effects of the first class
//    in the manager's table (each given a holder) at +0x248 and the
//    made-up model at +0x254.
// Id 9 of the values is not forced: its 0x005AA700 would run for real in
// the game's run. The made-up model is an object whose table of functions
// has a function doing nothing in every slot.
bool g_tcForce = false;
char* g_tcEffect = nullptr;
uint32_t g_tcModelTable[512] = {};
uint32_t g_tcModel[4] = {};

uint32_t __fastcall TcNothing(void*, void*)
{
    return 0;
}

char* TcModel()
{
    if (!g_tcModel[0]) {
        for (uint32_t& slot : g_tcModelTable)
            slot = reinterpret_cast<uint32_t>(&TcNothing);
        g_tcModel[0] = reinterpret_cast<uint32_t>(g_tcModelTable);
    }
    return reinterpret_cast<char*>(g_tcModel);
}

char* TcNew()
{
    char* const object = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x280));
    return CallThis<char*>(addr::SC4EffectC_Construct, object);
}

void TcDelete(char* object)
{
    char* const sub = object + 4;
    CallThis<uint32_t>(M<uint32_t>(At<uint32_t>(sub, 0) + 0xC), sub, 1u);
}

void TcFill(char* manager, char* object)
{
    const auto allocate = [](std::size_t size) {
        return reinterpret_cast<char*>(reinterpret_cast<uint32_t(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(size));
    };
    At<char*>(object, 0x220) = allocate(0x10);
    char* const words = allocate(0x10);
    At<char*>(object, 0x23C) = words;
    At<char*>(object, 0x240) = words + 8;
    At<char*>(object, 0x244) = words + 0x10;
    char* const batches = allocate(0x80);
    for (int i = 0; i < 2; ++i)
        CallThis<uint32_t>(0x005EC750, batches + i * 0x40, object + 0x28);
    At<char*>(object, 0x230) = batches;
    At<char*>(object, 0x234) = batches + 0x80;
    At<char*>(object, 0x238) = batches + 0x80;
    std::vector<char*> textures;
    for (char** it = At<char**>(manager, 0xBE8); it != At<char**>(manager, 0xBEC) && textures.size() < 2; ++it) {
        char* const texture = *it ? At<char*>(*it, 0x6C) : nullptr;
        if (texture && std::find(textures.begin(), textures.end(), texture) == textures.end())
            textures.push_back(texture);
    }
    if (!textures.empty()) {
        char* const storage = allocate(textures.size() * 4);
        for (std::size_t i = 0; i < textures.size(); ++i) {
            CallThis<uint32_t>(addr::SC4TextureBinding_AddRef, textures[i]);
            At<char*>(storage, i * 4) = textures[i];
        }
        At<char*>(object, 0x248) = storage;
        At<char*>(object, 0x24C) = storage + textures.size() * 4;
        At<char*>(object, 0x250) = storage + textures.size() * 4;
    }
    At<char*>(object, 0x254) = TcModel();
}

void TcForce(char* self)
{
    if (!g_tcForce || g_dwTrace != DwTrace::Off || g_depth > 0)
        return;
    char* const manager = self - 4;
    static uint32_t turn = 0;
    const uint32_t k = turn++ % 6;
    if (k == 0) {
        char* const made = CallThis<char*>(addr::SC4EffectsManager_NewEffectC, manager);
        uint32_t*& usedEnd = At<uint32_t*>(manager, 0xC64);
        if (made && usedEnd != At<uint32_t*>(manager, 0xC60)) {
            const uint32_t index = *--usedEnd;
            MxPushIndex(manager + 0xC6C, index);
            g_tcEffect = made;
        }
        return;
    }
    if (k == 4 || k == 5) {
        char* const object = TcNew();
        if (k == 5)
            TcFill(manager, object);
        TcDelete(object);
        return;
    }
    // The manager's table may have been emptied since (a city loaded): the
    // effect is used only while the table still holds it.
    char* const effect = g_tcEffect;
    if (!effect || std::find(At<char**>(manager, 0xC54), At<char**>(manager, 0xC58), effect) ==
                       At<char**>(manager, 0xC58)) {
        g_tcEffect = nullptr;
        return;
    }
    const std::vector<char> before(effect, effect + kEffectSizeC);
    if (k == 1) {
        const uint32_t point[3] = {0x3F800000, 0x40000000, 0x40400000}, word[3] = {0x12345678, 0, 0},
                       pair[2] = {0x3F000000, 0x3E800000}, kind[3] = {0x40A00000, 0, 0};
        const std::pair<uint32_t, const uint32_t*> calls[] = {
            {8, point}, {0x101, word}, {0, kind}, {0x102, pair}, {0x20, point}};
        for (const auto& [id, value] : calls)
            CallThis<uint32_t>(M<uint32_t>(At<uint32_t>(effect, 0) + 0x14), effect, id, Word(value), 0u);
    } else if (k == 2) {
        char* const sub = effect + 4;
        for (const uint8_t used : {uint8_t{1}, uint8_t{0}}) {
            // The base class's letting go releases the objects at +0x1F0 and
            // +0x1EC (the maker's block gives one): each is held once more
            // first, as the bytes are put back after.
            for (const std::size_t at : {std::size_t{0x1F0}, std::size_t{0x1EC}}) {
                if (char* const object = At<char*>(effect, at); object && used)
                    CallThis<uint32_t>(M<uint32_t>(At<uint32_t>(object, 0) + 0x4), object);
            }
            At<uint8_t>(effect, 8) = used;
            At<char*>(effect, 0x254) = TcModel();
            CallThis<uint32_t>(M<uint32_t>(At<uint32_t>(sub, 0) + 0x14), sub);
            std::memcpy(effect, before.data(), kEffectSizeC);
        }
    } else if (k == 3) {
        static uint32_t particle[0x20] = {};
        const uint32_t head = Global<uint32_t>(kParticleFreeC);
        char* const sub = effect + 4;
        for (const uint32_t given : {Word(particle), 0u})
            CallThis<uint32_t>(M<uint32_t>(At<uint32_t>(sub, 0) + 0x40), sub, given);
        Global<uint32_t>(kParticleFreeC) = head;
    }
    std::memcpy(effect, before.data(), kEffectSizeC);
}

bool InstallThird(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !((g_SC4EffectBaseC_LetGo ||
           Detour(addr::SC4EffectBaseC_LetGo, &TSC4EffectBaseC_LetGo, &g_SC4EffectBaseC_LetGo)) &&
          Detour(addr::SC4EffectBaseC_GiveBackParticle, &TSC4EffectBaseC_GiveBackParticle,
                 &g_SC4EffectBaseC_GiveBackParticle) &&
          Detour(addr::SC4EffectC_SetValue, &TSC4EffectC_SetValue, &g_SC4EffectC_SetValue)))
        return false;
    // The letting go's comparison is the other classes': the base class's
    // and what it calls recorded (InstallLetGo's set).
    if (mode == registry::Mode::Compare) {
        if (!(HookPool() && HookFrees() &&
              (g_SC4EffectBase_Stop ||
               Detour(addr::SC4EffectBase_Stop, &TSC4EffectBase_Stop, &g_SC4EffectBase_Stop)) &&
              (g_SC4EffectDescription_Release ||
               Detour(addr::SC4EffectDescription_Release, &TSC4EffectDescription_Release,
                      &g_SC4EffectDescription_Release))))
            return false;
        g_letGoHooked = true;
        const char* v = std::getenv("OPENSC4_EFFECT_FORCE_THIRD");
        if (v && *v == '1') {
            if (!g_SC4EffectsManager_Draw &&
                !Detour(addr::SC4EffectsManager_Draw, &TDwManager, &g_SC4EffectsManager_Draw))
                return false;
            g_tcForce = true;
            log::Write("water effects: the third effect class forced before the manager's draw");
        }
    }
    g_thirdMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the third effect class's letting go, giving back and values %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredThird = registry::Register({
    "renderer.effects.third",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectC_SetValue,
    "Three small functions of the third effect class, which no test city makes: letting go of its model, giving "
    "back a particle and setting a value by id; compare mode compares them only when forced, "
    "OPENSC4_EFFECT_FORCE_THIRD=1 making, setting, letting go, constructing and destroying effects of the class "
    "before the manager's draw (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallThird,
});

// --- The third class's description and model ---------------------------------------------
//
// The class taking a description (slot 0x28, 0x005AB190) and a model's
// instance (slot 0x2C, 0x005AAFE0), effects_third.h, with the model's
// load (0x005AB0F0), its materials (0x005AAE80), its batches (0x005AAD70)
// and the vector code under them, which only those two reach
// (renderer.effects.model_c). No test city makes an effect of the class,
// so in compare mode the two run as the game's and are compared only when
// forced (OPENSC4_EFFECT_FORCE_THIRD=1): before the manager's draw, one
// turn a frame of two, each on a pair of new effects of the class (made
// and deleted as TcNew and TcDelete make and delete them), the game's
// function on the first and ours on the second, step by step, the two
// compared after each step:
// 0. descriptions: two real ones (of effects of the first class in the
//    manager's table) with their fields changed for the step and put back
//    after (+0xF0 always 2.75), on effects given a made-up point at +0xB4:
//    without flag 1; with it and a model's ID (one of the second
//    class's descriptions has, the model loaded for real); the same again
//    (already held); the other one with ID 0 and +0x14 0 (the rate's
//    floor, the first let go); ID -1 and +0x14 a NaN; an ID no resource
//    has.
// 1. made-up instances (McInstance below), each with its materials and
//    box: three materials (one not drawn, one without a texture) with
//    +0x22C cleared, then five (the textures' and batches' vectors grow),
//    two (the batches taken out), five again (put in with room), the same
//    five again (already held), nine (the batches grow by as many as they
//    hold) and none (no materials: let go).
// The effects are compared by their bytes (but for the three vectors'
// pointers and the bytes in which the two differed when made), the
// contents and room of the vectors (each batch by its words and its two
// vectors' contents and room), and by what each run changed of the counts
// of the descriptions (+0x204), the textures (+0xC) and the made-up
// materials.
StepMode g_modelCMode = StepMode::Original;
Call1Fn g_SC4EffectC_SetDescription = nullptr, g_SC4EffectC_SetInstance = nullptr;

uint32_t __fastcall DescriptionCByMode(char* self, void*, uint32_t description)
{
    if (g_modelCMode != StepMode::Ours)
        return g_SC4EffectC_SetDescription(self, nullptr, description);
    SetEffectDescriptionC(self, Ptr(description));
    return 0;
}
OPENSC4_THUNK(TSC4EffectC_SetDescription, DescriptionCByMode, 4)

uint32_t __fastcall InstanceCByMode(char* self, void*, uint32_t instance)
{
    if (g_modelCMode != StepMode::Ours)
        return g_SC4EffectC_SetInstance(self, nullptr, instance);
    SetEffectInstanceC(self, Ptr(instance));
    return 0;
}
OPENSC4_THUNK(TSC4EffectC_SetInstance, InstanceCByMode, 4)

// The made-up instance: slot 0 answers its materials for 0x26A68448 (held
// once more), slot 0x20 fills the box unless it has none. Its materials:
// slots 4 and 8 count their holders, slot 0xC their number, slot 0x18 one
// of them (0x14 bytes: +0xB whether drawn, +0x10 its texture). A texture:
// its first word points at its name, +8 no owner, +0xC its count.
struct McTexture {
    uint32_t* name;
    uint32_t unused;
    uint32_t owner;
    int32_t count;
};
struct McMaterial {
    uint8_t bytes[0x10];
    McTexture* texture;
};
struct McMaterials {
    uint32_t table;
    int32_t held;
    uint32_t count;
    McMaterial* list;
};
struct McInstance {
    uint32_t table;
    McMaterials* materials;
    bool hasBox;
    float box[6];
};

uint32_t g_mcInstanceTable[16] = {}, g_mcMaterialsTable[16] = {};
uint32_t g_mcNames[9] = {};
McTexture g_mcTextures[9] = {};
McMaterial g_mcMaterial[9] = {};
McMaterials g_mcMaterials[4] = {};
McInstance g_mcInstances[5] = {};

uint32_t __fastcall McNothing(char*, void*)
{
    return 0;
}

uint32_t __fastcall McInstanceQuery(char* self, void*, uint32_t iid, uint32_t out)
{
    McMaterials* const materials = reinterpret_cast<McInstance*>(self)->materials;
    if (iid != 0x26A68448u || !materials)
        return 0;
    ++materials->held;
    *reinterpret_cast<McMaterials**>(Ptr(out)) = materials;
    return 1;
}

uint32_t __fastcall McInstanceBox(char* self, void*, uint32_t box)
{
    const McInstance& instance = *reinterpret_cast<McInstance*>(self);
    if (instance.hasBox)
        std::memcpy(Ptr(box), instance.box, sizeof instance.box);
    return 1;
}

uint32_t __fastcall McHold(char* self, void*)
{
    return static_cast<uint32_t>(++reinterpret_cast<McMaterials*>(self)->held);
}

uint32_t __fastcall McRelease(char* self, void*)
{
    return static_cast<uint32_t>(--reinterpret_cast<McMaterials*>(self)->held);
}

uint32_t __fastcall McCount(char* self, void*)
{
    return reinterpret_cast<McMaterials*>(self)->count;
}

uint32_t __fastcall McAt(char* self, void*, uint32_t index)
{
    return Word(&reinterpret_cast<McMaterials*>(self)->list[index]);
}

void McMakeFakes()
{
    if (g_mcInstanceTable[0])
        return;
    for (uint32_t i = 0; i < 16; ++i)
        g_mcInstanceTable[i] = g_mcMaterialsTable[i] = Word(reinterpret_cast<void*>(&McNothing));
    g_mcInstanceTable[0] = Word(reinterpret_cast<void*>(&McInstanceQuery));
    g_mcInstanceTable[0x20 / 4] = Word(reinterpret_cast<void*>(&McInstanceBox));
    g_mcMaterialsTable[1] = Word(reinterpret_cast<void*>(&McHold));
    g_mcMaterialsTable[2] = Word(reinterpret_cast<void*>(&McRelease));
    g_mcMaterialsTable[3] = Word(reinterpret_cast<void*>(&McCount));
    g_mcMaterialsTable[6] = Word(reinterpret_cast<void*>(&McAt));
    for (uint32_t i = 0; i < 9; ++i) {
        g_mcNames[i] = 0x11110000u + i * 0x0101u;
        g_mcTextures[i] = {&g_mcNames[i], 0, 0, 1000};
        g_mcMaterial[i].bytes[0xB] = 1;
        g_mcMaterial[i].texture = &g_mcTextures[i];
    }
    // Three: the second not drawn, the third without a texture (it is the
    // ninth's place in the list of nine, which has one).
    static McMaterial three[3];
    three[0] = g_mcMaterial[0];
    three[1] = g_mcMaterial[1];
    three[1].bytes[0xB] = 0;
    three[2] = g_mcMaterial[2];
    three[2].texture = nullptr;
    const std::pair<uint32_t, McMaterial*> lists[4] = {{3, three}, {5, g_mcMaterial + 1}, {2, g_mcMaterial + 4}, {9, g_mcMaterial}};
    for (uint32_t i = 0; i < 4; ++i)
        g_mcMaterials[i] = {Word(g_mcMaterialsTable), 1, lists[i].first, lists[i].second};
    const float boxes[5][6] = {{-3.5f, 0.25f, -1.0f, 4.0f, 12.75f, 2.5f},
                               {10.0f, 20.0f, 30.0f, 11.0f, 23.0f, 37.0f},
                               {-0.1f, -0.2f, -0.3f, 0.7f, 1e-7f, 0.3f},
                               {1e6f, 0.0f, -1e6f, 1e6f + 64.0f, 3.0f, -1e6f + 0.5f},
                               {}};
    McMaterials* const materials[5] = {&g_mcMaterials[0], &g_mcMaterials[1], &g_mcMaterials[2], nullptr,
                                       &g_mcMaterials[3]};
    for (uint32_t i = 0; i < 5; ++i) {
        g_mcInstances[i] = {Word(g_mcInstanceTable), materials[i], i != 4, {}};
        std::memcpy(g_mcInstances[i].box, boxes[i], sizeof boxes[i]);
    }
}

// What is compared of an effect, each value with a tag saying where it is
// from: 0x0xxx its bytes, 0x1xxxxx the words, 0x2xxxxx the textures,
// 0x3bbbxx the batches, 0x4xxxxx the counts' changes.
using McPrint = std::vector<std::pair<uint32_t, uint32_t>>;

void McVector(McPrint& print, uint32_t tag, char* vector, uint32_t size)
{
    char* const begin = At<char*>(vector, 0);
    const uint32_t used = static_cast<uint32_t>(At<char*>(vector, 4) - begin);
    print.push_back({tag, used / size});
    print.push_back({tag + 1, static_cast<uint32_t>(At<char*>(vector, 8) - begin) / size});
    if (size == 4) {
        for (uint32_t at = 0; at < used; at += 4)
            print.push_back({tag + 2 + at / 4, At<uint32_t>(begin, at)});
    } else {
        for (uint32_t at = 0; at < used; ++at)
            print.push_back({tag + 2 + at, At<uint8_t>(begin, at)});
    }
}

McPrint McPrintOf(char* effect, const std::vector<char>& mask)
{
    McPrint print;
    for (uint32_t at = 0; at < kEffectSizeC; at += 4) {
        if (at >= 0x230 && at < 0x254)
            continue;
        uint32_t word = 0, masked = 0;
        std::memcpy(&word, effect + at, 4);
        std::memcpy(&masked, mask.data() + at, 4);
        // A model's materials are made anew for each effect: only whether
        // there are some, unless they are made-up ones.
        if (at == 0x254 && word && !(word >= Word(g_mcMaterials) && word < Word(g_mcMaterials + 4)))
            word = 1;
        print.push_back({at, word & ~masked});
    }
    McVector(print, 0x100000, effect + 0x23C, 4);
    McVector(print, 0x200000, effect + 0x248, 4);
    char* const batches = effect + 0x230;
    print.push_back({0x300000, static_cast<uint32_t>(At<char*>(batches, 4) - At<char*>(batches, 0)) >> 6});
    print.push_back({0x300001, static_cast<uint32_t>(At<char*>(batches, 8) - At<char*>(batches, 0)) >> 6});
    uint32_t index = 1;
    for (char* batch = At<char*>(batches, 0); batch != At<char*>(batches, 4); batch += 0x40, ++index) {
        const uint32_t tag = 0x300000 + index * 0x10000;
        for (uint32_t at : {0x0u, 0x4u, 0x8u, 0xCu, 0x10u, 0x20u, 0x24u, 0x28u, 0x38u, 0x3Cu})
            print.push_back({tag + at, At<uint32_t>(batch, at)});
        McVector(print, tag + 0x1000, batch + 0x14, 1);
        McVector(print, tag + 0x8000, batch + 0x2C, 1);
    }
    return print;
}

std::vector<char*> g_mcSeen;

struct McPair {
    char* game;
    char* ours;
    std::vector<char> mask;
};

McPair McMakePair()
{
    McPair pair{TcNew(), TcNew(), std::vector<char>(kEffectSizeC)};
    for (std::size_t i = 0; i < kEffectSizeC; ++i)
        pair.mask[i] = pair.game[i] == pair.ours[i] ? 0 : static_cast<char>(0xFF);
    return pair;
}

// The counts a run may change: of the descriptions, the made-up textures
// and materials, the textures either effect holds before and those seen
// before. A real model's materials hold its textures too, so a texture
// first seen in the game's run is not compared then (its count before is
// not known); it is held from then on, so that it stays to be compared
// in the turns after.
void McAddCount(std::vector<int32_t*>& counts, int32_t* count)
{
    if (std::find(counts.begin(), counts.end(), count) == counts.end())
        counts.push_back(count);
}

void McAddTextures(std::vector<int32_t*>& counts, char* effect)
{
    for (char** it = At<char**>(effect, 0x248); it != At<char**>(effect, 0x24C); ++it) {
        if (*it)
            McAddCount(counts, reinterpret_cast<int32_t*>(*it + 0xC));
    }
}

std::vector<int32_t> McValues(const std::vector<int32_t*>& counts)
{
    std::vector<int32_t> values;
    for (int32_t* count : counts)
        values.push_back(*count);
    return values;
}

std::string McText(const McPrint& print, std::size_t at)
{
    char text[64];
    if (at < print.size())
        std::snprintf(text, sizeof text, "%zu values, at %06X %08X", print.size(), print[at].first, print[at].second);
    else
        std::snprintf(text, sizeof text, "%zu values", print.size());
    return text;
}

// One step: the game's on the first, ours on the second, compared.
template <typename Game, typename Ours>
void McStep(const char* id, McPair& pair, const std::vector<char*>& descriptions, Game&& game, Ours&& ours)
{
    std::vector<int32_t*> counts;
    for (char* description : descriptions)
        McAddCount(counts, reinterpret_cast<int32_t*>(description + 0x204));
    for (McTexture& texture : g_mcTextures)
        McAddCount(counts, &texture.count);
    for (McMaterials& materials : g_mcMaterials)
        McAddCount(counts, &materials.held);
    McAddTextures(counts, pair.game);
    McAddTextures(counts, pair.ours);
    for (char* texture : g_mcSeen)
        McAddCount(counts, reinterpret_cast<int32_t*>(texture + 0xC));
    const std::size_t known = counts.size();
    const std::vector<int32_t> before = McValues(counts);
    game(pair.game);
    const std::vector<int32_t> afterGame = McValues(counts);
    McPrint theirs = McPrintOf(pair.game, pair.mask);
    ours(pair.ours);
    McPrint mine = McPrintOf(pair.ours, pair.mask);
    for (std::size_t i = 0; i < known; ++i) {
        const uint32_t tag = 0x400000 + static_cast<uint32_t>(i);
        theirs.push_back({tag, static_cast<uint32_t>(afterGame[i] - before[i])});
        mine.push_back({tag, static_cast<uint32_t>(*counts[i] - afterGame[i])});
    }
    for (char** it = At<char**>(pair.game, 0x248); it != At<char**>(pair.game, 0x24C); ++it) {
        if (*it && std::find(counts.begin(), counts.end(), reinterpret_cast<int32_t*>(*it + 0xC)) == counts.end() &&
            std::find(g_mcSeen.begin(), g_mcSeen.end(), *it) == g_mcSeen.end()) {
            CallThis<uint32_t>(addr::SC4TextureBinding_AddRef, *it);
            g_mcSeen.push_back(*it);
        }
    }
    const bool same = theirs == mine;
    std::size_t at = 0;
    while (!same && at < theirs.size() && at < mine.size() && theirs[at] == mine[at])
        ++at;
    compare::Record(id, same, same ? std::string() : McText(theirs, at), same ? std::string() : McText(mine, at));
}

void McDescriptionStep(McPair& pair, char* description, const std::vector<char*>& held, uint32_t flag,
                       uint32_t which, const uint32_t* rate)
{
    uint8_t& flags = At<uint8_t>(description, 8);
    uint32_t& id = At<uint32_t>(description, 0xD0);
    uint32_t& rateWord = At<uint32_t>(description, 0x14);
    float& size = At<float>(description, 0xF0);
    const uint8_t oldFlags = flags;
    const uint32_t oldId = id, oldRate = rateWord;
    const float oldSize = size;
    flags = static_cast<uint8_t>((oldFlags & ~1u) | flag);
    id = which;
    if (rate)
        rateWord = *rate;
    size = 2.75f;
    McStep(flag ? "effects.description_c" : "effects.description_c.none", pair, held,
           [&](char* effect) { CallThis<uint32_t>(addr::SC4EffectC_SetDescription, effect, description); },
           [&](char* effect) { SetEffectDescriptionC(effect, description); });
    flags = oldFlags;
    id = oldId;
    rateWord = oldRate;
    size = oldSize;
}

uint32_t g_mcModel = 0;

void McDescriptions(char* manager)
{
    std::vector<char*> descriptions;
    for (char** it = At<char**>(manager, 0xBE8); it != At<char**>(manager, 0xBEC) && descriptions.size() < 2; ++it) {
        char* const description = *it ? At<char*>(*it, 0x24) : nullptr;
        if (description && std::find(descriptions.begin(), descriptions.end(), description) == descriptions.end())
            descriptions.push_back(description);
    }
    if (descriptions.empty())
        return;
    if (!g_mcModel) {
        for (char** it = At<char**>(manager, 0xC30); it != At<char**>(manager, 0xC34) && !g_mcModel; ++it) {
            char* const description = *it ? At<char*>(*it, 0x24) : nullptr;
            const uint32_t which = description ? At<uint32_t>(description, 0xD0) : 0;
            if (which && which != 0xFFFFFFFF) {
                g_mcModel = which;
                log::Write("water effects: the third class's forced descriptions load the model %08X", which);
            }
        }
    }
    McPair pair = McMakePair();
    // A made-up point at +0xB4, where a load takes +0x25C from.
    for (char* effect : {pair.game, pair.ours}) {
        At<float>(effect, 0xB4) = 1.5f;
        At<float>(effect, 0xB8) = -2.25f;
        At<float>(effect, 0xBC) = 40.0f;
    }
    char* const first = descriptions[0];
    char* const second = descriptions.size() > 1 ? descriptions[1] : nullptr;
    const uint32_t zero = 0, nan = 0x7FC00000;
    McDescriptionStep(pair, first, descriptions, 0, g_mcModel, nullptr);
    McDescriptionStep(pair, first, descriptions, 1, g_mcModel, nullptr);
    static bool told = false;
    if (!told) {
        told = true;
        log::Write("water effects: the third class's forced model: materials %s, %d textures, %d batches",
                   At<char*>(pair.game, 0x254) ? "kept" : "none",
                   static_cast<int>(At<char**>(pair.game, 0x24C) - At<char**>(pair.game, 0x248)),
                   static_cast<int>((At<char*>(pair.game, 0x234) - At<char*>(pair.game, 0x230)) >> 6));
    }
    McDescriptionStep(pair, first, descriptions, 1, g_mcModel, nullptr);
    if (second)
        McDescriptionStep(pair, second, descriptions, 1, 0, &zero);
    McDescriptionStep(pair, first, descriptions, 1, 0xFFFFFFFF, &nan);
    McDescriptionStep(pair, first, descriptions, 1, 0x0BADF00D, nullptr);
    TcDelete(pair.game);
    TcDelete(pair.ours);
}

void McInstances()
{
    McMakeFakes();
    McPair pair = McMakePair();
    const auto step = [&](uint32_t which) {
        char* const instance = reinterpret_cast<char*>(&g_mcInstances[which]);
        McStep("effects.instance_c", pair, {},
               [&](char* effect) { CallThis<uint32_t>(addr::SC4EffectC_SetInstance, effect, instance); },
               [&](char* effect) { SetEffectInstanceC(effect, instance); });
    };
    At<uint8_t>(pair.game, 0x22C) = 0;
    At<uint8_t>(pair.ours, 0x22C) = 0;
    step(0);
    At<uint8_t>(pair.game, 0x22C) = 1;
    At<uint8_t>(pair.ours, 0x22C) = 1;
    for (const uint32_t which : {1u, 2u, 1u, 1u, 4u, 3u})
        step(which);
    TcDelete(pair.game);
    TcDelete(pair.ours);
}

bool g_mcForce = false;

void McForce(char* self)
{
    if (!g_mcForce || g_dwTrace != DwTrace::Off || g_depth > 0)
        return;
    static uint32_t turn = 0;
    if (turn++ % 2 == 0)
        McDescriptions(self - 4);
    else
        McInstances();
}

bool InstallModelC(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(Detour(addr::SC4EffectC_SetDescription, &TSC4EffectC_SetDescription, &g_SC4EffectC_SetDescription) &&
          Detour(addr::SC4EffectC_SetInstance, &TSC4EffectC_SetInstance, &g_SC4EffectC_SetInstance)))
        return false;
    if (mode == registry::Mode::Compare) {
        const char* v = std::getenv("OPENSC4_EFFECT_FORCE_THIRD");
        if (v && *v == '1') {
            if (!g_SC4EffectsManager_Draw &&
                !Detour(addr::SC4EffectsManager_Draw, &TDwManager, &g_SC4EffectsManager_Draw))
                return false;
            g_mcForce = true;
            log::Write("water effects: the third class's description and model forced before the manager's draw");
        }
    }
    g_modelCMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the third class's description and model %s",
               mode == registry::Mode::Compare ? "compared with the game's when forced" : "ours");
    return true;
}

const bool g_registeredModelC = registry::Register({
    "renderer.effects.model_c",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectC_SetDescription,
    "The third effect class taking a description and a model's instance: its model loaded, its materials' "
    "textures held and a batch made for each; no test city makes one, so compare mode compares them only when "
    "forced, OPENSC4_EFFECT_FORCE_THIRD=1 running the game's and ours on pairs of new effects of the class before "
    "the manager's draw (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallModelC,
});

// --- The third class's pieces ------------------------------------------------------------------
//
// The class's model cut into pieces (effects_third.h): a quad by a grid
// (0x005A9150), a triangle halved until small (0x005A9FF0, recursive), one
// piece of a triangle (0x005A9820) and the particle made for each piece
// (0x005A8930), with the pieces' vector code under them
// (renderer.effects.pieces_c). The class's emit (0x005AA5D0, still the
// game's) is their only caller and no test city makes an effect of the
// class, so in compare mode they run as the game's and are compared only
// when forced (OPENSC4_EFFECT_FORCE_THIRD=1): before the manager's draw,
// one turn a frame of three, on a pair of new effects of the class (as
// McMakePair makes them), the game's function on the first and ours on the
// second, step by step. Both effects are given the same made-up matrix,
// place and scale, +0x1C8 and description (+0x24; four kinds by turn: a
// push from its point and a spread; no push, no spread and no direction;
// a push and a NaN direction; a push with each direction's range one
// value),
// and a copy of the sub-object's table of functions whose slot 0x2C notes
// the particles added instead of keeping them. The generator (0x00B22DF8)
// is put back before ours, so that both draw the same numbers, and after.
// The steps:
// 0. particles made straight away at four places, one on the
//    description's point and one within a hair of it (their +0x44, which
//    the callers set, made zero);
// 1. grids of 2, 1, 0 and 3 pieces a side on two made-up quads;
// 2. pieces of four triangles of made-up points;
// 3. four triangles halved down to a side of 3 (one with its first two
//    sides the same) with the points' vectors full or with room;
// 4. the pieces' vector code on its own (0x005A8D90 putting 3, 1 and 0
//    pieces in the middle, 0x005A6480 and 0x005A63F0) on made-up vectors.
// Each step is compared by the effect's bytes (but for the pieces'
// vector's pointers and the bytes in which the two differed when made),
// the pieces' vector's contents and room, the particles added (each one's
// 0x48 bytes but +0x34, which nothing writes: a new pool's particles have
// whatever malloc left there) and the generator after; the halving also by its two vectors. In
// one turn of eight (the first eight times), the list of free particles
// (0x00B0A5A0) is empty for both runs, so that its pool grows; what the
// two left of it is put back on the list after.
StepMode g_piecesCMode = StepMode::Original;
using PcCall4Fn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t, uint32_t);
using PcCall5Fn = uint32_t(__fastcall*)(char*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
PcCall4Fn g_SC4EffectC_CutGrid = nullptr, g_SC4EffectC_CutTriangle = nullptr;
PcCall5Fn g_SC4EffectC_SplitTriangle = nullptr;
Call1Fn g_SC4EffectC_MakeParticle = nullptr;

const float* PcFloats(uint32_t value)
{
    return reinterpret_cast<const float*>(Ptr(value));
}

uint32_t __fastcall GridCByMode(char* self, void*, uint32_t quad, uint32_t places, uint32_t count, uint32_t which)
{
    if (g_piecesCMode != StepMode::Ours)
        return g_SC4EffectC_CutGrid(self, nullptr, quad, places, count, which);
    CutGridC(self, PcFloats(quad), PcFloats(places), count, static_cast<uint8_t>(which));
    return 0;
}
OPENSC4_THUNK(TSC4EffectC_CutGrid, GridCByMode, 16)

uint32_t __fastcall TriangleCByMode(char* self, void*, uint32_t points, uint32_t places, uint32_t corner,
                                    uint32_t which)
{
    if (g_piecesCMode != StepMode::Ours)
        return g_SC4EffectC_CutTriangle(self, nullptr, points, places, corner, which);
    CutTriangleC(self, PcFloats(points), PcFloats(places), reinterpret_cast<const int32_t*>(Ptr(corner)),
                 static_cast<uint8_t>(which));
    return 0;
}
OPENSC4_THUNK(TSC4EffectC_CutTriangle, TriangleCByMode, 16)

uint32_t __fastcall SplitCByMode(char* self, void*, uint32_t points, uint32_t places, uint32_t corner, uint32_t most,
                                 uint32_t which)
{
    if (g_piecesCMode != StepMode::Ours)
        return g_SC4EffectC_SplitTriangle(self, nullptr, points, places, corner, most, which);
    float limit = 0.0f;
    std::memcpy(&limit, &most, 4);
    SplitTriangleC(self, Ptr(points), Ptr(places), reinterpret_cast<const int32_t*>(Ptr(corner)), limit, which);
    return 0;
}
OPENSC4_THUNK(TSC4EffectC_SplitTriangle, SplitCByMode, 20)

uint32_t __fastcall ParticleCByMode(char* self, void*, uint32_t at)
{
    if (g_piecesCMode != StepMode::Ours)
        return g_SC4EffectC_MakeParticle(self, nullptr, at);
    return Word(MakeParticleC(self, PcFloats(at)));
}
OPENSC4_THUNK(TSC4EffectC_MakeParticle, ParticleCByMode, 4)

bool g_pcForce = false;
std::vector<uint32_t> g_pcAdded;
uint32_t g_pcSubTable[18] = {};
alignas(4) char g_pcDescription[0x210] = {};

uint32_t __fastcall PcNoteAdded(char*, void*, uint32_t particle)
{
    g_pcAdded.push_back(particle);
    return static_cast<uint32_t>(g_pcAdded.size());
}

void PcAddParticles(McPrint& print, uint32_t tag, const std::vector<uint32_t>& particles)
{
    const uint32_t size = Global<uint32_t>(kParticlePoolC);
    print.push_back({tag, static_cast<uint32_t>(particles.size())});
    for (std::size_t i = 0; i < particles.size(); ++i) {
        for (uint32_t at = 0; at < size; at += 4) {
            const uint32_t word = M<uint32_t>(particles[i] + at);
            print.push_back({tag + 0x100 + static_cast<uint32_t>(i) * 0x100 + at, at == 0x34 ? 0 : word});
        }
    }
}

McPrint PcPrintOf(char* effect, const std::vector<char>& mask, const std::vector<uint32_t>& particles)
{
    McPrint print;
    for (uint32_t at = 0; at < kEffectSizeC; at += 4) {
        if (at >= 0x220 && at < 0x22C)
            continue;
        uint32_t word = 0, masked = 0;
        std::memcpy(&word, effect + at, 4);
        std::memcpy(&masked, mask.data() + at, 4);
        print.push_back({at, word & ~masked});
    }
    McVector(print, 0x100000, effect + 0x220, 4);
    PcAddParticles(print, 0x500000, particles);
    for (uint32_t at = 0; at < kSetupGeneratorSize; at += 4)
        print.push_back({0x700000 + at, M<uint32_t>(static_cast<uint32_t>(kSetupGenerator) + at)});
    return print;
}

uint32_t PcSplice(uint32_t first, uint32_t rest)
{
    if (!first)
        return rest;
    uint32_t last = first;
    while (M<uint32_t>(last))
        last = M<uint32_t>(last);
    M<uint32_t>(last) = rest;
    return first;
}

uint32_t g_pcEmptied = 0;

// One step: the game's on the first, ours on the second, compared;
// `extra` adds what else each run left (by the effect it ran on).
template <typename Game, typename Ours, typename Extra>
void PcStep(const char* id, McPair& pair, bool empty, Game&& game, Ours&& ours, Extra&& extra)
{
    char* const generator = reinterpret_cast<char*>(kSetupGenerator);
    char before[kSetupGeneratorSize];
    std::memcpy(before, generator, kSetupGeneratorSize);
    const uint32_t head = Global<uint32_t>(kParticleFreeC);
    if (empty)
        Global<uint32_t>(kParticleFreeC) = 0;
    g_pcAdded.clear();
    game(pair.game);
    const std::vector<uint32_t> theirsAdded = g_pcAdded;
    McPrint theirs = PcPrintOf(pair.game, pair.mask, theirsAdded);
    extra(pair.game, theirs);
    const uint32_t gameLeft = Global<uint32_t>(kParticleFreeC);
    std::memcpy(generator, before, kSetupGeneratorSize);
    if (empty)
        Global<uint32_t>(kParticleFreeC) = 0;
    g_pcAdded.clear();
    ours(pair.ours);
    const std::vector<uint32_t> mineAdded = g_pcAdded;
    McPrint mine = PcPrintOf(pair.ours, pair.mask, mineAdded);
    extra(pair.ours, mine);
    std::memcpy(generator, before, kSetupGeneratorSize);
    if (empty)
        Global<uint32_t>(kParticleFreeC) = PcSplice(Global<uint32_t>(kParticleFreeC), PcSplice(gameLeft, head));
    for (const uint32_t particle : theirsAdded)
        GiveBackParticleC(particle);
    for (const uint32_t particle : mineAdded)
        GiveBackParticleC(particle);
    g_pcAdded.clear();
    const bool same = theirs == mine;
    std::size_t at = 0;
    while (!same && at < theirs.size() && at < mine.size() && theirs[at] == mine[at])
        ++at;
    compare::Record(id, same, same ? std::string() : McText(theirs, at), same ? std::string() : McText(mine, at));
}

template <typename Game, typename Ours>
void PcStep(const char* id, McPair& pair, bool empty, Game&& game, Ours&& ours)
{
    PcStep(id, pair, empty, game, ours, [](char*, McPrint&) {});
}

// The made-up description, by turn.
void PcDescribe(uint32_t kind)
{
    std::memset(g_pcDescription, 0, sizeof g_pcDescription);
    const auto set = [](uint32_t at, float value) { std::memcpy(g_pcDescription + at, &value, 4); };
    set(0xC, 1.5f);
    set(0x10, 4.0f);
    const float low[3] = {-1.0f, 0.5f, -2.0f}, high[3] = {1.0f, 3.0f, 2.5f};
    for (uint32_t i = 0; i < 3; ++i) {
        set(0x30 + i * 4, low[i]);
        set(0x3C + i * 4, kind == 3 ? low[i] : high[i]);
    }
    if (kind == 1) {
        for (uint32_t at = 0x30; at < 0x48; at += 4)
            set(at, 0.0f);
    }
    if (kind == 2) {
        const uint32_t nan = 0x7FC00000;
        std::memcpy(g_pcDescription + 0x34, &nan, 4);
    }
    set(0x48, 2.0f);
    set(0x4C, 7.5f);
    set(0x68, 0.25f);
    set(0x6C, 0.125f);
    set(0x70, kind == 1 ? 0.0f : 0.3f);
    set(0xF0, kind == 1 ? 0.0f : 2.5f);
    set(0xF4, 101.0f);
    set(0xF8, 21.5f);
    set(0xFC, -299.0f);
}

void PcSetUp(char* effect, uint32_t kind)
{
    const float frame[13] = {0.8137f, -0.5812f, 0.1123f, 0.5791f, 0.8061f, -0.0517f, 0.0213f, 0.0731f, 0.9907f,
                             100.37f, 20.29f, -300.113f, kind % 2 ? 1.0f : 1.7381f};
    std::memcpy(effect + 0x90, frame, sizeof frame);
    At<float>(effect, 0x1C8) = 1.25f;
    At<char*>(effect, 0x24) = g_pcDescription;
    char* const sub = effect + 4;
    if (!g_pcSubTable[0]) {
        for (uint32_t i = 0; i < 18; ++i)
            g_pcSubTable[i] = M<uint32_t>(At<uint32_t>(sub, 0) + i * 4);
        g_pcSubTable[0x2C / 4] = Word(reinterpret_cast<void*>(&PcNoteAdded));
    }
    At<uint32_t*>(sub, 0) = g_pcSubTable;
}

void PcPutBack(char* effect, uint32_t table)
{
    At<char*>(effect, 0x24) = nullptr;
    At<uint32_t>(effect + 4, 0) = table;
}

// A vector of `size` things of `bytes` bytes with room for `room` more,
// from the game's allocator, filled from `from`.
void PcMakeVector(char* vector, const void* from, uint32_t size, uint32_t room, uint32_t bytes)
{
    if (!(size + room)) {
        At<char*>(vector, 0) = At<char*>(vector, 4) = At<char*>(vector, 8) = nullptr;
        return;
    }
    char* const storage = StlAllocate((size + room) * bytes);
    std::memcpy(storage, from, size * bytes);
    At<char*>(vector, 0) = storage;
    At<char*>(vector, 4) = storage + size * bytes;
    At<char*>(vector, 8) = storage + (size + room) * bytes;
}

void PcFreeVector(char* vector)
{
    if (At<char*>(vector, 0))
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(At<char*>(vector, 0));
    At<char*>(vector, 0) = At<char*>(vector, 4) = At<char*>(vector, 8) = nullptr;
}

// The pieces' vector code on its own: pieces numbered by their words.
void PcVectors()
{
    float made[8][15];
    for (uint32_t i = 0; i < 8; ++i) {
        for (uint32_t j = 0; j < 15; ++j)
            made[i][j] = static_cast<float>(i * 100 + j) + 0.5f;
    }
    float value[15];
    for (uint32_t j = 0; j < 15; ++j)
        value[j] = -1.0f - static_cast<float>(j);
    struct Grow {
        uint32_t size, room, where, count;
        bool atEnd;
    };
    const Grow grows[] = {{5, 0, 2, 3, false}, {5, 2, 1, 1, false}, {4, 0, 4, 1, true}, {3, 1, 1, 0, false},
                          {0, 0, 0, 2, false}};
    for (const Grow& g : grows) {
        alignas(4) char theirs[12] = {}, mine[12] = {};
        PcMakeVector(theirs, made, g.size, g.room, kPieceBytes);
        PcMakeVector(mine, made, g.size, g.room, kPieceBytes);
        uint8_t tag = 0;
        CallThis<uint32_t>(0x005A8D90, theirs, At<char*>(theirs, 0) + g.where * kPieceBytes, value, &tag, g.count,
                           static_cast<uint32_t>(g.atEnd));
        PcGrowPieces(mine, At<char*>(mine, 0) + g.where * kPieceBytes, reinterpret_cast<char*>(value), g.count,
                     g.atEnd);
        McPrint a, b;
        McVector(a, 0x100000, theirs, 4);
        McVector(b, 0x100000, mine, 4);
        compare::Record("effects.pieces_c.grow", a == b, a == b ? std::string() : McText(a, 0),
                        a == b ? std::string() : McText(b, 0));
        PcFreeVector(theirs);
        PcFreeVector(mine);
    }
    // Assigned down over the first two (0x005A6480), none, and copied
    // into fresh storage and to a null place (0x005A63F0).
    const std::pair<uint32_t, uint32_t> ranges[] = {{2, 8}, {3, 3}, {5, 2}};
    for (const auto& [first, last] : ranges) {
        alignas(4) float theirs[8][15], mine[8][15];
        std::memcpy(theirs, made, sizeof made);
        std::memcpy(mine, made, sizeof made);
        char* const t = reinterpret_cast<char*>(theirs);
        char* const m = reinterpret_cast<char*>(mine);
        const uint32_t endT = reinterpret_cast<uint32_t(__cdecl*)(char*, char*, char*)>(0x005A6480)(
            t + first * kPieceBytes, t + last * kPieceBytes, t);
        char* const endM = PcAssignPieces(m + first * kPieceBytes, m + last * kPieceBytes, m);
        const bool same = std::memcmp(theirs, mine, sizeof theirs) == 0 && endT - Word(t) == Word(endM) - Word(m);
        compare::Record("effects.pieces_c.assign", same, same ? std::string() : Hex(t, 8),
                        same ? std::string() : Hex(m, 8));
    }
    for (const bool null : {false, true}) {
        alignas(4) float theirs[3][15] = {}, mine[3][15] = {};
        char* const t = null ? nullptr : reinterpret_cast<char*>(theirs);
        char* const m = null ? nullptr : reinterpret_cast<char*>(mine);
        char* const from = reinterpret_cast<char*>(made);
        // Only the place itself is tested: a null start with one piece.
        const uint32_t pieces = null ? 1 : 3;
        const uint32_t endT = reinterpret_cast<uint32_t(__cdecl*)(char*, char*, char*)>(0x005A63F0)(
            from, from + pieces * kPieceBytes, t);
        char* const endM = PcCopyPieces(from, from + pieces * kPieceBytes, m);
        const bool same = std::memcmp(theirs, mine, sizeof theirs) == 0 && endT - Word(t) == Word(endM) - Word(m);
        compare::Record("effects.pieces_c.copy", same, same ? std::string() : Hex(theirs, 8),
                        same ? std::string() : Hex(mine, 8));
    }
}

void PcPieces(uint32_t turn)
{
    const uint32_t kind = turn % 4;
    const bool empty = turn % 8 == 5 && g_pcEmptied < 8;
    if (empty)
        ++g_pcEmptied;
    PcDescribe(kind);
    McPair pair = McMakePair();
    const uint32_t table = At<uint32_t>(pair.game + 4, 0);
    PcSetUp(pair.game, kind);
    PcSetUp(pair.ours, kind);
    // 0. Particles made straight away.
    const float places[4][3] = {
        {101.0f, 21.5f, -299.0f}, {101.00001f, 21.5f, -299.0f}, {-40.0f, 3.0f, 12.0f}, {101.5f, 20.0f, -298.0f}};
    for (const auto& place : places) {
        PcStep("effects.particle_c", pair, empty,
               [&](char* effect) {
                   g_pcAdded.push_back(CallThis<uint32_t>(addr::SC4EffectC_MakeParticle, effect, place));
                   M<uint32_t>(g_pcAdded.back() + 0x44) = 0;
               },
               [&](char* effect) {
                   g_pcAdded.push_back(Word(MakeParticleC(effect, place)));
                   M<uint32_t>(g_pcAdded.back() + 0x44) = 0;
               });
    }
    // 1. Grids.
    const float quads[2][12] = {{0.0f, 4.0f, 1.0f, 3.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.25f, 0.0f, 2.75f},
                                {-5.53f, 1.07f, 2.11f, 7.13f, -1.29f, 3.01f, 1.17f, 2.03f, -3.07f, 0.1f, 6.31f, 9.19f}};
    const float texture[2][8] = {{0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
                                 {0.9f, 0.1f, 0.7531f, 0.2519f, 0.1237f, 0.5113f, 0.3071f, 0.9533f}};
    const std::pair<uint32_t, uint32_t> grids[] = {{0, 2}, {1, 1}, {0, 0}, {1, 3}};
    for (const auto& [which, count] : grids) {
        const uint32_t material = which ? 200 : 3;
        PcStep("effects.grid_c", pair, empty,
               [&](char* effect) {
                   CallThis<uint32_t>(addr::SC4EffectC_CutGrid, effect, quads[which], texture[which], count, material);
               },
               [&](char* effect) {
                   CutGridC(effect, quads[which], texture[which], count, static_cast<uint8_t>(material));
               });
    }
    // 2. Triangles.
    // The last two make, with the first, a triangle whose first two sides
    // are both 5 (a tie).
    const float points[8][3] = {{0.0f, 0.0f, 0.0f},      {4.0f, 0.0f, 0.0f},   {0.0f, 4.0f, 0.0f},
                                {1.53f, -2.07f, 7.29f}, {-3.11f, 9.03f, 0.51f}, {12.07f, 0.131f, -6.03f},
                                {3.0f, 4.0f, 0.0f},      {3.0f, -1.0f, 0.0f}};
    const float uvs[8][2] = {{0.0f, 0.0f},     {1.0f, 0.0f},   {0.0f, 1.0f},  {0.5131f, 0.4973f},
                             {0.2017f, 0.9031f}, {0.7519f, 0.33f}, {0.6f, 0.8f}, {0.6f, 0.2f}};
    const int32_t corners[4][3] = {{0, 1, 2}, {3, 4, 5}, {5, 0, 4}, {0, 6, 7}};
    for (const auto& corner : corners) {
        PcStep("effects.triangle_c", pair, empty,
               [&](char* effect) {
                   CallThis<uint32_t>(addr::SC4EffectC_CutTriangle, effect, points, uvs, corner, 7u);
               },
               [&](char* effect) { CutTriangleC(effect, &points[0][0], &uvs[0][0], corner, 7); });
    }
    // 3. Triangles halved: the vectors made for each run, kept by effect.
    struct Vectors {
        alignas(4) char points[12];
        alignas(4) char places[12];
    };
    const std::pair<const int32_t*, uint32_t> splits[] = {{corners[0], 0}, {corners[1], 4}, {corners[2], 1}, {corners[3], 2}};
    for (const auto& [corner, room] : splits) {
        Vectors theirs{}, mine{};
        for (Vectors* v : {&theirs, &mine}) {
            PcMakeVector(v->points, points, 8, room, 12);
            PcMakeVector(v->places, uvs, 8, room, 8);
        }
        const float most = 3.0f;
        uint32_t mostBits = 0;
        std::memcpy(&mostBits, &most, 4);
        PcStep("effects.split_c", pair, empty,
               [&](char* effect) {
                   CallThis<uint32_t>(addr::SC4EffectC_SplitTriangle, effect, theirs.points, theirs.places, corner,
                                      mostBits, 9u);
               },
               [&](char* effect) { SplitTriangleC(effect, mine.points, mine.places, corner, most, 9); },
               [&](char* effect, McPrint& print) {
                   Vectors& v = effect == pair.game ? theirs : mine;
                   McVector(print, 0x200000, v.points, 4);
                   McVector(print, 0x300000, v.places, 4);
               });
        for (Vectors* v : {&theirs, &mine}) {
            PcFreeVector(v->points);
            PcFreeVector(v->places);
        }
    }
    PcPutBack(pair.game, table);
    PcPutBack(pair.ours, table);
    TcDelete(pair.game);
    TcDelete(pair.ours);
    // 4. The vector code on its own.
    PcVectors();
}

void PcForce(char*)
{
    if (!g_pcForce || g_dwTrace != DwTrace::Off || g_depth > 0)
        return;
    static uint32_t turn = 0;
    const uint32_t now = turn++;
    if (now % 3 == 0)
        PcPieces(now / 3);
}

bool InstallPiecesC(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(Detour(addr::SC4EffectC_CutGrid, &TSC4EffectC_CutGrid, &g_SC4EffectC_CutGrid) &&
          Detour(addr::SC4EffectC_CutTriangle, &TSC4EffectC_CutTriangle, &g_SC4EffectC_CutTriangle) &&
          Detour(addr::SC4EffectC_SplitTriangle, &TSC4EffectC_SplitTriangle, &g_SC4EffectC_SplitTriangle) &&
          Detour(addr::SC4EffectC_MakeParticle, &TSC4EffectC_MakeParticle, &g_SC4EffectC_MakeParticle)))
        return false;
    if (mode == registry::Mode::Compare) {
        const char* v = std::getenv("OPENSC4_EFFECT_FORCE_THIRD");
        if (v && *v == '1') {
            if (!g_SC4EffectsManager_Draw &&
                !Detour(addr::SC4EffectsManager_Draw, &TDwManager, &g_SC4EffectsManager_Draw))
                return false;
            g_pcForce = true;
            log::Write("water effects: the third class's pieces forced before the manager's draw");
        }
    }
    g_piecesCMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the third class's pieces %s",
               mode == registry::Mode::Compare ? "compared with the game's when forced" : "ours");
    return true;
}

const bool g_registeredPiecesC = registry::Register({
    "renderer.effects.pieces_c",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectC_CutGrid,
    "The third effect class's model cut into pieces: a quad by a grid, a triangle halved until small, one piece of "
    "a triangle and the particle made for each piece, with the pieces' vector code; no test city makes such an "
    "effect, so compare mode compares them only when forced, OPENSC4_EFFECT_FORCE_THIRD=1 running the game's and "
    "ours on pairs of new effects of the class before the manager's draw (docs/renderer.md, \"Water effects, sky "
    "and clouds\")",
    true,
    InstallPiecesC,
});

// --- The third class's emit and step ---------------------------------------------------------------
//
// The class's emit (0x005AA5D0) with the two ways it blows the model apart
// (0x005AA3A0 by triangles, 0x005A9E10 by quads), its particles' step
// (0x005A6520) and value 9's 0x005AA700, effects_third.h
// (renderer.effects.emit_c). No test city makes an effect of the class, so
// in compare mode they run as the game's and are compared only when forced
// (OPENSC4_EFFECT_FORCE_THIRD=1): before the manager's draw, one turn a
// frame of three, on a pair of new effects of the class set up as for the
// pieces (PcSetUp: the same matrix, place, scale and made-up description,
// with the step's and the emit's fields added by turn; the sub-object's
// slot 0x2C noting the particles; the generator put back), a second matrix
// at +0xC8, a point at +0x25C and the same materials at +0x254 (held once
// for each), by turn: a real model's (the one the descriptions' forcing
// loads, McDescriptions, kept held), or two sets made of its first mesh:
// for triangles three materials (the real mesh drawn, not drawn, and with
// its primitives twice), for quads two meshes with the vertices cut to a
// multiple of four (with the real primitives and with the first twice).
// The steps, the game's on the first and ours on the second, compared as
// the pieces are (PcStep: the effect's bytes, its pieces, the particles
// made, the generator after):
// 0. the emit: the first time (+0x22C clear: triangles; set: quads), again
//    (the front grown: last frame's kept; the clock moves), waiting (+0x190 above the step), done waiting (the
//    first time again, with the step left over), the clock reaching one,
//    with +0x188 set and with no materials (each told it is done, +9);
// 1. the step on a made-up particle of each effect (compared by its 0x48
//    bytes and the answer too): the front grown (at a fraction of the
//    clock, a whole clock, near its end), not grown, a particle not reached,
//    one reached while new, one ending, one on the point, one within a hair
//    of it, one far behind (both fall-offs at zero); the curve of one value or four by turn, and
//    in one turn of four a front speed of zero (the base class's step);
// 2. value 9's point along two directions, with +0x278 zero and not, and
//    with no materials; then value 9 asked of the first through the class's
//    table (SC4EffectC::SetValue, so that renderer.effects.third, when
//    compared, sees its call).
// Not reached: the cut of value 9's vector (it only grows) and an emit
// with materials whose mesh has no primitives.
StepMode g_emitCMode = StepMode::Original;
Call2Fn g_SC4EffectC_StepParticle = nullptr, g_SC4EffectC_SetPointAlong = nullptr;
Call0Fn g_SC4EffectC_Emit = nullptr, g_SC4EffectC_CutTriangles = nullptr, g_SC4EffectC_CutQuads = nullptr;

uint32_t __fastcall StepCByMode(char* self, void*, uint32_t particle, uint32_t step)
{
    if (g_emitCMode != StepMode::Ours)
        return g_SC4EffectC_StepParticle(self, nullptr, particle, step);
    float s = 0.0f;
    std::memcpy(&s, &step, 4);
    return StepParticleC(self, Ptr(particle), s);
}
OPENSC4_THUNK(TSC4EffectC_StepParticle, StepCByMode, 8)

uint32_t __fastcall EmitCByMode(char* self, void*)
{
    if (g_emitCMode != StepMode::Ours)
        return g_SC4EffectC_Emit(self, nullptr);
    EmitEffectC(self);
    return 0;
}
OPENSC4_THUNK(TSC4EffectC_Emit, EmitCByMode, 0)

uint32_t __fastcall TrianglesCByMode(char* self, void*)
{
    if (g_emitCMode != StepMode::Ours)
        return g_SC4EffectC_CutTriangles(self, nullptr);
    CutTrianglesC(self);
    return 0;
}
OPENSC4_THUNK(TSC4EffectC_CutTriangles, TrianglesCByMode, 0)

uint32_t __fastcall QuadsCByMode(char* self, void*)
{
    if (g_emitCMode != StepMode::Ours)
        return g_SC4EffectC_CutQuads(self, nullptr);
    CutQuadsC(self);
    return 0;
}
OPENSC4_THUNK(TSC4EffectC_CutQuads, QuadsCByMode, 0)

uint32_t __fastcall PointCByMode(char* self, void*, uint32_t at, uint32_t along)
{
    if (g_emitCMode != StepMode::Ours)
        return g_SC4EffectC_SetPointAlong(self, nullptr, at, along);
    SetPointAlongC(self, PcFloats(at), PcFloats(along));
    return 0;
}
OPENSC4_THUNK(TSC4EffectC_SetPointAlong, PointCByMode, 8)

// The made-up materials: slots 4 and 8 count their holders, slot 0xC their
// number, slot 0x10 a mesh, slot 0x18 a material.
struct EcList {
    uint32_t first;
    char* begin;
    char* end;
    char* room;
};
struct EcMesh {
    char* vertices;
    uint32_t indices;
    EcList* list;
};
struct EcMaterials {
    uint32_t table;
    int32_t held;
    uint32_t count;
    McMaterial* materials[3];
    char* meshes[3];
};

uint32_t __fastcall EcMeshAt(char* self, void*, uint32_t index)
{
    return Word(reinterpret_cast<EcMaterials*>(self)->meshes[index]);
}

uint32_t __fastcall EcMaterialAt(char* self, void*, uint32_t index)
{
    return Word(reinterpret_cast<EcMaterials*>(self)->materials[index]);
}

bool g_ecForce = false;
float g_ecScale = 1.0f;
float g_ecCurve[4] = {1.0f, 2.5f, 0.5f, 3.0f};
char* g_ecReal = nullptr;
bool g_ecTried = false;
uint32_t g_ecTable[16] = {};
McMaterial g_ecMaterial[2] = {};
uint32_t g_ecEntries[6] = {};
EcList g_ecList = {};
alignas(4) char g_ecVertices[0x10] = {};
EcMesh g_ecMeshes[3] = {};
EcMaterials g_ecFakes[2] = {};

// The model the descriptions' forcing loads (one a description of the
// second class names).
uint32_t McFindModel(char* manager)
{
    for (char** it = At<char**>(manager, 0xC30); it != At<char**>(manager, 0xC34) && !g_mcModel; ++it) {
        char* const description = *it ? At<char*>(*it, 0x24) : nullptr;
        const uint32_t which = description ? At<uint32_t>(description, 0xD0) : 0;
        if (which && which != 0xFFFFFFFF)
            g_mcModel = which;
    }
    return g_mcModel;
}

// The real materials, loaded once on an effect of the class made for it
// (SC4EffectC::LoadModel) and held for good; the made-up sets made of
// their first mesh.
void EcLoad(char* manager)
{
    if (g_ecTried || !McFindModel(manager))
        return;
    g_ecTried = true;
    char* const scratch = TcNew();
    CallThis<uint32_t>(addr::SC4EffectC_LoadModel, scratch, g_mcModel);
    char* const real = At<char*>(scratch, 0x254);
    if (real)
        SuSlot<uint32_t>(real, 4);
    TcDelete(scratch);
    if (!real)
        return;
    g_ecReal = real;
    const uint32_t count = SuSlot<uint32_t>(real, 0xC);
    char* const mesh = count ? reinterpret_cast<char*>(SuSlot<uint32_t>(real, 0x10, 0u)) : nullptr;
    char* const vertices = mesh ? At<char*>(mesh, 0) : nullptr;
    const uint32_t n = vertices ? At<uint16_t>(vertices, 6) : 0;
    const uint32_t primitives = mesh ? EcPrimitives(mesh) : 0;
    log::Write("water effects: the third class's forced emit: model %08X, %u materials, the first's mesh %u vertices, "
               "%u primitives",
               g_mcModel, count, n, primitives);
    if (n < 4 || !primitives)
        return;
    for (uint32_t& slot : g_ecTable)
        slot = Word(reinterpret_cast<void*>(&McNothing));
    g_ecTable[1] = Word(reinterpret_cast<void*>(&McHold));
    g_ecTable[2] = Word(reinterpret_cast<void*>(&McRelease));
    g_ecTable[3] = Word(reinterpret_cast<void*>(&McCount));
    g_ecTable[4] = Word(reinterpret_cast<void*>(&EcMeshAt));
    g_ecTable[6] = Word(reinterpret_cast<void*>(&EcMaterialAt));
    g_ecMaterial[0].bytes[0xB] = 1;
    std::memcpy(g_ecVertices, vertices, sizeof g_ecVertices);
    At<uint16_t>(g_ecVertices, 6) = static_cast<uint16_t>(n & ~3u);
    char* const list = At<char*>(mesh, 8);
    std::memcpy(g_ecEntries, At<char*>(list, 4), 12);
    std::memcpy(g_ecEntries + 3, At<char*>(list, 4), 12);
    char* const entries = reinterpret_cast<char*>(g_ecEntries);
    g_ecList = {At<uint32_t>(list, 0), entries, entries + 24, entries + 24};
    g_ecMeshes[0] = {vertices, At<uint32_t>(mesh, 4), &g_ecList};
    g_ecMeshes[1] = {g_ecVertices, At<uint32_t>(mesh, 4), At<EcList*>(mesh, 8)};
    g_ecMeshes[2] = {g_ecVertices, At<uint32_t>(mesh, 4), &g_ecList};
    char* const made[3] = {reinterpret_cast<char*>(&g_ecMeshes[0]), reinterpret_cast<char*>(&g_ecMeshes[1]),
                           reinterpret_cast<char*>(&g_ecMeshes[2])};
    g_ecFakes[0] = {Word(g_ecTable), 1, 3, {&g_ecMaterial[0], &g_ecMaterial[1], &g_ecMaterial[0]}, {mesh, mesh, made[0]}};
    g_ecFakes[1] = {Word(g_ecTable), 1, 2, {&g_ecMaterial[0], &g_ecMaterial[0], nullptr}, {made[1], made[2], nullptr}};
}

// The step's and the emit's fields of the made-up description: a pull, a
// wind, a slowing, the size, the front's speed (zero for kind 3), reach
// and fall-off, and its curve (one value for odd kinds, else four).
void EcDescribe(uint32_t kind, float size)
{
    PcDescribe(kind);
    const auto set = [](uint32_t at, float value) { std::memcpy(g_pcDescription + at, &value, 4); };
    set(0xE0, 0.25f);
    set(0xE4, -9.8f);
    set(0xE8, 0.125f);
    set(0xEC, 0.3f);
    set(0x100, 0.05f);
    set(0x1D8, size);
    set(0x1EC, kind == 3 ? 0.0f : 3.5f);
    set(0x1F0, 4.0f);
    set(0x200, 0.75f);
    At<float*>(g_pcDescription, 0x1F4) = g_ecCurve;
    At<float*>(g_pcDescription, 0x1F8) = g_ecCurve + (kind % 2 ? 1 : 4);
}

void EcSetUp(char* effect, uint32_t kind, char* materials)
{
    PcSetUp(effect, kind);
    At<float*>(effect, 0x1E8) = &g_ecScale;
    const float wind[4] = {2.0f, 0.0f, -1.0f, 1.0f};
    std::memcpy(effect + 0x1B8, wind, sizeof wind);
    At<float>(effect, 0x108) = 0.05f;
    At<double>(effect, 0x130) = 0.5;
    const float point[3] = {101.0f, 21.5f, -299.0f};
    std::memcpy(effect + 0x25C, point, sizeof point);
    const float frame[13] = {0.9211f, 0.0413f, -0.3871f, -0.0327f, 0.9991f, 0.0288f, 0.3879f, -0.0141f, 0.9216f,
                             -12.5f, 3.25f, 77.125f, kind % 2 ? 1.0f : 0.8125f};
    std::memcpy(effect + 0xC8, frame, sizeof frame);
    At<float>(effect, 0x278) = 0.0f;
    At<uint32_t>(effect, 0x188) = 0;
    if (materials)
        SuSlot<uint32_t>(materials, 4);
    At<char*>(effect, 0x254) = materials;
}

// A made-up particle: its age, life, place and velocity.
struct EcCase {
    float front, last;
    double clock;
    float age, life, place[3], velocity[3];
};

// The draws' switch (renderer.effects.draw_c, at the end of the file).
StepMode g_drawCMode = StepMode::Original;
void EdRun(char* effect, char* materials, bool quads, uint32_t turn);

void EcRun(char* manager, uint32_t turn)
{
    EcLoad(manager);
    const uint32_t kind = turn % 4, which = turn % 3;
    char* materials = nullptr;
    bool quads = false;
    if (which == 0) {
        materials = g_ecReal;
        quads = turn / 3 % 2 != 0;
    } else if (g_ecTable[0]) {
        materials = reinterpret_cast<char*>(&g_ecFakes[which - 1]);
        quads = which == 2;
    }
    // Triangles cut below 3 or 6 (the constant over the size), grids of 2
    // or 1 a side.
    const float size = quads ? (kind % 2 ? 1.25f : 2.5f) : M<float>(0x00A8D45C) / (kind % 2 ? 6.0f : 3.0f);
    EcDescribe(kind, size);
    McPair pair = McMakePair();
    const std::vector<char> made(pair.game, pair.game + kEffectSizeC), madeOurs(pair.ours, pair.ours + kEffectSizeC);
    EcSetUp(pair.game, kind, materials);
    EcSetUp(pair.ours, kind, materials);
    const auto both = [&](auto&& set) {
        set(pair.game);
        set(pair.ours);
    };
    const auto emit = [&](const char* id) {
        PcStep(id, pair, false, [](char* effect) { CallThis<uint32_t>(addr::SC4EffectC_Emit, effect); },
               [](char* effect) { EmitEffectC(effect); });
    };
    // 0. The emit.
    if (materials) {
        both([&](char* e) {
            At<uint8_t>(e, 0x22C) = quads ? 1 : 0;
            At<uint8_t>(e, 0x258) = 0;
            At<double>(e, 0x120) = 0.0;
            At<float>(e, 0x190) = 0.0f;
            At<uint32_t>(e, 0x18) = 0;
        });
        emit("effects.emit_c");
        static bool told[6] = {};
        if (!told[which * 2 + quads]) {
            told[which * 2 + quads] = true;
            log::Write("water effects: the third class's forced emit cut the %s by %s into %d pieces",
                       which ? "made-up materials" : "real model", quads ? "quads" : "triangles",
                       static_cast<int>((At<char*>(pair.game, 0x224) - At<char*>(pair.game, 0x220)) / 0x3C));
        }
        both([](char* e) { At<float>(e, 0x26C) = 3.25f; });
        emit("effects.emit_c");
        both([](char* e) { At<float>(e, 0x190) = 5.0f; });
        emit("effects.emit_c.wait");
        both([](char* e) {
            At<float>(e, 0x190) = 0.01f;
            At<uint8_t>(e, 0x258) = 0;
            At<double>(e, 0x120) = 0.0;
            At<uint32_t>(e, 0x18) = 0;
        });
        emit("effects.emit_c");
        both([](char* e) {
            At<double>(e, 0x120) = 0.99;
            At<double>(e, 0x130) = 1.0;
        });
        emit("effects.emit_c");
        both([](char* e) {
            At<uint32_t>(e, 0x188) = 1;
            At<uint8_t>(e, 9) = 1;
        });
        emit("effects.emit_c.stopped");
        both([](char* e) { At<uint32_t>(e, 0x188) = 0; });
    }
    both([](char* e) {
        At<uint8_t>(e, 9) = 1;
        At<char*>(e, 0x254) = nullptr;
    });
    emit("effects.emit_c.stopped");
    both([&](char* e) { At<char*>(e, 0x254) = materials; });
    // 1. The step.
    static const EcCase cases[] = {
        {2.0f, 2.0f, 0.37, 0.5f, 3.0f, {103.0f, 22.0f, -298.0f}, {1.0f, 2.0f, 3.0f}},
        {2.0f, 2.0f, 1.0, 0.5f, 3.0f, {99.5f, 24.0f, -300.5f}, {-1.0f, 0.5f, 0.25f}},
        {2.0f, 2.0f, 0.999, 0.75f, 3.0f, {101.5f, 21.0f, -297.25f}, {0.0f, 0.0f, 0.0f}},
        {6.0f, 5.5f, 0.5, 0.5f, 3.0f, {103.0f, 22.0f, -298.0f}, {1.0f, 2.0f, 3.0f}},
        {0.5f, 0.25f, 0.5, 0.0f, 3.0f, {103.0f, 22.0f, -298.0f}, {1.0f, 2.0f, 3.0f}},
        {5.0f, 4.0f, 0.5, 0.0f, 3.0f, {103.0f, 22.0f, -298.0f}, {1.0f, 2.0f, 3.0f}},
        {5.0f, 4.0f, 0.5, 2.99f, 3.0f, {103.0f, 22.0f, -298.0f}, {1.0f, 2.0f, 3.0f}},
        {5.0f, 5.0f, 0.25, 1.0f, 3.0f, {101.0f, 21.5f, -299.0f}, {0.5f, -0.5f, 0.125f}},
        {5.0f, 4.0f, 0.25, 1.0f, 3.0f, {101.003f, 21.5f, -299.0f}, {0.5f, -0.5f, 0.125f}},
        {0.1f, 0.05f, 0.5, 1.0f, 3.0f, {131.0f, 1.5f, -259.0f}, {7.0f, -2.0f, 3.5f}},
    };
    alignas(4) static char particles[2][0x48];
    uint32_t answers[2] = {};
    const auto particleOf = [&](char* effect) { return particles[effect == pair.game ? 0 : 1]; };
    for (const EcCase& c : cases) {
        both([&](char* e) {
            At<float>(e, 0x26C) = c.front;
            At<float>(e, 0x270) = c.last;
            At<float>(e, 0x274) = 1.75f;
            At<double>(e, 0x120) = c.clock;
            char* const p = particleOf(e);
            std::memset(p, 0, 0x48);
            At<uint8_t>(p, 0) = 1;
            At<float>(p, 4) = c.age;
            At<float>(p, 8) = c.life;
            std::memcpy(p + 0xC, c.place, sizeof c.place);
            std::memcpy(p + 0x1C, c.velocity, sizeof c.velocity);
        });
        const float step = 0.05f;
        PcStep(
            "effects.step_c", pair, false,
            [&](char* effect) {
                answers[0] = CallThis<uint32_t>(addr::SC4EffectC_StepParticle, effect, particleOf(effect), step) & 0xFF;
            },
            [&](char* effect) { answers[1] = StepParticleC(effect, particleOf(effect), step) & 0xFF; },
            [&](char* effect, McPrint& print) {
                char* const p = particleOf(effect);
                for (uint32_t at = 0; at < 0x48; at += 4)
                    print.push_back({0x600000 + at, At<uint32_t>(p, at)});
                print.push_back({0x600100, answers[effect == pair.game ? 0 : 1]});
            });
    }
    // 2. Value 9's point.
    const float values[2][6] = {{3.0f, -1.5f, 40.0f, 0.0f, 1.0f, 0.0f}, {-7.25f, 2.0f, 15.5f, 0.6f, 0.0f, 0.8f}};
    const auto point = [&](const char* id, const float* value) {
        PcStep(id, pair, false,
               [&](char* effect) { CallThis<uint32_t>(addr::SC4EffectC_SetPointAlong, effect, value, value + 3); },
               [&](char* effect) { SetPointAlongC(effect, value, value + 3); });
    };
    for (const float extra : {0.0f, 2.5f}) {
        both([&](char* e) { At<float>(e, 0x278) = extra; });
        for (const auto& value : values)
            point(materials ? "effects.point_c" : "effects.point_c.none", value);
    }
    if (materials) {
        both([](char* e) { At<char*>(e, 0x254) = nullptr; });
        point("effects.point_c.none", values[0]);
        both([&](char* e) { At<char*>(e, 0x254) = materials; });
        CallThis<uint32_t>(M<uint32_t>(At<uint32_t>(pair.game, 0) + 0x14), pair.game, 9u, Word(values[1]), 0u);
    }
    // 3. The draws (renderer.effects.draw_c), on the first.
    if (g_drawCMode == StepMode::Compare)
        EdRun(pair.game, materials, quads, turn);
    // Put back as made, but for the pieces, the batches, the words, the
    // textures and the materials (each effect lets go of them and its hold).
    for (const auto& [effect, bytes] : {std::pair<char*, const std::vector<char>*>{pair.game, &made},
                                        std::pair<char*, const std::vector<char>*>{pair.ours, &madeOurs}}) {
        char keep[0x38];
        std::memcpy(keep, effect + 0x220, sizeof keep);
        std::memcpy(effect, bytes->data(), kEffectSizeC);
        std::memcpy(effect + 0x220, keep, sizeof keep);
    }
    TcDelete(pair.game);
    TcDelete(pair.ours);
}

void EcForce(char* self)
{
    if (!g_ecForce || g_dwTrace != DwTrace::Off || g_depth > 0)
        return;
    static uint32_t turn = 0;
    const uint32_t now = turn++;
    if (now % 3 == 1)
        EcRun(self - 4, now / 3);
}

bool InstallEmitC(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(Detour(addr::SC4EffectC_StepParticle, &TSC4EffectC_StepParticle, &g_SC4EffectC_StepParticle) &&
          Detour(addr::SC4EffectC_Emit, &TSC4EffectC_Emit, &g_SC4EffectC_Emit) &&
          Detour(addr::SC4EffectC_CutTriangles, &TSC4EffectC_CutTriangles, &g_SC4EffectC_CutTriangles) &&
          Detour(addr::SC4EffectC_CutQuads, &TSC4EffectC_CutQuads, &g_SC4EffectC_CutQuads) &&
          Detour(addr::SC4EffectC_SetPointAlong, &TSC4EffectC_SetPointAlong, &g_SC4EffectC_SetPointAlong)))
        return false;
    if (mode == registry::Mode::Compare) {
        const char* v = std::getenv("OPENSC4_EFFECT_FORCE_THIRD");
        if (v && *v == '1') {
            if (!g_SC4EffectsManager_Draw &&
                !Detour(addr::SC4EffectsManager_Draw, &TDwManager, &g_SC4EffectsManager_Draw))
                return false;
            g_ecForce = true;
            log::Write("water effects: the third class's emit and step forced before the manager's draw");
        }
    }
    g_emitCMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the third class's emit and step %s",
               mode == registry::Mode::Compare ? "compared with the game's when forced" : "ours");
    return true;
}

const bool g_registeredEmitC = registry::Register({
    "renderer.effects.emit_c",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectC_Emit,
    "The third effect class's emit (its model blown apart by triangles or quads the first time), its particles' "
    "step (a growing front pushing them out) and value 9's point along a direction; no test city makes such an "
    "effect, so compare mode compares them only when forced, OPENSC4_EFFECT_FORCE_THIRD=1 running the game's and "
    "ours on pairs of new effects of the class before the manager's draw (docs/renderer.md, \"Water effects, sky "
    "and clouds\")",
    true,
    InstallEmitC,
});

// --- The third class's draws -----------------------------------------------------------------------
//
// The class's two draws (0x005A6990, slot 0x44; 0x005A77F0, slot 0x48),
// effects_third.h (renderer.effects.draw_c). No test city makes an effect
// of the class, so in compare mode they run as the game's and are
// compared only when forced (OPENSC4_EFFECT_FORCE_THIRD=1, as for the emit
// and step; EcRun's last part, after the emit has cut the pieces): on the
// first effect of the pair, its materials kept and their batches made
// (SetEffectMaterialsC, as SC4EffectC::SetMaterials; kept, with the
// textures held, for the effect's destructor), a made-up list of five particles (ages, lives,
// places, a batch each by turn and a piece each, one without a turn) in
// place of its own, the description's lists of colours (one or three),
// opacities (one or four), sizes (one or three) and turns (one or two) by
// turn and its turn about y (+0x104) in one turn of two, colour scales
// (+0x1D4), a box point (+0x194) and the blocks' bytes (+0x8D, +0xC5) by
// turn. Each compared as the base class's draws are (BfComparedSized, the
// game's run noting its calls, ours answered from the notes), dry: the
// render context is made up and none of its calls (the model transform,
// the material, the mesh, the texture) nor the batch's draw is made. The
// first draw: the model whole (+0x258 clear), the pieces (set), the
// pieces without materials, no particles. The second, with a made-up
// dynamic vertex buffer of five quads' room (its lock answering memory of
// its own): five particles' count, thirteen (three runs), none, and five
// with the lock answering nothing.
Call1Fn g_SC4EffectC_Draw = nullptr, g_SC4EffectC_DrawBuffered = nullptr;
Call1Fn g_EdTransform = nullptr, g_EdMesh = nullptr;
Call2Fn g_EdMaterial = nullptr;

uint32_t __fastcall DrawCByMode(char* self, void*, uint32_t context)
{
    if (g_drawCMode != StepMode::Ours)
        return g_SC4EffectC_Draw(self, nullptr, context);
    DrawEffectC(self, Ptr(context));
    return 0;
}
OPENSC4_THUNK(TSC4EffectC_Draw, DrawCByMode, 4)

uint32_t __fastcall DrawBufferedCByMode(char* self, void*, uint32_t context)
{
    if (g_drawCMode != StepMode::Ours)
        return g_SC4EffectC_DrawBuffered(self, nullptr, context);
    DrawEffectCBuffered(self);
    return 0;
}
OPENSC4_THUNK(TSC4EffectC_DrawBuffered, DrawBufferedCByMode, 4)

// The context's model calls, noted in the game's run.
uint32_t __fastcall EdTransformNoted(char* self, void*, uint32_t transform)
{
    if (!BNoting())
        return g_EdTransform(self, nullptr, transform);
    alignas(4) char noted[kEcTransform];
    EcNoteBlock(noted, Ptr(transform), kEcTransform);
    return BCallee(addr::SC4RenderContext_SetModelTransform, Word(self), Word(noted), kEcTransform,
                   [&] { return g_bDry ? 0u : g_EdTransform(self, nullptr, transform); });
}
OPENSC4_THUNK(TEdTransform, EdTransformNoted, 4)

uint32_t __fastcall EdMaterialNoted(char* self, void*, uint32_t material, uint32_t properties)
{
    if (!BNoting())
        return g_EdMaterial(self, nullptr, material, properties);
    alignas(4) char noted[4 + kEcProperties];
    At<uint32_t>(noted, 0) = material;
    EcNoteBlock(noted + 4, Ptr(properties), kEcProperties);
    return BCallee(addr::SC4RenderContext_SetMaterial, Word(self), Word(noted), sizeof noted,
                   [&] { return g_bDry ? 0u : g_EdMaterial(self, nullptr, material, properties); });
}
OPENSC4_THUNK(TEdMaterial, EdMaterialNoted, 8)

uint32_t __fastcall EdMeshNoted(char* self, void*, uint32_t mesh)
{
    if (!BNoting())
        return g_EdMesh(self, nullptr, mesh);
    return BCallee(addr::SC4RenderContext_DrawMesh, Word(self), Word(&mesh), 4,
                   [&] { return g_bDry ? 0u : g_EdMesh(self, nullptr, mesh); });
}
OPENSC4_THUNK(TEdMesh, EdMeshNoted, 4)

// The made-up dynamic vertex buffer: slot 0x14 its room in vertices,
// slot 0x18 its lock (memory of its own, or nothing), slot 0x20 its unlock
// and slot 0x28 its draw (neither does anything). The notes' engine swaps
// in its stubs for slots 0x18 to 0x28 (BSwapRole), as for a real one.
constexpr uint32_t kEdRoom = 20;
uint32_t g_edBufferTable[kBSlots] = {};
uint32_t g_edBuffer[0x10] = {};
alignas(4) char g_edLocked[0x1000] = {};
alignas(4) char g_edContext[0x100] = {};
bool g_edNoLock = false;

uint32_t __fastcall EdRoom(char*, void*, uint32_t)
{
    return kEdRoom;
}

uint32_t __fastcall EdLock(char*, void*, uint32_t, uint32_t count)
{
    if (g_edNoLock || count * 0x20 > sizeof g_edLocked)
        return 0;
    std::memset(g_edLocked, 0x5A, sizeof g_edLocked);
    return Word(g_edLocked);
}

uint32_t __fastcall EdNothing(char*, void*, uint32_t)
{
    return 0;
}

uint32_t __fastcall EdDrawNothing(char*, void*, uint32_t, uint32_t, uint32_t, uint32_t)
{
    return 0;
}

char* EdBuffer()
{
    if (!g_edBufferTable[0]) {
        for (uint32_t& slot : g_edBufferTable)
            slot = Word(reinterpret_cast<void*>(&EdNothing));
        g_edBufferTable[0x14 / 4] = Word(reinterpret_cast<void*>(&EdRoom));
        g_edBufferTable[0x18 / 4] = Word(reinterpret_cast<void*>(&EdLock));
        g_edBufferTable[0x28 / 4] = Word(reinterpret_cast<void*>(&EdDrawNothing));
        g_edBuffer[0] = Word(g_edBufferTable);
    }
    return reinterpret_cast<char*>(g_edBuffer);
}

// A node of an effect's list of particles.
struct EdNode {
    EdNode* next;
    EdNode* prev;
    char* particle;
};

struct EdParticle {
    float age, life, place[3], spin;
};

void EdLink(EdNode* nodes, std::size_t count)
{
    for (std::size_t i = 0; i <= count; ++i) {
        nodes[i].next = &nodes[(i + 1) % (count + 1)];
        nodes[(i + 1) % (count + 1)].prev = &nodes[i];
    }
}

void EdRun(char* effect, char* materials, bool quads, uint32_t turn)
{
    const uint32_t pieces =
        static_cast<uint32_t>((At<char*>(effect, 0x224) - At<char*>(effect, 0x220)) / static_cast<int32_t>(kPieceBytes));
    if (!materials || !pieces || !g_SC4EffectC_Draw)
        return;
    SetEffectMaterialsC(effect, materials);
    const uint32_t batches = EcBatches(effect);
    if (!batches)
        return;
    // The description's lists, by turn.
    static const float colours[9] = {1.0f, 0.5f, 0.25f, 0.2f, 0.9f, 0.6f, 0.75f, 0.125f, 1.0f};
    static const float opacities[4] = {1.0f, 0.75f, 0.3f, 0.0f};
    static const float sizes[3] = {1.5f, 0.75f, 2.25f};
    static const float turns[2] = {0.0f, 0.375f};
    const bool many = turn % 2 == 0;
    const auto list = [](uint32_t at, const float* begin, std::size_t count) {
        At<const float*>(g_pcDescription, at) = begin;
        At<const float*>(g_pcDescription, at + 4) = begin + count;
    };
    list(0x94, colours, many ? 9 : 3);
    list(0xA0, opacities, many ? 4 : 1);
    list(0xAC, sizes, turn % 3 ? 3 : 1);
    list(0xC4, many ? turns : turns + 1, many ? 2 : 1);
    At<float>(g_pcDescription, 0x104) = turn / 2 % 2 ? 0.002f : 0.0f;
    // The particles and their list.
    static const EdParticle made[5] = {{0.25f, 2.0f, {1.0f, 2.0f, 3.0f}, 1.0f},
                                       {1.0f, 4.0f, {-3.0f, 0.5f, 7.0f}, 0.0f},
                                       {0.0f, 1.5f, {10.0f, -2.0f, 0.25f}, -2.0f},
                                       {2.9f, 3.0f, {0.0f, 0.0f, 0.0f}, 0.5f},
                                       {0.6f, 1.2f, {-1.0f, 4.0f, -6.0f}, 1.5f}};
    alignas(4) static char particles[5][0x48];
    static EdNode nodes[6];
    for (uint32_t k = 0; k < 5; ++k) {
        char* const p = particles[k];
        std::memset(p, 0, sizeof particles[k]);
        At<uint8_t>(p, 0) = 1;
        At<float>(p, 4) = made[k].age;
        At<float>(p, 8) = made[k].life;
        std::memcpy(p + 0xC, made[k].place, sizeof made[k].place);
        At<uint8_t>(p, 0x18) = static_cast<uint8_t>((k + turn) % batches);
        At<float>(p, 0x30) = made[k].spin;
        At<uint32_t>(p, 0x44) = (k * 7 + turn) % pieces;
        nodes[k + 1].particle = p;
    }
    EdLink(nodes, 5);
    char* const list14 = At<char*>(effect, 0x14);
    char* const buffer = At<char*>(effect, 0x1EC);
    At<char*>(effect, 0x14) = reinterpret_cast<char*>(nodes);
    const float scales[3] = {0.9f, 0.75f, 1.0f}, point[3] = {100.5f, 20.25f, -300.0f};
    std::memcpy(effect + 0x1D4, scales, sizeof scales);
    std::memcpy(effect + 0x194, point, sizeof point);
    At<uint8_t>(effect, 0x8D) = turn / 4 % 4 & 1;
    At<uint8_t>(effect, 0xC5) = turn / 4 % 4 >> 1 & 1;
    At<uint32_t>(effect, 0x18) = 5;
    char* const context = g_edContext;
    const auto draw = [&](const char* id) {
        BfComparedSized(
            id, effect, kEffectSizeC, [&] { return g_SC4EffectC_Draw(effect, nullptr, Word(context)); },
            [&] { DrawEffectC(effect, context); });
    };
    const auto buffered = [&](const char* id, uint32_t count) {
        At<uint32_t>(effect, 0x18) = count;
        BSwapRole(At<char*>(effect, 0x1EC), kBfBuffer,
                  {{0x18, reinterpret_cast<uint32_t>(&BfLockStub)},
                   {0x20, reinterpret_cast<uint32_t>(&BfUnlockStub)},
                   {0x28, reinterpret_cast<uint32_t>(&BfDrawStub)}});
        BfComparedSized(
            id, effect, kEffectSizeC, [&] { return g_SC4EffectC_DrawBuffered(effect, nullptr, Word(context)); },
            [&] { DrawEffectCBuffered(effect); });
    };
    g_bDry = true;
    At<uint8_t>(effect, 0x258) = 0;
    draw("effects.draw_c.model");
    At<uint8_t>(effect, 0x258) = 1;
    draw("effects.draw_c");
    At<uint8_t>(effect, 0x258) = 0;
    At<char*>(effect, 0x254) = nullptr;
    draw("effects.draw_c");
    At<char*>(effect, 0x254) = materials;
    At<uint8_t>(effect, 0x258) = 1;
    nodes[0].next = nodes[0].prev = &nodes[0];
    draw("effects.draw_c.empty");
    EdLink(nodes, 5);
    At<char*>(effect, 0x1EC) = EdBuffer();
    buffered("effects.draw_c.buffered", 5);
    buffered("effects.draw_c.buffered", 13);
    buffered("effects.draw_c.buffered", 0);
    g_edNoLock = true;
    buffered("effects.draw_c.buffered.none", 5);
    g_edNoLock = false;
    g_bDry = false;
    static bool told[6] = {};
    const uint32_t which = (materials == g_ecReal ? 0 : materials == reinterpret_cast<char*>(&g_ecFakes[0]) ? 1 : 2) * 2 +
                           (quads ? 1 : 0);
    if (!told[which]) {
        told[which] = true;
        log::Write("water effects: the third class's forced draws: %s by %s, %u pieces, %u batches (kind %u)",
                   which / 2 ? "made-up materials" : "real model", quads ? "quads" : "triangles", pieces, batches,
                   At<uint32_t>(At<char*>(effect, 0x230), 8));
    }
    At<char*>(effect, 0x14) = list14;
    At<char*>(effect, 0x1EC) = buffer;
}

bool InstallDrawC(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(Detour(addr::SC4EffectC_Draw, &TSC4EffectC_Draw, &g_SC4EffectC_Draw) &&
          Detour(addr::SC4EffectC_DrawBuffered, &TSC4EffectC_DrawBuffered, &g_SC4EffectC_DrawBuffered)))
        return false;
    if (mode == registry::Mode::Compare) {
        const char* v = std::getenv("OPENSC4_EFFECT_FORCE_THIRD");
        if (v && *v == '1') {
            if (!(PdHookBindTexture() &&
                  (g_PdBatchDraw || Detour(addr::SC4IndexBatch_Draw, &TPdBatchDraw, &g_PdBatchDraw)) &&
                  Detour(addr::SC4RenderContext_SetModelTransform, &TEdTransform, &g_EdTransform) &&
                  Detour(addr::SC4RenderContext_SetMaterial, &TEdMaterial, &g_EdMaterial) &&
                  Detour(addr::SC4RenderContext_DrawMesh, &TEdMesh, &g_EdMesh)))
                return false;
            if (!g_SC4EffectsManager_Draw &&
                !Detour(addr::SC4EffectsManager_Draw, &TDwManager, &g_SC4EffectsManager_Draw))
                return false;
            g_ecForce = true;
            log::Write("water effects: the third class's draws forced before the manager's draw");
        }
    }
    g_drawCMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    log::Write("water effects: the third class's draws %s",
               mode == registry::Mode::Compare ? "compared with the game's when forced" : "ours");
    return true;
}

const bool g_registeredDrawC = registry::Register({
    "renderer.effects.draw_c",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectC_Draw,
    "The third effect class's two draws: the model whole until it is blown apart, then its pieces into the "
    "batches, a texture each (slot 0x44), or into the dynamic vertex buffer (slot 0x48); no test city makes such "
    "an effect, so compare mode compares them only when forced, OPENSC4_EFFECT_FORCE_THIRD=1 drawing pairs of new "
    "effects of the class dry, the game's calls noted and ours answered from the notes (docs/renderer.md, "
    "\"Water effects, sky and clouds\")",
    true,
    InstallDrawC,
});

// --- The three callees ------------------------------------------------------------------------
//
// The three game functions the ported code above still called by address
// (effects_life.h and effects_second.h): the full step's end for
// descriptions with bit 0 of +4 (0x005B8020), the start of another effect
// named by the effect and a suffix (0x005B8290) and the move of a place by
// a description's list (0x005AC070). Each has a switch: ours makes the
// ported callers call ours and the detour run ours for the game's callers.
//
// The step's end and the move write only the particle or the place and
// can run twice: the step's end is compared as the set-up is
// (SuCompared, its occupant manager asked twice), the move on a copy of the
// place. The start cannot run twice: its comparison runs both dry, ours
// with the four calls it makes only noted and the game's with the effects
// manager (0x00B43D1C) swapped for a made-up one whose make notes the name
// and gives a made-up effect that notes its block, its start and its
// release; each answer in turn (made and not made, the second recorded as
// .none), and then the game's start runs for real. The block's third and
// fourth bytes, which the game leaves as they were on its stack, are
// noted as zero.
//
// The test cities may not reach all of them: OPENSC4_EFFECT_FORCE_CALLEES=1
// (compare mode only) compares each switch in compare mode before every
// running effect's draw on made-up arguments, with copies only (EnForce).
StepMode g_stepB1Mode = StepMode::Original, g_namedMode = StepMode::Original, g_moveMode = StepMode::Original;
Call1Fn g_SC4Effect_StepFlagB1 = nullptr;
using MovePlaceFn = void(__cdecl*)(uint32_t, char*, float*);
MovePlaceFn g_SC4Effect_MovePlace = nullptr;
bool g_enForce = false;

// The step's end compared on a particle; the particle's end (its first
// byte cleared by the game's) recorded too as <id>.ended.
void EnB1Compared(const char* id, char* self, char* particle)
{
    const uint8_t before = At<uint8_t>(particle, 0);
    SuCompared(
        id, self, particle,
        [&] {
            StepFlagB1(particle);
            return 0u;
        },
        [&] {
            g_SC4Effect_StepFlagB1(self, nullptr, Word(particle));
            return 0u;
        });
    if (before && !At<uint8_t>(particle, 0))
        compare::Record((std::string(id) + ".ended").c_str(), true, std::string(), std::string());
}

uint32_t __fastcall EnStepB1ByMode(char* self, void*, uint32_t particle)
{
    if (g_stepB1Mode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4Effect_StepFlagB1(self, nullptr, particle);
    if (g_stepB1Mode == StepMode::Ours) {
        StepFlagB1(Ptr(particle));
        return 0;
    }
    EnB1Compared("effects.step_b1", self, Ptr(particle));
    return 0;
}
OPENSC4_THUNK(TEnStepB1, EnStepB1ByMode, 4)

// The move compared: ours on a copy of the place, the game's on the place.
void EnMoveCompared(const char* id, uint32_t t, char* list, float* place)
{
    float mine[3];
    std::memcpy(mine, place, sizeof mine);
    MovePlace(LfFloat(t), list, mine);
    g_SC4Effect_MovePlace(t, list, place);
    const bool same = std::memcmp(mine, place, sizeof mine) == 0;
    compare::Record(id, same, same ? std::string() : Hex(place, 12), same ? std::string() : Hex(mine, 12));
}

void __cdecl EnMoveByMode(uint32_t t, char* list, float* place)
{
    if (g_moveMode == StepMode::Ours)
        MovePlace(LfFloat(t), list, place);
    else if (g_moveMode == StepMode::Original || g_effectRecord || g_effectReplay)
        g_SC4Effect_MovePlace(t, list, place);
    else
        EnMoveCompared("effects.move_place", t, list, place);
}
OPENSC4_CDECL_THUNK(TEnMove, EnMoveByMode, 12)

// The start's dry runs: the notes of the current run, the answer the make
// gives, the made-up manager and effect (their tables filled at install).
std::vector<std::string>* g_enNotes = nullptr;
bool g_enMade = false;
uint32_t g_enManagerTable[16] = {}, g_enEffectTable[16] = {};
uint32_t g_enManager[4] = {}, g_enEffect[4] = {};

void EnNote(const char* what, const std::string& data)
{
    if (g_enNotes)
        g_enNotes->push_back(std::string(what) + " " + data);
}

std::string EnBlockText(const char* block)
{
    char copy[0x38];
    std::memcpy(copy, block, sizeof copy);
    copy[2] = copy[3] = 0;
    return Hex(copy, sizeof copy);
}

std::string EnEffectText(char* effect)
{
    char text[16];
    std::snprintf(text, sizeof text, "%s", effect == reinterpret_cast<char*>(g_enEffect) ? "made" : "other");
    return text;
}

// What ours asks in the dry run, noted as the made-up objects note the
// game's.
struct EnDryOps {
    bool Make(const char* name, char** out)
    {
        EnNote("make", name);
        if (g_enMade)
            *out = reinterpret_cast<char*>(g_enEffect);
        return g_enMade;
    }
    void Block(char* effect, char* block) { EnNote("block", EnEffectText(effect) + " " + EnBlockText(block)); }
    void Start(char* effect) { EnNote("start", EnEffectText(effect) + " 0"); }
    void Release(char* effect) { EnNote("release", EnEffectText(effect)); }
};

uint32_t __fastcall EnFakeMake(char*, void*, uint32_t name, uint32_t out)
{
    EnNote("make", Ptr(name));
    if (g_enMade)
        *reinterpret_cast<uint32_t*>(Ptr(out)) = Word(g_enEffect);
    return g_enMade ? 1u : 0u;
}
OPENSC4_THUNK(TEnFakeMake, EnFakeMake, 8)

uint32_t __fastcall EnFakeBlock(char* effect, void*, uint32_t block)
{
    EnNote("block", EnEffectText(effect) + " " + EnBlockText(Ptr(block)));
    return 0;
}
OPENSC4_THUNK(TEnFakeBlock, EnFakeBlock, 4)

uint32_t __fastcall EnFakeStart(char* effect, void*, uint32_t value)
{
    char text[16];
    std::snprintf(text, sizeof text, " %X", value);
    EnNote("start", EnEffectText(effect) + text);
    return 0;
}
OPENSC4_THUNK(TEnFakeStart, EnFakeStart, 4)

uint32_t __fastcall EnFakeRelease(char* effect, void*)
{
    EnNote("release", EnEffectText(effect));
    return 0;
}
OPENSC4_THUNK(TEnFakeRelease, EnFakeRelease, 0)

std::string EnNotesText(const std::vector<std::string>& notes, std::size_t at)
{
    char text[48];
    std::snprintf(text, sizeof text, "%zu calls", notes.size());
    if (at >= notes.size())
        return text;
    std::snprintf(text, sizeof text, "%zu calls, #%zu ", notes.size(), at);
    return text + notes[at];
}

// Both dry, for each answer of the make.
void EnNamedCompared(const char* id, char* self, uint32_t suffix, uint32_t place)
{
    for (const bool made : {true, false}) {
        std::vector<std::string> ours, game;
        g_enMade = made;
        g_enNotes = &ours;
        StartNamed(self, reinterpret_cast<const char*>(Ptr(suffix)), reinterpret_cast<const float*>(Ptr(place)),
                   EnDryOps{});
        g_enNotes = &game;
        char*& manager = M<char*>(kEffectsManager);
        char* const real = manager;
        manager = reinterpret_cast<char*>(g_enManager);
        g_SC4Effect_StartNamed(self, nullptr, suffix, place);
        manager = real;
        g_enNotes = nullptr;
        const bool same = ours == game;
        std::string gameText, ourText;
        if (!same) {
            std::size_t at = 0;
            while (at < ours.size() && at < game.size() && ours[at] == game[at])
                ++at;
            gameText = EnNotesText(game, at);
            ourText = EnNotesText(ours, at);
        }
        compare::Record(made ? id : (std::string(id) + ".none").c_str(), same, gameText, ourText);
    }
}

uint32_t EnNamedRun(char* self, uint32_t suffix, uint32_t place)
{
    if (g_namedMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4Effect_StartNamed(self, nullptr, suffix, place);
    if (g_namedMode == StepMode::Ours) {
        StartNamed(self, reinterpret_cast<const char*>(Ptr(suffix)), reinterpret_cast<const float*>(Ptr(place)),
                   LfNamedOps{});
        return 0;
    }
    EnNamedCompared("effects.start_named", self, suffix, place);
    return g_SC4Effect_StartNamed(self, nullptr, suffix, place);
}

// Forcing, before each running effect's draw (DwEffectDrawByMode): for
// each switch in compare mode, in turn of a counter (k).
// - The move: the description's own list when it has one, else made-up
//   lists of up to three records (values from the first running
//   particle and the effect, a NaN, a sum out of FSIN's range, none) on a
//   copy of a place (the particle's, zeros, large) at a time (the
//   effect's +0x110 or +0x120 as floats, the particle's age).
// - The start: a suffix of eight ("_water", "_terrain" as the game's
//   strings, none, a long one, ones of no effect) at the first running
//   particle's place or a made-up one.
// - The step's end: copies of the first running particle, running, as it
//   is, at its place through the effect's second block (the terrain's
//   grid, where the occupants are) and at the block's own place; the
//   copies that end are recorded as effects.step_b1.forced.ended.
void EnForce(char* self)
{
    if (!g_enForce || g_effectRecord || g_effectReplay || !At<uint8_t>(self, 9) || !At<char*>(self, 0x24))
        return;
    char* const description = At<char*>(self, 0x24);
    char* particle = nullptr;
    for (char* node = At<char*>(At<char*>(self, 0x14), 0); node != At<char*>(self, 0x14) && !particle;
         node = At<char*>(node, 0)) {
        if (At<uint8_t>(At<char*>(node, 8), 0))
            particle = At<char*>(node, 8);
    }
    static uint32_t turn = 0;
    const uint32_t k = turn++;
    const float zeros[3] = {0.0f, 0.0f, 0.0f};
    const float* const at = particle ? reinterpret_cast<const float*>(particle + 0xC) : zeros;
    const float age = particle ? At<float>(particle, 4) : 0.0f;
    if (g_moveMode == StepMode::Compare) {
        const uint32_t nan = 0x7FC00000u;
        float notANumber = 0.0f;
        std::memcpy(&notANumber, &nan, 4);
        const float v[3] = {particle ? At<float>(particle, 0x1C) : 0.25f, particle ? At<float>(particle, 0x20) : -0.5f,
                            particle ? At<float>(particle, 0x24) : 2.0f};
        float records[3][7] = {
            {1.5f, 0.01f, 0.02f, -0.03f, v[0], v[1], v[2]},
            {-0.7f, at[0] * 1.0e-3f, 0.5f, at[2] * 1.0e-3f, 0.3f, -0.2f, 0.1f},
            {static_cast<float>(At<double>(self, 0x110)), 3.0f, -1.0f, 0.25f, 1.0f, 1.0f, 1.0f},
        };
        const uint32_t kind = k % 6;
        if (kind == 3)
            records[1][2] = notANumber;
        if (kind == 4)
            records[0][0] = 1.0e20f;
        const uint32_t count = kind == 5 ? 0 : kind == 0 ? 1 : 3;
        uint32_t list[3] = {Word(records[0]), Word(&records[0][0] + 7 * count), 0};
        char* used = reinterpret_cast<char*>(list);
        if (kind == 0 && At<uint32_t>(description, 0x108) != At<uint32_t>(description, 0x10C))
            used = description + 0x108;
        float place[3];
        const uint32_t where = k / 6 % 3;
        if (where == 0)
            std::memcpy(place, at, sizeof place);
        else if (where == 1)
            std::memcpy(place, zeros, sizeof place);
        else
            place[0] = 1234.5f, place[1] = 270.25f, place[2] = -3.0e4f;
        const uint32_t when = k / 18 % 3;
        const float t = when == 0   ? static_cast<float>(At<double>(self, 0x110))
                        : when == 1 ? static_cast<float>(At<double>(self, 0x120))
                                    : age;
        EnMoveCompared("effects.move_place.forced", LfFloatBits(t), used, place);
    }
    if (g_namedMode == StepMode::Compare) {
        static const char* const suffixes[8] = {
            reinterpret_cast<const char*>(0x00AA0F2Cu),
            reinterpret_cast<const char*>(0x00AA0F20u),
            "",
            "_e11_a_suffix_long_enough_to_grow_the_string_past_what_it_held_before",
            "_e11",
            "_smoke",
            "x",
            "_water_e11",
        };
        float place[3];
        std::memcpy(place, at, sizeof place);
        if (k / 8 % 2)
            place[0] = -1.0f, place[1] = 512.0f, place[2] = 8.0e3f;
        EnNamedCompared("effects.start_named.forced", self, Word(suffixes[k % 8]), Word(place));
    }
    if (g_stepB1Mode == StepMode::Compare && particle) {
        const auto f = [&](uint32_t offset) { return static_cast<double>(At<float>(self, offset)); };
        char copy[kParticleBytes];
        for (uint32_t which = 0; which < 3; ++which) {
            std::memcpy(copy, particle, sizeof copy);
            copy[0] = 1;
            if (which == 1) {
                // As the full step takes it to the terrain's grid.
                const double x = At<float>(particle, 0xC), y = At<float>(particle, 0x10), z = At<float>(particle, 0x14);
                const float gx0 = static_cast<float>((z * f(0xD0) + y * f(0xCC)) + x * f(0xC8));
                const float gy0 = static_cast<float>((x * f(0xD4) + z * f(0xDC)) + y * f(0xD8));
                const double gz0 = (z * f(0xE8) + y * f(0xE4)) + x * f(0xE0);
                const double scale = f(0xF8);
                At<float>(copy, 0xC) = static_cast<float>(static_cast<double>(static_cast<float>(gx0 * scale)) + f(0xEC));
                At<float>(copy, 0x10) = static_cast<float>(static_cast<double>(static_cast<float>(gy0 * scale)) + f(0xF0));
                At<float>(copy, 0x14) = static_cast<float>(gz0 * scale + f(0xF4));
            } else if (which == 2) {
                std::memcpy(copy + 0xC, self + 0xEC, 12);
            }
            EnB1Compared("effects.step_b1.forced", self, copy);
        }
    }
}

bool EnHookForce()
{
    const char* v = std::getenv("OPENSC4_EFFECT_FORCE_CALLEES");
    if (!v || *v != '1')
        return true;
    if (!DwHookEffectDraw())
        return false;
    if (!g_enForce)
        log::Write("water effects: the step's end, the named start and the move forced before each effect's draw");
    g_enForce = true;
    return true;
}

bool InstallStepB1(registry::Mode mode)
{
    if (!SuInstall(mode, g_stepB1Mode, addr::SC4Effect_StepFlagB1, &TEnStepB1, &g_SC4Effect_StepFlagB1,
                   "the end of a particle's other step (buildings and props)"))
        return false;
    g_stepB1Ours = mode == registry::Mode::Ours;
    return mode != registry::Mode::Compare || EnHookForce();
}

bool InstallStartNamed(registry::Mode mode)
{
    if (!SuInstall(mode, g_namedMode, addr::SC4Effect_StartNamed, &TLfStartNamed, &g_SC4Effect_StartNamed,
                   "the start of an effect named by an effect and a suffix"))
        return false;
    const auto fill = [](uint32_t* table, std::initializer_list<std::pair<uint32_t, void (*)()>> slots) {
        for (const auto& [slot, thunk] : slots)
            table[slot / 4] = reinterpret_cast<uint32_t>(thunk);
    };
    fill(g_enManagerTable, {{0x1C, &TEnFakeMake}});
    fill(g_enEffectTable, {{0x8, &TEnFakeRelease}, {0xC, &TEnFakeStart}, {0x20, &TEnFakeBlock}});
    g_enManager[0] = Word(g_enManagerTable);
    g_enEffect[0] = Word(g_enEffectTable);
    g_startNamedOurs = mode == registry::Mode::Ours;
    return mode != registry::Mode::Compare || EnHookForce();
}

bool InstallMovePlace(registry::Mode mode)
{
    if (!game::IsBuild641() ||
        !(g_SC4Effect_MovePlace || Detour(addr::SC4Effect_MovePlace, &TEnMove, &g_SC4Effect_MovePlace)))
        return false;
    g_moveMode = mode == registry::Mode::Compare ? StepMode::Compare : StepMode::Ours;
    g_movePlaceOurs = mode == registry::Mode::Ours;
    log::Write("water effects: the move of a place by a description's list %s",
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return mode != registry::Mode::Compare || EnHookForce();
}

const bool g_registeredStepB1 = registry::Register({
    "renderer.effects.step_b1",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StepFlagB1,
    "The end of a particle's other step for descriptions with bit 0 of +4 (0x005B8020): the particle ends when a "
    "building or a prop is within one of its place (the occupant manager's box); compare mode runs ours and the "
    "game's on the same particle, OPENSC4_EFFECT_FORCE_CALLEES=1 forces it on copies of each running effect's first "
    "particle (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallStepB1,
});

const bool g_registeredStartNamed = registry::Register({
    "renderer.effects.start_named",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_StartNamed,
    "The start of another effect named by the effect's name and a suffix at a place (0x005B8290; \"_water\", "
    "\"_terrain\", the starts at set ages and at birth); compare mode runs ours and the game's dry with the effects "
    "manager made up, each answer of its make, then the game's for real; OPENSC4_EFFECT_FORCE_CALLEES=1 forces it "
    "on every running effect with made-up suffixes (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallStartNamed,
});

const bool g_registeredMovePlace = registry::Register({
    "renderer.effects.move_place",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Effect_MovePlace,
    "The move of a particle's place by the sines of a description's list at +0x108 (0x005AC070), for the particle "
    "draws and the second class's block; compare mode runs ours on a copy of the place, "
    "OPENSC4_EFFECT_FORCE_CALLEES=1 forces it on every running effect with made-up lists (docs/renderer.md, "
    "\"Water effects, sky and clouds\")",
    true,
    InstallMovePlace,
});

// --- The pool's three parts -------------------------------------------------------------------
//
// The pool's grow (0x005A61B0) and allocate (0x005A6290) and a particle's
// clear (0x005B9DF0), written by hand in effects_pool.h. Each has a switch:
// ours makes the ported callers call ours (the takes of the three effect
// classes; the database files' records, packed_file_3.cpp and
// save_file_2.cpp, reach the allocate by its address, so through the
// detour) and the detour run ours for the game's callers.
//
// The clear writes only the particle: compare mode runs ours on a copy and
// the game's on the particle. The allocate is compared on the pool: ours,
// the pool's four words put back, then the game's; when the list is empty
// the game's runs first and its grow is replayed for ours (the chunks as
// the game's left them, the free list the block it answered), recorded as
// <id>.grown. The grow cannot run twice (each takes memory): the game's runs
// for real, the pool's words are put back and ours runs with its
// allocation answered by the game's chunk. So that the bytes neither writes
// still count, a block of the chunk's size is first taken from malloc,
// filled with 0xA5 and given back: when the game's grow gets that block
// (the usual case), ours runs on the chunk filled again with 0xA5 and the
// chunk is compared up to the end of its last block (what follows may hold
// the heap's own words); otherwise ours runs on what the game's left and the
// whole chunk is compared, recorded as <id>.reused.
//
// OPENSC4_EFFECT_FORCE_POOL=1 (compare mode only) compares each switch in
// compare mode before every running effect's draw on made-up pools and
// particles (PlForce).
StepMode g_growMode = StepMode::Original, g_allocateMode = StepMode::Original, g_clearMode = StepMode::Original;
Call0Fn g_SC4Pool_Grow = nullptr, g_SC4Pool_Allocate = nullptr, g_SC4Particle_Clear = nullptr;
bool g_plForce = false;

std::string PlText(const uint32_t* words, uint32_t value, const char* what, const char* bytes, std::size_t at)
{
    char text[128];
    std::snprintf(text, sizeof text, "pool %08X %08X %08X %08X, %s %08X", words[0], words[1], words[2], words[3], what,
                  value);
    std::string result = text;
    if (bytes) {
        std::snprintf(text, sizeof text, ", at +%zX ", at);
        result += text + Hex(bytes + at, 16);
    }
    return result;
}

// The grow compared on `pool` (the game's chunk kept).
void PlGrowCompared(const char* id, char* pool)
{
    uint32_t before[4], after[4], mine[4];
    std::memcpy(before, pool, sizeof before);
    const uint32_t bytes = before[1] + 8;
    char* const probe = PlMalloc(bytes);
    if (probe) {
        std::memset(probe, 0xA5, bytes);
        PlFree(probe);
    }
    g_SC4Pool_Grow(pool, nullptr);
    std::memcpy(after, pool, sizeof after);
    char* const chunk = reinterpret_cast<char*>(after[2]);
    const std::vector<char> game(chunk, chunk + bytes);
    const bool filled = probe && chunk == probe && (before[0] <= 4 || static_cast<uint8_t>(chunk[8]) == 0xA5);
    const std::size_t compared = filled ? 4 + before[1] / before[0] * before[0] : bytes;
    if (filled)
        std::memset(chunk, 0xA5, bytes);
    std::memcpy(pool, before, sizeof before);
    uint32_t asked = 0;
    PlGrowWith(pool, [&](uint32_t size) {
        asked = size;
        return chunk;
    });
    std::memcpy(mine, pool, sizeof mine);
    std::size_t at = 0;
    while (at < compared && chunk[at] == game[at])
        ++at;
    const bool same = asked == bytes && std::memcmp(mine, after, sizeof mine) == 0 && at == compared;
    std::string gameText, ourText;
    if (!same) {
        at = at < compared ? at & ~std::size_t{3} : 0;
        gameText = PlText(after, bytes, "asked", game.data(), at);
        ourText = PlText(mine, asked, "asked", chunk, at);
    }
    std::memcpy(chunk, game.data(), bytes);
    std::memcpy(pool, after, sizeof after);
    compare::Record(filled ? id : (std::string(id) + ".reused").c_str(), same, gameText, ourText);
}

// The allocate compared on `pool`; answers the game's block.
uint32_t PlAllocateCompared(const char* id, char* pool)
{
    uint32_t before[4], after[4], mine[4];
    std::memcpy(before, pool, sizeof before);
    bool grew = false;
    uint32_t ours = 0, result = 0;
    if (before[3]) {
        ours = PlAllocateWith(pool, [&](char*) { grew = true; });
        std::memcpy(mine, pool, sizeof mine);
        std::memcpy(pool, before, sizeof before);
        result = g_SC4Pool_Allocate(pool, nullptr);
        std::memcpy(after, pool, sizeof after);
    } else {
        result = g_SC4Pool_Allocate(pool, nullptr);
        std::memcpy(after, pool, sizeof after);
        std::memcpy(pool, before, sizeof before);
        ours = PlAllocateWith(pool, [&](char* grown) {
            grew = true;
            At<uint32_t>(grown, 8) = after[2];
            At<uint32_t>(grown, 0xC) = result;
        });
        std::memcpy(mine, pool, sizeof mine);
        std::memcpy(pool, after, sizeof after);
    }
    const bool same = grew == !before[3] && ours == result && std::memcmp(mine, after, sizeof mine) == 0;
    compare::Record(before[3] ? id : (std::string(id) + ".grown").c_str(), same,
                    same ? std::string() : PlText(after, result, before[3] ? "block" : "grown, block", nullptr, 0),
                    same ? std::string() : PlText(mine, ours, grew ? "grown, block" : "block", nullptr, 0));
    return result;
}

// The clear compared: ours on a copy, the game's on the particle.
uint32_t PlClearCompared(const char* id, char* particle)
{
    char mine[kParticleBytes];
    std::memcpy(mine, particle, sizeof mine);
    const char* const answer = PlClear(mine);
    const uint32_t result = g_SC4Particle_Clear(particle, nullptr);
    const bool same = answer == mine && result == Word(particle) && std::memcmp(mine, particle, sizeof mine) == 0;
    compare::Record(id, same, same ? std::string() : Hex(particle, sizeof mine),
                    same ? std::string() : Hex(mine, sizeof mine) + (answer == mine ? "" : " (answer not the particle)"));
    return result;
}

uint32_t __fastcall PlGrowByMode(char* pool, void*)
{
    if (g_growMode == StepMode::Ours)
        return PlGrow(pool);
    if (g_growMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4Pool_Grow(pool, nullptr);
    PlGrowCompared("effects.grow", pool);
    return 0;
}
OPENSC4_THUNK(TPlGrow, PlGrowByMode, 0)

uint32_t __fastcall PlAllocateByMode(char* pool, void*)
{
    if (g_allocateMode == StepMode::Ours)
        return PlAllocateWith(pool, [](char* grown) { PlGrowFor(Word(grown)); });
    if (g_allocateMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4Pool_Allocate(pool, nullptr);
    return PlAllocateCompared("effects.allocate", pool);
}
OPENSC4_THUNK(TPlAllocate, PlAllocateByMode, 0)

uint32_t __fastcall PlClearByMode(char* particle, void*)
{
    if (g_clearMode == StepMode::Ours)
        return Word(PlClear(particle));
    if (g_clearMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4Particle_Clear(particle, nullptr);
    return PlClearCompared("effects.clear", particle);
}
OPENSC4_THUNK(TPlClear, PlClearByMode, 0)

// Forcing, before each running effect's draw (DwEffectDrawByMode): for
// each switch in compare mode, in turn of a counter (k).
// - The grow: a made-up pool of blocks of 4 to 0x54 bytes, chunks of one
//   to 64 of them and what is left of 0, 1 or a block less one byte; the
//   chunk the game's took is given back after.
// - The allocate: a made-up pool of 0x10-byte blocks with a list of three
//   (from the first, the second or the last) or none, which grows (the
//   chunk given back after).
// - The clear: copies of the first running particle as it is and filled
//   with 0xA5, 0xFF and counting bytes.
void PlForce(char* self)
{
    if (!g_plForce || g_effectRecord || g_effectReplay || !At<uint8_t>(self, 9))
        return;
    static uint32_t turn = 0;
    const uint32_t k = turn++;
    constexpr uint32_t kMadeUpChunks = 0x0BADC0DEu;
    if (g_growMode == StepMode::Compare) {
        static const uint32_t sizes[7] = {4, 8, 0xC, 0x10, 0x30, 0x44, 0x54};
        static const uint32_t counts[5] = {1, 2, 3, 7, 64};
        const uint32_t size = sizes[k % 7], count = counts[k / 7 % 5];
        const uint32_t left[3] = {0, 1, size - 1};
        uint32_t pool[4] = {size, size * count + left[k / 35 % 3], kMadeUpChunks, 0xFFFFFFFFu};
        PlGrowCompared("effects.grow.forced", reinterpret_cast<char*>(pool));
        if (pool[2] != kMadeUpChunks)
            PlFree(reinterpret_cast<void*>(pool[2]));
    }
    if (g_allocateMode == StepMode::Compare) {
        uint32_t blocks[3][4] = {};
        for (uint32_t i = 0; i < 3; ++i)
            blocks[i][0] = i < 2 ? Word(blocks[i + 1]) : 0;
        const uint32_t from = k % 4;
        uint32_t pool[4] = {0x10, 0x10 * (1 + k / 4 % 3), kMadeUpChunks, from < 3 ? Word(blocks[from]) : 0};
        PlAllocateCompared("effects.allocate.forced", reinterpret_cast<char*>(pool));
        if (pool[2] != kMadeUpChunks)
            PlFree(reinterpret_cast<void*>(pool[2]));
    }
    if (g_clearMode == StepMode::Compare) {
        char* particle = nullptr;
        if (At<char*>(self, 0x14))
            for (char* node = At<char*>(At<char*>(self, 0x14), 0); node != At<char*>(self, 0x14) && !particle;
                 node = At<char*>(node, 0)) {
                if (At<uint8_t>(At<char*>(node, 8), 0))
                    particle = At<char*>(node, 8);
            }
        char copy[kParticleBytes];
        const uint32_t kind = k % 4;
        if (kind == 0 && particle)
            std::memcpy(copy, particle, sizeof copy);
        else if (kind == 3)
            for (std::size_t i = 0; i < sizeof copy; ++i)
                copy[i] = static_cast<char>(i + 1);
        else
            std::memset(copy, kind == 2 ? 0xFF : 0xA5, sizeof copy);
        PlClearCompared("effects.clear.forced", copy);
    }
}

bool PlHookForce()
{
    const char* v = std::getenv("OPENSC4_EFFECT_FORCE_POOL");
    if (!v || *v != '1')
        return true;
    if (!DwHookEffectDraw())
        return false;
    if (!g_plForce)
        log::Write("water effects: the pool's grow and allocate and the particle's clear forced before each effect's draw");
    g_plForce = true;
    return true;
}

bool InstallGrow(registry::Mode mode)
{
    if (!SuInstall(mode, g_growMode, addr::SC4Pool_Grow, &TPlGrow, &g_SC4Pool_Grow, "the pool's grow"))
        return false;
    g_growOurs = mode == registry::Mode::Ours;
    return mode != registry::Mode::Compare || PlHookForce();
}

bool InstallAllocate(registry::Mode mode)
{
    if (!SuInstall(mode, g_allocateMode, addr::SC4Pool_Allocate, &TPlAllocate, &g_SC4Pool_Allocate,
                   "the pool's allocate"))
        return false;
    return mode != registry::Mode::Compare || PlHookForce();
}

bool InstallClear(registry::Mode mode)
{
    if (!SuInstall(mode, g_clearMode, addr::SC4Particle_Clear, &TPlClear, &g_SC4Particle_Clear,
                   "a particle's clear"))
        return false;
    g_clearOurs = mode == registry::Mode::Ours;
    return mode != registry::Mode::Compare || PlHookForce();
}

const bool g_registeredGrow = registry::Register({
    "renderer.effects.grow",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Pool_Grow,
    "The grow of a pool of fixed-size blocks (0x005A61B0): a chunk from malloc cut into blocks linked as the free "
    "list (the particle pools, the database files' records); compare mode runs the game's for real and ours on its "
    "chunk, OPENSC4_EFFECT_FORCE_POOL=1 forces it on made-up pools before each running effect's draw "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallGrow,
});

const bool g_registeredAllocate = registry::Register({
    "renderer.effects.allocate",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Pool_Allocate,
    "The allocate of a pool of fixed-size blocks (0x005A6290): the first block of the free list, grown when empty "
    "(the database files' records); compare mode runs ours and the game's on the same pool, "
    "OPENSC4_EFFECT_FORCE_POOL=1 forces it on made-up pools before each running effect's draw (docs/renderer.md, "
    "\"Water effects, sky and clouds\")",
    true,
    InstallAllocate,
});

const bool g_registeredClear = registry::Register({
    "renderer.effects.clear",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Particle_Clear,
    "A particle's clear (0x005B9DF0): its first 0x1C bytes zeroed, for the takes of the three effect classes; "
    "compare mode runs ours on a copy, OPENSC4_EFFECT_FORCE_POOL=1 forces it on made-up particles before each "
    "running effect's draw (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallClear,
});

// --- The index batch's three parts ------------------------------------------------------------
//
// The index batch's clear (0x005EC4A0), add of quads' indices (0x005EC850)
// and add of quads' vertices (0x005EC8C0), written by hand in
// effects_index_batch.h. Each has a switch: ours makes the ported draws
// (effects_particles_draw.h, effects_third.h) call ours and the detour run
// ours for the game's callers. The batch's draw (0x005EC5D0) is still the
// game's.
//
// The clear writes only the batch: compare mode runs ours on a copy and the
// game's on the batch. The adds may resize a vector of the batch (memory
// taken and given back), so they cannot run twice: the game's runs for real,
// with the batch's fill function (when it has one) swapped for one that
// notes its arguments and calls it; ours runs on a copy of the batch as it
// was, with the resize noted and answered by the vector's words as the
// game's left them, and the fill only noted. Compared: the batch's 0x40
// bytes, the answer, whether and to what size the vector was resized (and,
// with no fill, the value against the game's first new element) and the
// fills' arguments; a resize is recorded as <id>.grown.
//
// OPENSC4_EFFECT_FORCE_BATCH=1 (compare mode only) compares each switch in
// compare mode before every running effect's draw on made-up batches
// (IbForce).
StepMode g_batchClearMode = StepMode::Original, g_batchAddQuadsMode = StepMode::Original,
         g_batchAddVerticesMode = StepMode::Original;
Call0Fn g_SC4IndexBatch_Clear = nullptr;
Call1Fn g_SC4IndexBatch_AddQuads = nullptr, g_SC4IndexBatch_AddVertices = nullptr;
bool g_ibForce = false;

constexpr std::size_t kIndexBatchBytes = 0x40;

// The game's fills while an add is compared: the real fill and its calls.
uint32_t g_ibFillReal = 0;
std::vector<std::array<uint32_t, 4>>* g_ibFillNotes = nullptr;

void __cdecl IbFillNoted(uint32_t begin, uint32_t from, uint32_t to)
{
    g_ibFillNotes->push_back({g_ibFillReal, begin, from, to});
    IbFill(g_ibFillReal, begin, from, to);
}

std::string IbText(const char* batch, uint32_t answer, bool resized, uint32_t at, uint32_t size, uint32_t value,
                   const std::vector<std::array<uint32_t, 4>>& fills)
{
    char text[96];
    std::snprintf(text, sizeof text, "answer %08X, ", answer);
    std::string result = text + Hex(batch, kIndexBatchBytes);
    if (resized) {
        std::snprintf(text, sizeof text, ", resized +%X to %X with %X", at, size, value);
        result += text;
    }
    for (const auto& fill : fills) {
        std::snprintf(text, sizeof text, ", fill %08X (%08X, %X, %X)", fill[0], fill[1], fill[2], fill[3]);
        result += text;
    }
    return result;
}

// An add compared on `batch` (the indices' when `indices`, else the
// vertices'); answers the game's.
uint32_t IbAddCompared(const char* id, char* batch, uint32_t quads, bool indices)
{
    const std::size_t vector = indices ? 0x2C : 0x14, fillAt = indices ? 0x3C : 0x28, element = indices ? 2 : 1;
    char before[kIndexBatchBytes], after[kIndexBatchBytes], mine[kIndexBatchBytes];
    std::memcpy(before, batch, sizeof before);
    std::vector<std::array<uint32_t, 4>> gameFills, ourFills;
    const uint32_t realFill = At<uint32_t>(batch, fillAt);
    const uint32_t outerFill = g_ibFillReal;
    auto* const outerNotes = g_ibFillNotes;
    g_ibFillReal = realFill;
    g_ibFillNotes = &gameFills;
    if (realFill)
        At<uint32_t>(batch, fillAt) = reinterpret_cast<uint32_t>(&IbFillNoted);
    const uint32_t result = (indices ? g_SC4IndexBatch_AddQuads : g_SC4IndexBatch_AddVertices)(batch, nullptr, quads);
    At<uint32_t>(batch, fillAt) = realFill;
    g_ibFillReal = outerFill;
    g_ibFillNotes = outerNotes;
    std::memcpy(after, batch, sizeof after);
    const bool grew = std::memcmp(before + vector, after + vector, 12) != 0;
    const auto elements = [&](char* words) {
        const int32_t bytes = static_cast<int32_t>(At<uint32_t>(words, vector + 4) - At<uint32_t>(words, vector));
        return static_cast<uint32_t>(indices ? bytes >> 1 : bytes);
    };

    std::memcpy(mine, before, sizeof mine);
    bool resized = false;
    uint32_t resizedAt = 0, resizedTo = 0, resizedWith = 0;
    const auto resize = [&](char* words, uint32_t size, auto value) {
        resized = true;
        resizedAt = static_cast<uint32_t>(words - mine);
        resizedTo = size;
        resizedWith = value;
        if (grew)
            std::memcpy(mine + vector, after + vector, 12);
    };
    const auto fill = [&](uint32_t fn, uint32_t begin, uint32_t from, uint32_t to) {
        ourFills.push_back({fn, begin, from, to});
    };
    const uint32_t ours =
        indices ? IbAddQuadsWith(mine, quads, resize, fill) : IbAddVerticesWith(mine, quads, resize, fill);

    // The game's resize gave the new size and, with no fill after it, the
    // value of the new elements.
    const uint32_t gameTo = grew ? elements(after) : 0;
    uint32_t gameWith = resizedWith;
    if (grew && !realFill && gameTo > elements(before)) {
        const uint32_t first = At<uint32_t>(after, vector) + elements(before) * element;
        gameWith = indices ? M<uint16_t>(first) : M<uint8_t>(first);
    }
    const bool same = resized == grew && (!grew || (resizedAt == vector && resizedTo == gameTo && resizedWith == gameWith)) &&
                      ours == result && gameFills == ourFills && std::memcmp(mine, after, sizeof mine) == 0;
    compare::Record(grew ? (std::string(id) + ".grown").c_str() : id, same,
                    same ? std::string() : IbText(after, result, grew, static_cast<uint32_t>(vector), gameTo, gameWith, gameFills),
                    same ? std::string() : IbText(mine, ours, resized, resizedAt, resizedTo, resizedWith, ourFills));
    return result;
}

// The clear compared: ours on a copy, the game's on the batch.
uint32_t IbClearCompared(const char* id, char* batch)
{
    char mine[kIndexBatchBytes];
    std::memcpy(mine, batch, sizeof mine);
    const uint32_t answer = IbClear(mine);
    const uint32_t result = g_SC4IndexBatch_Clear(batch, nullptr);
    const bool same = answer == result && std::memcmp(mine, batch, sizeof mine) == 0;
    compare::Record(id, same, same ? std::string() : Hex(batch, sizeof mine),
                    same ? std::string() : Hex(mine, sizeof mine));
    return result;
}

uint32_t __fastcall IbClearByMode(char* batch, void*)
{
    if (g_batchClearMode == StepMode::Ours)
        return IbClear(batch);
    if (g_batchClearMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4IndexBatch_Clear(batch, nullptr);
    return IbClearCompared("effects.batch_clear", batch);
}
OPENSC4_THUNK(TIbClear, IbClearByMode, 0)

uint32_t __fastcall IbAddQuadsByMode(char* batch, void*, uint32_t quads)
{
    if (g_batchAddQuadsMode == StepMode::Ours)
        return IbAddQuads(batch, quads);
    if (g_batchAddQuadsMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4IndexBatch_AddQuads(batch, nullptr, quads);
    return IbAddCompared("effects.batch_add_quads", batch, quads, true);
}
OPENSC4_THUNK(TIbAddQuads, IbAddQuadsByMode, 4)

uint32_t __fastcall IbAddVerticesByMode(char* batch, void*, uint32_t quads)
{
    if (g_batchAddVerticesMode == StepMode::Ours)
        return IbAddVertices(batch, quads);
    if (g_batchAddVerticesMode == StepMode::Original || g_effectRecord || g_effectReplay)
        return g_SC4IndexBatch_AddVertices(batch, nullptr, quads);
    return IbAddCompared("effects.batch_add_vertices", batch, quads, false);
}
OPENSC4_THUNK(TIbAddVertices, IbAddVerticesByMode, 4)

// --- The index batch's draw ------------------------------------------------------------------
//
// renderer.effects.batch_draw: the batch's draw (0x005EC5D0) ours
// (effects_index_batch.h), called by ours from the ported draws and, for the
// game's callers, through the detour the particle draws' comparisons already
// have of it (TPdBatchDraw: it notes the draw as their callee while they
// note, and otherwise goes by this switch's mode).
//
// Compare mode: the draw only calls the render context and lowers the
// batch's counts. The game's runs on the batch with the context's three
// (0x007D2970, 0x007D29F0, 0x007FC2D0) noted by detours of their own and
// passed on; ours runs on a copy of the batch as it was, with the three only
// noted. Compared: the calls (which, on what, with what, in turn) and the
// batch's 0x40 bytes after.
//
// OPENSC4_EFFECT_FORCE_BATCH=1 (compare mode only) also compares it before
// every running effect's draw on made-up batches with a made-up context,
// whose calls are only noted (IbForce).
StepMode g_batchDrawMode = StepMode::Original;
Call3Fn g_IbSetVertexArray = nullptr, g_IbDrawElements = nullptr, g_IbDrawVertexRange = nullptr;

struct IbCall {
    uint32_t target, context, args[3];
    bool operator==(const IbCall& other) const
    {
        return target == other.target && context == other.context && args[0] == other.args[0] &&
               args[1] == other.args[1] && args[2] == other.args[2];
    }
};

// The game's draw's calls while it is compared, on this thread; when dry,
// not passed on.
std::vector<IbCall>* g_ibCalls = nullptr;
bool g_ibDry = false;
DWORD g_ibThread = 0;

uint32_t IbContextNoted(uint32_t target, Call3Fn real, char* context, uint32_t a, uint32_t b, uint32_t c)
{
    if (!g_ibCalls || GetCurrentThreadId() != g_ibThread)
        return real(context, nullptr, a, b, c);
    g_ibCalls->push_back({target, Word(context), {a, b, c}});
    return g_ibDry ? 0u : real(context, nullptr, a, b, c);
}

uint32_t __fastcall IbSetVertexArrayNoted(char* self, void*, uint32_t a, uint32_t b, uint32_t c)
{
    return IbContextNoted(addr::SC4RenderContext_SetVertexArray, g_IbSetVertexArray, self, a, b, c);
}
OPENSC4_THUNK(TIbSetVertexArray, IbSetVertexArrayNoted, 12)

uint32_t __fastcall IbDrawElementsNoted(char* self, void*, uint32_t a, uint32_t b, uint32_t c)
{
    return IbContextNoted(addr::SC4RenderContext_DrawElements, g_IbDrawElements, self, a, b, c);
}
OPENSC4_THUNK(TIbDrawElements, IbDrawElementsNoted, 12)

uint32_t __fastcall IbDrawVertexRangeNoted(char* self, void*, uint32_t a, uint32_t b, uint32_t c)
{
    return IbContextNoted(addr::SC4RenderContext_DrawVertexRange, g_IbDrawVertexRange, self, a, b, c);
}
OPENSC4_THUNK(TIbDrawVertexRange, IbDrawVertexRangeNoted, 12)

// The batch and the calls from the first one that differs (at most four of
// them).
std::string IbDrawText(const char* batch, const std::vector<IbCall>& calls, std::size_t from)
{
    char text[96];
    std::snprintf(text, sizeof text, "%zu calls, ", calls.size());
    std::string result = text + Hex(batch, kIndexBatchBytes);
    for (std::size_t i = from; i < calls.size() && i < from + 4; ++i) {
        const IbCall& call = calls[i];
        std::snprintf(text, sizeof text, ", #%zu %08X on %08X (%X, %X, %X)", i, call.target, call.context,
                      call.args[0], call.args[1], call.args[2]);
        result += text;
    }
    return result;
}

// The draw compared on `batch` with `context` (whose calls are not passed on
// when `dry`); answers the game's.
uint32_t IbDrawCompared(const char* id, char* batch, uint32_t context, bool dry)
{
    char mine[kIndexBatchBytes];
    std::memcpy(mine, batch, sizeof mine);
    std::vector<IbCall> gameCalls, ourCalls;
    auto* const outerCalls = g_ibCalls;
    const bool outerDry = g_ibDry;
    g_ibCalls = &gameCalls;
    g_ibDry = dry;
    g_ibThread = GetCurrentThreadId();
    const uint32_t result = g_PdBatchDraw(batch, nullptr, context);
    g_ibCalls = outerCalls;
    g_ibDry = outerDry;

    const auto noted = [&](uint32_t target) {
        return [&, target](uint32_t a, uint32_t b, uint32_t c) { ourCalls.push_back({target, context, {a, b, c}}); };
    };
    IbDrawWith(mine, noted(addr::SC4RenderContext_SetVertexArray), noted(addr::SC4RenderContext_DrawElements),
               noted(addr::SC4RenderContext_DrawVertexRange));
    const bool same = gameCalls == ourCalls && std::memcmp(mine, batch, sizeof mine) == 0;
    std::size_t from = 0;
    while (!same && from < gameCalls.size() && from < ourCalls.size() && gameCalls[from] == ourCalls[from])
        ++from;
    compare::Record(id, same, same ? std::string() : IbDrawText(batch, gameCalls, from),
                    same ? std::string() : IbDrawText(mine, ourCalls, from));
    return result;
}

uint32_t IbDrawByMode(char* batch, uint32_t context)
{
    if (g_batchDrawMode == StepMode::Ours) {
        IbDraw(batch, Ptr(context));
        return 0;
    }
    if (g_batchDrawMode != StepMode::Compare || g_effectRecord || g_effectReplay)
        return g_PdBatchDraw(batch, nullptr, context);
    return IbDrawCompared("effects.batch_draw", batch, context, false);
}

// A made-up fill for the forced batches: each 16-bit index (the indices')
// or byte (the vertices') from `from` to `to` gets its number and a mark.
void __cdecl IbFillIndicesMadeUp(uint32_t begin, uint32_t from, uint32_t to)
{
    for (uint32_t i = from; i < to; ++i)
        M<uint16_t>(begin + i * 2) = static_cast<uint16_t>(i ^ 0x5A00u);
}

void __cdecl IbFillVerticesMadeUp(uint32_t begin, uint32_t from, uint32_t to)
{
    for (uint32_t i = from; i < to; ++i)
        M<uint8_t>(begin + i) = static_cast<uint8_t>(i ^ 0xC3u);
}

// Forcing, before each running effect's draw (DwEffectDrawByMode): for
// each switch in compare mode, in turn of a counter (k).
// - The clear: the effect's own batch (+0x28) copied, and made-up ones
//   filled with 0xA5, 0xFF and counting bytes.
// - The adds: a made-up batch (counting bytes) of 4, 3 or 1 vertices of
//   0x20, 0x18 or 0x1C bytes and 6, 4 or 3 indices to a quad, counts of 0, 5
//   or 0x600, a made-up fill or none, adding 0, 1, 2, 7 or 64 quads three
//   times: to empty vectors (the resize takes memory), after the vectors are
//   cut to half their size (it grows in place) and after the counts are
//   zeroed (no resize unless it adds more than the first did). The vectors'
//   memory is given back after (0x0090CF63).
// - The draw: a made-up batch (counting bytes) with a primitive of 0 to 6,
//   its indices at 0, near the top of memory or at their counting bytes,
//   and counts of 0, 1, 2, 0x5FF, 0x600, 0x601, 0xBFF, 0xC00, 0xC01, 0x1801
//   or 0x3000 vertices and (every other turn) indices, drawn dry with a
//   made-up context; one turn in 4,999 with 0x80000001 indices or vertices
//   (above 0x600 only unsigned).
void IbForce(char* self)
{
    if (!g_ibForce || g_effectRecord || g_effectReplay || !At<uint8_t>(self, 9))
        return;
    static uint32_t turn = 0;
    const uint32_t k = turn++;
    if (g_batchClearMode == StepMode::Compare) {
        char batch[kIndexBatchBytes];
        const uint32_t kind = k % 4;
        if (kind == 0)
            std::memcpy(batch, self + 0x28, sizeof batch);
        else if (kind == 3)
            for (std::size_t i = 0; i < sizeof batch; ++i)
                batch[i] = static_cast<char>(i + 1);
        else
            std::memset(batch, kind == 2 ? 0xFF : 0xA5, sizeof batch);
        IbClearCompared("effects.batch_clear.forced", batch);
    }
    for (const bool indices : {true, false}) {
        if ((indices ? g_batchAddQuadsMode : g_batchAddVerticesMode) != StepMode::Compare)
            continue;
        static const uint32_t perQuad[2][3] = {{6, 4, 3}, {4, 3, 1}};
        static const uint32_t strides[3] = {0x20, 0x18, 0x1C};
        static const uint32_t counts[3] = {0, 5, 0x600};
        static const uint32_t quads[5] = {0, 1, 2, 7, 64};
        char batch[kIndexBatchBytes];
        for (std::size_t i = 0; i < sizeof batch; ++i)
            batch[i] = static_cast<char>(0x40 + i);
        const std::size_t vector = indices ? 0x2C : 0x14, count = indices ? 0x38 : 0x24, fillAt = indices ? 0x3C : 0x28;
        At<uint32_t>(batch, indices ? 0xC : 8) = perQuad[indices ? 0 : 1][k % 3];
        At<uint32_t>(batch, 0x20) = strides[k / 3 % 3];
        At<uint32_t>(batch, count) = counts[k / 9 % 3];
        At<uint32_t>(batch, fillAt) =
            k / 27 % 2 ? reinterpret_cast<uint32_t>(indices ? &IbFillIndicesMadeUp : &IbFillVerticesMadeUp) : 0;
        for (const std::size_t words : {vector, vector + 4, vector + 8})
            At<uint32_t>(batch, words) = 0;
        const char* const id = indices ? "effects.batch_add_quads.forced" : "effects.batch_add_vertices.forced";
        IbAddCompared(id, batch, quads[k / 54 % 5], indices);
        const uint32_t half = (At<uint32_t>(batch, vector + 4) - At<uint32_t>(batch, vector)) / 2;
        At<uint32_t>(batch, vector + 4) = At<uint32_t>(batch, vector) + (indices ? half & ~1u : half);
        IbAddCompared(id, batch, quads[(k / 54 + 1) % 5], indices);
        At<uint32_t>(batch, count) = 0;
        IbAddCompared(id, batch, quads[(k / 54 + 2) % 5], indices);
        if (At<uint32_t>(batch, vector))
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(Ptr(At<uint32_t>(batch, vector)));
    }
    if (g_batchDrawMode == StepMode::Compare) {
        static const uint32_t counts[11] = {0, 1, 2, 0x5FF, 0x600, 0x601, 0xBFF, 0xC00, 0xC01, 0x1801, 0x3000};
        static char context[0x40];
        char batch[kIndexBatchBytes];
        for (std::size_t i = 0; i < sizeof batch; ++i)
            batch[i] = static_cast<char>(0x60 + i);
        At<uint32_t>(batch, 4) = k % 7;
        if (k % 3 != 2)
            At<uint32_t>(batch, 0x2C) = k % 3 ? 0xFFFFF800u : 0u;
        At<uint32_t>(batch, 0x38) = k % 2 ? 0u : counts[k / 2 % 11];
        At<uint32_t>(batch, 0x24) = counts[k / 22 % 11];
        if (k % 4999 == 4998)
            At<uint32_t>(batch, k / 4999 % 2 ? 0x24 : 0x38) = 0x80000001u;
        IbDrawCompared("effects.batch_draw.forced", batch, Word(context), true);
    }
}

bool IbHookForce()
{
    const char* v = std::getenv("OPENSC4_EFFECT_FORCE_BATCH");
    if (!v || *v != '1')
        return true;
    if (!DwHookEffectDraw())
        return false;
    if (!g_ibForce)
        log::Write("water effects: the index batch's clear, adds and draw forced before each effect's draw");
    g_ibForce = true;
    return true;
}

bool InstallBatchClear(registry::Mode mode)
{
    if (!SuInstall(mode, g_batchClearMode, addr::SC4IndexBatch_Clear, &TIbClear, &g_SC4IndexBatch_Clear,
                   "the index batch's clear"))
        return false;
    g_batchClearOurs = mode == registry::Mode::Ours;
    return mode != registry::Mode::Compare || IbHookForce();
}

bool InstallBatchAddQuads(registry::Mode mode)
{
    if (!SuInstall(mode, g_batchAddQuadsMode, addr::SC4IndexBatch_AddQuads, &TIbAddQuads, &g_SC4IndexBatch_AddQuads,
                   "the index batch's add of quads"))
        return false;
    g_batchAddQuadsOurs = mode == registry::Mode::Ours;
    return mode != registry::Mode::Compare || IbHookForce();
}

bool InstallBatchDraw(registry::Mode mode)
{
    if (mode == registry::Mode::Compare &&
        !((g_IbSetVertexArray ||
           Detour(addr::SC4RenderContext_SetVertexArray, &TIbSetVertexArray, &g_IbSetVertexArray)) &&
          (g_IbDrawElements || Detour(addr::SC4RenderContext_DrawElements, &TIbDrawElements, &g_IbDrawElements)) &&
          (g_IbDrawVertexRange ||
           Detour(addr::SC4RenderContext_DrawVertexRange, &TIbDrawVertexRange, &g_IbDrawVertexRange))))
        return false;
    if (!SuInstall(mode, g_batchDrawMode, addr::SC4IndexBatch_Draw, &TPdBatchDraw, &g_PdBatchDraw,
                   "the index batch's draw"))
        return false;
    g_batchDrawOurs = mode == registry::Mode::Ours;
    return mode != registry::Mode::Compare || IbHookForce();
}

bool InstallBatchAddVertices(registry::Mode mode)
{
    if (!SuInstall(mode, g_batchAddVerticesMode, addr::SC4IndexBatch_AddVertices, &TIbAddVertices,
                   &g_SC4IndexBatch_AddVertices, "the index batch's add of vertices"))
        return false;
    g_batchAddVerticesOurs = mode == registry::Mode::Ours;
    return mode != registry::Mode::Compare || IbHookForce();
}

const bool g_registeredBatchClear = registry::Register({
    "renderer.effects.batch_clear",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4IndexBatch_Clear,
    "The index batch's clear (0x005EC4A0): its vertex and index counts zeroed, before the particle draws fill it; "
    "compare mode runs ours on a copy, OPENSC4_EFFECT_FORCE_BATCH=1 forces it on made-up batches before each "
    "running effect's draw (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallBatchClear,
});

const bool g_registeredBatchAddQuads = registry::Register({
    "renderer.effects.batch_add_quads",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4IndexBatch_AddQuads,
    "The index batch's add of quads' indices (0x005EC850): the index count raised, the indices resized and filled "
    "when short; compare mode runs the game's for real and ours on a copy with its resize answered by the game's, "
    "OPENSC4_EFFECT_FORCE_BATCH=1 forces it on made-up batches before each running effect's draw "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallBatchAddQuads,
});

const bool g_registeredBatchAddVertices = registry::Register({
    "renderer.effects.batch_add_vertices",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4IndexBatch_AddVertices,
    "The index batch's add of quads' vertices (0x005EC8C0): the vertex count raised, the vertices resized and "
    "filled when short; compare mode runs the game's for real and ours on a copy with its resize answered by the "
    "game's, OPENSC4_EFFECT_FORCE_BATCH=1 forces it on made-up batches before each running effect's draw "
    "(docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallBatchAddVertices,
});

const bool g_registeredBatchDraw = registry::Register({
    "renderer.effects.batch_draw",
    "renderer/effects",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4IndexBatch_Draw,
    "The index batch's draw (0x005EC5D0): its vertices handed to the render context, then its indices (else its "
    "vertices) drawn in runs of at most 0x600 from the end, the count lowered after each; compare mode runs the "
    "game's with the context's calls noted and ours on a copy with them only noted, "
    "OPENSC4_EFFECT_FORCE_BATCH=1 forces it on made-up batches with a made-up context before each running "
    "effect's draw (docs/renderer.md, \"Water effects, sky and clouds\")",
    true,
    InstallBatchDraw,
});

} // namespace
} // namespace opensc4::water_effects
