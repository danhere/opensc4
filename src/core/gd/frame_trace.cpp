// Draw-call parity: the checksum of a city frame's draws (frame_trace.h).
// diagnostics.trace.frames brackets cSC43DRender::Draw (0x007CB530); the
// D3D11 driver records each draw it hands its backend in between. Only
// what reaches the picture is hashed: the state fields the backend reads
// and the vertices and indices the draw uses, never pointers.
#include "core/gd/frame_trace.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include "core/gd/pixels.h"
#include "core/gd/vertex_format.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace opensc4::gd::frame_trace {
namespace {

bool g_active = false;
int g_depth = 0;
uint32_t g_draws = 0;
uint32_t g_hash = 2166136261u;
Frame g_last;
Frame g_first;
bool g_marked = false;

uint32_t g_drawHash = 2166136261u; // the draw being recorded
std::unordered_map<uint32_t, uint32_t> g_textures; // backend handle -> content checksum

uint32_t Fnv(uint32_t h, const void* data, std::size_t size)
{
    const auto* b = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i)
        h = (h ^ b[i]) * 16777619u;
    return h;
}

uint32_t TextureContent(uint32_t texture)
{
    const auto it = g_textures.find(texture);
    return it == g_textures.end() ? (texture | 0x80000000u) : it->second;
}

void Bytes(const void* data, std::size_t size)
{
    const auto* b = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i)
        g_drawHash = (g_drawHash ^ b[i]) * 16777619u;
}

// OPENSC4_FRAME_DUMP=<file>: each marked frame's draws, one line each
// (index, primitive, vertex format, count, first stage's texture, the
// model-view translation, the draw's checksum), to find where two runs part.
std::FILE* g_dump = nullptr;
bool g_dumping = false;
uint32_t g_dumpFrames = 0;

template <typename T>
void Value(const T& value)
{
    Bytes(&value, sizeof value);
}

void State(const DrawState& s)
{
    Bytes(s.modelView, sizeof s.modelView);
    Bytes(s.projection, sizeof s.projection);
    Value(s.viewport.x), Value(s.viewport.y), Value(s.viewport.width), Value(s.viewport.height);
    Bytes(s.enabled, sizeof s.enabled);
    Value(s.depthFunc), Value(s.depthMask);
    Value(s.stencilFunc), Value(s.stencilRef), Value(s.stencilReadMask), Value(s.stencilWriteMask);
    Bytes(s.stencilOps, sizeof s.stencilOps);
    Value(s.blendSrc), Value(s.blendDst), Value(s.alphaFunc), Value(s.alphaRef), Value(s.shadeModel);
    Value(s.polygonOffset), Value(s.lighting);
    Bytes(s.colorMultiplier, sizeof s.colorMultiplier);
    Value(s.vertexColors), Value(s.diffuseFromVertex);
    Bytes(s.textureFactor, sizeof s.textureFactor);
    Value(s.fogMode);
    Bytes(s.fogColor, sizeof s.fogColor);
    Value(s.fogDensity), Value(s.fogStart), Value(s.fogEnd);
    Value(s.stageCount);
    for (uint32_t k = 0; k < s.stageCount; ++k) {
        const StageDesc& t = s.stages[k];
        Value(t.enabled), Value(TextureContent(t.texture)), Value(t.envMode);
        Bytes(&t.combiner, sizeof t.combiner);
        Value(t.texCoord), Value(t.hasMatrix);
        if (t.hasMatrix)
            Bytes(t.matrix, sizeof t.matrix);
        Bytes(t.params, sizeof t.params);
    }
}

uint32_t IndexAt(const void* indices, uint32_t type, uint32_t i)
{
    switch (type) {
    case UnsignedByte: return static_cast<const uint8_t*>(indices)[i];
    case UnsignedShort: return static_cast<const uint16_t*>(indices)[i];
    default: return static_cast<const uint32_t*>(indices)[i];
    }
}

using DrawFn = uint8_t(__fastcall*)(void*, void*);
DrawFn g_originalDraw = nullptr;

uint8_t __fastcall Draw(void* self, void* edx)
{
    if (g_depth++ == 0) {
        g_draws = 0;
        g_hash = 2166136261u;
        g_dumping = g_dump && g_marked;
        if (g_dumping)
            std::fprintf(g_dump, "frame %u (marked %u)\n", g_last.number + 1, ++g_dumpFrames);
    }
    const uint8_t result = g_originalDraw(self, edx);
    if (--g_depth == 0) {
        ++g_last.number;
        g_last.draws = g_draws;
        g_last.hash = g_hash;
        if (g_marked) {
            g_first = g_last;
            g_marked = false;
        }
        if (g_dumping) {
            std::fprintf(g_dump, "end draws %u hash %08X\n", g_draws, g_hash);
            std::fflush(g_dump);
            g_dumping = false;
        }
    }
    return result;
}

bool Install(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC43DRender_Draw);
    if (!game::IsBuild641() || !entry || !hook::Install(*entry, &Draw, &g_originalDraw))
        return false;
    g_active = true;
    if (const char* path = std::getenv("OPENSC4_FRAME_DUMP"); path && *path)
        g_dump = std::fopen(path, "w");
    log::Write("frame trace: city frames are hashed (cSC43DRender::Draw)");
    return true;
}

const bool g_registered = registry::Register({
    "diagnostics.trace.frames",
    "diagnostics",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC43DRender_Draw,
    "A checksum of every draw of each city frame, from the D3D11 driver (docs/renderer.md, \"Draw-call parity\")",
    false,
    Install,
});

} // namespace

bool Active() { return g_active; }

void Record(const DrawState& state, const DrawCall& call)
{
    if (!g_active || g_depth == 0)
        return;
    ++g_draws;
    g_drawHash = 2166136261u;
    Value(call.primitive), Value(call.vertexFormat), Value(call.vertexStride), Value(call.count);
    State(state);
    if (call.vertices && !call.indices) {
        Bytes(call.vertices + static_cast<std::size_t>(call.first) * call.vertexStride,
              static_cast<std::size_t>(call.count) * call.vertexStride);
    } else if (call.vertices && call.count) {
        uint32_t low = 0xFFFFFFFFu, high = 0;
        for (uint32_t i = 0; i < call.count; ++i) {
            const uint32_t index = IndexAt(call.indices, call.indexType, i);
            Value(index);
            low = index < low ? index : low;
            high = index > high ? index : high;
        }
        Bytes(call.vertices + static_cast<std::size_t>(low) * call.vertexStride,
              static_cast<std::size_t>(high - low + 1) * call.vertexStride);
    }
    for (int i = 0; i < 4; ++i)
        g_hash = (g_hash ^ ((g_drawHash >> (8 * i)) & 0xFF)) * 16777619u;
    // OPENSC4_FRAME_DUMP_DRAW=<n>: that draw's vertex bytes too, in hex.
    static const int dumpDraw = [] {
        const char* v = std::getenv("OPENSC4_FRAME_DUMP_DRAW");
        return v ? std::atoi(v) : -1;
    }();
    if (g_dumping && static_cast<int>(g_draws) == dumpDraw && call.vertices) {
        uint32_t first = call.first, last = call.first + call.count;
        if (call.indices) {
            first = 0xFFFFFFFFu, last = 0;
            for (uint32_t i = 0; i < call.count; ++i) {
                const uint32_t index = IndexAt(call.indices, call.indexType, i);
                std::fprintf(g_dump, "i%u %u\n", i, index);
                first = index < first ? index : first;
                last = index + 1 > last ? index + 1 : last;
            }
        }
        for (uint32_t i = first; i < last; ++i) {
            std::fprintf(g_dump, "v%u", i);
            for (uint32_t b = 0; b < call.vertexStride; ++b)
                std::fprintf(g_dump, " %02X", call.vertices[static_cast<std::size_t>(i) * call.vertexStride + b]);
            std::fprintf(g_dump, "\n");
        }
    }
    if (g_dumping)
        std::fprintf(g_dump, "%u p%u f%08X n%u t%08X m %.3f %.3f %.3f h%08X\n", g_draws, call.primitive, call.vertexFormat,
                     call.count, state.stageCount ? TextureContent(state.stages[0].texture) : 0u, state.modelView[12],
                     state.modelView[13], state.modelView[14], g_drawHash);
}

void TextureCreated(uint32_t texture, uint32_t format, uint32_t width, uint32_t height, uint32_t levels)
{
    if (!g_active)
        return;
    const uint32_t desc[4] = {format, width, height, levels};
    g_textures[texture] = Fnv(2166136261u, desc, sizeof desc);
}

void TextureUploaded(uint32_t texture, int32_t level, int32_t x, int32_t y, int32_t width, int32_t height,
                     uint32_t format, uint32_t type, uint32_t rowLength, const void* pixels)
{
    if (!g_active || level != 0 || !pixels || width <= 0 || height <= 0)
        return;
    const auto it = g_textures.find(texture);
    if (it == g_textures.end())
        return;
    std::vector<uint8_t> bgra(static_cast<std::size_t>(width) * height * 4);
    const uint32_t w = static_cast<uint32_t>(width), h = static_cast<uint32_t>(height);
    if (IsCompressed(format))
        DecodeDXT(format, pixels, w, h, bgra.data(), w * 4);
    else if (!ConvertToBGRA(format, type, pixels, w, h, rowLength ? rowLength : w, bgra.data(), w * 4))
        return;
    const int32_t box[4] = {x, y, width, height};
    it->second = Fnv(Fnv(it->second, box, sizeof box), bgra.data(), bgra.size());
}

Frame Last() { return g_last; }

void Mark()
{
    g_first = Frame{};
    g_marked = true;
}

Frame FirstSinceMark() { return g_first; }

} // namespace opensc4::gd::frame_trace
