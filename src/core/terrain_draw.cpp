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
#include <cmath>
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

bool g_compareLand = false, g_compareWater = false;
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
    regions.push_back({"water", Global<char*>(addr::STE_WaterVertices), vertices});
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

// --- The water pass --------------------------------------------------------------------

// 0x00752240: the water mesh over the rows whose water is in view (the land
// pass's rows and columns), with the zoom's water texture.
void DrawWaterSurface(char* self, char* context, void* buffers)
{
    CallThis<void>(addr::SC4RenderContext_SetState4010, context, uint8_t{1});
    CallThis<void>(addr::SC4RenderContext_SetBlend, context, 4u, 5u);
    CallThis<void>(addr::SC4RenderContext_SetState40B0, context, 1u, 0u);
    char* const textures = Global<char*>(addr::STE_WaterTexturesByZoom + 4 * Global<int32_t>(addr::STE_Zoom));
    CallThis<void>(addr::SC4RenderContext_BindTexture, context, **reinterpret_cast<uint32_t**>(textures), 0u);
    CallThis<void>(addr::SC4RenderContext_SetState28A0, context, 1u);
    const int32_t* const starts = RowStarts();
    const int32_t* const ends = RowEnds();
    const uint8_t* const water = Global<uint8_t*>(addr::STE_LandRowWater);
    const int32_t first = At<int32_t>(self, 0xA4), end = At<int32_t>(self, 0xAC);
    if (buffers) {
        LockBatch();
        Global<char*>(addr::STE_BatchSource) = Global<char*>(addr::STE_WaterVertices);
        Global<uint16_t*>(addr::STE_IndexTemplate) = Global<uint16_t*>(addr::STE_RowIndices);
        Global<uint8_t>(addr::STE_IndexTemplatePerRow) = 1;
        for (int32_t r = first; r < end; ++r) {
            if (!water[r] && !water[r + 1])
                continue;
            if (ends[r] - 1 > starts[r])
                AddStrip(r, starts[r], ends[r] - 1);
        }
        FlushBatch();
    } else {
        const uint32_t columns = Columns();
        const uint32_t perRow = Global<uint32_t>(addr::STE_IndicesPerRow);
        for (int32_t r = first; r < end; ++r) {
            if (!water[r] && !water[r + 1])
                continue;
            const int32_t triangles = (ends[r] - starts[r]) * 2 - 2;
            if (triangles <= 0)
                continue;
            CallThis<void>(addr::SC4RenderContext_SetIndexArray, context, perRow,
                           Global<uint16_t*>(addr::STE_RowIndices) + static_cast<std::size_t>(perRow) * r);
            CallThis<void>(addr::SC4RenderContext_SetVertexArray, context, 0xBu, columns,
                           Global<char*>(addr::STE_WaterVertices) + static_cast<std::size_t>(columns) * r * 32);
            CallThis<void>(addr::SC4RenderContext_DrawIndexed, context, 0u, starts[r] * 6, triangles * 3);
        }
    }
    CallThis<void>(addr::SC4RenderContext_SetState28A0, context, 3u);
    CallThis<void>(addr::SC4RenderContext_SetState4010, context, uint8_t{0});
}

uint8_t Truncated(double value) { return static_cast<uint8_t>(static_cast<int32_t>(value)); }

// 0x00756D20: the water vertices' colours over the view's dirty rectangle
// (+0x4C to +0x58): the depth ramp's colour for the vertex's depth, mixed
// with the colour picture or faded to the tint, and an alpha that grows
// with depth.
void RefreshWaterColours(char* self)
{
    const float fade = At<float>(self, 0x44);
    const bool fading = fade > 0.0f;
    const uint8_t plain = At<uint8_t>(At<char*>(Global<char*>(addr::SC4_Preferences43CC4), 0xC), 0x39C);
    const bool usePicture = At<uint8_t>(self, 0x49) && !fading && !plain;
    char* const* const depthRecords = &At<char*>(At<char*>(self, 0x24), 0x18);
    float tint[3] = {0.0f, 0.0f, 0.0f}; // unset in the game unless fading
    if (fading)
        for (int i = 0; i < 3; ++i)
            tint[i] = static_cast<float>(1.0f - (1.0f - static_cast<double>((&Global<float>(addr::STE_WaterFadeTint))[i])) * fade);
    void* const picture = Global<void*>(addr::STE_WaterColourPicture);
    if (!picture)
        return;
    const float alphaDepth = Global<float>(addr::STE_WaterAlphaDepth);
    const uint8_t minAlpha = Global<uint8_t>(addr::STE_WaterMinAlpha);
    float alphaScale = 1.0f;
    if (alphaDepth > 0.0f)
        alphaScale = static_cast<float>(static_cast<double>(0xFF - minAlpha) / alphaDepth);
    const int32_t w = Virtual<int32_t>(picture, 0x24) - 1;
    const int32_t h = Virtual<int32_t>(picture, 0x28) - 1;
    const uint32_t columns = Columns();
    uint32_t u = static_cast<uint32_t>(w), v = static_cast<uint32_t>(w);
    for (int32_t r = At<int32_t>(self, 0x50); r <= At<int32_t>(self, 0x58); ++r) {
        for (int32_t c = At<int32_t>(self, 0x4C); c <= At<int32_t>(self, 0x54); ++c) {
            const std::size_t index = static_cast<std::size_t>(columns) * r + c;
            const float depth = At<float>(*depthRecords, index * 0x24);
            int32_t rampIndex = static_cast<int32_t>(static_cast<double>(Global<float>(addr::STE_WaterRampScale)) * depth);
            if (rampIndex < 0)
                rampIndex = 0;
            else if (static_cast<uint32_t>(rampIndex) > Global<uint32_t>(addr::STE_WaterDepthRampLast))
                rampIndex = static_cast<int32_t>(Global<uint32_t>(addr::STE_WaterDepthRampLast));
            const uint32_t pixel = Virtual<uint32_t>(Global<void*>(addr::STE_WaterDepthRamp), 0x54, 0, rampIndex);
            double red = (pixel >> 16) & 0xFF;
            float green = static_cast<float>((pixel >> 8) & 0xFF);
            float blue = static_cast<float>(pixel & 0xFF);
            if (!fading) {
                switch (Global<int32_t>(addr::STE_Rotation)) {
                case 0: v = static_cast<uint32_t>(r), u = static_cast<uint32_t>(c); break;
                case 1: v = Global<uint32_t>(addr::STE_CellsZ) - c, u = static_cast<uint32_t>(r); break;
                case 2: v = Global<uint32_t>(addr::STE_CellsZ) - r, u = Global<uint32_t>(addr::STE_CellsX) - c; break;
                case 3: u = Global<uint32_t>(addr::STE_CellsX) - r, v = static_cast<uint32_t>(c); break;
                default: break;
                }
                uint32_t y = v * static_cast<uint32_t>(h) / Global<uint32_t>(addr::STE_Rows);
                if (static_cast<int32_t>(y) > h)
                    y = static_cast<uint32_t>(h);
                uint32_t x = u * static_cast<uint32_t>(w) / Columns();
                if (static_cast<int32_t>(x) > w)
                    x = static_cast<uint32_t>(w);
                if (usePicture) {
                    const uint32_t mix = Virtual<uint32_t>(picture, 0x54, x, y) & 0xFF;
                    const double a = static_cast<double>(mix) * 0.00392156886f; // 1/255 as the game's float
                    const float keep = static_cast<float>(1.0 - a);
                    const float add = static_cast<float>(a * 255.0f);
                    const float redF = static_cast<float>(red);
                    red = static_cast<double>(keep) * redF + add;
                    green = static_cast<float>(static_cast<double>(keep) * green + add);
                    blue = static_cast<float>(static_cast<double>(keep) * blue + add);
                }
            } else {
                red = red * tint[0];
                green = static_cast<float>(static_cast<double>(tint[1]) * green);
                blue = static_cast<float>(static_cast<double>(tint[2]) * blue);
            }
            char* const vertex = Global<char*>(addr::STE_WaterVertices) + index * 32;
            vertex[0xE] = static_cast<char>(Truncated(red));
            vertex[0xD] = static_cast<char>(Truncated(green));
            vertex[0xC] = static_cast<char>(Truncated(blue));
            if (depth >= alphaDepth) {
                vertex[0xF] = static_cast<char>(0xFF);
            } else if (!(depth > 0.0f) && depth == depth) {
                vertex[0xF] = static_cast<char>(minAlpha);
            } else {
                const float rounded = static_cast<float>((static_cast<double>(depth) * alphaScale + minAlpha) + 12582912.0f);
                std::memcpy(&vertex[0xF], &rounded, 1);
            }
        }
    }
    if (plain) {
        // As the game's loop: the row's columns once, then its outer counter is past the end.
        char* const water = Global<char*>(addr::STE_WaterVertices);
        for (int32_t r = At<int32_t>(self, 0x50); r <= At<int32_t>(self, 0x58); ++r) {
            int32_t i = At<int32_t>(self, 0x4C) + static_cast<int32_t>(columns) * r;
            const int32_t last = At<int32_t>(self, 0x54) + static_cast<int32_t>(columns) * r;
            for (; i <= last; ++i)
                for (int32_t c = At<int32_t>(self, 0x4C); c <= At<int32_t>(self, 0x54); ++c, ++i)
                    water[static_cast<std::size_t>(i) * 32 + 0xF] = static_cast<char>(0xFF);
        }
    }
    At<uint8_t>(self, 0x48) = 1;
}

// --- The skirts along the city edges ---------------------------------------------------

int32_t MagicInt(float rounded)
{
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof bits);
    return static_cast<int32_t>(bits + 0xB4C00000u);
}

int32_t Floor(double value)
{
    const int32_t n = MagicInt(static_cast<float>((value - 0.5f) + 12582912.0f));
    return value == static_cast<double>(n + 1) ? n + 1 : n;
}

int32_t Ceil(double value)
{
    const int32_t n = MagicInt(static_cast<float>((0.5f + value) + 12582912.0f));
    return value == static_cast<double>(n - 1) ? n - 1 : n;
}

// 0x005D7420 (as in terrain_view.cpp).
bool Intersect(const float* point, const float* direction, const float* other, const float* otherDirection, float* out)
{
    const double ax = static_cast<double>(other[0]) - point[0];
    const double ay = static_cast<double>(other[1]) - point[1];
    const double negY = -static_cast<double>(otherDirection[1]);
    const double x = otherDirection[0];
    const double den = x * direction[1] + negY * direction[0];
    if (std::fabs(den) < 0.01f)
        return false;
    const double t = (x * ay + negY * ax) / den;
    out[0] = static_cast<float>(t * direction[0] + point[0]);
    out[1] = static_cast<float>(t * direction[1] + point[1]);
    return true;
}

// The part of a city edge between the two frustum edges, as cell numbers
// along it: the edge's line (from its first vertex along the edge) met with
// both, floored and ceiled, mirrored for the sides that run backwards, and
// clamped to the edge's vertices.
struct EdgeSpan {
    int32_t first, last, count; // count: vertices along the edge
};

EdgeSpan EdgeInView(char* context, const char* vertices, std::size_t stride, std::size_t pointZ, uint32_t side,
                    float (&a)[2], float (&b)[2])
{
    char* const camera = At<char*>(context, 0x24);
    const uint32_t odd = side & 1;
    const int32_t cells = static_cast<int32_t>(Global<uint32_t>(odd ? addr::STE_CellsZ : addr::STE_CellsX));
    const float point[2] = {*reinterpret_cast<const float*>(vertices), *reinterpret_cast<const float*>(vertices + pointZ)};
    const char* const end = vertices + static_cast<std::size_t>(cells) * stride;
    const float along[2] = {
        static_cast<float>(static_cast<double>(*reinterpret_cast<const float*>(end)) - *reinterpret_cast<const float*>(vertices)),
        static_cast<float>(static_cast<double>(*reinterpret_cast<const float*>(end + 8)) -
                           *reinterpret_cast<const float*>(vertices + 8))};
    Intersect(point, along, &At<float>(camera, 0x3BC), &At<float>(camera, 0x3C4), a);
    Intersect(point, along, &At<float>(camera, 0x3CC), &At<float>(camera, 0x3D4), b);
    const double perMetre = Global<float>(addr::STE_CellsPerMetre);
    const bool swap = !(a[odd] < b[odd]);
    int32_t first = Floor(perMetre * (swap ? b[odd] : a[odd]));
    int32_t last = Ceil(perMetre * (swap ? a[odd] : b[odd]));
    if (side == 1 || side == 2) {
        const int32_t mirroredFirst = cells - last;
        last = cells - first;
        first = mirroredFirst;
    }
    const int32_t count = static_cast<int32_t>(Global<uint32_t>(odd ? addr::STE_Rows : addr::STE_Columns));
    const int32_t top = count - 1;
    if (first < 0)
        first = 0;
    else if (first > top)
        first = top;
    if (last > top)
        last = top;
    else if (last < 0)
        last = 0;
    return {first, last, count};
}

// The runs of cells where the edge's top row is above its bottom row, each
// widened by a cell at its start.
void DrawEdgeRuns(char* context, const char* vertices, std::size_t stride, const EdgeSpan& span)
{
    int32_t run = -1;
    if (span.first > span.last)
        return;
    int32_t j = span.first;
    for (; j <= span.last; ++j) {
        const float topY = *reinterpret_cast<const float*>(vertices + static_cast<std::size_t>(j) * stride + 4);
        const float bottomY =
            *reinterpret_cast<const float*>(vertices + static_cast<std::size_t>(span.count + j) * stride + 4);
        if (topY > bottomY) {
            if (run < 0) {
                run = j - 1;
                if (run < 0)
                    run = 0;
            }
        } else if (run >= 0) {
            if ((j - run) * 2 > 0)
                CallThis<void>(addr::SC4RenderContext_DrawIndexed, context, 0u, run * 6, (j - run) * 6);
            run = -1;
        }
    }
    if (run < 0)
        return;
    if (j > span.count - 1)
        j = span.count - 1;
    if ((j - run) * 2 > 0)
        CallThis<void>(addr::SC4RenderContext_DrawIndexed, context, 0u, run * 6, (j - run) * 6);
}

// 0x007551C0: the water's faces along the city edges, when the water level
// (terrain slot 29) is not below the terrain's slot 21 height.
void DrawWaterSkirts(char* self, char* context)
{
    void* const terrain = At<void*>(self, 0x20);
    const float level = Virtual<float>(terrain, 0x74);
    if (Virtual<float>(terrain, 0x54) > level)
        return;
    CallThis<void>(addr::SC4RenderContext_SetState2850, context, uint8_t{0});
    CallThis<void>(addr::SC4RenderContext_SetState3EF0, context, uint8_t{0}, uint8_t{0});
    CallThis<void>(addr::SC4RenderContext_SetIndexArray, context, Global<uint32_t>(addr::STE_IndicesPerRow),
                   Global<uint16_t*>(addr::STE_SkirtIndices));
    CallThis<void>(addr::SC4RenderContext_SetState28A0, context, 1u);
    const int32_t depthOffset = At<int32_t>(context, 0xBC);
    float a[2] = {0.0f, 0.0f}, b[2] = {0.0f, 0.0f}; // stack locals in the game: kept across sides
    for (uint32_t side = 0; side < 4; ++side) {
        const bool facing = side == At<uint32_t>(self, 0x5C) || side == At<uint32_t>(self, 0x60);
        CallThis<void>(addr::SC4RenderContext_SetDepthOffset, context, facing ? -1 : -5);
        const char* const vertices = (&Global<char*>(addr::STE_WaterSkirts))[side];
        const EdgeSpan span = EdgeInView(context, vertices, 16, 0x18, side, a, b);
        CallThis<void>(addr::SC4RenderContext_SetVertexArray, context, 1u, Columns() * 2, vertices);
        DrawEdgeRuns(context, vertices, 16, span);
    }
    CallThis<void>(addr::SC4RenderContext_SetDepthOffset, context, depthOffset);
    CallThis<void>(addr::SC4RenderContext_SetState28A0, context, 3u);
    CallThis<void>(addr::SC4RenderContext_SetState3EF0, context, uint8_t{1}, uint8_t{0});
    CallThis<void>(addr::SC4RenderContext_SetState2850, context, uint8_t{1});
}

// 0x00753E00: the land's faces along the two city edges that face the
// camera, in three textured layers.
uint32_t DrawLandSkirts(char* self, char* context)
{
    static const uint32_t kLayers[3] = {0x11, 0x10, 0x15};
    const int32_t depthOffset = At<int32_t>(context, 0xBC);
    CallThis<void>(addr::SC4RenderContext_SetDepthOffset, context, 1);
    CallThis<void>(addr::SC4RenderContext_SetState40B0, context, 1u, 0u);
    CallThis<void>(addr::SC4RenderContext_SetIndexArray, context, Global<uint32_t>(addr::STE_IndicesPerRow),
                   Global<uint16_t*>(addr::STE_SkirtIndices));
    float a[2] = {0.0f, 0.0f}, b[2] = {0.0f, 0.0f};
    char* const textures = Global<char*>(addr::STE_TexturesByZoom + 4 * Global<int32_t>(addr::STE_Zoom));
    for (uint32_t side = 0; side < 4; ++side) {
        if (side != At<uint32_t>(self, 0x5C) && side != At<uint32_t>(self, 0x60))
            continue;
        char* const* const layers = &Global<char*>(addr::STE_LandSkirts) + 3 * side;
        const EdgeSpan span = EdgeInView(context, layers[0], 24, 8, side, a, b);
        for (int k = 0; k < 3; ++k) {
            CallThis<void>(addr::SC4RenderContext_SetVertexArray, context, 10u, static_cast<uint32_t>(span.count) * 2,
                           layers[k]);
            CallThis<void>(addr::SC4RenderContext_BindTexture, context,
                           ***reinterpret_cast<uint32_t***>(textures + 4 * kLayers[k]), 0u);
            DrawEdgeRuns(context, layers[k], 24, span);
        }
    }
    CallThis<void>(addr::SC4RenderContext_SetDepthOffset, context, depthOffset);
    return 1;
}

// 0x007572A0.
void DrawWater(char* self, char* context, void* buffers)
{
    if (Global<uint8_t>(addr::STE_DebugCells))
        return;
    if (!Virtual<uint8_t>(At<void*>(self, 0x20), 0x14C) || !Global<uint8_t>(addr::STE_ShowWater))
        return;
    if (!At<uint8_t>(self, 0x48)) {
        static uint32_t refreshes = 0;
        ++refreshes;
        if ((refreshes & (refreshes - 1)) == 0)
            log::Write("terrain draw: %u water colour refreshes (ours)", refreshes);
        RefreshWaterColours(self);
    }
    DrawWaterSkirts(self, context);
    DrawWaterSurface(self, context, buffers);
}

// --- The grid --------------------------------------------------------------------------

// The extra quads of a row between two columns (0x00B4C6B8 counts them per
// column, 0x00B4C6B0 holds them per row).
struct QuadRun {
    const char* vertices;
    int32_t count; // vertices
};

QuadRun RowQuadRun(int32_t row, uint32_t perRow, int32_t first, int32_t end)
{
    const uint16_t* const offsets = Global<uint16_t*>(addr::STE_RowQuadOffsets);
    const int32_t base = static_cast<int32_t>(perRow) * row;
    const int32_t from = offsets[base + first], to = offsets[base + end - 1];
    if (from >= to)
        return {nullptr, 0};
    const char* const quads = At<const char*>(Global<char*>(addr::STE_RowQuads), static_cast<std::size_t>(row) * 12);
    return {quads + static_cast<std::size_t>(from) * 0x80, (to - from) * 4};
}

struct VertexRect {
    int32_t x0, z0, x1, z1;
};

// The rows of the view inside the rectangle: [first, last).
VertexRect ClampRows(char* self, VertexRect r)
{
    r.z0 = std::max(std::max(r.z0, 0), At<int32_t>(self, 0x98));
    r.z1 = std::min(At<int32_t>(self, 0x9C), std::min(r.z1, Global<int32_t>(addr::STE_Rows))) - 1;
    r.x0 = std::max(r.x0, 0);
    r.x1 = std::min(r.x1, Global<int32_t>(addr::STE_Columns));
    return r;
}

// 0x007545C0: the land rows inside a rectangle through the dynamic buffers,
// then the rows' extra quads.
void DrawRowsBuffered(char* self, char* context, VertexRect rect)
{
    if (!LockBatch())
        return;
    Global<char*>(addr::STE_BatchSource) = Land();
    Global<uint16_t*>(addr::STE_IndexTemplate) = Global<uint16_t*>(addr::STE_RowIndices);
    Global<uint8_t>(addr::STE_IndexTemplatePerRow) = 1;
    const VertexRect r = ClampRows(self, rect);
    for (int32_t row = r.z0; row < r.z1; ++row) {
        if (!RowShown(row))
            continue;
        const int32_t first = std::max(r.x0, RowStarts()[row]);
        const int32_t last = std::min(RowEnds()[row], r.x1) - 1;
        if ((last - first) * 2 > 0)
            AddStrip(row, first, last);
    }
    FlushBatch();

    void* buffers = nullptr;
    Virtual<uint8_t>(At<void*>(context, 0x30), 0, 0x09CD86F9u, &buffers);
    const int32_t buffer = Virtual<int32_t>(buffers, 0xC, 0xBu);
    char* locked = nullptr;
    int32_t triangles = 0;
    const auto draw = [&] {
        Virtual<void>(buffers, 0x20, buffer);
        Virtual<void>(buffers, 0x28, buffer, 0u, triangles * 3, Global<uint16_t*>(addr::STE_QuadIndices));
    };
    for (int32_t row = r.z0; row < r.z1; ++row) {
        if (!RowShown(row))
            continue;
        const int32_t first = std::max(r.x0, RowStarts()[row]);
        const int32_t end = std::min(RowEnds()[row], r.x1);
        if (end <= first)
            continue;
        const QuadRun run = RowQuadRun(row, Global<uint32_t>(addr::STE_CellsX) + 1, first, end);
        if (!run.vertices)
            continue;
        if (locked) {
            const int32_t total = run.count / 2 + triangles;
            char* appended = nullptr;
            if (static_cast<uint32_t>(total * 3) <= Global<uint32_t>(addr::STE_IndicesPerRow))
                appended = Virtual<char*>(buffers, 0x1C, buffer, run.count);
            if (appended) {
                triangles = total;
                locked = appended;
                std::memcpy(locked, run.vertices, static_cast<std::size_t>(run.count) * 32);
                continue;
            }
            draw();
        }
        locked = Virtual<char*>(buffers, 0x18, buffer, run.count);
        triangles = run.count / 2;
        if (locked)
            std::memcpy(locked, run.vertices, static_cast<std::size_t>(run.count) * 32);
    }
    if (locked)
        draw();
    Virtual<uint32_t>(buffers, 8);
}

// 0x007541C0: the same with vertex arrays.
void DrawRowsArrays(char* self, char* context, VertexRect rect)
{
    const VertexRect r = ClampRows(self, rect);
    for (int32_t row = r.z0; row < r.z1; ++row) {
        if (!RowShown(row))
            continue;
        const int32_t first = std::max(r.x0, RowStarts()[row]);
        const int32_t end = std::min(RowEnds()[row], r.x1);
        if (first >= end)
            continue;
        SetRowArrays(context, row);
        const int32_t triangles = (end - first) * 2 - 2;
        if (triangles > 0)
            CallThis<void>(addr::SC4RenderContext_DrawIndexed, context, 0u, first * 6, triangles * 3);
        const QuadRun run = RowQuadRun(row, Columns(), first, end);
        if (run.vertices)
            CallThis<void>(addr::SC4RenderContext_DrawArrays, context, 6u, 0xBu, run.count, run.vertices);
    }
}

// The grid's alpha at a vertex: opaque from 2 m above the water (the pass
// takes 2 m off first, so 4 m), none below, 0.5 per metre between, through
// the 49152.0 rounding trick.
uint8_t GridAlpha(float land, float water, double slope)
{
    const double above = (static_cast<double>(land) - water) - 2.0f;
    const float stored = static_cast<float>(above);
    if (above >= 2.0)
        return 0xFF;
    if (stored < 0.0f)
        return 0;
    const float scaled = static_cast<float>(stored * slope + 49152.0f);
    uint32_t bits;
    std::memcpy(&bits, &scaled, 4);
    bits += 0xB8C00000;
    return static_cast<uint8_t>(bits - (bits >> 8));
}

union Cell {
    float f;
    uint32_t u;
};

// 0x00757650.
uint32_t DrawGrid(char* self, char* context, void* buffers)
{
    if (!At<void*>(self, 0x88) || At<uint8_t>(At<char*>(At<char*>(context, 0x38), 0xC), 0x39C))
        return 0;
    uint32_t& flags = Global<uint32_t>(addr::STE_GridStaticFlags);
    uint32_t* const state = &Global<uint32_t>(addr::STE_GridState);
    if (!(flags & 1)) {
        flags |= 1;
        state[0] = 0;
        state[1] = 0;
    }
    if (!Global<uint8_t>(addr::STE_GridStateReady)) {
        CallThis<void>(addr::SC4RenderState_Init, state, 1u, 0u, 1u, 0u, 1u, 1u, 1u, 3u, 4u, 5u, 0xFFFFFFFFu, -1.0f, 1u,
                       0xFFFFFFFFu);
        Global<uint8_t>(addr::STE_GridStateReady) = 1;
    }
    CallThis<void>(addr::SC4RenderContext_SetRenderState, context, state);
    CallThis<void>(addr::SC4RenderContext_BindTexture, context, **reinterpret_cast<uint32_t**>(At<void*>(self, 0x88)), 0u);
    CallThis<void>(addr::SC4RenderContext_SetState4280, context, 1u, 7u, 0u);
    CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 3u, 3u, 0u);

    // The texture matrix from the camera's: the game's stack frame as cells,
    // the arithmetic translated from 0x00757775 to 0x0075821F.
    Cell L[105] = {};
    for (int i = 6; i < 22; ++i)
        L[i].u = Global<uint32_t>(addr::STE_MatrixFill);
    CallThis<void>(addr::SC4RenderContext_UpdateViewMatrix, context);
    CallThis<void>(addr::SC4RenderContext_GetViewMatrix, context, &L[6]);
#include "core/terrain_grid_matrix.inc"
    L[56].u = r436;
    L[57].u = r437;
    CallThis<void>(addr::SC4RenderContext_SetTextureMatrix, context, &L[42], 0u);

    const int32_t firstRow = At<int32_t>(self, 0x98), endRow = At<int32_t>(self, 0x9C);
    if (firstRow < endRow) {
        uint32_t seen = flags;
        double slope = Global<float>(addr::STE_GridAlphaSlope);
        const int32_t columns = static_cast<int32_t>(Columns());
        for (int32_t row = firstRow; row < endRow; ++row) {
            const int32_t first = RowStarts()[row] + columns * row, end = RowEnds()[row] + columns * row;
            for (int32_t i = first; i < end; ++i) {
                if (!(seen & 2)) {
                    seen |= 2;
                    slope = 0.5f;
                }
                char* const land = Land() + static_cast<std::size_t>(i) * 32;
                const char* const water = Global<char*>(addr::STE_WaterVertices) + static_cast<std::size_t>(i) * 32;
                At<uint8_t>(land, 0xF) = GridAlpha(At<float>(land, 4), At<float>(const_cast<char*>(water), 4), slope);
            }
        }
        Global<float>(addr::STE_GridAlphaSlope) = static_cast<float>(slope);
        flags = seen;
    }

    void* const driver = At<void*>(context, 0x30);
    const VertexRect all = {0, 0, static_cast<int32_t>(L[70].u), static_cast<int32_t>(L[71].u)};
    Virtual<void>(driver, 0x88, 0u, 1u);
    if (buffers)
        DrawRowsBuffered(self, context, all);
    else
        DrawRowsArrays(self, context, all);
    Virtual<void>(driver, 0x88, 1u, 1u);
    CallThis<void>(addr::SC4RenderContext_ClearTextureMatrix, context, 0u);
    return 1;
}

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
    if (!g_compareLand)
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

using WaterFn = void(__fastcall*)(char*, void*, char*, void*);
WaterFn g_water;

void __fastcall DWater(char* self, void*, char* context, void* buffers)
{
    if (g_depth > 0)
        return g_water(self, nullptr, context, buffers);
    if (!g_compareWater)
        return DrawWater(self, context, buffers);
    DrawCompared(g_selfCheck ? "terrain.water.selfcheck" : "terrain.water", PassRegions(self, context),
                 [&] {
                     if (g_selfCheck)
                         g_water(self, nullptr, context, buffers);
                     else
                         DrawWater(self, context, buffers);
                 },
                 [&] { g_water(self, nullptr, context, buffers); });
}

using SkirtsFn = uint32_t(__fastcall*)(char*, void*, char*);
SkirtsFn g_landSkirts;
bool g_compareSkirts = false;

uint32_t __fastcall DLandSkirts(char* self, void*, char* context)
{
    if (g_depth > 0)
        return g_landSkirts(self, nullptr, context);
    if (!g_compareSkirts)
        return DrawLandSkirts(self, context);
    uint32_t result = 0;
    DrawCompared(g_selfCheck ? "terrain.landskirts.selfcheck" : "terrain.landskirts", PassRegions(self, context),
                 [&] {
                     if (g_selfCheck)
                         g_landSkirts(self, nullptr, context);
                     else
                         DrawLandSkirts(self, context);
                 },
                 [&] { result = g_landSkirts(self, nullptr, context); });
    return result;
}

using GridFn = uint32_t(__fastcall*)(char*, void*, char*, void*);
GridFn g_grid;
bool g_compareGrid = false;

uint32_t __fastcall DGrid(char* self, void*, char* context, void* buffers)
{
    if (g_depth > 0)
        return g_grid(self, nullptr, context, buffers);
    static bool logged[2] = {};
    if (!logged[buffers != nullptr]) {
        logged[buffers != nullptr] = true;
        log::Write("terrain draw: grid pass through %s", buffers ? "the driver's dynamic vertex buffers" : "vertex arrays");
    }
    if (!g_compareGrid)
        return DrawGrid(self, context, buffers);
    uint32_t result = 0;
    DrawCompared(g_selfCheck ? "terrain.grid.selfcheck" : "terrain.grid", PassRegions(self, context),
                 [&] {
                     if (g_selfCheck)
                         g_grid(self, nullptr, context, buffers);
                     else
                         DrawGrid(self, context, buffers);
                 },
                 [&] { result = g_grid(self, nullptr, context, buffers); });
    return result;
}

OPENSC4_THUNK(TGrid, DGrid, 8)
OPENSC4_THUNK(TLandSkirts, DLandSkirts, 4)
OPENSC4_THUNK(TLand, DLand, 8)
OPENSC4_THUNK(TWater, DWater, 8)

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
    g_compareLand = mode == registry::Mode::Compare;
    if (!Detour(addr::cSTETerrainView3D_DrawLand, &TLand, &g_land))
        return false;
    log::Write("terrain draw: land pass %s", g_compareLand ? (g_selfCheck ? "compared, the game's against itself" : "compared with the game's") : "ours");
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

bool InstallWater(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareWater = mode == registry::Mode::Compare;
    if (!Detour(addr::cSTETerrainView3D_DrawWater, &TWater, &g_water))
        return false;
    log::Write("terrain draw: water pass %s", g_compareWater ? (g_selfCheck ? "compared, the game's against itself" : "compared with the game's") : "ours");
    return true;
}

const bool g_registeredWater = registry::Register({
    "renderer.terrain.water",
    "renderer/terrain",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSTETerrainView3D_DrawWater,
    "The terrain's water pass (the surface ours; colour refresh and skirts still the game's); compare mode as "
    "renderer.terrain.land (docs/renderer.md, \"The terrain\")",
    true,
    InstallWater,
});

bool InstallSkirts(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareSkirts = mode == registry::Mode::Compare;
    if (!Detour(addr::cSTETerrainView3D_DrawLandSkirts, &TLandSkirts, &g_landSkirts))
        return false;
    log::Write("terrain draw: land skirts %s", g_compareSkirts ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredSkirts = registry::Register({
    "renderer.terrain.skirts",
    "renderer/terrain",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSTETerrainView3D_DrawLandSkirts,
    "The land's faces along the city edges (the water's are part of renderer.terrain.water); compare mode as "
    "renderer.terrain.land (docs/renderer.md, \"The terrain\")",
    true,
    InstallSkirts,
});

bool InstallGrid(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareGrid = mode == registry::Mode::Compare;
    if (!Detour(addr::cSTETerrainView3D_DrawGrid, &TGrid, &g_grid))
        return false;
    log::Write("terrain draw: grid %s", g_compareGrid ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredGrid = registry::Register({
    "renderer.terrain.grid",
    "renderer/terrain",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSTETerrainView3D_DrawGrid,
    "The cell grid over the land; compare mode as renderer.terrain.land (docs/renderer.md, \"The terrain\")",
    true,
    InstallGrid,
});

} // namespace
} // namespace opensc4::terrain_draw
