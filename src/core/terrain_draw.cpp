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
// cache and, apart from it, its texture stage cache), the S3D camera (depth offset, projection), the land vertices
// (colours and blend weights are written while drawing).
std::vector<Region> PassRegions(char* self, char* context)
{
    char* const render = At<char*>(context, 0x34);
    char* const camera = render ? At<char*>(render, 0xD8) : nullptr;
    const std::size_t vertices = static_cast<std::size_t>(Global<uint32_t>(addr::STE_VertexCount)) * 32;
    std::vector<Region> regions{{"view", self, kViewSize}, {"context", context, kContextSize}};
    if (camera)
        regions.push_back({"camera", camera, kCameraSize});
    // The context's texture stage cache: a vector of 0x68-byte stages.
    if (char* const stages = At<char*>(context, 0xE0))
        regions.push_back({"stages", stages, static_cast<std::size_t>(At<char*>(context, 0xE4) - stages)});
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
void DrawCompared(const char* id, const std::vector<Region>& regions, Ours&& ours, Original&& original,
                  bool localVertices = false)
{
    std::vector<std::vector<char>> before, mine;
    for (const Region& r : regions)
        before.emplace_back(r.data, r.data + r.size);
    const std::vector<char> pipeline = gd::SaveState();
    ++g_depth;
    gd::BeginCapture(false);
    ours();
    const std::vector<uint32_t> ourDraws = gd::EndCapture();
    std::vector<char> ourPipeline = gd::SaveState();
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
    std::vector<char> gamePipeline = gd::SaveState();
    // A pass that draws from an array on its own stack leaves the driver
    // with that pointer (the pipeline state's last member), which differs
    // between our frame and the game's.
    if (localVertices && gamePipeline.size() >= sizeof(void*) && ourPipeline.size() == gamePipeline.size()) {
        std::memset(gamePipeline.data() + gamePipeline.size() - sizeof(void*), 0, sizeof(void*));
        std::memset(ourPipeline.data() + ourPipeline.size() - sizeof(void*), 0, sizeof(void*));
    }
    if (gamePipeline != ourPipeline) {
        std::size_t at = 0;
        while (at < gamePipeline.size() && gamePipeline[at] == ourPipeline[at])
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

// Which rows and columns a rectangle draw goes by: the view's row range and
// the per-row tables of the land's rows or the water's.
struct RowSet {
    std::size_t first, end; // offsets in the view
    std::uintptr_t drawn, starts, ends;
};
constexpr RowSet kLandRows = {0x98, 0x9C, addr::STE_LandRowDrawn, addr::STE_LandRowStart, addr::STE_LandRowEnd};

bool Shown(const RowSet& set, int32_t row)
{
    const uint8_t* const drawn = Global<uint8_t*>(set.drawn);
    return drawn[row] || drawn[row + 1];
}

// The rows of the view inside the rectangle: [first, end).
VertexRect ClampRows(char* self, const RowSet& set, VertexRect r)
{
    r.z0 = std::max(std::max(r.z0, 0), At<int32_t>(self, set.first));
    r.z1 = std::min(At<int32_t>(self, set.end), std::min(r.z1, Global<int32_t>(addr::STE_Rows)));
    r.x0 = std::max(r.x0, 0);
    r.x1 = std::min(r.x1, Global<int32_t>(addr::STE_Columns));
    return r;
}

// 0x007545C0 and 0x00754910: the land vertices of the rows inside a
// rectangle through the dynamic buffers, then the rows' extra quads.
void DrawRowsBuffered(char* self, char* context, const RowSet& set, VertexRect rect)
{
    const int32_t* const starts = Global<int32_t*>(set.starts);
    const int32_t* const ends = Global<int32_t*>(set.ends);
    if (!LockBatch())
        return;
    Global<char*>(addr::STE_BatchSource) = Land();
    Global<uint16_t*>(addr::STE_IndexTemplate) = Global<uint16_t*>(addr::STE_RowIndices);
    Global<uint8_t>(addr::STE_IndexTemplatePerRow) = 1;
    VertexRect r = ClampRows(self, set, rect);
    --r.z1;
    for (int32_t row = r.z0; row < r.z1; ++row) {
        if (!Shown(set, row))
            continue;
        const int32_t first = std::max(r.x0, starts[row]);
        const int32_t last = std::min(ends[row], r.x1) - 1;
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
        if (!Shown(set, row))
            continue;
        const int32_t first = std::max(r.x0, starts[row]);
        const int32_t end = std::min(ends[row], r.x1);
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

// 0x007541C0 and 0x007543C0: the same with vertex arrays.
void DrawRowsArrays(char* self, char* context, const RowSet& set, uint32_t quadsPerRow, VertexRect rect)
{
    const int32_t* const starts = Global<int32_t*>(set.starts);
    const int32_t* const ends = Global<int32_t*>(set.ends);
    VertexRect r = ClampRows(self, set, rect);
    --r.z1;
    for (int32_t row = r.z0; row < r.z1; ++row) {
        if (!Shown(set, row))
            continue;
        const int32_t first = std::max(r.x0, starts[row]);
        const int32_t end = std::min(ends[row], r.x1);
        if (first >= end)
            continue;
        SetRowArrays(context, row);
        const int32_t triangles = (end - first) * 2 - 2;
        if (triangles > 0)
            CallThis<void>(addr::SC4RenderContext_DrawIndexed, context, 0u, first * 6, triangles * 3);
        const QuadRun run = RowQuadRun(row, quadsPerRow, first, end);
        if (run.vertices)
            CallThis<void>(addr::SC4RenderContext_DrawArrays, context, 6u, 0xBu, run.count, run.vertices);
    }
}

// 0x00755560 and 0x00755700: the water vertices of the rows inside a
// rectangle, with vertex arrays.
void DrawWaterRowsArrays(char* self, char* context, const RowSet& set, VertexRect rect)
{
    const VertexRect r = ClampRows(self, set, rect);
    if (r.z0 >= r.z1)
        return;
    CallThis<void>(addr::SC4RenderContext_SetIndexArray, context, Global<uint32_t>(addr::STE_IndicesPerRow),
                   Global<uint16_t*>(addr::STE_SkirtIndices));
    const int32_t* const starts = Global<int32_t*>(set.starts);
    const int32_t* const ends = Global<int32_t*>(set.ends);
    for (int32_t row = r.z0; row < r.z1 - 1; ++row) {
        if (!Shown(set, row))
            continue;
        const int32_t first = std::max(r.x0, starts[row]);
        const int32_t triangles = (std::min(ends[row], r.x1) - first) * 2 - 2;
        if (triangles <= 0)
            continue;
        const uint32_t columns = Columns();
        CallThis<void>(addr::SC4RenderContext_SetVertexArray, context, 0xBu, columns,
                       Global<char*>(addr::STE_WaterVertices) + static_cast<std::size_t>(columns) * row * 32);
        CallThis<void>(addr::SC4RenderContext_DrawIndexed, context, 0u, first * 6, triangles * 3);
    }
}

// cSC4OverlayManager's rectangle draw (0x00735720): a rectangle of cells
// (x0, z0, x1, z1, inclusive) of the terrain with the state the caller set,
// by the manager's flags: water vertices (+0x16) or land vertices, the
// water's rows (+0x15) or the land's, dynamic buffers (+0x94) or arrays.
void DrawOverlayRect(char* manager, char* context, const int32_t* cells)
{
    const VertexRect rect = {cells[0], cells[1], cells[2] + 1, cells[3] + 1};
    char* const view = At<char*>(manager, 0x8C);
    const bool waterRows = At<uint8_t>(manager, 0x15) != 0;
    if (At<uint8_t>(manager, 0x16)) {
        static constexpr RowSet kWater = {0xD4, 0xD8, addr::STE_WaterRowWater, addr::STE_WaterRowStart, addr::STE_WaterRowEnd};
        static constexpr RowSet kLand = {0xA4, 0xA8, addr::STE_LandRowWater, addr::STE_LandRowStart, addr::STE_LandRowEnd};
        DrawWaterRowsArrays(view, context, waterRows ? kWater : kLand, rect);
    } else if (At<void*>(manager, 0x94)) {
        static constexpr RowSet kWater = {0xC8, 0xCC, addr::STE_WaterRowDrawn, addr::STE_WaterRowStart, addr::STE_WaterRowEnd};
        DrawRowsBuffered(view, context, waterRows ? kWater : kLandRows, rect);
    } else {
        static constexpr RowSet kWater = {0xBC, 0xC0, addr::STE_WaterRowDrawn, addr::STE_WaterRowStart, addr::STE_WaterRowEnd};
        if (waterRows)
            DrawRowsArrays(view, context, kWater, Global<uint32_t>(addr::STE_CellsX) + 1, rect);
        else
            DrawRowsArrays(view, context, kLandRows, Columns(), rect);
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
        DrawRowsBuffered(self, context, kLandRows, all);
    else
        DrawRowsArrays(self, context, kLandRows, Columns(), all);
    Virtual<void>(driver, 0x88, 1u, 1u);
    CallThis<void>(addr::SC4RenderContext_ClearTextureMatrix, context, 0u);
    return 1;
}

// cSC4OverlayManager's partial rectangle draw (0x007356D0, through the
// terrain view's 0x00754C60 or 0x00754F10): cell by cell, each a quad of
// the land's vertices raised to the water level where that is higher, split
// along the diagonal the terrain gives for the cell.
void DrawOverlayRectPartial(char* manager, char* context, const int32_t* cells)
{
    char* const view = At<char*>(manager, 0x8C);
    const bool waterRows = At<uint8_t>(manager, 0x15) != 0;
    const std::size_t firstOffset = waterRows ? 0xBC : 0x8C, endOffset = waterRows ? 0xC0 : 0x90;
    const uint8_t* const drawn = Global<uint8_t*>(waterRows ? addr::STE_WaterRowDrawn : addr::STE_LandRowDrawn);
    const uint8_t* const water = Global<uint8_t*>(waterRows ? addr::STE_WaterRowWater : addr::STE_LandRowWater);
    const int32_t columns = static_cast<int32_t>(Columns());
    char quad[4 * 32];
    CallThis<void>(addr::SC4RenderContext_SetVertexArray, context, 0xBu, 4u, quad);
    const int32_t first = std::max(std::max(cells[1], 0), At<int32_t>(view, firstOffset));
    const int32_t last = std::min(At<int32_t>(view, endOffset), std::min(cells[3] + 1, Global<int32_t>(addr::STE_Rows))) - 1;
    for (int32_t row = first; row < last; ++row) {
        if (!drawn[row] && !drawn[row + 1] && !water[row] && !water[row + 1])
            continue;
        const int32_t end = std::min(RowEnds()[row], cells[2] + 1);
        for (int32_t column = std::max(cells[0], RowStarts()[row]); column < end; ++column) {
            const int32_t at = columns * row + column;
            const int32_t corners[4] = {at, at + 1, at + columns, at + columns + 1};
            for (int k = 0; k < 4; ++k) {
                char* const vertex = quad + k * 32;
                std::memcpy(vertex, Land() + static_cast<std::size_t>(corners[k]) * 32, 32);
                const float level = At<float>(Global<char*>(addr::STE_WaterVertices), static_cast<std::size_t>(corners[k]) * 32 + 4);
                if (At<float>(vertex, 4) < level)
                    At<float>(vertex, 4) = level;
            }
            const bool turned = Virtual<uint8_t>(At<void*>(view, 0x20), 0x118, column, row) != 0;
            CallThis<void>(addr::SC4RenderContext_SetIndexArray, context, 6u,
                           reinterpret_cast<uint16_t*>(turned ? addr::STE_QuadIndicesTurned : addr::STE_QuadIndicesPlain));
            CallThis<void>(addr::SC4RenderContext_DrawIndexed, context, 0u, 0, 6);
        }
    }
}

// cSC4OverlayManager's decal pass (0x00736790). An overlay is a 0xB4-byte
// record in the manager's vector at +0x98: +0 -1 when in use, +4 flags, +8
// the index of its render state (the manager's +0x24, 8 bytes each), +0xC
// its rectangle of cells, +0x1C its matrix, +0x9C a driver value, +0xA0 its
// colour, +0xAC its texture or +0xB0 an object that gives one.
void DrawDecals(char* manager, const float* matrix, char* context, char** list)
{
    if (list[0] == list[1])
        return;
    CallThis<void>(addr::SC4RenderContext_SetDepthOffset, context, 2);
    const uint32_t wanted = At<uint8_t>(At<char*>(At<char*>(context, 0x38), 0xC), 0x39C) ? 0x91 : 0x11;
    At<uint32_t>(manager, 0xD8) = 0;
    void* const driver = At<void*>(context, 0x30);
    for (uint32_t i = 0; i < static_cast<uint32_t>((list[1] - list[0]) >> 2); ++i) {
        char* const overlay = At<char*>(manager, 0x98) + At<int32_t>(list[0], i * 4) * 0xB4;
        if (At<int32_t>(overlay, 0) != -1 || (At<uint32_t>(overlay, 4) & wanted) != wanted)
            continue;
        const uint32_t flags = At<uint32_t>(overlay, 4);
        CallThis<void>(addr::SC4RenderContext_SetDriverValue84, context, At<uint32_t>(overlay, 0x9C));
        if (flags & 0x40) {
            CallThis<void>(addr::SC4RenderContext_SetColourAlt, context, overlay + 0xA0);
            Virtual<void>(driver, 0x88, 0u, 0u);
        } else {
            CallThis<void>(addr::SC4RenderContext_SetColour, context, overlay + 0xA0);
            Virtual<void>(driver, 0x88, 1u, 0u);
        }
        CallThis<void>(addr::SC4RenderContext_SetRenderState, context, At<char*>(manager, 0x24) + At<uint8_t>(overlay, 8) * 8);
        const uint32_t texture = At<void*>(overlay, 0xAC) ? **reinterpret_cast<uint32_t**>(At<void*>(overlay, 0xAC))
                                                         : Virtual<uint32_t>(At<void*>(overlay, 0xB0), 0x10);
        CallThis<void>(addr::SC4RenderContext_BindTexture, context, texture, 0u);
        CallThis<void>(addr::SC4RenderContext_SetState4280, context, 1u, 7u, 0u);
        if (flags & 0x20)
            CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 3u, 2u, 0u);
        else if (flags & 2)
            CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 3u, 3u, 0u);
        else
            CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 2u, 2u, 0u);
        Cell L[38] = {};
        const float* const record = reinterpret_cast<const float*>(overlay);
#include "core/overlay_decal_matrix.inc"
        CallThis<void>(addr::SC4RenderContext_SetTextureMatrix, context, &L[10], 0u);
        CallThis<void>(addr::cSC4OverlayManager_DrawRect, manager, context, overlay + 0xC);
        ++At<uint32_t>(manager, 0xD8);
    }
    CallThis<void>(addr::SC4RenderContext_ResetDriverValue84, context);
    CallThis<void>(addr::SC4RenderContext_ResetColour, context);
    Virtual<void>(driver, 0x88, 1u, 1u);
}

// cSC4OverlayManager's plain shadow pass (0x00737870): the overlays with
// flags 9, all with the manager's shadow texture and one render state, each
// with a texture matrix from its second matrix (+0x5C).
void DrawShadowsPlain(char* manager, const float* matrix, char* context, char** list)
{
    if (list[0] == list[1])
        return;
    CallThis<void>(addr::SC4RenderContext_SetRenderState, context, At<char*>(manager, 0x24) + 0x20);
    void* const driver = At<void*>(context, 0x30);
    const bool wasOn = Virtual<uint8_t>(driver, 0xB8, 7u) != 0;
    Virtual<void>(driver, 0xB0, 7u);
    Cell light[4];
    for (Cell& c : light)
        c.u = Global<uint32_t>(0x00B4B3C8);
    float amount;
    void* const source = Virtual<void*>(Global<void*>(addr::SC4_Lighting43DD0), 0x24);
    Virtual<void>(source, 0x38, &light[3], &amount, &light[0]);
    CallThis<void>(addr::SC4RenderContext_SetLightVector, context, &light[0]);
    if (amount > Global<double>(0x00A80990)) {
        CallThis<void>(addr::SC4RenderContext_SetState3E90, context, 1u);
        CallThis<void>(addr::SC4RenderContext_SetState2F50, context, 6u,
                       static_cast<float>(static_cast<double>(light[3].f) * amount));
    } else {
        CallThis<void>(addr::SC4RenderContext_SetState3E90, context, 0u);
    }
    CallThis<void>(addr::SC4RenderContext_BindTexture, context, ***reinterpret_cast<uint32_t***>(manager + 0xD4), 0u);
    CallThis<void>(addr::SC4RenderContext_SetState4280, context, 1u, 1u, 0u);
    CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 2u, 2u, 0u);
    CallThis<void>(addr::SC4RenderContext_SetState3030, context, manager + 0x38, 0u);
    CallThis<void>(addr::SC4RenderContext_SetDepthOffset, context, 3);
    At<uint32_t>(manager, 0xDC) = 0;
    for (uint32_t i = 0; i < static_cast<uint32_t>((list[1] - list[0]) >> 2); ++i) {
        char* const overlay = At<char*>(manager, 0x98) + At<int32_t>(list[0], i * 4) * 0xB4;
        if (At<int32_t>(overlay, 0) != -1 || (At<uint32_t>(overlay, 4) & 9) != 9)
            continue;
        Cell L[43] = {};
        const float* const record = reinterpret_cast<const float*>(overlay);
#include "core/overlay_shadow_matrix.inc"
        CallThis<void>(addr::SC4RenderContext_SetTextureMatrix, context, &L[15], 0u);
        CallThis<void>(addr::cSC4OverlayManager_DrawRect, manager, context, overlay + 0xC);
        ++At<uint32_t>(manager, 0xDC);
    }
    if (!wasOn)
        Virtual<void>(driver, 0xB4, 7u);
}

// cSC4OverlayManager's two-stage shadow pass (0x00736BF0): each overlay's
// own texture in stage 0 with the manager's shadow texture in stage 1, or
// the shadow texture alone when the manager's +0x1C is 4. L is the game's
// stack frame: the light vector at L[7..10], the amount at L[5], the
// strength at L[12].
void DrawShadows(char* manager, const float* matrix, char* context, char** list)
{
    if (list[0] == list[1])
        return;
    CallThis<void>(addr::SC4RenderContext_SetRenderState, context, At<char*>(manager, 0x24) + 0x20);
    Cell L[117] = {};
    for (int i = 7; i < 11; ++i)
        L[i].u = Global<uint32_t>(0x00B4B3C8);
    void* const source = Virtual<void*>(Global<void*>(addr::SC4_Lighting43DD0), 0x24);
    Virtual<void>(source, 0x38, &L[10], &L[5], &L[7]);
    CallThis<void>(addr::SC4RenderContext_SetLightVector, context, &L[7]);
    L[12].u = L[10].u;
    const double none = Global<double>(0x00A80990);
    if (L[5].f > none) {
        CallThis<void>(addr::SC4RenderContext_SetState3E90, context, 1u);
        CallThis<void>(addr::SC4RenderContext_SetState2F50, context, 6u,
                       static_cast<float>(static_cast<double>(L[12].f) * L[5].f));
    } else {
        CallThis<void>(addr::SC4RenderContext_SetState3E90, context, 0u);
    }
    const bool single = At<int32_t>(manager, 0x1C) == 4;
    CallThis<void>(addr::SC4RenderContext_SetState4280, context, 1u, 1u, 0u);
    CallThis<void>(addr::SC4RenderContext_SetState3030, context, manager + 0x38, 0u);
    if (At<void*>(manager, 0xD4)) {
        const uint32_t stage = single ? 0 : 1;
        CallThis<void>(addr::SC4RenderContext_SetState3EF0, context, 1u, stage);
        CallThis<void>(addr::SC4RenderContext_BindTexture, context, ***reinterpret_cast<uint32_t***>(manager + 0xD4), stage);
        CallThis<void>(addr::SC4RenderContext_SetState4280, context, 0u, 0u, stage);
        CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 2u, 2u, stage);
        CallThis<void>(addr::SC4RenderContext_SetState3030, context, manager + (single ? 0x50 : 0x44), stage);
    }
    CallThis<void>(addr::SC4RenderContext_SetDepthOffset, context, 3);
    At<uint32_t>(manager, 0xDC) = 0;
    for (uint32_t i = 0; i < static_cast<uint32_t>((list[1] - list[0]) >> 2); ++i) {
        char* const overlay = At<char*>(manager, 0x98) + At<int32_t>(list[0], i * 4) * 0xB4;
        if (At<int32_t>(overlay, 0) != -1 || (At<uint32_t>(overlay, 4) & 9) != 9)
            continue;
        const float* const record = reinterpret_cast<const float*>(overlay);
        const int32_t* const cells = reinterpret_cast<const int32_t*>(overlay + 0xC);
        if (!single) {
            CallThis<void>(addr::SC4RenderContext_BindTexture, context, ***reinterpret_cast<uint32_t***>(overlay + 0xAC), 0u);
            if (At<uint32_t>(overlay, 4) & 2)
                CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 3u, 3u, 0u);
            else
                CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 2u, 2u, 0u);
            {
#include "core/overlay_shadow2_a.inc"
            }
            CallThis<void>(addr::SC4RenderContext_SetTextureMatrix, context, &L[33], 0u);
            {
#include "core/overlay_shadow2_b.inc"
            }
            CallThis<void>(addr::SC4RenderContext_SetTextureMatrix, context, &L[65], 1u);
            if (Global<uint8_t>(addr::STE_OverlaySunTest)) {
                // The light at the overlay's centre, in cells.
                const double x = cells[0] + cells[2], z = cells[1] + cells[3];
                L[10].u = L[12].u;
                const double half = static_cast<double>(At<float>(manager, 0x64)) * Global<double>(0x00A92D28);
                L[27].f = static_cast<float>(half * x);
                L[28].f = static_cast<float>(half * z);
                if (!At<uint8_t>(manager, 0x16))
                    L[10].f = static_cast<float>(Virtual<double>(source, 0x3C, &L[27]) * L[10].f);
                if (L[10].f < Global<double>(0x00AB39A0))
                    continue;
                if (L[5].f > none)
                    CallThis<void>(addr::SC4RenderContext_SetState2F50, context, 6u,
                                   static_cast<float>(static_cast<double>(L[10].f) * L[5].f));
                CallThis<void>(addr::SC4RenderContext_SetLightVector, context, &L[7]);
            }
        } else {
            {
#include "core/overlay_shadow2_c.inc"
            }
            CallThis<void>(addr::SC4RenderContext_SetTextureMatrix, context, &L[49], 0u);
        }
        bool partial = false;
        if (At<uint8_t>(manager, 0x16)) {
            std::memcpy(&L[29], cells, 16);
            const int32_t counted = Virtual<int32_t>(At<void*>(manager, 0x88), 0xD4, &L[29]);
            partial = counted < (cells[3] - cells[1] + 1) * (cells[2] - cells[0] + 1);
        }
        CallThis<void>(partial ? addr::cSC4OverlayManager_DrawRectPartial : addr::cSC4OverlayManager_DrawRect, manager,
                       context, cells);
        ++At<uint32_t>(manager, 0xDC);
    }
    CallThis<void>(addr::SC4RenderContext_SetState3EF0, context, 0u, 1u);
}

// cSC4OverlayManager's list of overlays in view (0x00738390). The manager
// keeps its overlays in a grid of buckets (cells >> +0xE4 a side, +0xE8
// buckets a row, +0xF0 the buckets: lists of {next, overlay index}); the
// terrain view gives the rows in view and their first and end columns.
// Each overlay is listed once, by a stamp (+0xE0, the overlay's +0xA).
void ListVisibleOverlays(char* manager, char** list)
{
    list[1] = list[0];
    int32_t firstRow = 0, endRow = 0;
    const int32_t* starts = nullptr;
    const int32_t* ends = nullptr;
    CallThis<void>(addr::cSTETerrainView3D_RowsInView, At<void*>(manager, 0x8C), &firstRow, &endRow, &starts, &ends);
    const int32_t shift = At<int32_t>(manager, 0xE4);
    const int32_t round = (1 << shift) - 1;
    const int32_t endBucket = (round + endRow - 1) >> shift;
    const uint16_t stamp = ++At<uint16_t>(manager, 0xE0);
    for (int32_t z = firstRow >> shift; z < endBucket; ++z) {
        const int32_t endX = (ends[z] + round - 1) >> shift;
        for (int32_t x = starts[z] >> shift; x < endX; ++x) {
            const char* node = At<const char**>(manager, 0xF0)[At<int32_t>(manager, 0xE8) * z + x];
            for (; node; node = At<const char*>(const_cast<char*>(node), 0)) {
                uint32_t index = At<uint32_t>(const_cast<char*>(node), 4);
                char* const overlay = At<char*>(manager, 0x98) + index * 0xB4;
                if (At<uint16_t>(overlay, 0xA) == stamp)
                    continue;
                At<uint16_t>(overlay, 0xA) = stamp;
                CallThis<void>(addr::SC4PtrVector_PushBack, list, &index);
            }
        }
    }
}

// cSC4OverlayManager's draw (0x00738A50): the inverse of the view matrix,
// the list of overlays to draw, then the passes.
void DrawOverlays(char* manager, char* context)
{
    if (At<uint8_t>(manager, 0x15) && At<uint8_t>(At<char*>(At<char*>(context, 0x38), 0xC), 0x39C))
        return;
    Cell L[87] = {};
    for (int i = 4; i < 20; ++i)
        L[i].u = Global<uint32_t>(0x00B4B3C8);
    CallThis<void>(addr::SC4RenderContext_UpdateViewMatrix, context);
    CallThis<void>(addr::SC4RenderContext_GetViewMatrix, context, &L[4]);
    {
#include "core/overlay_view_inverse.inc"
        L[19].u = r332;
    }
    const int32_t depthOffset = At<int32_t>(context, 0xBC);
    uint32_t& flags = Global<uint32_t>(addr::STE_OverlayListFlags);
    char** const list = &Global<char*>(addr::STE_OverlayList);
    if (!(flags & 1)) {
        flags |= 1;
        list[0] = list[1] = list[2] = nullptr;
        reinterpret_cast<int(__cdecl*)(std::uintptr_t)>(addr::SC4_atexit)(0x00A7D300);
    }
    if (!At<uint8_t>(manager, 0x15) && At<uint8_t>(manager, 0x20)) {
        CallThis<void>(addr::cSC4OverlayManager_ListVisible, manager, list);
    } else {
        CallThis<void>(addr::SC4Uint32Vector_Clear, list);
        const auto count = [&] {
            return static_cast<uint32_t>(static_cast<int32_t>(At<char*>(manager, 0x9C) - At<char*>(manager, 0x98)) / 0xB4);
        };
        for (uint32_t i = 0; i < count(); ++i) {
            if (At<int32_t>(At<char*>(manager, 0x98), i * 0xB4) != -1)
                continue;
            uint32_t index = i;
            CallThis<void>(addr::SC4PtrVector_PushBack, list, &index);
        }
    }
    At<void*>(manager, 0x94) = reinterpret_cast<void*(__cdecl*)(void*)>(addr::STE_DynamicBuffersFor)(At<void*>(context, 0x30));
    const auto preference = [&](std::size_t offset) {
        return At<uint8_t>(At<char*>(At<char*>(context, 0x38), 0xC), offset) != 0;
    };
    if (list[0] != list[1]) {
        if (preference(0x15C))
            CallThis<void>(addr::cSC4OverlayManager_DrawDecals, manager, &L[4], context, list);
        if (list[0] != list[1] && preference(0x13C)) {
            if (At<int32_t>(manager, 0x18) > 2 && preference(0x17C))
                CallThis<void>(addr::cSC4OverlayManager_DrawShadows, manager, &L[4], context, list);
            else
                CallThis<void>(addr::cSC4OverlayManager_DrawShadowsPlain, manager, &L[4], context, list);
        }
    }
    CallThis<void>(addr::SC4RenderContext_ClearTextureMatrix, context, 0u);
    CallThis<void>(addr::SC4RenderContext_SetDepthOffset, context, depthOffset);
    if (void* const buffers = At<void*>(manager, 0x94)) {
        CallThis<void>(addr::STE_ReleaseDynamicBuffers, buffers);
        At<void*>(manager, 0x94) = nullptr;
    }
}

// --- Models and lots: the 3D render's passes ------------------------------------------------

// OPENSC4_MODELS_CENSUS=1: counts the draws our passes ask for by the
// drawable's vtable and logs the totals now and then, to see which classes
// do the drawing.
void CountDrawable(const void* drawable, const char* pass)
{
    static const bool on = [] {
        const char* v = std::getenv("OPENSC4_MODELS_CENSUS");
        return v && *v == '1';
    }();
    if (!on)
        return;
    static std::vector<std::pair<uint32_t, uint64_t>> counts;
    static uint64_t total = 0;
    const uint32_t vtable = *static_cast<const uint32_t*>(drawable);
    auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first == vtable; });
    if (it == counts.end())
        it = counts.insert(counts.end(), {vtable, 0});
    ++it->second;
    if (++total % 200000 != 0)
        return;
    std::string text;
    char entry[48];
    for (const auto& [table, count] : counts) {
        std::snprintf(entry, sizeof entry, " %08X:%llu", table, static_cast<unsigned long long>(count));
        text += entry;
    }
    log::Write("models: census after %llu draws (last from the %s pass), slot 0x30 at vtable + 0x30:%s",
               static_cast<unsigned long long>(total), pass, text.c_str());
}

// A {drawable, key} pair onto one of the render's vectors (the game's
// vector growth: twice the size, from its STL allocator).
void PushDrawable(char** vector, uint32_t drawable, uint32_t key)
{
    auto* end = reinterpret_cast<uint32_t*>(vector[1]);
    if (vector[1] != vector[2]) {
        end[0] = drawable;
        end[1] = key;
        vector[1] += 8;
        return;
    }
    const std::size_t size = static_cast<std::size_t>(vector[1] - vector[0]) >> 3;
    const std::size_t capacity = size + (size ? size : 1);
    auto* const grown = reinterpret_cast<uint32_t*>(
        reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(capacity * 8));
    std::memcpy(grown, vector[0], size * 8);
    grown[size * 2] = drawable;
    grown[size * 2 + 1] = key;
    if (vector[0])
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(vector[0]);
    vector[0] = reinterpret_cast<char*>(grown);
    vector[1] = reinterpret_cast<char*>(grown + size * 2 + 2);
    vector[2] = reinterpret_cast<char*>(grown + capacity * 2);
}

// cSC43DRender's static pass (0x007C7370): the drawables with flag 8 from
// the grid cells in view, each once (in its own first cell, or the first
// cell of the range), keyed by depth and the drawable's own order, sorted,
// then drawn in rounds: a drawable that answers 2 is drawn again in the
// next round, between the render's two vectors, up to 12 rounds.
void DrawStatic(char* render)
{
    char* const context = At<char*>(render, 0x11C);
    At<uint32_t>(context, 0x50) = 1;
    float bounds[4], plane[4];
    CallThis<void>(addr::cSC43DRender_ViewBounds, render, bounds, 1);
    reinterpret_cast<void(__cdecl*)(float*, void*)>(addr::cSC43DRender_DepthPlane)(plane, At<void*>(render, 0x114));
    char** const first = &At<char*>(render, 0x134);
    char** const second = &At<char*>(render, 0x140);
    first[1] = first[0];
    second[1] = second[0];
    if (char* const grid = At<char*>(render, 0x120)) {
        int32_t x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        CallThis<void>(addr::SC4DrawGrid_CellRange, grid, bounds, &x0, &z0, &x1, &z1);
        for (int32_t x = x0; x <= x1; ++x) {
            for (int32_t z = z0; z <= z1; ++z) {
                const char* const cell = At<char*>(At<char*>(grid, 0x14), static_cast<std::size_t>(x) * 0xC) + z * 0x10;
                char** const end = At<char**>(const_cast<char*>(cell), 4);
                for (char** it = At<char**>(const_cast<char*>(cell), 0); it != end; ++it) {
                    char* const item = *it;
                    if (!(At<uint8_t>(item, 6) & 8))
                        continue;
                    const uint32_t inX = static_cast<uint32_t>(x - At<int32_t>(item, 8) - 1) | static_cast<uint32_t>(x - x0 - 1);
                    const uint32_t inZ = static_cast<uint32_t>(z - At<int32_t>(item, 0xC) - 1) | static_cast<uint32_t>(z - z0 - 1);
                    if (static_cast<int32_t>(inX & inZ) >= 0)
                        continue;
                    uint32_t pair[2];
                    CallThis<void>(addr::cSC43DRender_DepthKey, pair, item, plane);
                    uint32_t order = 0;
                    Virtual<void>(At<void*>(item, 0x18), 0x44, &order);
                    PushDrawable(first, pair[0], (order & 0xFFFF0000) + pair[1] + (order << 16));
                }
            }
        }
    }
    int32_t count = static_cast<int32_t>((first[1] - first[0]) >> 3);
    if (count != 0)
        reinterpret_cast<void(__cdecl*)(char*, int32_t)>(addr::cSC43DRender_SortByKey)(first[0], count);
    At<uint32_t>(context, 4) = 0;
    char** current = first;
    char** other = second;
    do {
        for (int32_t i = 0; i < count; ++i) {
            const uint32_t* const pair = reinterpret_cast<const uint32_t*>(current[0]) + i * 2;
            CountDrawable(reinterpret_cast<const void*>(pair[0]), "static");
            if (Virtual<int32_t>(reinterpret_cast<void*>(pair[0]), 0x30, context, 0u) == 2) {
                const uint32_t drawable = pair[0], key = pair[1];
                PushDrawable(other, drawable, key);
            }
        }
        current[1] = current[0];
        std::swap(current, other);
        count = static_cast<int32_t>((current[1] - current[0]) >> 3);
        ++At<uint32_t>(context, 4);
    } while (count != 0 && At<uint32_t>(context, 4) < 12);
}

// cSC43DRender's moving-objects pass (0x007C7830): from the grid cells in
// view that are flagged (+0xC bit 0), the items without flag 8 or with flag
// 0x80, each once as in the static pass. Those without flag 8 go to one of
// three lists (flag 1: +0x14C; flag 0x100: +0x140; else +0x134 with the
// drawable's order in the key); those with flag 0x80 also to a fourth
// (+0x128). A cell keeps its flag only while it holds such an item. The
// three lists are sorted and drawn in rounds, compacted in place; the round
// count runs on across them and ends the pass at 12. The fourth list is
// drawn last with a depth offset of 4.
void DrawMoving(char* render)
{
    char* const context = At<char*>(render, 0x11C);
    At<uint32_t>(context, 0x50) = 4;
    char** const plain = &At<char*>(render, 0x134);
    char** const second = &At<char*>(render, 0x140);
    char** const third = &At<char*>(render, 0x14C);
    char** const overlaid = &At<char*>(render, 0x128);
    plain[1] = plain[0];
    second[1] = second[0];
    third[1] = third[0];
    float bounds[4], plane[4];
    CallThis<void>(addr::cSC43DRender_ViewBounds, render, bounds, 2);
    reinterpret_cast<void(__cdecl*)(float*, void*)>(addr::cSC43DRender_DepthPlane)(plane, At<void*>(render, 0x114));
    overlaid[1] = overlaid[0];
    if (char* const grid = At<char*>(render, 0x120)) {
        int32_t x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        CallThis<void>(addr::SC4DrawGrid_CellRange, grid, bounds, &x0, &z0, &x1, &z1);
        for (int32_t x = x0; x <= x1; ++x) {
            for (int32_t z = z0; z <= z1; ++z) {
                char* const cell = At<char*>(At<char*>(grid, 0x14), static_cast<std::size_t>(x) * 0xC) + z * 0x10;
                char** const end = At<char**>(cell, 4);
                char** it = At<char**>(cell, 0);
                if (it == end || !(At<uint16_t>(cell, 0xC) & 1))
                    continue;
                uint32_t cellFlags = At<uint16_t>(cell, 0xC) & 0xFFFFFFFE;
                for (; it != end; ++it) {
                    char* item = *it;
                    const uint32_t flags = At<uint16_t>(item, 6);
                    if ((flags & 8) && !(flags & 0x80))
                        continue;
                    const uint32_t inX = static_cast<uint32_t>(x - At<int32_t>(item, 8) - 1) | static_cast<uint32_t>(x - x0 - 1);
                    const uint32_t inZ = static_cast<uint32_t>(z - At<int32_t>(item, 0xC) - 1) | static_cast<uint32_t>(z - z0 - 1);
                    if (static_cast<int32_t>(inX & inZ) < 0) {
                        if (!(flags & 8)) {
                            uint32_t pair[2];
                            CallThis<void>(addr::cSC43DRender_DepthKey, pair, item, plane);
                            if (flags & 1) {
                                PushDrawable(third, pair[0], pair[1]);
                            } else if (flags & 0x100) {
                                PushDrawable(second, pair[0], pair[1]);
                            } else {
                                uint32_t order = 0;
                                Virtual<void>(At<void*>(item, 0x18), 0x44, &order);
                                PushDrawable(plain, pair[0], (order << 16) + (order & 0xFFFF0000) + pair[1]);
                            }
                        }
                        if (At<uint8_t>(item, 6) & 0x80)
                            CallThis<void>(addr::SC4PtrVector_PushBack, overlaid, &item);
                    }
                    cellFlags |= 1;
                }
                At<uint16_t>(cell, 0xC) = static_cast<uint16_t>(cellFlags);
            }
        }
    }
    const auto count = [](char** vector) { return static_cast<int32_t>((vector[1] - vector[0]) >> 3); };
    const auto sort = [&](std::uintptr_t function, char** vector) {
        if (count(vector) != 0)
            reinterpret_cast<void(__cdecl*)(char*, int32_t)>(function)(vector[0], count(vector));
    };
    sort(addr::cSC43DRender_SortByKeyDescending, third);
    sort(addr::cSC43DRender_SortByKeyDescending, second);
    sort(addr::cSC43DRender_SortByKey, plain);
    At<uint32_t>(context, 4) = 0;
    bool stopped = false;
    for (int which = 0; which < 3 && !stopped; ++which) {
        char** const vector = plain + which * 3;
        auto* const begin = reinterpret_cast<uint32_t*>(vector[0]);
        if (vector[0] == vector[1])
            continue;
        uint32_t* end = begin + count(vector) * 2;
        uint32_t* kept;
        do {
            kept = begin;
            for (uint32_t* pair = begin; pair < end; pair += 2) {
                CountDrawable(reinterpret_cast<const void*>(pair[0]), "moving");
                if (Virtual<int32_t>(reinterpret_cast<void*>(pair[0]), 0x30, context, 0u) == 2) {
                    kept[0] = pair[0];
                    kept[1] = pair[1];
                    kept += 2;
                }
            }
            if (++At<uint32_t>(context, 4) > 11) {
                stopped = true;
                break;
            }
            end = kept;
        } while (begin < kept);
        if (!stopped)
            vector[1] = vector[0];
    }
    if (overlaid[0] != overlaid[1]) {
        At<uint32_t>(context, 4) = 0;
        CallThis<void>(addr::SC4RenderContext_SetDepthOffset, context, 4);
        const int32_t items = static_cast<int32_t>((overlaid[1] - overlaid[0]) >> 2);
        for (int32_t i = 0; i < items; ++i)
            Virtual<int32_t>(At<void*>(At<char*>(overlaid[0], static_cast<std::size_t>(i) * 4), 0x18), 0x30, context, 1u);
        CallThis<void>(addr::SC4RenderContext_SetDepthOffset, context, 0);
    }
}

// --- Models and lots: the drawables ----------------------------------------------------------

// x87 instructions and the game's own asin, as the game executes them
// (53-bit precision).
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

static_assert(addr::CRT_CIasin == 0x009F03B0);
__declspec(naked) double __cdecl GameAsin(double)
{
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x009F03B0
        call eax
        ret
    }
}

// The machine the translated functions run on (see billboard_corners.inc):
// memory by address, the x87 stack as doubles, its compare flags as the
// status word's high byte holds them (C0 bit 0, C2 bit 2, C3 bit 6).
template <typename T>
T& M(uint32_t address)
{
    return *reinterpret_cast<T*>(static_cast<std::uintptr_t>(address));
}

bool Even(uint32_t value)
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

// The billboard's corners after a change of zoom or rotation (0x007FE400),
// translated instruction by instruction.
uint32_t UpdateBillboardCorners(char* object, char* context)
{
    uint32_t stackmem[160] = {};
    [[maybe_unused]] uint32_t eax = 0, ebx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, fa = 0, fb = 0;
    uint32_t ecx = reinterpret_cast<uint32_t>(object);
    uint32_t esp = reinterpret_cast<uint32_t>(&stackmem[128]);
    M<uint32_t>(esp + 4) = reinterpret_cast<uint32_t>(context);
    double st[8] = {};
    int top = -1;
    uint32_t status = 0;
#define ST(i) st[top - (i)]
#define PUSH(value)                                                                                                      \
    do {                                                                                                                 \
        const double pushed = (value);                                                                                   \
        st[++top] = pushed;                                                                                              \
    } while (0)
#define POP() --top
#define FCOM(value) status = CompareStatus(ST(0), (value))
#include "core/billboard_corners.inc"
#undef ST
#undef PUSH
#undef POP
#undef FCOM
}

// The billboard's draw (0x007FEA90): a textured quad that faces the camera,
// the most drawn thing in a city. `self` is the object + 0x170, as the
// adjustor in its vtable passes it. Fields of the object: +0x20 flags,
// +0x24 colour, +0x30 alpha, +0x38 a state byte with its copy at +0x9E,
// +0x44/+0x50/+0x5C a direction, +0x60 position, +0x88/+0x89 the zoom and
// rotation its picture was picked for, +0x8C render state, +0x94 the
// picture's index (by pointer), +0x9C/+0x9D dirty bytes, +0xC4 textures,
// +0xDC four 24-byte vertices, +0x13C the four corners.
uint32_t DrawBillboard(char* self, char* context, uint8_t overlaid)
{
    if (overlaid || At<int32_t>(context, 0x14) < 2)
        return 1;
    char* const object = self - 0x170;
    if (!At<uint8_t>(object, 0x9D))
        At<uint8_t>(object, 0x9D) = !(At<uint32_t>(context, 0x14) == At<uint8_t>(object, 0x88) &&
                                      At<uint32_t>(context, 0x18) == At<uint8_t>(object, 0x89));
    if (At<uint8_t>(object, 0x9D) || At<uint8_t>(object, 0x9C)) {
        At<uint8_t>(object, 0x88) = At<uint8_t>(context, 0x14);
        At<uint8_t>(object, 0x89) = At<uint8_t>(context, 0x18);
        const float heading = static_cast<float>(X87Atan2(At<float>(object, 0x5C), At<float>(object, 0x44)));
        const float pitch = static_cast<float>(GameAsin(At<float>(object, 0x50)));
        CallThis<void>(addr::cS3DBillboard_SelectView, object, pitch, heading, 0u,
                       static_cast<uint32_t>(At<uint8_t>(object, 0x88)), static_cast<uint32_t>(At<uint8_t>(object, 0x89)),
                       static_cast<uint32_t>(At<uint8_t>(object, 0x9F)), 0u, At<uint32_t>(object, 0x94));
        if (At<uint8_t>(object, 0x9D))
            CallThis<void>(addr::cS3DBillboard_UpdateCorners, object, context);
        At<uint8_t>(object, 0x9C) = 0;
    }
    if (At<uint8_t>(object, 0x9D) || At<uint8_t>(object, 0x9E) != At<uint8_t>(object, 0x38)) {
        for (std::size_t corner = 0; corner < 4; ++corner)
            for (std::size_t axis = 0; axis < 3; ++axis)
                At<float>(object, 0xDC + corner * 0x18 + axis * 4) = static_cast<float>(
                    static_cast<double>(At<float>(object, 0x60 + axis * 4)) + At<float>(object, 0x13C + corner * 0xC + axis * 4));
        At<uint8_t>(object, 0x9E) = At<uint8_t>(object, 0x38);
        At<uint8_t>(object, 0x9D) = 0;
    }
    CallThis<void>(addr::SC4RenderContext_UpdateViewMatrix, context);
    if (At<uint8_t>(object, 0x20) & 4)
        CallThis<void>(addr::SC4RenderContext_SetColourAlt, context, object + 0x24);
    else
        CallThis<void>(addr::SC4RenderContext_SetColourLit, context, object + 4 + At<int32_t>(At<char*>(object, 4), 4),
                       object + 0x24);
    CallThis<void>(addr::SC4RenderContext_SetRenderState, context, object + 0x8C);
    if (At<uint8_t>(object, 0x20) & 2) {
        CallThis<void>(addr::SC4RenderContext_SetDriverValue84, context, At<uint32_t>(object, 0x30));
        CallThis<void>(addr::SC4RenderContext_SetState2F50, context, 4u,
                       static_cast<float>(At<float>(object, 0x30) * Global<double>(0x00A92D28)));
    }
    CallThis<void>(addr::SC4RenderContext_BindTexture, context,
                   (*At<uint32_t**>(object, 0xC4))[*At<uint32_t*>(object, 0x94)], 0u);
    CallThis<void>(addr::SC4RenderContext_DrawArrays, context, 2u, 0xAu, 4, object + 0xDC);
    CallThis<void>(addr::SC4RenderContext_ResetColour, context);
    CallThis<void>(addr::SC4RenderContext_ResetDriverValue84, context);
    return 1;
}

// The terrain view's cell quad (0x00752940, slot 0x3C of its interface at
// +0xC): one cell of the land drawn with the caller's texture coordinates
// and alpha, which is how a quad without vertices of its own is laid on the
// terrain. Outside the rows in view it draws nothing unless `always`.
void DrawCellQuad(char* interface, char* context, uint32_t x, uint32_t z, const uint32_t* coordinates, float alpha,
                  uint8_t always)
{
    char* const view = interface - 0xC;
    if (!always) {
        if (z < At<uint32_t>(view, 0x98) || z >= At<uint32_t>(view, 0x9C))
            return;
        if (x < static_cast<uint32_t>(RowStarts()[z]) || x >= static_cast<uint32_t>(RowEnds()[z]))
            return;
        if (!RowShown(static_cast<int32_t>(z)))
            return;
    }
    const std::size_t at = static_cast<std::size_t>(Columns()) * z + x;
    const bool turned = (At<uint8_t>(Records(view), at * 0x24 + 0x23) & 0x30) != 0;
    CallThis<void>(addr::SC4RenderContext_SetIndexArray, context, 6u,
                   reinterpret_cast<uint16_t*>(turned ? addr::STE_QuadIndicesTurned : addr::STE_QuadIndicesPlain));
    const float rounded = static_cast<float>(static_cast<double>(alpha) + 49152.0f);
    uint32_t bits;
    std::memcpy(&bits, &rounded, 4);
    bits -= 0x47400000;
    const uint8_t opacity = static_cast<uint8_t>(bits - (bits >> 8));
    char quad[4 * 32];
    const std::size_t corners[4] = {at, at + 1, at + Columns(), at + Columns() + 1};
    for (int corner = 0; corner < 4; ++corner) {
        char* const vertex = quad + corner * 32;
        std::memcpy(vertex, Land() + corners[corner] * 32, 32);
        std::memcpy(vertex + 0x10, coordinates + corner * 2, 8);
        vertex[0xF] = static_cast<char>(opacity);
    }
    CallThis<void>(addr::SC4RenderContext_SetVertexArray, context, 0xBu, 4u, quad);
    CallThis<void>(addr::SC4RenderContext_DrawIndexed, context, 0u, 0, 6);
}

// The ground quad's draw (0x005E0200): a textured quad, the third most drawn
// thing. `self` is the object + 0xEC. Fields of the object: +0x38/+0x39 two
// bytes for the draped draw, +0x3C a colour (r, g, b, alpha), +0x40 a tint
// mode (0xFF: none), +0x41 the texture's index, +0x44 a translucency flag,
// +0x45/+0x46 the zoom and rotation it is set up for, +0x48 its textures
// ({handles, count}), +0x64 four vertices when +0xC4 is set, +0xC8 what the
// draped draw takes instead.
uint32_t DrawGroundQuad(char* self, char* context, uint8_t overlaid)
{
    if (overlaid)
        return 1;
    char* const object = self - 0xEC;
    if (At<uint32_t>(context, 0x14) != At<uint8_t>(object, 0x45))
        CallThis<void>(addr::SC4GroundQuad_SetZoom, object, At<uint32_t>(context, 0x14));
    const uint32_t rotation = At<uint32_t>(context, 0x18);
    if (rotation != At<uint8_t>(object, 0x46)) {
        Virtual<void>(object, 0x10, 0u, 1u, 0u, 0u);
        At<uint8_t>(object, 0x46) = static_cast<uint8_t>(rotation);
    }
    const char* const textures = At<const char*>(object, 0x48);
    if (!textures)
        return 1;
    const int mode = At<uint8_t>(object, 0x40) != 0xFF ? 2 : (At<uint8_t>(object, 0x44) || At<uint8_t>(object, 0x3F) < 0xFF) ? 1 : 0;
    if (mode != 0 && At<uint32_t>(context, 4) == 0)
        return 2;
    CallThis<void>(addr::SC4RenderContext_UpdateViewMatrix, context);
    CallThis<void>(addr::SC4RenderContext_ResetColour, context);
    const uint32_t* const handles = At<const uint32_t*>(const_cast<char*>(textures), 0);
    const uint32_t index = At<uint8_t>(object, 0x41);
    CallThis<void>(addr::SC4RenderContext_BindTexture, context,
                   index < At<uint32_t>(const_cast<char*>(textures), 4) ? handles[index] : handles[0], 0u);
    const float scale = Global<float>(0x00A90BBC);
    if (mode == 2) {
        CallThis<void>(addr::SC4RenderContext_SetRenderState, context, reinterpret_cast<void*>(0x00B230A0));
        CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 2u, 2u, 0u);
        CallThis<void>(addr::SC4RenderContext_SetState4280, context, 1u, 1u, 0u);
        Cell light[3];
        for (Cell& cell : light)
            cell.u = Global<uint32_t>(0x00B2305C);
        Virtual<void>(At<void*>(context, 0x44), 0x28, light);
        uint8_t alpha = At<uint8_t>(object, 0x3F);
        uint8_t& tint = Global<uint8_t>(0x00B0A878);
        tint = At<uint8_t>(object, 0x40);
        if (tint == 3 && At<uint8_t>(At<char*>(At<char*>(context, 0x38), 0xC), 0x3BC)) {
            tint = 1;
            alpha >>= 1;
        }
        float colour[4];
        for (int channel = 0; channel < 3; ++channel)
            colour[channel] = static_cast<float>(
                static_cast<double>(static_cast<int32_t>(At<uint8_t>(object, 0x3C + channel))) * light[0].f * scale);
        colour[3] = static_cast<float>(static_cast<double>(static_cast<int32_t>(alpha)) * scale);
        CallThis<void>(addr::SC4RenderContext_SetLightVector, context, colour);
        CallThis<void>(addr::SC4RenderContext_SetState3030, context, &tint, 0u);
    } else {
        CallThis<void>(addr::SC4RenderContext_SetRenderState, context, reinterpret_cast<void*>(mode == 0 ? 0x00B23090 : 0x00B23098));
        CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 2u, 2u, 0u);
    }
    if (At<uint8_t>(object, 0xC4)) {
        CallThis<void>(addr::SC4RenderContext_DrawArrays, context, 1u, 0xAu, 4, object + 0x64);
        return 1;
    }
    const float alpha = static_cast<float>(static_cast<double>(static_cast<int32_t>(At<uint8_t>(object, 0x3F))) * scale);
    Virtual<void>(At<void*>(context, 0x4C), 0x3C, context, static_cast<uint32_t>(At<uint8_t>(object, 0x38)),
                  static_cast<uint32_t>(At<uint8_t>(object, 0x39)), object + 0xC8, alpha,
                  static_cast<uint32_t>(At<uint32_t>(context, 0x20) == 8));
    return 1;
}

// The draw of the drawable with vtable 0x00AA2EA8 (0x005E27C0; `self` is
// the object + 0xAC): a strip of triangles with one texture, shown only
// from the two rotations its side (+0x78) faces. `scratch` is the caller's
// second argument slot, which the game lends to the rotation change.
uint32_t DrawSideStrip(char* self, char* context, uint32_t overlaid)
{
    if (static_cast<uint8_t>(overlaid))
        return 1;
    char* const object = self - 0xAC;
    const uint32_t zoom = At<uint32_t>(context, 0x14);
    if (zoom != At<uint8_t>(object, 0x64))
        CallThis<void>(addr::SC4DrawableAA2EA8_SetZoom, object, static_cast<uint32_t>(At<uint8_t>(object, 0x64)), zoom, context);
    if (At<uint32_t>(context, 0x18) != At<uint8_t>(object, 0x65)) {
        At<uint8_t>(object, 0x65) = At<uint8_t>(context, 0x18);
        uint32_t scratch = overlaid;
        CallThis<void>(addr::SC4DrawableAA2EA8_SetRotation, object, 0u, 1u, &scratch, 0u, 0u, At<uint32_t>(context, 0x44));
    }
    if (!At<uint8_t>(object, 0x66))
        return 1;
    const int32_t rotation = At<int32_t>(context, 0x18);
    const int32_t side = At<int8_t>(object, 0x78);
    if (side != (rotation + 3) % 4 && side != (rotation + 4) % 4)
        return 1;
    if (!At<void*>(object, 0x98))
        return 1;
    const uint32_t texture = **At<uint32_t**>(object, 0x98);
    CallThis<void>(addr::SC4RenderContext_UpdateViewMatrix, context);
    CallThis<void>(addr::SC4RenderContext_SetRenderState, context, reinterpret_cast<void*>(0x00B230B4));
    CallThis<void>(addr::SC4RenderContext_BindTexture, context, texture, 0u);
    char* const vertices = At<char*>(object, 0x9C);
    CallThis<void>(addr::SC4RenderContext_DrawArrays, context, 0u, 0xAu,
                   static_cast<int32_t>(At<char*>(object, 0xA0) - vertices) / 24, vertices);
    return 1;
}

// The draw of the drawable with vtable 0x00AA2808 (0x005D5FC0; `self` is
// the object + 0x274): up to two quads picked by the rotation with one
// texture, when the zoom's flag is set, then one more quad with a second
// texture.
uint32_t DrawRotationQuads(char* self, char* context, uint32_t overlaid)
{
    if (static_cast<uint8_t>(overlaid))
        return 1;
    char* const object = self - 0x274;
    if (At<int32_t>(context, 0x18) != At<int8_t>(object, 0x49)) {
        At<uint8_t>(object, 0x49) = At<uint8_t>(context, 0x18);
        CallThis<void>(addr::SC4CameraView_RotationPair, At<void*>(context, 0x24), object + 0x50, object + 0x54);
        CallThis<void>(addr::SC4DrawableAA2808_SetRotation, object, 0u, 1u, At<uint32_t>(context, 0x44));
    }
    if (At<int32_t>(context, 0x14) != At<int8_t>(object, 0x48))
        CallThis<void>(addr::SC4DrawableAA2808_SetZoom, object, static_cast<int32_t>(At<int8_t>(object, 0x48)),
                       At<int32_t>(context, 0x14), context);
    const auto zoomFlag = [&] { return At<uint8_t>(object, 0x4A + At<int8_t>(object, 0x48)) != 0; };
    if (!zoomFlag() && !At<void*>(object, 0x20C))
        return 1;
    CallThis<void>(addr::SC4RenderContext_UpdateViewMatrix, context);
    CallThis<void>(addr::SC4RenderContext_SetRenderState, context, reinterpret_cast<void*>(0x00B23034));
    const auto quad = [&](char* vertices) {
        CallThis<void>(addr::SC4RenderContext_DrawArrays, context, 6u, 0xAu, 4, vertices);
    };
    if (zoomFlag() && (At<int32_t>(object, 0x50) >= 0 || At<int32_t>(object, 0x54) >= 0) && At<void*>(object, 0x70)) {
        if (const uint32_t texture = **At<uint32_t**>(object, 0x70)) {
            CallThis<void>(addr::SC4RenderContext_BindTexture, context, texture, 0u);
            if (At<int32_t>(object, 0x50) >= 0)
                quad(object + 0x74 + At<int32_t>(object, 0x50) * 0x60);
            if (At<int32_t>(object, 0x54) >= 0)
                quad(object + 0x74 + At<int32_t>(object, 0x54) * 0x60);
        }
    }
    if (At<void*>(object, 0x20C)) {
        if (const uint32_t texture = **At<uint32_t**>(object, 0x20C)) {
            CallThis<void>(addr::SC4RenderContext_BindTexture, context, texture, 0u);
            quad(object + 0x210);
        }
    }
    return 1;
}

// The draw of the drawable with vtable 0x00AA7F10 (0x0061A850; `self` is
// the object + 0x17C): a quad drawn as one or two triangles by the object's
// flags (its slot 0x20 answers whether a flag is set) with up to two
// textures, then a list of textured triangles kept above the terrain. In
// the overlaid round everything is drawn a second time with another render
// state. `contextWord` is how the game passes one answer on: in the low
// byte of the context pointer's stack slot.
uint32_t DrawFlaggedQuad(char* self, char* context, uint32_t overlaidWord)
{
    const bool overlaid = static_cast<uint8_t>(overlaidWord) != 0;
    char* const object = self - 0x17C;
    const auto flag = [&](uint32_t bits) { return Virtual<uint8_t>(object, 0x20, bits) != 0; };
    const auto texture = [&](uint32_t a, uint32_t b) { return Virtual<uint32_t**>(object, 0x134, a, b); };
    if (Global<uint32_t>(0x00B0B374) != At<uint32_t>(context, 0x14))
        Virtual<void>(object, 0x138, context);
    if (!flag(0x8000))
        Virtual<void>(object, 0x13C, At<uint32_t>(context, 0x44));
    void* state = reinterpret_cast<void*>(0x00B45260);
    if (flag(0x80000) || (flag(0x40000000) && At<uint8_t>(object, 0xAB) == 0xFF)) {
        char* const pieces = Virtual<char*>(object, 0xB4);
        uint32_t bits = At<uint32_t>(pieces, 8);
        const uint32_t* piece = reinterpret_cast<const uint32_t*>(At<char*>(pieces, 0xC) + 4);
        for (uint32_t n = At<uint8_t>(pieces, 0); n != 0; --n, piece += 2)
            bits |= *piece;
        static constexpr uint32_t kPlain[] = {0x3010000, 0x30100,   0x301,   0x1000003, 0x1000000, 0x10000,
                                              0x100,     1,         0x3000000, 0x30000,  0x300,     3};
        if (std::find(std::begin(kPlain), std::end(kPlain), bits) == std::end(kPlain))
            state = reinterpret_cast<void*>(0x00B45268);
    }
    CallThis<void>(addr::SC4RenderContext_SetRenderState, context, state);
    CallThis<void>(addr::SC4RenderContext_UpdateViewMatrix, context);
    if (overlaid) {
        if (!Virtual<uint32_t>(object + 4, 0x40))
            return 1;
        CallThis<void>(addr::SC4RenderContext_SetColourAlt, context, reinterpret_cast<void*>(0x00B0B388));
    } else {
        CallThis<void>(addr::SC4RenderContext_ResetColour, context);
    }
    void* const second = reinterpret_cast<void*>(0x00B45270);
    if (Virtual<uint8_t>(object, 0x24, 0x6000000u)) {
        uint32_t** textures[2];
        textures[0] = At<uint32_t>(object, 0x100) ? texture(At<uint32_t>(object, 0x100), 0) : nullptr;
        const uint32_t base = At<uint32_t>(object, 0xFC);
        textures[1] = texture(base + At<uint8_t>(object, 0x105) * 16u, base);
        if (textures[1]) {
            CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 2u, 2u, 0u);
            CallThis<void>(addr::SC4RenderContext_SetState4280, context, 1u, 1u, 0u);
            CallThis<void>(addr::SC4RenderContext_SetVertexArray, context, 0xAu, 4u, object + 0x9C);
            for (uint32_t** const entry : textures) {
                if (!entry)
                    continue;
                CallThis<void>(addr::SC4RenderContext_BindTexture, context, **entry, 0u);
                char* const indices = reinterpret_cast<char*>(flag(0x1000000) ? 0x00AA7D34 : 0x00AA7D28);
                const auto triangles = [&] {
                    if (flag(0x2000000))
                        CallThis<void>(addr::SC4RenderContext_DrawElements, context, 0u, 3u, indices);
                    if (flag(0x4000000))
                        CallThis<void>(addr::SC4RenderContext_DrawElements, context, 0u, 3u, indices + 6);
                };
                triangles();
                if (overlaid) {
                    CallThis<void>(addr::SC4RenderContext_SetRenderState, context, second);
                    CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 2u, 2u, 0u);
                    CallThis<void>(addr::SC4RenderContext_SetState4280, context, 1u, 1u, 0u);
                    triangles();
                    CallThis<void>(addr::SC4RenderContext_SetRenderState, context, state);
                }
            }
            if (const uint8_t extra = Virtual<uint8_t>(object, 0x114))
                Virtual<void>(object, 0x130, context, overlaidWord, state,
                              (reinterpret_cast<uint32_t>(context) & 0xFFFFFF00) | extra);
        }
    }
    if (At<char*>(object, 0x118) != At<char*>(object, 0x11C)) {
        CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 3u, 3u, 0u);
        CallThis<void>(addr::SC4RenderContext_SetState4280, context, 1u, 1u, 0u);
        void* const terrain = Global<void*>(0x00B43CF4);
        const bool checked = Virtual<uint8_t>(terrain, 0x14C) != 0;
        char* const end = At<char*>(object, 0x11C);
        for (char* entry = At<char*>(object, 0x118); entry != end; entry += 0x4C) {
            char* const vertices = entry + 4;
            if (checked && Virtual<double>(terrain, 0x78, At<uint32_t>(vertices, 0), At<uint32_t>(vertices, 8)) >
                               At<float>(vertices, 4))
                continue;
            uint32_t** const found = texture(At<uint32_t>(entry, 0), 0);
            if (!found)
                continue;
            CallThis<void>(addr::SC4RenderContext_BindTexture, context, **found, 0u);
            CallThis<void>(addr::SC4RenderContext_DrawArrays, context, 0u, 0xAu, 3, vertices);
            if (overlaid) {
                CallThis<void>(addr::SC4RenderContext_SetRenderState, context, second);
                CallThis<void>(addr::SC4RenderContext_SetState41E0, context, 3u, 3u, 0u);
                CallThis<void>(addr::SC4RenderContext_SetState4280, context, 1u, 1u, 0u);
                CallThis<void>(addr::SC4RenderContext_DrawArrays, context, 0u, 0xAu, 3, vertices);
                CallThis<void>(addr::SC4RenderContext_SetRenderState, context, state);
            }
        }
    }
    if (overlaid)
        CallThis<void>(addr::SC4RenderContext_ResetColour, context);
    return 1;
}

// The context's mesh draw (0x007D4A80). A mesh is three pointers: a vertex
// buffer {format, +6 a count that must not be 0, +8 the vertices}, an index
// buffer (a pointer to uint16 indices) or null, and its primitive groups (a
// vector at +4 of {primitive, first, count}).
void DrawMesh(char* context, char** mesh)
{
    char* const vertices = mesh[0];
    if (!vertices || At<uint16_t>(vertices, 6) == 0)
        return;
    void* const driver = At<void*>(context, 0x30);
    Virtual<void>(driver, 0x14, At<uint32_t>(vertices, 0), 0u, At<uint32_t>(vertices, 8));
    const int32_t groups = static_cast<int32_t>(At<char*>(mesh[2], 8) - At<char*>(mesh[2], 4)) / 0xC;
    if (mesh[1]) {
        At<uint32_t>(context, 0x1FC) = At<uint32_t>(mesh[1], 0);
        for (int32_t i = 0; i < groups; ++i) {
            const uint32_t* const group = reinterpret_cast<const uint32_t*>(At<char*>(mesh[2], 4)) + i * 3;
            Virtual<void>(driver, 0x10, group[0], group[2], 3u, At<uint32_t>(context, 0x1FC) + group[1] * 2);
        }
    } else {
        for (int32_t i = 0; i < groups; ++i) {
            const uint32_t* const group = reinterpret_cast<const uint32_t*>(At<char*>(mesh[2], 4)) + i * 3;
            Virtual<void>(driver, 0xC, group[0], group[1], group[2]);
        }
    }
}

// The material setup of the overlaid round (0x007D5670): the texture in
// stage 0 with the material's environment mode (+0x19), wrap (+0x14, +0x15)
// and filters (+0x16, +0x17), past the context's stage cache.
void SetMaterialOverlaid(char* context, char* material, char* properties)
{
    CallThis<void>(addr::SC4RenderContext_SetState4FB0, context, static_cast<uint32_t>(At<uint8_t>(properties, 1)));
    if (At<uint8_t>(material, 0xB) == 0)
        return;
    void* const driver = At<void*>(context, 0x30);
    const uint32_t texture = **At<uint32_t**>(material, 0x10);
    Virtual<void>(driver, 0xE8, texture, 0u);
    At<uint32_t>(At<char*>(context, 0xE0), 0) = texture;
    const uint32_t mode = At<uint8_t>(material, 0x19);
    if (At<uint32_t>(At<char*>(context, 0xE0), 4) != mode) {
        Virtual<void>(driver, 0xCC, 0u);
        Virtual<void>(driver, 0xD0, mode);
    }
    At<uint32_t>(At<char*>(context, 0xE0), 4) = mode;
    CallThis<void>(addr::SC4RenderContext_SetState41E0, context, static_cast<uint32_t>(At<uint8_t>(material, 0x14)),
                   static_cast<uint32_t>(At<uint8_t>(material, 0x15)), 0u);
    CallThis<void>(addr::SC4RenderContext_SetState4280, context, static_cast<uint32_t>(At<uint8_t>(material, 0x16)),
                   static_cast<uint32_t>(At<uint8_t>(material, 0x17)), 0u);
}

// The context's material setup (0x007D5310). A material: +4 flags (16
// bits), +6 to +0xA state values, +8 an alpha (0 to 255), +0xB the number of
// texture stages (0 to 2), then per stage 0x14 bytes from +0x10: a texture
// (by two pointers), wrap (+4, +5), filters (+6, +7), a coordinate source
// (+8), the environment mode (+9), a flag for an animated texture matrix
// (+0xA) and its period (+0xC). The properties' flags change it: 8 dims the
// colour by the value at +0x10, 2 makes it translucent by it, 0x10 and 0x20
// pick other state values.
void SetMaterial(char* context, char* material, char* properties)
{
    const uint8_t flags = At<uint8_t>(properties, 0);
    const bool other = (flags >> 4) & 1, forced = (flags >> 5) & 1;
    bool translucent = (flags >> 1) & 1;
    float alpha = static_cast<float>(static_cast<double>(static_cast<int32_t>(At<uint8_t>(material, 8))) *
                                     Global<double>(0x00ABB4E0));
    const float value = At<float>(properties, 0x10);
    void* const driver = At<void*>(context, 0x30);
    if (flags & 8) {
        Virtual<void>(driver, 0x80, value, value, value);
        translucent = false;
    } else if (translucent && value < Global<float>(0x00A81228)) {
        alpha = static_cast<float>(static_cast<double>(value) * alpha);
        Virtual<void>(driver, 0x84, value);
    }
    const uint32_t bits = At<uint16_t>(material, 4);
    const uint8_t stages = At<uint8_t>(material, 0xB);
    const uint32_t second = stages >= 2 ? At<uint8_t>(material, 0x2C) : 0xFFFFFFFF;
    const uint32_t first = stages >= 1 ? At<uint8_t>(material, 0x18) : 0xFFFFFFFF;
    const uint32_t a = translucent ? 5 : At<uint8_t>(material, 0xA);
    const uint32_t b = translucent ? 4 : At<uint8_t>(material, 9);
    const uint32_t c = forced ? 4 : At<uint8_t>(material, 6);
    const uint32_t d = translucent ? 1 : (bits >> 4) & 1;
    uint32_t e = 0, f = 0;
    if (!other) {
        if (forced) {
            e = 1;
        } else {
            e = (bits >> 1) & 1;
            f = (bits >> 7) & 1;
        }
    }
    uint32_t state[2] = {0, 0};
    CallThis<void>(addr::SC4RenderState_Init, state, (bits >> 8) & 0xFFFFFF01, f, e, bits & 1, d,
                   static_cast<uint32_t>(stages), (bits >> 3) & 0xFFFFFF01, c, b, a,
                   static_cast<uint32_t>(At<uint8_t>(material, 7)), alpha, first, second);
    CallThis<void>(addr::SC4RenderContext_SetRenderState, context, state);
    if (!(bits & 0x20))
        return;
    const auto secondStage = [&] { return At<uint8_t>(At<char*>(At<char*>(context, 0x38), 0xC), 0x17C) != 0; };
    for (uint32_t stage = 0; stage < At<uint8_t>(material, 0xB); ++stage) {
        char* const texture = material + 0x10 + stage * 0x14;
        const uint32_t handle = **At<uint32_t**>(texture, 0);
        if (stage == 0 || secondStage())
            Virtual<void>(driver, 0xE8, handle, stage);
        At<uint32_t>(At<char*>(context, 0xE0), stage * 0x68) = handle;
        const uint32_t mode = At<uint8_t>(texture, 9);
        if ((stage == 0 || secondStage()) && At<uint32_t>(At<char*>(context, 0xE0), stage * 0x68 + 4) != mode) {
            Virtual<void>(driver, 0xCC, stage);
            Virtual<void>(driver, 0xD0, mode);
        }
        At<uint32_t>(At<char*>(context, 0xE0), stage * 0x68 + 4) = mode;
        CallThis<void>(addr::SC4RenderContext_SetState41E0, context, static_cast<uint32_t>(At<uint8_t>(texture, 4)),
                       static_cast<uint32_t>(At<uint8_t>(texture, 5)), stage);
        CallThis<void>(addr::SC4RenderContext_SetState4280, context, static_cast<uint32_t>(At<uint8_t>(texture, 6)),
                       static_cast<uint32_t>(At<uint8_t>(texture, 7)), stage);
        if (At<uint8_t>(texture, 8) == 4)
            CallThis<void>(addr::SC4RenderContext_SetState3030, context, context + 0xC0, stage);
        CallThis<void>(addr::SC4RenderContext_SetState40B0, context, static_cast<uint32_t>(At<uint8_t>(texture, 8)), stage);
        if (At<uint8_t>(texture, 0xA) & 1) {
            float period;
            std::memcpy(&period, texture + 0xC, 4);
            const double scale = static_cast<double>(Global<float>(0x00A81228)) / period;
            Cell matrix[16];
            for (Cell& cell : matrix)
                cell.u = Global<uint32_t>(0x00B4E324);
            matrix[0].f = static_cast<float>(scale);
            matrix[5].f = static_cast<float>(-scale);
            matrix[12].f = static_cast<float>(-(scale * At<float>(context, 0x19C)));
            matrix[13].f = static_cast<float>(scale * At<float>(context, 0x1A0));
            CallThis<void>(addr::SC4RenderContext_SetTextureMatrix, context, matrix, stage);
        } else {
            CallThis<void>(addr::SC4RenderContext_ClearTextureMatrix, context, stage);
        }
    }
}

// The context's model draw (0x007D5710): an instance's transform and colour,
// then each group of the model: its material set up, its mesh drawn.
// `properties` is a flags byte (4: plain colour, 8: not in the overlaid
// round) and a colour at +4. `original` draws the bounding box instead when
// the debug preference at +0x35C asks for it.
template <typename Original>
void DrawModel(char* context, void* instance, void* model, char* properties, uint8_t overlaid, Original&& original)
{
    const auto preference = [&](std::size_t offset) {
        return At<uint8_t>(At<char*>(At<char*>(context, 0x38), 0xC), offset) != 0;
    };
    if (!overlaid) {
        if (preference(0x35C))
            return original();
    } else if (At<uint8_t>(properties, 0) & 8) {
        return;
    }
    if (preference(0x3DC)) {
        float box[6] = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
        Virtual<void>(instance, 0x20, box);
        float size[3], length;
        for (int axis = 0; axis < 3; ++axis)
            size[axis] = static_cast<float>(static_cast<double>(box[3 + axis]) - box[axis]);
        const float* const measured =
            reinterpret_cast<const float*(__cdecl*)(const float*, float*)>(addr::S3D_VectorLength)(size, &length);
        if (static_cast<double>(At<float>(At<char*>(context, 0x24), 0x154)) * *measured <
            At<float>(At<char*>(At<char*>(context, 0x38), 0x24), 0x184))
            return;
    }
    const uint32_t groups = Virtual<uint32_t>(model, 0xC);
    struct {
        uint8_t flags[4];
        float rotation[9];
        float position[3];
        float scale;
    } transform = {{0, 0, 0, 0}, {1, 0, 0, 0, 1, 0, 0, 0, 1}, {0, 0, 0}, 1};
    Virtual<void>(instance, 0x24, &transform);
    CallThis<void>(addr::SC4RenderContext_SetModelTransform, context, &transform);
    void* const driver = At<void*>(context, 0x30);
    if (!overlaid && !(At<uint8_t>(properties, 0) & 4)) {
        CallThis<void>(addr::SC4RenderContext_SetColourLit, context, instance, properties + 4);
    } else {
        Virtual<void>(driver, 0x80, At<uint32_t>(properties, 4), At<uint32_t>(properties, 8), At<uint32_t>(properties, 0xC));
        At<uint8_t>(context, 0x1F8) = 1;
    }
    for (uint32_t group = 0; group < groups; ++group) {
        char* const material = Virtual<char*>(model, 0x18, group);
        void* const mesh = Virtual<void*>(model, 0x10, group);
        CallThis<void>(overlaid ? addr::SC4RenderContext_SetMaterialOverlaid : addr::SC4RenderContext_SetMaterial, context,
                       material, properties);
        CallThis<void>(addr::SC4RenderContext_DrawMesh, context, mesh);
        if (!preference(0x17C) && At<uint8_t>(material, 0xB) == 2) {
            CallThis<void>(addr::SC4RenderContext_BeginSecondPass, context);
            CallThis<void>(addr::SC4RenderContext_DrawMesh, context, mesh);
        }
    }
    if (At<uint8_t>(context, 0x1F8)) {
        Virtual<void>(driver, 0x80, At<uint32_t>(context, 0x94), At<uint32_t>(context, 0x98), At<uint32_t>(context, 0x9C));
        At<uint8_t>(context, 0x1F8) = 0;
    }
    Virtual<void>(driver, 0x84, 1.0f);
}

// --- The rows' layer lists and the skirts' colours ------------------------------------------

// 0x0075ABA0: a row's list of texture layers with their spans of columns,
// from the layer ids of the row's vertices and the next row's, one layer
// slot at a time.
void RebuildRowLayers(char* self, int32_t row)
{
    const char* const records = Records(self);
    char** const list = &Global<char**>(addr::STE_RowLayers)[row];
    CallThis<void>(addr::STE_ClearRowLayers, list);
    const int32_t index = Global<int32_t>(addr::STE_DirtyRowBase) + row;
    uint32_t* word = Global<uint32_t*>(addr::STE_DirtyRowBits) + index / 32;
    int32_t bit = index % 32;
    if (bit < 0) {
        bit += 32;
        --word;
    }
    *word &= ~(1u << bit);
    const uint32_t columns = Columns(), cells = Global<uint32_t>(addr::STE_CellsX);
    uint32_t start = static_cast<uint32_t>(row);
    const auto add = [&](uint32_t layer, uint32_t last) {
        CallThis<void>(addr::cSTETerrainView3D_AddRowLayerSpan, self, list, layer, start, last);
    };
    for (uint32_t slot = 0; slot < 3; ++slot) {
        uint32_t current = 0;
        uint32_t vertex = columns * static_cast<uint32_t>(row);
        for (uint32_t column = 0; column < columns; ++column) {
            for (int next = 0; next < 2; ++next) {
                const uint32_t id = *reinterpret_cast<const uint16_t*>(records + (slot + vertex * 9) * 4 + 0x16);
                if (current != id) {
                    if (current != 0)
                        add(current, next ? column : column - 1);
                    current = id;
                    if (current != 0)
                        start = column;
                }
                vertex += next ? 0u - cells : columns;
            }
        }
        if (current != 0)
            add(current, cells);
    }
}

std::vector<uint32_t> RowLayerList(int32_t row)
{
    std::vector<uint32_t> out;
    const char* const head = RowLayers(row);
    for (const char* node = Next(head); node != head; node = Next(node)) {
        out.push_back(LayerId(node));
        const char* const spans = At<const char*>(const_cast<char*>(node), 0xC);
        for (const char* span = Next(spans); span != spans; span = Next(span)) {
            out.push_back(At<uint32_t>(const_cast<char*>(span), 8));
            out.push_back(At<uint32_t>(const_cast<char*>(span), 0xC));
        }
        out.push_back(0xFFFFFFFF);
    }
    return out;
}

uint8_t Byte(double value) { return static_cast<uint8_t>(static_cast<int64_t>(value)); }

// 0x007558B0: the land skirts' colours. Each side gets a grey from its
// normal and the light; the top row has it doubled where the terrain is
// above a height and a blend below, and each layer's bottom row takes the
// next layer's top (the last the plain grey).
void UpdateSkirtColours()
{
    uint32_t& flags = Global<uint32_t>(addr::STE_SkirtStaticFlags);
    float* const normals = &Global<float>(addr::STE_SkirtNormals);
    if (!(flags & 1)) {
        flags |= 1;
        static const float kNormals[12] = {0, 0, 1, 1, 0, 0, 0, 0, -1, -1, 0, 0};
        std::memcpy(normals, kNormals, sizeof(kNormals));
    }
    void* const transform = Global<void*>(addr::SC4_Transform43DDC);
    const float* const light = &Global<float>(addr::STE_SkirtLightScale);
    const float limit = Global<float>(0x00AB43E4);
    for (uint32_t side = 0; side < 4; ++side) {
        float turned[3];
        const float* const n = Virtual<const float*>(transform, 0x5C, turned, normals + side * 3);
        const float rounded = static_cast<float>(
            ((static_cast<double>(light[2]) * n[1] + static_cast<double>(light[3]) * n[2]) + static_cast<double>(light[1]) * n[0]) +
            49152.0f);
        uint32_t bits;
        std::memcpy(&bits, &rounded, 4);
        bits += 0xB8C00000;
        double low = static_cast<double>(static_cast<int32_t>(static_cast<uint8_t>(bits - (bits >> 8)))) * light[0];
        if (low > limit)
            low = limit;
        double high = low + low;
        if (high > limit)
            high = limit;
        const float slope = static_cast<float>((high - low) * Global<float>(0x00AB43E0));
        const uint8_t top = Byte(high), bottom = Byte(low);
        const int32_t count = (side & 1) ? Global<int32_t>(addr::STE_Rows) : static_cast<int32_t>(Columns());
        char** const arrays = &Global<char*>(addr::STE_LandSkirts) + side * 3;
        const auto fill = [](char* vertex, uint8_t value) {
            vertex[0xC] = vertex[0xD] = vertex[0xE] = static_cast<char>(value);
        };
        for (int32_t i = 0; i < count; ++i) {
            const std::size_t upper = static_cast<std::size_t>(i) * 0x18;
            const std::size_t lower = static_cast<std::size_t>(count + i) * 0x18;
            for (int layer = 0; layer < 3; ++layer) {
                const float height = At<float>(arrays[layer], upper + 4);
                fill(arrays[layer] + upper,
                     height >= Global<float>(0x00AB43DC) ? top : Byte(static_cast<double>(slope) * height + low));
            }
            fill(arrays[0] + lower, static_cast<uint8_t>(arrays[1][upper + 0xE]));
            fill(arrays[1] + lower, static_cast<uint8_t>(arrays[2][upper + 0xE]));
            fill(arrays[2] + lower, bottom);
        }
    }
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
    // OPENSC4_TEST_SINGLE_STAGE=1: the paths for cards without a second
    // texture stage (the overlays' plain shadow pass among them), by
    // clearing the preference at +0x17C.
    static const bool singleStage = [] {
        const char* v = std::getenv("OPENSC4_TEST_SINGLE_STAGE");
        return v && *v == '1';
    }();
    if (singleStage)
        At<uint8_t>(At<char*>(Global<char*>(0x00B43CC4), 0xC), 0x17C) = 0;
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
using RebuildFn = void(__fastcall*)(char*, void*, int32_t);
RebuildFn g_rebuild;
bool g_compareRebuild = false;

void __fastcall DRebuild(char* self, void*, int32_t row)
{
    if (g_depth > 0)
        return g_rebuild(self, nullptr, row);
    if (!g_compareRebuild)
        return RebuildRowLayers(self, row);
    const int32_t index = Global<int32_t>(addr::STE_DirtyRowBase) + row;
    uint32_t* const word = Global<uint32_t*>(addr::STE_DirtyRowBits) + (index >> 5);
    const uint32_t before = *word;
    RebuildRowLayers(self, row);
    const std::vector<uint32_t> mine = RowLayerList(row);
    const uint32_t mineWord = *word;
    *word = before;
    g_rebuild(self, nullptr, row);
    const std::vector<uint32_t> theirs = RowLayerList(row);
    const bool same = mine == theirs && mineWord == *word;
    char text[96] = "";
    if (!same)
        std::snprintf(text, sizeof(text), "row %d: %zu values and dirty word %08X", row, theirs.size(), *word);
    char mineText[96] = "";
    if (!same)
        std::snprintf(mineText, sizeof(mineText), "row %d: %zu values and dirty word %08X", row, mine.size(), mineWord);
    compare::Record("terrain.rowlayers", same, text, mineText);
}

using ColoursFn = void(__fastcall*)(char*, void*);
ColoursFn g_colours;
bool g_compareColours = false;

std::vector<char> SkirtColourBytes()
{
    std::vector<char> out;
    for (uint32_t side = 0; side < 4; ++side) {
        const std::size_t count = (side & 1) ? Global<uint32_t>(addr::STE_Rows) : Columns();
        for (uint32_t layer = 0; layer < 3; ++layer) {
            const char* const array = (&Global<char*>(addr::STE_LandSkirts))[side * 3 + layer];
            out.insert(out.end(), array, array + count * 2 * 0x18);
        }
    }
    return out;
}

void __fastcall DColours(char* self, void*)
{
    if (g_depth > 0)
        return g_colours(self, nullptr);
    UpdateSkirtColours();
    if (!g_compareColours)
        return;
    const std::vector<char> mine = SkirtColourBytes();
    g_colours(self, nullptr);
    const std::vector<char> theirs = SkirtColourBytes();
    std::size_t at = 0;
    while (at < mine.size() && mine[at] == theirs[at])
        ++at;
    const bool same = at == mine.size();
    char text[64] = "", mineText[64] = "";
    if (!same) {
        std::snprintf(text, sizeof(text), "byte %zu: %02X", at, static_cast<uint8_t>(theirs[at]));
        std::snprintf(mineText, sizeof(mineText), "byte %zu: %02X", at, static_cast<uint8_t>(mine[at]));
    }
    compare::Record("terrain.skirtcolours", same, text, mineText);
}

OPENSC4_THUNK(TRebuild, DRebuild, 4)
OPENSC4_THUNK(TColours, DColours, 0)
using OverlayRectFn = void(__fastcall*)(char*, void*, char*, const int32_t*);
OverlayRectFn g_overlayRect;
bool g_compareOverlayRect = false;

void __fastcall DOverlayRect(char* manager, void*, char* context, const int32_t* cells)
{
    if (g_depth > 0)
        return g_overlayRect(manager, nullptr, context, cells);
    static uint32_t paths = 0;
    const uint32_t path = 1u << ((At<uint8_t>(manager, 0x16) ? 4 : At<void*>(manager, 0x94) ? 2 : 0) + (At<uint8_t>(manager, 0x15) ? 1 : 0));
    if (!(paths & path)) {
        paths |= path;
        log::Write("overlays: first rectangle with water vertices %d, water rows %d, dynamic buffers %d",
                   At<uint8_t>(manager, 0x16), At<uint8_t>(manager, 0x15), At<void*>(manager, 0x94) != nullptr);
    }
    // OPENSC4_TEST_OVERLAY_PARTIAL=1: every rectangle of water vertices is
    // also drawn cell by cell, ours compared with the game's (the test cities
    // have no overlay partly under water, so nothing else reaches that draw;
    // the extra quads show).
    static const bool partial = [] {
        const char* v = std::getenv("OPENSC4_TEST_OVERLAY_PARTIAL");
        return v && *v == '1';
    }();
    if (partial && At<uint8_t>(manager, 0x16))
        DrawCompared("overlays.rect_partial", PassRegions(At<char*>(manager, 0x8C), context),
                     [&] { DrawOverlayRectPartial(manager, context, cells); },
                     [&] { CallThis<void>(addr::cSC4OverlayManager_DrawRectPartial, manager, context, cells); }, true);
    if (!g_compareOverlayRect)
        return DrawOverlayRect(manager, context, cells);
    DrawCompared("overlays.rect", PassRegions(At<char*>(manager, 0x8C), context),
                 [&] { DrawOverlayRect(manager, context, cells); },
                 [&] { g_overlayRect(manager, nullptr, context, cells); });
}

using DecalsFn = void(__fastcall*)(char*, void*, const float*, char*, char**);
DecalsFn g_decals;
bool g_compareDecals = false;

void __fastcall DDecals(char* manager, void*, const float* matrix, char* context, char** list)
{
    if (g_depth > 0)
        return g_decals(manager, nullptr, matrix, context, list);
    if (!g_compareDecals)
        return DrawDecals(manager, matrix, context, list);
    std::vector<Region> regions = PassRegions(At<char*>(manager, 0x8C), context);
    regions.push_back({"manager", manager, 0xE0});
    DrawCompared(g_selfCheck ? "overlays.decals.selfcheck" : "overlays.decals", regions,
                 [&] {
                     if (g_selfCheck)
                         g_decals(manager, nullptr, matrix, context, list);
                     else
                         DrawDecals(manager, matrix, context, list);
                 },
                 [&] { g_decals(manager, nullptr, matrix, context, list); });
}

DecalsFn g_shadowsPlain;
bool g_compareShadowsPlain = false;

void __fastcall DShadowsPlain(char* manager, void*, const float* matrix, char* context, char** list)
{
    if (g_depth > 0)
        return g_shadowsPlain(manager, nullptr, matrix, context, list);
    if (!g_compareShadowsPlain)
        return DrawShadowsPlain(manager, matrix, context, list);
    std::vector<Region> regions = PassRegions(At<char*>(manager, 0x8C), context);
    regions.push_back({"manager", manager, 0xE0});
    DrawCompared(g_selfCheck ? "overlays.shadows.selfcheck" : "overlays.shadows", regions,
                 [&] {
                     if (g_selfCheck)
                         g_shadowsPlain(manager, nullptr, matrix, context, list);
                     else
                         DrawShadowsPlain(manager, matrix, context, list);
                 },
                 [&] { g_shadowsPlain(manager, nullptr, matrix, context, list); });
}

DecalsFn g_shadows;
bool g_compareShadows = false;

void __fastcall DShadows(char* manager, void*, const float* matrix, char* context, char** list)
{
    if (g_depth > 0)
        return g_shadows(manager, nullptr, matrix, context, list);
    static uint32_t cases = 0;
    const uint32_t which = (At<int32_t>(manager, 0x1C) == 4 ? 1u : 2u) | (At<uint8_t>(manager, 0x16) ? 4u : 8u);
    if ((cases & which) != which) {
        cases |= which;
        log::Write("overlays: shadow pass with the manager's +0x1C %d, water vertices %d, sun test %d",
                   At<int32_t>(manager, 0x1C), At<uint8_t>(manager, 0x16), Global<uint8_t>(addr::STE_OverlaySunTest));
    }
    if (!g_compareShadows)
        return DrawShadows(manager, matrix, context, list);
    std::vector<Region> regions = PassRegions(At<char*>(manager, 0x8C), context);
    regions.push_back({"manager", manager, 0xE0});
    DrawCompared(g_selfCheck ? "overlays.shadows2.selfcheck" : "overlays.shadows2", regions,
                 [&] {
                     if (g_selfCheck)
                         g_shadows(manager, nullptr, matrix, context, list);
                     else
                         DrawShadows(manager, matrix, context, list);
                 },
                 [&] { g_shadows(manager, nullptr, matrix, context, list); });
}

using OverlaysFn = void(__fastcall*)(char*, void*, char*);
OverlaysFn g_overlays;
bool g_compareOverlays = false;

void __fastcall DOverlays(char* manager, void*, char* context)
{
    if (g_depth > 0)
        return g_overlays(manager, nullptr, context);
    if (!g_compareOverlays)
        return DrawOverlays(manager, context);
    std::vector<Region> regions = PassRegions(At<char*>(manager, 0x8C), context);
    regions.push_back({"manager", manager, 0xE0});
    // The list is rebuilt by both runs; its contents are compared apart
    // (its buffer may move, so it is not put back in between).
    const auto listed = [] {
        const uint32_t* const* const list = &Global<const uint32_t*>(addr::STE_OverlayList);
        return std::vector<uint32_t>(list[0], list[1]);
    };
    std::vector<uint32_t> mine, theirs;
    DrawCompared(g_selfCheck ? "overlays.draw.selfcheck" : "overlays.draw", regions,
                 [&] {
                     if (g_selfCheck)
                         g_overlays(manager, nullptr, context);
                     else
                         DrawOverlays(manager, context);
                     mine = listed();
                 },
                 [&] {
                     g_overlays(manager, nullptr, context);
                     theirs = listed();
                 });
    if (mine != theirs) {
        char text[64], mineText[64];
        std::snprintf(text, sizeof text, "%zu overlays listed", theirs.size());
        std::snprintf(mineText, sizeof mineText, "%zu overlays listed", mine.size());
        compare::Record("overlays.draw.list", false, text, mineText);
    }
}

using ListVisibleFn = void(__fastcall*)(char*, void*, char**);
ListVisibleFn g_listVisible;
bool g_compareListVisible = false;

void __fastcall DListVisible(char* manager, void*, char** list)
{
    if (g_depth > 0)
        return g_listVisible(manager, nullptr, list);
    if (!g_compareListVisible)
        return ListVisibleOverlays(manager, list);
    const uint16_t stamp = At<uint16_t>(manager, 0xE0);
    ListVisibleOverlays(manager, list);
    const std::vector<uint32_t> mine(reinterpret_cast<uint32_t*>(list[0]), reinterpret_cast<uint32_t*>(list[1]));
    const bool stamped = At<uint16_t>(manager, 0xE0) == static_cast<uint16_t>(stamp + 1);
    g_listVisible(manager, nullptr, list);
    const std::vector<uint32_t> theirs(reinterpret_cast<uint32_t*>(list[0]), reinterpret_cast<uint32_t*>(list[1]));
    const bool same = stamped && mine == theirs;
    char text[64] = "", mineText[64] = "";
    if (!same) {
        std::snprintf(text, sizeof text, "%zu overlays", theirs.size());
        std::snprintf(mineText, sizeof mineText, "%zu overlays, stamp %s", mine.size(), stamped ? "ok" : "off");
    }
    compare::Record("overlays.visible", same, text, mineText);
}

using RenderPassFn = void(__fastcall*)(char*, void*);
RenderPassFn g_drawStatic;
bool g_compareStatic = false;

// The memory a 3D render pass may change outside the drawables: the render
// context with its stage cache and the S3D camera. The render's own vectors
// are rebuilt by each run.
std::vector<Region> RenderRegions(char* render)
{
    char* const context = At<char*>(render, 0x11C);
    std::vector<Region> regions{{"context", context, kContextSize}};
    if (char* const camera = At<char*>(render, 0xD8))
        regions.push_back({"camera", camera, kCameraSize});
    if (char* const stages = At<char*>(context, 0xE0))
        regions.push_back({"stages", stages, static_cast<std::size_t>(At<char*>(context, 0xE4) - stages)});
    return regions;
}

// A run whose draws are recorded and thrown away, with the regions and the
// driver's state put back after it.
template <typename Run>
void QuietRun(const std::vector<Region>& regions, Run&& run)
{
    std::vector<std::vector<char>> before;
    for (const Region& r : regions)
        before.emplace_back(r.data, r.data + r.size);
    const std::vector<char> pipeline = gd::SaveState();
    ++g_depth;
    gd::BeginCapture(false);
    run();
    gd::EndCapture();
    --g_depth;
    for (std::size_t i = 0; i < regions.size(); ++i)
        std::memcpy(regions[i].data, before[i].data(), regions[i].size);
    gd::RestoreState(pipeline);
}

void __fastcall DDrawStatic(char* render, void*)
{
    if (g_depth > 0)
        return g_drawStatic(render, nullptr);
    if (!g_compareStatic)
        return DrawStatic(render);
    // Drawables change on their first draw (a texture arrives, a placeholder
    // goes), so the game's pass differs from itself when run twice. A quiet
    // run of the game's first lets both compared runs see them settled.
    QuietRun(RenderRegions(render), [&] { g_drawStatic(render, nullptr); });
    DrawCompared(g_selfCheck ? "models.static.selfcheck" : "models.static", RenderRegions(render),
                 [&] {
                     if (g_selfCheck)
                         g_drawStatic(render, nullptr);
                     else
                         DrawStatic(render);
                 },
                 [&] { g_drawStatic(render, nullptr); }, true);
}

RenderPassFn g_drawMoving;
bool g_compareMoving = false;

void __fastcall DDrawMoving(char* render, void*)
{
    if (g_depth > 0)
        return g_drawMoving(render, nullptr);
    if (!g_compareMoving)
        return DrawMoving(render);
    QuietRun(RenderRegions(render), [&] { g_drawMoving(render, nullptr); });
    DrawCompared(g_selfCheck ? "models.moving.selfcheck" : "models.moving", RenderRegions(render),
                 [&] {
                     if (g_selfCheck)
                         g_drawMoving(render, nullptr);
                     else
                         DrawMoving(render);
                 },
                 [&] { g_drawMoving(render, nullptr); }, true);
}

std::vector<Region> ContextRegions(char* context)
{
    std::vector<Region> regions{{"context", context, kContextSize}};
    if (char* const stages = At<char*>(context, 0xE0))
        regions.push_back({"stages", stages, static_cast<std::size_t>(At<char*>(context, 0xE4) - stages)});
    return regions;
}

using BillboardFn = uint32_t(__fastcall*)(char*, void*, char*, uint32_t);
BillboardFn g_billboard;
bool g_compareBillboard = false;

uint32_t __fastcall DBillboard(char* self, void*, char* context, uint32_t overlaid)
{
    if (g_depth > 0)
        return g_billboard(self, nullptr, context, overlaid);
    if (!g_compareBillboard)
        return DrawBillboard(self, context, static_cast<uint8_t>(overlaid));
    std::vector<Region> regions{{"context", context, kContextSize}, {"billboard", self - 0x170, 0x170}};
    if (char* const render = At<char*>(context, 0x34))
        if (char* const camera = At<char*>(render, 0xD8))
            regions.push_back({"camera", camera, kCameraSize});
    if (char* const stages = At<char*>(context, 0xE0))
        regions.push_back({"stages", stages, static_cast<std::size_t>(At<char*>(context, 0xE4) - stages)});
    uint32_t result = 0, mine = 0;
    DrawCompared(g_selfCheck ? "models.billboard.selfcheck" : "models.billboard", regions,
                 [&] {
                     mine = g_selfCheck ? g_billboard(self, nullptr, context, overlaid)
                                        : DrawBillboard(self, context, static_cast<uint8_t>(overlaid));
                 },
                 [&] { result = g_billboard(self, nullptr, context, overlaid); });
    if (mine != result)
        compare::Record("models.billboard.result", false, std::to_string(result), std::to_string(mine));
    return result;
}

using ModelFn = void(__fastcall*)(char*, void*, void*, void*, char*, uint32_t);
ModelFn g_model;
bool g_compareModel = false;

void __fastcall DModel(char* context, void*, void* instance, void* model, char* properties, uint32_t overlaid)
{
    const auto original = [&] { g_model(context, nullptr, instance, model, properties, overlaid); };
    if (g_depth > 0)
        return original();
    const auto ours = [&] { DrawModel(context, instance, model, properties, static_cast<uint8_t>(overlaid), original); };
    if (!g_compareModel)
        return ours();
    std::vector<Region> regions{{"context", context, kContextSize}};
    if (char* const render = At<char*>(context, 0x34))
        if (char* const camera = At<char*>(render, 0xD8))
            regions.push_back({"camera", camera, kCameraSize});
    if (char* const stages = At<char*>(context, 0xE0))
        regions.push_back({"stages", stages, static_cast<std::size_t>(At<char*>(context, 0xE4) - stages)});
    QuietRun(regions, original);
    DrawCompared(g_selfCheck ? "models.model.selfcheck" : "models.model", regions,
                 [&] {
                     if (g_selfCheck)
                         original();
                     else
                         ours();
                 },
                 original, true);
}

using MeshFn = void(__fastcall*)(char*, void*, char**);
MeshFn g_mesh;
bool g_compareMesh = false;

void __fastcall DMesh(char* context, void*, char** mesh)
{
    if (g_depth > 0)
        return g_mesh(context, nullptr, mesh);
    if (!g_compareMesh)
        return DrawMesh(context, mesh);
    DrawCompared("models.mesh", ContextRegions(context), [&] { DrawMesh(context, mesh); },
                 [&] { g_mesh(context, nullptr, mesh); });
}

using MaterialFn = void(__fastcall*)(char*, void*, char*, char*);
MaterialFn g_materialOverlaid;
bool g_compareMaterialOverlaid = false;

void __fastcall DMaterialOverlaid(char* context, void*, char* material, char* properties)
{
    if (g_depth > 0)
        return g_materialOverlaid(context, nullptr, material, properties);
    if (!g_compareMaterialOverlaid)
        return SetMaterialOverlaid(context, material, properties);
    DrawCompared("models.material_overlaid", ContextRegions(context),
                 [&] { SetMaterialOverlaid(context, material, properties); },
                 [&] { g_materialOverlaid(context, nullptr, material, properties); });
}

MaterialFn g_material;
bool g_compareMaterial = false;

void __fastcall DMaterial(char* context, void*, char* material, char* properties)
{
    if (g_depth > 0)
        return g_material(context, nullptr, material, properties);
    if (!g_compareMaterial)
        return SetMaterial(context, material, properties);
    DrawCompared("models.material", ContextRegions(context), [&] { SetMaterial(context, material, properties); },
                 [&] { g_material(context, nullptr, material, properties); }, true);
}

OPENSC4_THUNK(TMaterial, DMaterial, 8)
OPENSC4_THUNK(TMesh, DMesh, 4)
OPENSC4_THUNK(TMaterialOverlaid, DMaterialOverlaid, 8)
OPENSC4_THUNK(TModel, DModel, 16)
BillboardFn g_groundQuad;
bool g_compareGroundQuad = false;

uint32_t __fastcall DGroundQuad(char* self, void*, char* context, uint32_t overlaid)
{
    if (g_depth > 0)
        return g_groundQuad(self, nullptr, context, overlaid);
    if (!g_compareGroundQuad)
        return DrawGroundQuad(self, context, static_cast<uint8_t>(overlaid));
    std::vector<Region> regions = ContextRegions(context);
    regions.push_back({"quad", self - 0xEC, 0xEC});
    regions.push_back({"tint", reinterpret_cast<char*>(0x00B0A878), 1});
    uint32_t result = 0, mine = 0;
    const auto original = [&] { result = g_groundQuad(self, nullptr, context, overlaid); };
    QuietRun(regions, original);
    DrawCompared(g_selfCheck ? "models.groundquad.selfcheck" : "models.groundquad", regions,
                 [&] {
                     mine = g_selfCheck ? g_groundQuad(self, nullptr, context, overlaid)
                                        : DrawGroundQuad(self, context, static_cast<uint8_t>(overlaid));
                 },
                 original, true);
    if (mine != result)
        compare::Record("models.groundquad.result", false, std::to_string(result), std::to_string(mine));
    return result;
}

// The two rarer drawables share one detour shape.
struct SmallDrawable {
    const char* id;
    std::size_t base, size;
    uint32_t (*ours)(char*, char*, uint32_t);
    BillboardFn original;
    bool compare;
};
SmallDrawable g_sideStrip = {"models.sidestrip", 0xAC, 0xAC, DrawSideStrip, nullptr, false};
SmallDrawable g_rotationQuads = {"models.rotationquads", 0x274, 0x274, DrawRotationQuads, nullptr, false};
SmallDrawable g_flaggedQuad = {"models.flaggedquad", 0x17C, 0x17C, DrawFlaggedQuad, nullptr, false};

uint32_t DrawSmall(SmallDrawable& d, char* self, char* context, uint32_t overlaid)
{
    if (g_depth > 0)
        return d.original(self, nullptr, context, overlaid);
    if (!d.compare)
        return d.ours(self, context, overlaid);
    std::vector<Region> regions = ContextRegions(context);
    regions.push_back({"object", self - d.base, d.size});
    uint32_t result = 0, mine = 0;
    const auto original = [&] { result = d.original(self, nullptr, context, overlaid); };
    QuietRun(regions, original);
    DrawCompared(d.id, regions, [&] { mine = g_selfCheck ? d.original(self, nullptr, context, overlaid) : d.ours(self, context, overlaid); },
                 original, true);
    if (mine != result)
        compare::Record(d.id, false, std::to_string(result), std::to_string(mine));
    return result;
}

uint32_t __fastcall DSideStrip(char* self, void*, char* context, uint32_t overlaid)
{
    return DrawSmall(g_sideStrip, self, context, overlaid);
}

uint32_t __fastcall DRotationQuads(char* self, void*, char* context, uint32_t overlaid)
{
    return DrawSmall(g_rotationQuads, self, context, overlaid);
}

uint32_t __fastcall DFlaggedQuad(char* self, void*, char* context, uint32_t overlaid)
{
    return DrawSmall(g_flaggedQuad, self, context, overlaid);
}

OPENSC4_THUNK(TFlaggedQuad, DFlaggedQuad, 8)
OPENSC4_THUNK(TSideStrip, DSideStrip, 8)
OPENSC4_THUNK(TRotationQuads, DRotationQuads, 8)
using CellQuadFn = void(__fastcall*)(char*, void*, char*, uint32_t, uint32_t, const uint32_t*, float, uint32_t);
CellQuadFn g_cellQuad;
bool g_compareCellQuad = false;

void __fastcall DCellQuad(char* interface, void*, char* context, uint32_t x, uint32_t z, const uint32_t* coordinates,
                          float alpha, uint32_t always)
{
    const auto original = [&] { g_cellQuad(interface, nullptr, context, x, z, coordinates, alpha, always); };
    if (g_depth > 0)
        return original();
    const auto ours = [&] { DrawCellQuad(interface, context, x, z, coordinates, alpha, static_cast<uint8_t>(always)); };
    if (!g_compareCellQuad)
        return ours();
    DrawCompared("terrain.cellquad", ContextRegions(context), ours, original, true);
}

OPENSC4_THUNK(TCellQuad, DCellQuad, 24)
OPENSC4_THUNK(TGroundQuad, DGroundQuad, 8)
using CornersFn = uint32_t(__fastcall*)(char*, void*, char*);
CornersFn g_corners;
bool g_compareCorners = false;

uint32_t __fastcall DCorners(char* object, void*, char* context)
{
    if (!g_compareCorners)
        return UpdateBillboardCorners(object, context);
    constexpr std::size_t kSize = 0x170;
    const std::vector<char> before(object, object + kSize);
    UpdateBillboardCorners(object, context);
    const std::vector<char> mine(object, object + kSize);
    std::memcpy(object, before.data(), kSize);
    const uint32_t result = g_corners(object, nullptr, context);
    std::size_t at = 0;
    while (at < kSize && mine[at] == object[at])
        ++at;
    const bool same = at == kSize;
    char text[64] = "", mineText[64] = "";
    if (!same) {
        at &= ~std::size_t{3};
        std::snprintf(text, sizeof text, "+%zX %s", at, Hex(object + at, 8).c_str());
        std::snprintf(mineText, sizeof mineText, "+%zX %s", at, Hex(mine.data() + at, 8).c_str());
    }
    compare::Record("models.billboard_corners", same, text, mineText);
    return result;
}

OPENSC4_THUNK(TCorners, DCorners, 4)
OPENSC4_THUNK(TBillboard, DBillboard, 8)
OPENSC4_THUNK(TDrawMoving, DDrawMoving, 0)
OPENSC4_THUNK(TDrawStatic, DDrawStatic, 0)
OPENSC4_THUNK(TListVisible, DListVisible, 4)
OPENSC4_THUNK(TOverlays, DOverlays, 4)
OPENSC4_THUNK(TShadows, DShadows, 12)
OPENSC4_THUNK(TShadowsPlain, DShadowsPlain, 12)
OPENSC4_THUNK(TDecals, DDecals, 12)
OverlayRectFn g_overlayRectPartial;
bool g_compareOverlayRectPartial = false;

void __fastcall DOverlayRectPartial(char* manager, void*, char* context, const int32_t* cells)
{
    if (g_depth > 0)
        return g_overlayRectPartial(manager, nullptr, context, cells);
    if (!g_compareOverlayRectPartial)
        return DrawOverlayRectPartial(manager, context, cells);
    DrawCompared("overlays.rect_partial", PassRegions(At<char*>(manager, 0x8C), context),
                 [&] { DrawOverlayRectPartial(manager, context, cells); },
                 [&] { g_overlayRectPartial(manager, nullptr, context, cells); }, true);
}

OPENSC4_THUNK(TOverlayRectPartial, DOverlayRectPartial, 8)
OPENSC4_THUNK(TOverlayRect, DOverlayRect, 8)
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

bool InstallRowLayers(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareRebuild = mode == registry::Mode::Compare;
    if (!Detour(addr::cSTETerrainView3D_RebuildRowLayers, &TRebuild, &g_rebuild))
        return false;
    log::Write("terrain draw: row layer lists %s", g_compareRebuild ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredRowLayers = registry::Register({
    "renderer.terrain.rowlayers",
    "renderer/terrain",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSTETerrainView3D_RebuildRowLayers,
    "A row's list of texture layers and their spans; compare mode builds ours, then the game's, and compares the "
    "lists (docs/renderer.md, \"The terrain\")",
    true,
    InstallRowLayers,
});

bool InstallSkirtColours(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareColours = mode == registry::Mode::Compare;
    if (!Detour(addr::cSTETerrainView3D_UpdateSkirtColours, &TColours, &g_colours))
        return false;
    log::Write("terrain draw: skirt colours %s", g_compareColours ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredSkirtColours = registry::Register({
    "renderer.terrain.skirtcolours",
    "renderer/terrain",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSTETerrainView3D_UpdateSkirtColours,
    "The land skirts' vertex colours; compare mode fills ours, then the game's, and compares the vertex arrays "
    "(docs/renderer.md, \"The terrain\")",
    true,
    InstallSkirtColours,
});

bool InstallOverlayRect(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareOverlayRect = mode == registry::Mode::Compare;
    if (!Detour(addr::cSC4OverlayManager_DrawRect, &TOverlayRect, &g_overlayRect))
        return false;
    log::Write("overlays: rectangle draws %s", g_compareOverlayRect ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredOverlayRect = registry::Register({
    "renderer.overlays.rect",
    "renderer/overlays",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4OverlayManager_DrawRect,
    "The overlay manager's draw of a rectangle of terrain cells; compare mode as renderer.terrain.land "
    "(docs/renderer.md, \"The overlays\")",
    true,
    InstallOverlayRect,
});

bool InstallDecals(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareDecals = mode == registry::Mode::Compare;
    if (!Detour(addr::cSC4OverlayManager_DrawDecals, &TDecals, &g_decals))
        return false;
    log::Write("overlays: decal pass %s", g_compareDecals ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredDecals = registry::Register({
    "renderer.overlays.decals",
    "renderer/overlays",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4OverlayManager_DrawDecals,
    "The overlay manager's decal pass; compare mode as renderer.terrain.land (docs/renderer.md, \"The overlays\")",
    true,
    InstallDecals,
});

bool InstallShadowsPlain(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareShadowsPlain = mode == registry::Mode::Compare;
    if (!Detour(addr::cSC4OverlayManager_DrawShadowsPlain, &TShadowsPlain, &g_shadowsPlain))
        return false;
    log::Write("overlays: plain shadow pass %s", g_compareShadowsPlain ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredShadowsPlain = registry::Register({
    "renderer.overlays.shadows_plain",
    "renderer/overlays",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4OverlayManager_DrawShadowsPlain,
    "The overlay manager's plain shadow pass; compare mode as renderer.terrain.land (docs/renderer.md, \"The "
    "overlays\")",
    true,
    InstallShadowsPlain,
});

bool InstallShadows(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareShadows = mode == registry::Mode::Compare;
    if (!Detour(addr::cSC4OverlayManager_DrawShadows, &TShadows, &g_shadows))
        return false;
    log::Write("overlays: shadow pass %s", g_compareShadows ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredShadows = registry::Register({
    "renderer.overlays.shadows",
    "renderer/overlays",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4OverlayManager_DrawShadows,
    "The overlay manager's two-stage shadow pass; compare mode as renderer.terrain.land (docs/renderer.md, \"The "
    "overlays\")",
    true,
    InstallShadows,
});

bool InstallOverlays(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareOverlays = mode == registry::Mode::Compare;
    if (!Detour(addr::cSC4OverlayManager_Draw, &TOverlays, &g_overlays))
        return false;
    log::Write("overlays: draw %s", g_compareOverlays ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredOverlays = registry::Register({
    "renderer.overlays.draw",
    "renderer/overlays",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4OverlayManager_Draw,
    "The overlay manager's draw: the inverse view matrix, the list of overlays and the passes; compare mode as "
    "renderer.terrain.land, with the passes the game's in both runs (docs/renderer.md, \"The overlays\")",
    true,
    InstallOverlays,
});

bool InstallListVisible(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareListVisible = mode == registry::Mode::Compare;
    if (!Detour(addr::cSC4OverlayManager_ListVisible, &TListVisible, &g_listVisible))
        return false;
    log::Write("overlays: list of overlays in view %s", g_compareListVisible ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredListVisible = registry::Register({
    "renderer.overlays.visible",
    "renderer/overlays",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4OverlayManager_ListVisible,
    "The list of overlays in view; compare mode builds ours, then the game's, and compares the lists "
    "(docs/renderer.md, \"The overlays\")",
    true,
    InstallListVisible,
});

bool InstallOverlayRectPartial(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareOverlayRectPartial = mode == registry::Mode::Compare;
    if (!Detour(addr::cSC4OverlayManager_DrawRectPartial, &TOverlayRectPartial, &g_overlayRectPartial))
        return false;
    log::Write("overlays: partial rectangle draws %s", g_compareOverlayRectPartial ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredOverlayRectPartial = registry::Register({
    "renderer.overlays.rect_partial",
    "renderer/overlays",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4OverlayManager_DrawRectPartial,
    "The overlay manager's cell-by-cell draw of a rectangle partly under water; compare mode as "
    "renderer.terrain.land (docs/renderer.md, \"The overlays\")",
    true,
    InstallOverlayRectPartial,
});

bool InstallStatic(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareStatic = mode == registry::Mode::Compare;
    if (!Detour(addr::cSC43DRender_DrawStatic, &TDrawStatic, &g_drawStatic))
        return false;
    log::Write("models: static pass %s", g_compareStatic ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredStatic = registry::Register({
    "renderer.models.static",
    "renderer/models",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC43DRender_DrawStatic,
    "The 3D render's static pass: collects the drawables in view, sorts them and has them drawn; compare mode as "
    "renderer.terrain.land, the drawables the game's in both runs (docs/renderer.md, \"Models and lots\")",
    true,
    InstallStatic,
});

bool InstallMoving(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareMoving = mode == registry::Mode::Compare;
    if (!Detour(addr::cSC43DRender_DrawMoving, &TDrawMoving, &g_drawMoving))
        return false;
    log::Write("models: moving-objects pass %s", g_compareMoving ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredMoving = registry::Register({
    "renderer.models.moving",
    "renderer/models",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC43DRender_DrawMoving,
    "The 3D render's moving-objects pass; compare mode as renderer.models.static (docs/renderer.md, \"Models and "
    "lots\")",
    true,
    InstallMoving,
});

bool InstallBillboard(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareBillboard = mode == registry::Mode::Compare;
    if (!Detour(addr::cS3DBillboard_Draw, &TBillboard, &g_billboard))
        return false;
    log::Write("models: billboard draw %s", g_compareBillboard ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredBillboard = registry::Register({
    "renderer.models.billboard",
    "renderer/models",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cS3DBillboard_Draw,
    "The billboard's draw, a textured quad facing the camera; compare mode as renderer.terrain.land, per "
    "billboard (docs/renderer.md, \"Models and lots\")",
    true,
    InstallBillboard,
});

bool InstallModel(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareModel = mode == registry::Mode::Compare;
    if (!Detour(addr::SC4RenderContext_DrawModel, &TModel, &g_model))
        return false;
    log::Write("models: model draw %s", g_compareModel ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredModel = registry::Register({
    "renderer.models.model",
    "renderer/models",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4RenderContext_DrawModel,
    "The render context's model draw: transform, colour, then each group's material and mesh; compare mode as "
    "renderer.models.static, per model (docs/renderer.md, \"Models and lots\")",
    true,
    InstallModel,
});

bool InstallMesh(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareMesh = mode == registry::Mode::Compare;
    if (!Detour(addr::SC4RenderContext_DrawMesh, &TMesh, &g_mesh))
        return false;
    log::Write("models: mesh draw %s", g_compareMesh ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredMesh = registry::Register({
    "renderer.models.mesh",
    "renderer/models",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4RenderContext_DrawMesh,
    "The render context's mesh draw: the vertex buffer to the driver, then each primitive group; compare mode as "
    "renderer.terrain.land, per mesh (docs/renderer.md, \"Models and lots\")",
    true,
    InstallMesh,
});

bool InstallMaterialOverlaid(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareMaterialOverlaid = mode == registry::Mode::Compare;
    if (!Detour(addr::SC4RenderContext_SetMaterialOverlaid, &TMaterialOverlaid, &g_materialOverlaid))
        return false;
    log::Write("models: overlaid material setup %s", g_compareMaterialOverlaid ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredMaterialOverlaid = registry::Register({
    "renderer.models.material_overlaid",
    "renderer/models",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4RenderContext_SetMaterialOverlaid,
    "The material setup of the overlaid round; compare mode compares the driver state and the context "
    "(docs/renderer.md, \"Models and lots\")",
    true,
    InstallMaterialOverlaid,
});

bool InstallMaterial(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareMaterial = mode == registry::Mode::Compare;
    if (!Detour(addr::SC4RenderContext_SetMaterial, &TMaterial, &g_material))
        return false;
    log::Write("models: material setup %s", g_compareMaterial ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredMaterial = registry::Register({
    "renderer.models.material",
    "renderer/models",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4RenderContext_SetMaterial,
    "The render context's material setup: a render state from the material and the properties, then each "
    "texture stage; compare mode compares the driver state and the context (docs/renderer.md, \"Models and lots\")",
    true,
    InstallMaterial,
});

bool InstallGroundQuad(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareGroundQuad = mode == registry::Mode::Compare;
    if (!Detour(addr::SC4GroundQuad_Draw, &TGroundQuad, &g_groundQuad))
        return false;
    log::Write("models: ground quad draw %s", g_compareGroundQuad ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredGroundQuad = registry::Register({
    "renderer.models.groundquad",
    "renderer/models",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4GroundQuad_Draw,
    "The ground quad's draw, a textured quad plain, translucent or tinted; compare mode as "
    "renderer.models.static, per quad (docs/renderer.md, \"Models and lots\")",
    true,
    InstallGroundQuad,
});

bool InstallSideStrip(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_sideStrip.compare = mode == registry::Mode::Compare;
    if (!Detour(addr::SC4DrawableAA2EA8_Draw, &TSideStrip, &g_sideStrip.original))
        return false;
    log::Write("models: side strip draw %s", g_sideStrip.compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredSideStrip = registry::Register({
    "renderer.models.sidestrip",
    "renderer/models",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4DrawableAA2EA8_Draw,
    "The draw of the drawable with vtable 0x00AA2EA8, a strip of triangles shown from two rotations; compare mode "
    "as renderer.models.static, per object (docs/renderer.md, \"Models and lots\")",
    true,
    InstallSideStrip,
});

bool InstallRotationQuads(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_rotationQuads.compare = mode == registry::Mode::Compare;
    if (!Detour(addr::SC4DrawableAA2808_Draw, &TRotationQuads, &g_rotationQuads.original))
        return false;
    log::Write("models: rotation quads draw %s", g_rotationQuads.compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredRotationQuads = registry::Register({
    "renderer.models.rotationquads",
    "renderer/models",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4DrawableAA2808_Draw,
    "The draw of the drawable with vtable 0x00AA2808, quads picked by the rotation; compare mode as "
    "renderer.models.static, per object (docs/renderer.md, \"Models and lots\")",
    true,
    InstallRotationQuads,
});

bool InstallFlaggedQuad(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_flaggedQuad.compare = mode == registry::Mode::Compare;
    if (!Detour(addr::SC4DrawableAA7F10_Draw, &TFlaggedQuad, &g_flaggedQuad.original))
        return false;
    log::Write("models: flagged quad draw %s", g_flaggedQuad.compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredFlaggedQuad = registry::Register({
    "renderer.models.flaggedquad",
    "renderer/models",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4DrawableAA7F10_Draw,
    "The draw of the drawable with vtable 0x00AA7F10, a quad as one or two triangles and a list of triangles; "
    "compare mode as renderer.models.static, per object (docs/renderer.md, \"Models and lots\")",
    true,
    InstallFlaggedQuad,
});

bool InstallCellQuad(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareCellQuad = mode == registry::Mode::Compare;
    if (!Detour(addr::cSTETerrainView3D_DrawCellQuad, &TCellQuad, &g_cellQuad))
        return false;
    log::Write("terrain draw: cell quads %s", g_compareCellQuad ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredCellQuad = registry::Register({
    "renderer.terrain.cellquad",
    "renderer/terrain",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSTETerrainView3D_DrawCellQuad,
    "One cell of the land as a quad with a caller's texture coordinates and alpha (how quads are laid on the "
    "terrain); compare mode as renderer.terrain.land, per quad (docs/renderer.md, \"Models and lots\")",
    true,
    InstallCellQuad,
});

bool InstallCorners(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compareCorners = mode == registry::Mode::Compare;
    if (!Detour(addr::cS3DBillboard_UpdateCorners, &TCorners, &g_corners))
        return false;
    log::Write("models: billboard corners %s", g_compareCorners ? "compared with the game's" : "ours");
    return true;
}

const bool g_registeredCorners = registry::Register({
    "renderer.models.billboard_corners",
    "renderer/models",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cS3DBillboard_UpdateCorners,
    "The billboard's corners after a change of zoom or rotation; compare mode runs ours, puts the billboard back, "
    "runs the game's and compares the billboard's bytes (docs/renderer.md, \"Models and lots\")",
    true,
    InstallCorners,
});

} // namespace
} // namespace opensc4::terrain_draw
