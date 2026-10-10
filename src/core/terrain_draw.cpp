// Our terrain view's drawing (cSTETerrainView3D, build 641; docs/renderer.md,
// "The terrain"). Draws are compared in the same frame: ours runs first with
// the D3D11 driver recording a checksum per draw and keeping them off the
// screen, the state it changed is put back (the render context's state
// cache, the S3D camera, the view, the vertex colours, the driver's pipeline
// state), then the game's runs and draws for real, recorded the same way,
// and the two lists of draws are compared.
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

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace opensc4::terrain_draw {
namespace {

constexpr std::size_t kViewSize = 0xE8;
constexpr std::size_t kContextSize = 0x200; // cSC43DRender + 0x11C (0x007C9F50)
constexpr std::size_t kCameraSize = 0x1A8;

bool g_compare = false;
int g_depth = 0;
uint64_t g_passesCompared = 0, g_drawsCompared = 0;

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

// The memory a pass may change: the view, the render context (its state
// cache), the S3D camera (depth offset, projection), the land vertices
// (colours and blend weights are written while drawing).
std::vector<Region> PassRegions(char* self, char* context)
{
    char* const render = At<char*>(context, 0x34);
    char* const camera = render ? At<char*>(render, 0xD8) : nullptr;
    const std::size_t vertices = static_cast<std::size_t>(Global<uint32_t>(addr::STE_VertexCount)) * 32;
    std::vector<Region> regions{{"view", self, kViewSize}, {"context", context, kContextSize}};
    if (camera)
        regions.push_back({"camera", camera, kCameraSize});
    regions.push_back({"land", Global<char*>(addr::STE_LandVertices), vertices});
    for (const auto& [name, address] : {std::pair{"batchsource", addr::STE_BatchSource},
                                        std::pair{"template", addr::STE_IndexTemplate},
                                        std::pair{"templaterow", addr::STE_IndexTemplatePerRow},
                                        std::pair{"locked", addr::STE_BatchVertices},
                                        std::pair{"batchvertices", addr::STE_BatchVertexCount},
                                        std::pair{"batchindices", addr::STE_BatchIndexCount}})
        regions.push_back({name, reinterpret_cast<char*>(address), name == std::string("templaterow") ? 1u : 4u});
    return regions;
}

// Whether a row in [first, end) has its texture-layer spans to rebuild: the
// rebuild frees and allocates list nodes, which a copy cannot undo, so such
// frames run the game's only.
bool DirtyRows(int32_t first, int32_t end)
{
    const uint32_t* const bits = Global<uint32_t*>(addr::STE_DirtyRowBits);
    const int32_t base = Global<int32_t>(addr::STE_DirtyRowBase);
    for (int32_t r = first; r < end; ++r) {
        const int32_t index = base + r;
        if (index >= 0 && (bits[index >> 5] & (1u << (index & 31))))
            return true;
    }
    return false;
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
        log::Write("terrain draw: %s: %llu passes, %llu draws compared so far", id,
                   static_cast<unsigned long long>(g_passesCompared), static_cast<unsigned long long>(g_drawsCompared));
    char text[96];
    if (ourDraws != gameDraws) {
        std::size_t at = 0;
        while (at < ourDraws.size() && at < gameDraws.size() && ourDraws[at] == gameDraws[at])
            ++at;
        std::snprintf(text, sizeof text, "draw %zu of %zu: %08X", at, gameDraws.size(),
                      at < gameDraws.size() ? gameDraws[at] : 0u);
        std::string game = text;
        std::snprintf(text, sizeof text, "draw %zu of %zu: %08X", at, ourDraws.size(),
                      at < ourDraws.size() ? ourDraws[at] : 0u);
        compare::Record(id, false, game, text);
        return;
    }
    if (gd::SaveState() != ourPipeline) {
        compare::Record(id, false, "driver state", "driver state");
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

// --- The land pass ---------------------------------------------------------------------

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

char* Land() { return Global<char*>(addr::STE_LandVertices); }
uint32_t Columns() { return Global<uint32_t>(addr::STE_Columns); }
const int32_t* RowStarts() { return Global<int32_t*>(addr::STE_LandRowStart); }
const int32_t* RowEnds() { return Global<int32_t*>(addr::STE_LandRowEnd); }
const uint8_t* RowsDrawn() { return Global<uint8_t*>(addr::STE_LandRowDrawn); }

// The terrain record, 0x24 bytes per vertex (cSTETerrain + 0x6C): colours at
// +0x10 and +0x13, three texture layers {uint16 id, uint8 weight, pad} at +0x16.
char* Records(char* self) { return At<char*>(At<char*>(self, 0x20), 0x6C); }

// The dynamic vertex buffer path (0x0075C9F0, 0x0075CAD0, 0x0075CA30, 0x0075CB50).
void* Buffers() { return Global<void*>(addr::STE_DynamicBuffers); }

bool LockBatch()
{
    char*& locked = Global<char*>(addr::STE_BatchVertices);
    locked = Virtual<char*>(Buffers(), 0x18, Global<int32_t>(addr::STE_Buffer), Global<uint32_t>(addr::STE_BufferCapacity));
    if (!locked)
        return false;
    Global<uint32_t>(addr::STE_BatchVertexCount) = 0;
    Global<int32_t>(addr::STE_BatchIndexCount) = 0;
    return true;
}

void FlushBatch()
{
    char*& locked = Global<char*>(addr::STE_BatchVertices);
    if (!locked)
        return;
    const int32_t buffer = Global<int32_t>(addr::STE_Buffer);
    Virtual<void>(Buffers(), 0x20, buffer);
    locked = nullptr;
    const int32_t indices = Global<int32_t>(addr::STE_BatchIndexCount);
    if (indices > 0)
        Virtual<void>(Buffers(), 0x28, buffer, 0u, (indices / 3) * 3, Global<uint16_t*>(addr::STE_BatchIndices));
    Global<uint32_t>(addr::STE_BatchVertexCount) = 0;
    Global<int32_t>(addr::STE_BatchIndexCount) = 0;
}

void CopyVertices(int32_t row, int32_t first, uint32_t count)
{
    uint32_t& used = Global<uint32_t>(addr::STE_BatchVertexCount);
    const char* const from = Global<char*>(addr::STE_BatchSource) +
                             (static_cast<std::size_t>(Global<uint32_t>(addr::STE_BatchRowStride)) * row + first) * 32;
    std::memcpy(Global<char*>(addr::STE_BatchVertices) + static_cast<std::size_t>(used) * 32, from,
                static_cast<std::size_t>(count & 0x7FFFFFF) * 32);
    used += count;
}

// Columns first to last of a row and the next row into the batch, with
// their part of the strip's index template.
void AddStrip(int32_t row, int32_t first, int32_t last)
{
    const int32_t span = last - first;
    const uint32_t capacity = Global<uint32_t>(addr::STE_BufferCapacity);
    if (capacity < Global<uint32_t>(addr::STE_BatchVertexCount) + static_cast<uint32_t>(span + 1) * 2) {
        FlushBatch();
        Global<char*>(addr::STE_BatchVertices) =
            Virtual<char*>(Buffers(), 0x18, Global<int32_t>(addr::STE_Buffer), capacity);
    }
    if (!Global<char*>(addr::STE_BatchVertices))
        return;
    const int32_t base0 = static_cast<int32_t>(Global<uint32_t>(addr::STE_BatchVertexCount));
    CopyVertices(row, first, static_cast<uint32_t>(span + 1));
    const int32_t base1 = static_cast<int32_t>(Global<uint32_t>(addr::STE_BatchVertexCount));
    CopyVertices(row + 1, first, static_cast<uint32_t>(span + 1));
    int32_t& indexCount = Global<int32_t>(addr::STE_BatchIndexCount);
    const uint32_t needed = static_cast<uint32_t>(indexCount + span * 6);
    uint16_t*& begin = Global<uint16_t*>(addr::STE_BatchIndices);
    uint16_t*& end = *(&Global<uint16_t*>(addr::STE_BatchIndices) + 1);
    const uint32_t size = static_cast<uint32_t>(end - begin);
    if (size < needed) {
        const uint32_t grown = needed + 16;
        if (grown < size) {
            end = begin + grown;
        } else {
            const uint16_t zero = 0;
            CallThis<void>(addr::SC4Uint16Vector_InsertN, &begin, end, grown - size, &zero);
        }
    }
    const uint16_t stride = static_cast<uint16_t>(Global<uint32_t>(addr::STE_BatchRowStride));
    const uint16_t* source = Global<uint16_t*>(addr::STE_IndexTemplate) + first * 6;
    if (Global<uint8_t>(addr::STE_IndexTemplatePerRow))
        source += Global<uint32_t>(addr::STE_BatchTemplateRow) * row;
    const uint16_t* const sourceEnd = source + span * 6;
    uint16_t* out = begin + indexCount;
    const int32_t lastColumn = static_cast<int32_t>(Global<uint32_t>(addr::STE_BatchLastColumn));
    for (; source < sourceEnd; ++source) {
        uint16_t offset = static_cast<uint16_t>(base0 - first);
        if (lastColumn < static_cast<int32_t>(*source))
            offset = static_cast<uint16_t>((base1 - stride) - first);
        *out++ = static_cast<uint16_t>(*source + offset);
    }
    indexCount = static_cast<int32_t>(needed);
}

// Colours from the terrain record (+0x10 or +0x13) into the land vertices
// (BGRA at +0xC) for the rows [first, end) and the columns the view spans.
void SetColours(char* self, std::size_t from)
{
    const char* const records = Records(self);
    char* const land = Land();
    const uint32_t columns = Columns();
    for (int32_t r = At<int32_t>(self, 0x98); r < At<int32_t>(self, 0x9C); ++r) {
        const int32_t first = At<int32_t>(self, 0xB0) + static_cast<int32_t>(columns) * r;
        const int32_t end = At<int32_t>(self, 0xB4) + static_cast<int32_t>(columns) * r;
        for (int32_t i = first; i < end; ++i) {
            const char* const record = records + static_cast<std::size_t>(i) * 0x24 + from;
            char* const vertex = land + static_cast<std::size_t>(i) * 32;
            vertex[0xE] = record[0];
            vertex[0xD] = record[1];
            vertex[0xC] = record[2];
        }
    }
}

bool RowShown(int32_t r) { return RowsDrawn()[r] || RowsDrawn()[r + 1]; }

void SetRowArrays(char* context, int32_t row)
{
    const uint32_t columns = Columns();
    CallThis<void>(addr::SC4RenderContext_SetVertexArray, context, 0xBu, columns * 2,
                   Land() + static_cast<std::size_t>(columns) * row * 32);
    const uint32_t perRow = Global<uint32_t>(addr::STE_IndicesPerRow);
    CallThis<void>(addr::SC4RenderContext_SetIndexArray, context, perRow,
                   Global<uint16_t*>(addr::STE_RowIndices) + static_cast<std::size_t>(perRow) * row);
}

// 0x00752100 / 0x007521C0: the untextured pass over the rows in view.
void DrawBase(char* self, char* context, void* buffers)
{
    const int32_t* const starts = RowStarts();
    const int32_t* const ends = RowEnds();
    if (!buffers) {
        for (int32_t r = At<int32_t>(self, 0x98); r < At<int32_t>(self, 0xA0); ++r) {
            if (!RowShown(r))
                continue;
            SetRowArrays(context, r);
            CallThis<void>(addr::SC4RenderContext_DrawIndexed, context, 0u, starts[r] * 6,
                           (ends[r] - starts[r] - 1) * 6);
        }
        return;
    }
    LockBatch();
    for (int32_t r = At<int32_t>(self, 0x98); r < At<int32_t>(self, 0xA0); ++r) {
        if (!RowShown(r))
            continue;
        const int32_t first = starts[r], last = ends[r] - 1;
        if (first < last)
            AddStrip(r, first, last);
    }
    FlushBatch();
}

// A layer's spans in one row (0x0075B010): its spans clipped to the row's
// columns in view.
struct RowSpans {
    int32_t row;
    std::vector<std::pair<int32_t, int32_t>> spans;
};

bool AddSpans(int32_t row, const char* layer, std::vector<RowSpans>& group)
{
    const char* const head = At<const char*>(const_cast<char*>(layer), 0xC);
    bool added = false;
    for (const char* node = At<const char*>(const_cast<char*>(head), 0); node != head;
         node = At<const char*>(const_cast<char*>(node), 0)) {
        int32_t first = At<int32_t>(const_cast<char*>(node), 8);
        if (first <= RowStarts()[row])
            first = RowStarts()[row];
        int32_t last = At<int32_t>(const_cast<char*>(node), 0xC);
        if (RowEnds()[row] - 1 <= last)
            last = RowEnds()[row] - 1;
        if (first > last)
            continue;
        if (!added) {
            group.push_back({row, {}});
            added = true;
        }
        group.back().spans.emplace_back(first, last);
    }
    return added;
}

const char* RowLayers(int32_t row) { return Global<char**>(addr::STE_RowLayers)[row]; }
uint32_t LayerId(const char* node) { return At<uint32_t>(const_cast<char*>(node), 8); }
const char* Next(const char* node) { return At<const char*>(const_cast<char*>(node), 0); }

// The weight of `layer` at a record (0 when the vertex does not have it).
uint8_t Weight(const char* record, uint32_t layer)
{
    for (int k = 0; k < 3; ++k) {
        const uint16_t id = *reinterpret_cast<const uint16_t*>(record + 0x16 + 4 * k);
        if (id == layer)
            return static_cast<uint8_t>(record[0x16 + 4 * k + 2]);
        if (id == 0)
            break;
    }
    return 0;
}

// 0x0075BA90 with 0x0075B1D0, 0x0075B460 and 0x0075B790: the texture layers,
// one texture at a time in the order the rows in view first show them, each
// vertex's alpha set to its weight for that layer.
void DrawLayers(char* self, char* context, void* buffers)
{
    if (buffers)
        LockBatch();
    std::vector<int32_t> rows;
    const uint32_t* const dirtyBits = Global<uint32_t*>(addr::STE_DirtyRowBits);
    for (int32_t r = At<int32_t>(self, 0x98); r < At<int32_t>(self, 0xA0); ++r) {
        if (!RowShown(r))
            continue;
        const int32_t bit = Global<int32_t>(addr::STE_DirtyRowBase) + r;
        if (dirtyBits[bit >> 5] & (1u << (bit & 31)))
            CallThis<void>(addr::cSTETerrainView3D_RebuildRowLayers, self, r);
        rows.push_back(r);
    }
    if (rows.empty()) {
        if (buffers)
            FlushBatch();
        return;
    }
    const char* const records = Records(self);
    char* const land = Land();
    const uint32_t columns = Columns();
    char* const textures = Global<char*>(addr::STE_TexturesByZoom + 4 * Global<int32_t>(addr::STE_Zoom));
    std::vector<uint32_t> visited;
    std::size_t cursor = 0;
    for (;;) {
        std::vector<RowSpans> group;
        uint32_t layer = 0;
        bool found = false;
        while (!found && cursor < rows.size()) {
            const char* const head = RowLayers(rows[cursor]);
            for (const char* node = Next(head); node != head; node = Next(node)) {
                const uint32_t id = LayerId(node);
                if (std::find(visited.begin(), visited.end(), id) != visited.end())
                    continue;
                if (AddSpans(rows[cursor], node, group)) {
                    layer = id;
                    found = true;
                    break;
                }
            }
            if (!found)
                ++cursor;
        }
        if (!found)
            break;
        for (std::size_t i = cursor + 1; i < rows.size(); ++i) {
            const char* const head = RowLayers(rows[i]);
            for (const char* node = Next(head); node != head; node = Next(node)) {
                if (LayerId(node) == layer) {
                    AddSpans(rows[i], node, group);
                    break;
                }
                if (layer <= LayerId(node))
                    break;
            }
        }
        visited.push_back(layer);
        if (buffers) {
            FlushBatch();
            LockBatch();
        }
        char* const variants = **reinterpret_cast<char***>(textures + 4 * (layer & 0xFFFF0FFFu));
        CallThis<void>(addr::SC4RenderContext_BindTexture, context, At<uint32_t>(variants, 4 * ((layer >> 12) & 0xF)), 0u);
        for (const RowSpans& element : group) {
            const int32_t rowBase = static_cast<int32_t>(columns) * element.row;
            if (!buffers)
                SetRowArrays(context, element.row);
            for (const auto& [first, last] : element.spans) {
                for (int32_t i = rowBase + first; i <= rowBase + last; ++i) {
                    land[static_cast<std::size_t>(i) * 32 + 0xF] =
                        static_cast<char>(Weight(records + static_cast<std::size_t>(i) * 0x24, layer));
                    const std::size_t below = static_cast<std::size_t>(i) + columns;
                    land[below * 32 + 0xF] = static_cast<char>(Weight(records + below * 0x24, layer));
                }
                if ((last - first) * 2 <= 0)
                    continue;
                if (buffers)
                    AddStrip(element.row, first, last);
                else
                    CallThis<void>(addr::SC4RenderContext_DrawIndexed, context, 0u, first * 6, (last - first) * 6);
            }
        }
    }
    if (buffers)
        FlushBatch();
}

// 0x0075BD10: the land: the untextured pass with the record's +0x13 colours,
// then the texture layers with its +0x10 colours, blended.
uint32_t DrawLand(char* self, char* context, void* buffers)
{
    if (buffers) {
        Global<char*>(addr::STE_BatchSource) = Land();
        Global<uint16_t*>(addr::STE_IndexTemplate) = Global<uint16_t*>(addr::STE_RowIndices);
        Global<uint8_t>(addr::STE_IndexTemplatePerRow) = 1;
    }
    SetColours(self, 0x13);
    CallThis<void>(addr::SC4RenderContext_SetState3EF0, context, uint8_t{0}, uint8_t{0});
    DrawBase(self, context, buffers);
    SetColours(self, 0x10);
    CallThis<void>(addr::SC4RenderContext_SetState4010, context, uint8_t{1});
    CallThis<void>(addr::SC4RenderContext_SetBlend, context, 4u, 1u);
    CallThis<void>(addr::SC4RenderContext_SetState3EF0, context, uint8_t{1}, uint8_t{0});
    CallThis<void>(addr::SC4RenderContext_SetState40B0, context, 1u, 0u);
    CallThis<void>(addr::SC4RenderContext_SetState4280, context, 1u, 7u, 0u);
    CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 3u, 3u, 0u);
    DrawLayers(self, context, buffers);
    CallThis<void>(addr::SC4RenderContext_SetState4010, context, uint8_t{0});
    return 1;
}

using LandFn = uint32_t(__fastcall*)(char*, void*, char*, void*);
LandFn g_land;
uint64_t g_skipped = 0;

// OPENSC4_TERRAIN_SELFCHECK=1: compare mode runs the game's pass against
// itself, to check the comparison.
const bool g_selfCheck = [] {
    const char* v = std::getenv("OPENSC4_TERRAIN_SELFCHECK");
    return v && *v == '1';
}();

uint32_t __fastcall DLand(char* self, void*, char* context, void* buffers)
{
    if (g_depth > 0)
        return g_land(self, nullptr, context, buffers);
    // OPENSC4_TEST_TERRAIN_ARRAYS=1: the vertex array path, by clearing the
    // preference the pass checks before using the driver's dynamic buffers
    // (0x0075CD40: byte +0x33C of the object at 0x00B43CC4's +0xC).
    static const bool arrays = [] {
        const char* v = std::getenv("OPENSC4_TEST_TERRAIN_ARRAYS");
        return v && *v == '1';
    }();
    if (arrays)
        At<uint8_t>(At<char*>(Global<char*>(0x00B43CC4), 0xC), 0x33C) = 0;
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("terrain draw: land pass through %s", buffers ? "the driver's dynamic vertex buffers" : "vertex arrays");
    }
    if (!g_compare)
        return DrawLand(self, context, buffers);
    if (DirtyRows(At<int32_t>(self, 0x98), At<int32_t>(self, 0xA0))) {
        if (g_skipped++ % 100 == 0)
            log::Write("terrain draw: %llu land passes with rows to rebuild not compared",
                       static_cast<unsigned long long>(g_skipped));
        return g_land(self, nullptr, context, buffers);
    }
    uint32_t result = 0;
    DrawCompared(g_selfCheck ? "terrain.land.selfcheck" : "terrain.land", PassRegions(self, context),
                 [&] {
                     if (g_selfCheck)
                         g_land(self, nullptr, context, buffers);
                     else
                         DrawLand(self, context, buffers);
                 },
                 [&] { result = g_land(self, nullptr, context, buffers); });
    return result;
}

OPENSC4_THUNK(TLand, DLand, 8)

template <typename Fn>
bool Detour(std::uintptr_t address, void (*thunk)(), Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, reinterpret_cast<Fn>(thunk), original);
}

bool Install(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compare = mode == registry::Mode::Compare;
    if (!Detour(addr::cSTETerrainView3D_DrawLand, &TLand, &g_land))
        return false;
    log::Write("terrain draw: land pass %s", g_compare ? (g_selfCheck ? "compared, the game's against itself" : "compared with the game's") : "ours");
    return true;
}

const bool g_registered = registry::Register({
    "renderer.terrain.land",
    "renderer/terrain",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSTETerrainView3D_DrawLand,
    "The terrain's land pass; compare mode records the draws of a quiet run and of the real one in the same frame "
    "(docs/renderer.md, \"The terrain\")",
    true,
    Install,
});

} // namespace
} // namespace opensc4::terrain_draw
