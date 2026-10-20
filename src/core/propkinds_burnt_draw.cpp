// Our cSC4PropOccupantBurntBuilding's drawing (build 641; the class map and
// the fields are in propkinds.h, the logic in propkinds_burnt.cpp): the quad
// builder, which lays a quad over the burnt building's box, a tenth of a
// metre above its floor, and the class's own functions behind the model
// interface (the virtual base at +0xD4, vtable 0x00AA49B0): the occupant,
// the box, the draw and the texture.
//
// The model interface's 19 slots are adjustor thunks (0x005EF9D0 to
// 0x005EFB10; each subtracts the displacement at +0xD0 first). They stay the
// game's: the four that end in `jmp` to the class's own functions land in
// the detours below; the other fifteen go to QueryInterface (piece 2's),
// AddRef and Release (the prop's, 0x006491C0 and 0x006491D0), the model
// part's functions shared with every model (0x00801890, 0x008018A0,
// 0x008018B0, 0x00801DC0, 0x008018E0, 0x008018F0, 0x00801E10, 0x00801E50) or
// stubs shared with other classes (0x00648F10 twice, 0x005DEB40,
// 0x004E13E0). The functions behind the thunks take the virtual base (the
// object + 0xD4, less the displacement, which is 0) as `this`.
//
// Every call below has its argument count from the callee's own `ret N`:
// the camera view's 0x007F77D0 (ret 4); the render context's 0x007D2D20,
// 0x007D3290 and 0x007D5200 (ret), 0x007D4530, 0x007D4010, 0x007D4480 and
// 0x007D4420 (ret 4), 0x007D31E0, 0x007D4070 and 0x007D4340 (ret 8),
// 0x007D41E0 (ret 0xC), 0x007D2990 (ret 0x10); cdecl 0x005E55E0 (one).
//
// Callers: the quad builder from the class's SetOccupant and Init
// (0x005EEBBC, 0x005F06E1; ours in propkinds_burnt.cpp call the same
// address), neither keeping anything in ECX or EDX across it; the four
// model functions from their thunks only. No jump or call in the exe lands
// inside the first five bytes of any of the five. The detours are plain
// functions.
#include "core/propkinds.h"

#include "core/compare.h"
#include "core/gd/gd.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace opensc4::propkinds {
namespace {

constexpr uint32_t kSixteenth = 0x3D800000;      // 1/16 (0x00A8E174)
constexpr uint32_t kMinusSixteenth = 0xBD800000; // -1/16 (0x00A8ECD0)
constexpr uint32_t kOne = 0x3F800000;

bool g_compare = false;

float FloatOf(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

// ====================================================================================
// Not virtual (`self` is the object)
// ====================================================================================

// 0x005EDC60: the quad (0x50 bytes from the game's operator new, made
// once; nothing more written to it when that fails): four vertices at the
// box's corners, (low x, low z), (high x, low z), (high x, high z), (low x,
// high z), all at the box's low y plus 0.1 (each sum the x87's, stored as a
// float); the texture coordinates are left as they are. Then the box's low
// corner, scaled by the global at 0x00B0A8D8 times 1/16, into +0xC4.
void __fastcall BuildQuad(char* self, void*)
{
    static bool logged = false;
    FirstCall("burnt BuildQuad", logged);
    if (!At<char*>(self, burnt::kQuad))
        At<char*>(self, burnt::kQuad) = Cdecl<char*>(addr::SC4_operator_new, uint32_t{burnt::kQuadSize});
    if (char* const quad = At<char*>(self, burnt::kQuad)) {
        const float y = static_cast<float>(static_cast<double>(At<float>(self, burnt::kMin + 4)) + static_cast<double>(0.1f));
        for (std::size_t vertex = 0; vertex < burnt::kQuadSize; vertex += burnt::kVertexSize)
            At<float>(quad, vertex + 4) = y;
        At<uint32_t>(quad, 0x00) = At<uint32_t>(self, burnt::kMin);
        At<uint32_t>(quad, 0x08) = At<uint32_t>(self, burnt::kMin + 8);
        At<uint32_t>(quad, 0x14) = At<uint32_t>(self, burnt::kMax);
        At<uint32_t>(quad, 0x1C) = At<uint32_t>(self, burnt::kMin + 8);
        At<uint32_t>(quad, 0x28) = At<uint32_t>(self, burnt::kMax);
        At<uint32_t>(quad, 0x30) = At<uint32_t>(self, burnt::kMax + 8);
        At<uint32_t>(quad, 0x3C) = At<uint32_t>(self, burnt::kMin);
        At<uint32_t>(quad, 0x44) = At<uint32_t>(self, burnt::kMax + 8);
    }
    std::memcpy(self + burnt::kScaled, self + burnt::kMin, 12);
    const double scale = static_cast<double>(Global<float>(addr::cSC4PropOccupantBurntBuilding_Scale)) * FloatOf(kSixteenth);
    for (std::size_t i = 0; i < 12; i += 4)
        At<float>(self, burnt::kScaled + i) = static_cast<float>(scale * static_cast<double>(At<float>(self, burnt::kScaled + i)));
}

// ====================================================================================
// The model interface (`model` is the virtual base, the object + 0xD4)
// ====================================================================================

char* SelfOf(char* model) { return model - burnt::kVirtualBase; }

// Slot 3 (0x005EFA10): the occupant part, null for a null object.
void* __fastcall GetOccupant(char* model, void*)
{
    static bool logged = false;
    FirstCall("burnt GetOccupant", logged);
    return SelfOf(model) ? SelfOf(model) + burnt::kOccupant : nullptr;
}

// Slot 8 (0x005EDC20): the prop's box (low corner, high corner) into `out`.
void __fastcall GetBox(char* model, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("burnt GetBox", logged);
    std::memcpy(out, SelfOf(model) + burnt::kMin, 24);
}

// Slot 12 (0x005EEC00, ret 8): nothing in a pass whose second argument's
// byte is set, without a quad, or when the binding the context's +0x14
// picks among the five is null. Otherwise: the view matrix, the render
// state, the colour lit (the model, the model part's colour), that
// binding's texture, states (3, 3, 0) and 0, depth offset 5; a texture
// matrix that maps the ground around the scaled corner (in the camera's
// frame, 0x007F77D0) to the texture: 1/16, -1/16 and 1/16 down the diagonal,
// the corner times -1/16, 1/16 and -1/16 in the last row; the quad (fan,
// format 2, 4 vertices); the texture matrix cleared, the colour reset, the
// render state popped (0x007D5200), depth offset 0. True.
uint32_t __fastcall Draw(char* model, void*, void* context, uint32_t pass)
{
    static bool logged = false;
    FirstCall("burnt Draw", logged);
    char* const self = SelfOf(model);
    if (static_cast<uint8_t>(pass) || !At<void*>(self, burnt::kQuad))
        return 1;
    void** const textures = At<void**>(self, burnt::kTextures);
    if (!textures[At<uint32_t>(context, 0x14)])
        return 1;
    This<void>(addr::SC4RenderContext_UpdateViewMatrix, context);
    This<void, void*>(addr::SC4RenderContext_SetRenderState, context, static_cast<void*>(self + burnt::kRenderState));
    char* const virtualBase = self ? self + burnt::kVbptr + At<int32_t>(At<char*>(self, burnt::kVbptr), 0x4) : nullptr;
    This<void, void*, void*>(addr::SC4RenderContext_SetColourLit, context, static_cast<void*>(virtualBase),
                             static_cast<void*>(self + burnt::kModelColour));
    void* const binding = At<void**>(self, burnt::kTextures)[At<uint32_t>(context, 0x14)];
    This<void, uint32_t, uint32_t>(addr::SC4RenderContext_BindTexture, context, **static_cast<uint32_t**>(binding), 0u);
    This<void, uint32_t, uint32_t, uint32_t>(addr::SC4RenderContext_SetState41E0, context, 3u, 3u, 0u);
    This<void, uint32_t>(addr::SC4RenderContext_SetState4010, context, 0u);
    This<void, int32_t>(addr::SC4RenderContext_SetDepthOffset, context, 5);
    float corner[3];
    std::memcpy(corner, self + burnt::kScaled, 12);
    This<void, float*>(addr::SC4CameraView_ToViewLocal, Global<void*>(addr::SC4_CameraView), static_cast<float*>(corner));
    uint32_t matrix[16] = {kSixteenth, 0, 0, 0, 0, kMinusSixteenth, 0, 0, 0, 0, kSixteenth, 0, 0, 0, 0, kOne};
    const float row[3] = {static_cast<float>(static_cast<double>(corner[0]) * FloatOf(kMinusSixteenth)),
                          static_cast<float>(static_cast<double>(corner[1]) * FloatOf(kSixteenth)),
                          static_cast<float>(static_cast<double>(corner[2]) * FloatOf(kMinusSixteenth))};
    std::memcpy(matrix + 12, row, 12);
    This<void, uint32_t*, uint32_t>(addr::SC4RenderContext_SetTextureMatrix, context, static_cast<uint32_t*>(matrix), 0u);
    This<void, uint32_t, uint32_t, uint32_t, void*>(addr::SC4RenderContext_DrawArrays, context, 6u, 2u, 4u, At<void*>(self, burnt::kQuad));
    This<void, uint32_t>(addr::SC4RenderContext_ClearTextureMatrix, context, 0u);
    This<void>(addr::SC4RenderContext_ResetColour, context);
    This<void>(addr::SC4RenderContext_PopRenderState, context);
    This<void, int32_t>(addr::SC4RenderContext_SetDepthOffset, context, 0);
    return 1;
}

// Slot 17 (0x005EEBD0): the first binding's texture into `out` (the
// binding's first word, read through), or null without bindings or when
// the first is null.
void __fastcall GetTexture(char* model, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("burnt GetTexture", logged);
    char* const self = SelfOf(model);
    void** const begin = At<void**>(self, burnt::kTextures);
    if (begin != At<void**>(self, burnt::kTextures + 4) && begin[0]) {
        *out = **static_cast<uint32_t**>(begin[0]);
        return;
    }
    *out = 0;
}

// ====================================================================================
// Compare mode
// ====================================================================================

using BuildFn = void(__fastcall*)(char*, void*);
using PointerFn = void*(__fastcall*)(char*, void*);
using OutFn = void(__fastcall*)(char*, void*, uint32_t*);
using DrawFn = uint32_t(__fastcall*)(char*, void*, void*, uint32_t);
BuildFn g_build;
PointerFn g_getOccupant;
OutFn g_getBox, g_getTexture;
DrawFn g_draw;
int g_depth = 0;

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

bool RecordBytes(const char* id, const char* what, const char* game, const char* ours, std::size_t size)
{
    std::size_t at = 0;
    while (at < size && game[at] == ours[at])
        ++at;
    if (at == size) {
        compare::Record(id, true, "", "");
        return true;
    }
    at &= ~std::size_t{3};
    const std::size_t n = size - at < 8 ? size - at : 8;
    char where[64];
    std::snprintf(where, sizeof where, "%s+%zX ", what, at);
    compare::Record(id, false, where + Hex(game + at, n), where + Hex(ours + at, n));
    return false;
}

// The quad's positions (three floats of each vertex; the builder leaves
// the texture coordinates alone).
std::vector<char> Positions(const char* quad)
{
    std::vector<char> positions;
    for (std::size_t vertex = 0; vertex < burnt::kQuadSize; vertex += burnt::kVertexSize)
        positions.insert(positions.end(), quad + vertex, quad + vertex + 12);
    return positions;
}

// The quad builder compared: the game's first, its object and quad taken;
// then ours. A burnt building without a quad gets one from each (the
// game's kept, ours freed after); one with a quad has its positions and
// the scaled corner filled with 0xCD before ours runs. When they differ the
// game's output (the positions and the scaled corner) is put back.
void CompareBuild(char* self)
{
    constexpr std::size_t kSize = burnt::kObjectSize;
    const bool had = At<char*>(self, burnt::kQuad) != nullptr;
    ++g_depth;
    g_build(self, nullptr);
    --g_depth;
    std::vector<char> game(self, self + kSize);
    char* const gameQuad = At<char*>(self, burnt::kQuad);
    const std::vector<char> gamePositions = gameQuad ? Positions(gameQuad) : std::vector<char>();
    std::memset(self + burnt::kScaled, 0xCD, 12);
    if (had) {
        for (std::size_t vertex = 0; vertex < burnt::kQuadSize; vertex += burnt::kVertexSize)
            std::memset(gameQuad + vertex, 0xCD, 12);
    } else {
        At<char*>(self, burnt::kQuad) = nullptr;
    }
    BuildQuad(self, nullptr);
    char* const ourQuad = At<char*>(self, burnt::kQuad);
    const std::vector<char> ourPositions = ourQuad ? Positions(ourQuad) : std::vector<char>();
    At<char*>(self, burnt::kQuad) = gameQuad;
    std::vector<char> mine(self, self + kSize);
    bool same = RecordBytes("propkinds.burnt.quad", "burnt", game.data(), mine.data(), kSize);
    if (gamePositions.size() != ourPositions.size()) {
        compare::Record("propkinds.burnt.quad.positions", false, std::to_string(gamePositions.size()), std::to_string(ourPositions.size()));
        same = false;
    } else if (!gamePositions.empty()) {
        same = RecordBytes("propkinds.burnt.quad.positions", "quad", gamePositions.data(), ourPositions.data(), gamePositions.size()) && same;
    }
    if (!had && ourQuad && ourQuad != gameQuad)
        Cdecl<void, void*>(addr::SC4_operator_delete, static_cast<void*>(ourQuad));
    if (!same) {
        std::memcpy(self + burnt::kScaled, game.data() + burnt::kScaled, 12);
        if (gameQuad)
            for (std::size_t vertex = 0, at = 0; vertex < burnt::kQuadSize; vertex += burnt::kVertexSize, at += 12)
                std::memcpy(gameQuad + vertex, gamePositions.data() + at, 12);
    }
}

void __fastcall DBuildQuad(char* self, void*)
{
    if (!g_compare)
        return BuildQuad(self, nullptr);
    if (g_depth > 0)
        return g_build(self, nullptr);
    CompareBuild(self);
}

void* __fastcall DGetOccupant(char* model, void*)
{
    if (!g_compare)
        return GetOccupant(model, nullptr);
    return compare::Call("propkinds.burnt.model.occupant", [&] { return g_getOccupant(model, nullptr); },
                         [&] { return GetOccupant(model, nullptr); });
}

void __fastcall DGetBox(char* model, void*, uint32_t* out)
{
    if (!g_compare)
        return GetBox(model, nullptr, out);
    uint32_t mine[6];
    GetBox(model, nullptr, mine);
    g_getBox(model, nullptr, out);
    RecordBytes("propkinds.burnt.model.box", "out", reinterpret_cast<const char*>(out), reinterpret_cast<const char*>(mine), 24);
}

void __fastcall DGetTexture(char* model, void*, uint32_t* out)
{
    if (!g_compare)
        return GetTexture(model, nullptr, out);
    uint32_t mine = *out;
    GetTexture(model, nullptr, &mine);
    g_getTexture(model, nullptr, out);
    compare::Record("propkinds.burnt.model.texture", mine == *out, std::to_string(*out), std::to_string(mine));
}

// The draw compared in the same frame, as the sign's (propkinds_sign_draw.cpp).
struct Region {
    const char* name;
    char* data;
    std::size_t size;
};

std::vector<Region> ContextRegions(void* context)
{
    char* const c = static_cast<char*>(context);
    std::vector<Region> regions{{"context", c, 0x200}};
    if (char* const render = At<char*>(c, 0x34))
        if (char* const camera = At<char*>(render, 0xD8))
            regions.push_back({"camera", camera, 0x1A8});
    if (char* const stages = At<char*>(c, 0xE0))
        regions.push_back({"stages", stages, static_cast<std::size_t>(At<char*>(c, 0xE4) - stages)});
    return regions;
}

uint32_t __fastcall DDraw(char* model, void*, void* context, uint32_t pass)
{
    if (!g_compare)
        return Draw(model, nullptr, context, pass);
    const std::vector<char> pipeline = gd::SaveState();
    if (pipeline.empty() || g_depth > 0)
        return g_draw(model, nullptr, context, pass);
    const std::vector<Region> regions = ContextRegions(context);
    std::vector<std::vector<char>> before;
    for (const Region& r : regions)
        before.emplace_back(r.data, r.data + r.size);
    const auto putBack = [&] {
        for (std::size_t i = 0; i < regions.size(); ++i)
            std::memcpy(regions[i].data, before[i].data(), regions[i].size);
    };
    ++g_depth;
    gd::BeginCapture(false);
    g_draw(model, nullptr, context, pass);
    gd::EndCapture();
    putBack();
    gd::RestoreState(pipeline);
    gd::BeginCapture(false);
    const uint32_t mine = Draw(model, nullptr, context, pass);
    const std::vector<uint32_t> ourDraws = gd::EndCapture();
    const std::vector<char> ourPipeline = gd::SaveState();
    std::vector<std::vector<char>> after;
    for (const Region& r : regions)
        after.emplace_back(r.data, r.data + r.size);
    putBack();
    gd::RestoreState(pipeline);
    gd::BeginCapture(true);
    const uint32_t result = g_draw(model, nullptr, context, pass);
    const std::vector<uint32_t> gameDraws = gd::EndCapture();
    --g_depth;
    const char* const id = "propkinds.burnt.draw";
    if (ourDraws != gameDraws) {
        std::size_t at = 0;
        while (at < ourDraws.size() && at < gameDraws.size() && ourDraws[at] == gameDraws[at])
            ++at;
        char game[64], ours[64];
        std::snprintf(game, sizeof game, "draw %zu of %zu: %08X", at, gameDraws.size(), at < gameDraws.size() ? gameDraws[at] : 0u);
        std::snprintf(ours, sizeof ours, "draw %zu of %zu: %08X", at, ourDraws.size(), at < ourDraws.size() ? ourDraws[at] : 0u);
        compare::Record(id, false, game, ours);
        return result;
    }
    const std::vector<char> gamePipeline = gd::SaveState();
    if (gamePipeline != ourPipeline) {
        RecordBytes(id, "driver state", gamePipeline.data(), ourPipeline.data(), gamePipeline.size());
        return result;
    }
    for (std::size_t i = 0; i < regions.size(); ++i)
        if (std::memcmp(regions[i].data, after[i].data(), regions[i].size) != 0) {
            RecordBytes(id, regions[i].name, regions[i].data, after[i].data(), regions[i].size);
            return result;
        }
    compare::Record(id, result == mine, std::to_string(result), std::to_string(mine));
    static uint64_t compared = 0, draws = 0;
    ++compared;
    draws += gameDraws.size();
    if ((compared & (compared - 1)) == 0)
        log::Write("propkinds burnt draw: %llu draws of the burnt building compared, %llu draw calls",
                   static_cast<unsigned long long>(compared), static_cast<unsigned long long>(draws));
    return result;
}

// ====================================================================================
// Installation
// ====================================================================================

struct Hooked {
    std::uintptr_t address;
    void* ours;
    void** original;
};

// The first instructions are six bytes or more (GetTexture's first two: its
// short jump at +6 lies after them).
const Hooked kBurntDrawDetours[] = {
    {addr::cSC4PropOccupantBurntBuilding_BuildQuad, reinterpret_cast<void*>(&DBuildQuad), reinterpret_cast<void**>(&g_build)},
    {addr::cSC4PropOccupantBurntBuilding_GetOccupant, reinterpret_cast<void*>(&DGetOccupant), reinterpret_cast<void**>(&g_getOccupant)},
    {addr::cSC4PropOccupantBurntBuilding_GetBox, reinterpret_cast<void*>(&DGetBox), reinterpret_cast<void**>(&g_getBox)},
    {addr::cSC4PropOccupantBurntBuilding_Draw, reinterpret_cast<void*>(&DDraw), reinterpret_cast<void**>(&g_draw)},
    {addr::cSC4PropOccupantBurntBuilding_GetTexture, reinterpret_cast<void*>(&DGetTexture), reinterpret_cast<void**>(&g_getTexture)},
};

bool InstallBurntDraw(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Hooked& h : kBurntDrawDetours)
        if (!addr_check::Find(h.address)) {
            log::Write("propkinds burnt draw: no address entry for 0x%08X", static_cast<unsigned>(h.address));
            return false;
        }
    for (const Hooked& h : kBurntDrawDetours)
        if (!hook::Install(*addr_check::Find(h.address), h.ours, h.original))
            return false;
    g_compare = mode == registry::Mode::Compare;
    log::Write("propkinds burnt draw: %u detours%s", static_cast<unsigned>(std::size(kBurntDrawDetours)),
               g_compare ? ", compared with the game's" : "");
    return true;
}

const bool g_registered = registry::Register({
    "simulators.propkinds.burnt_draw",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantBurntBuilding_BuildQuad,
    "Our cSC4PropOccupantBurntBuilding's drawing: the quad builder and the model interface's occupant, box, draw and "
    "texture (5 detours; compare mode checks the quad, the slots and the draw against the game's; "
    "src/core/propkinds_burnt_draw.cpp)",
    true,
    InstallBurntDraw,
});

} // namespace
} // namespace opensc4::propkinds
