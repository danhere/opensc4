// Our terrain view (cSTETerrainView3D, build 641), first part: which rows
// and columns of the terrain mesh are in view for the land and water passes,
// and the meshes' texture coordinates (docs/renderer.md, "The terrain").
// Each function is a detour on the game's; in compare mode ours runs first,
// the view object and the arrays it writes are put back, the game's runs,
// and the two results are compared bit for bit.
//
// The game computes on the x87 at 53-bit precision and stores floats; here
// register values are doubles and each store to memory is a float, at the
// same points and in the same order of operations as the game's code.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace opensc4::terrain_view {
namespace {

constexpr std::size_t kViewSize = 0xE8;

// One pass's fields in the view object and its arrays.
struct Pass {
    std::size_t first;          // +0x8C land, +0xBC water: first row, then end and last row
    std::size_t zLow, zHigh;    // the camera view's z range
    std::size_t edgeA, edgeADir, edgeB, edgeBDir; // two frustum edges (point, direction) in x, z
    std::size_t plane;          // the camera view's plane for the thresholds
    std::uintptr_t start, end, drawn, water; // the per-row arrays
};

constexpr Pass kLand{0x8C, 0x25C, 0x268, 0x3BC, 0x3C4, 0x3CC, 0x3D4, 0x334,
                     addr::STE_LandRowStart, addr::STE_LandRowEnd, addr::STE_LandRowDrawn, addr::STE_LandRowWater};
constexpr Pass kWater{0xBC, 0x98, 0xA4, 0x208, 0x210, 0x218, 0x220, 0x180,
                      addr::STE_WaterRowStart, addr::STE_WaterRowEnd, addr::STE_WaterRowDrawn, addr::STE_WaterRowWater};

float& F(char* p, std::size_t offset) { return At<float>(p, offset); }
int32_t& I(char* p, std::size_t offset) { return At<int32_t>(p, offset); }

int32_t* Ints(std::uintptr_t global) { return Global<int32_t*>(global); }
uint8_t* Bytes(std::uintptr_t global) { return Global<uint8_t*>(global); }
uint32_t Columns() { return Global<uint32_t>(addr::STE_Columns); }
uint32_t Rows() { return Global<uint32_t>(addr::STE_Rows); }

constexpr float kMagic = 12582912.0f; // 1.5 * 2^23
constexpr float kTile = 16.0f;

int32_t MagicInt(float rounded)
{
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof bits);
    return static_cast<int32_t>(bits + 0xB4C00000u);
}

// The game's floor and ceiling through the rounding trick.
int32_t Floor(double value)
{
    const int32_t n = MagicInt(static_cast<float>((value - 0.5f) + kMagic));
    return value == static_cast<double>(n + 1) ? n + 1 : n;
}

int32_t Ceil(double value)
{
    const int32_t n = MagicInt(static_cast<float>((0.5f + value) + kMagic));
    return value == static_cast<double>(n - 1) ? n - 1 : n;
}

// 0x005D7420: where the line `point` + t `direction` meets the line through
// `other` along `otherDirection`; out unchanged when they are nearly parallel.
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

// 0x00752E80 / 0x00753210: the rows from the camera view's z range and, per
// row, the columns between the two frustum edges, clamped and widened so
// that each row covers the next.
void RowSpans(char* self, char* context, const Pass& p)
{
    char* const camera = At<char*>(context, 0x24);
    const double perMetre = Global<float>(addr::STE_CellsPerMetre);
    int32_t& first = I(self, p.first);
    int32_t& end = I(self, p.first + 4);
    int32_t& last = I(self, p.first + 8);
    first = Floor(perMetre * F(camera, p.zLow));
    if (first < 0)
        first = 0;
    end = Ceil(perMetre * F(camera, p.zHigh)) + 1;
    if (static_cast<uint32_t>(end) > Rows())
        end = static_cast<int32_t>(Rows());
    last = end - 1;
    int32_t* const starts = Ints(p.start);
    int32_t* const ends = Ints(p.end);
    // The game's outputs are stack locals: when a line is parallel to the
    // row, the previous row's point stays (unset on the first row).
    float a[2] = {0.0f, 0.0f}, b[2] = {0.0f, 0.0f};
    const float along[2] = {1.0f, 0.0f};
    for (int32_t r = first; r < end; ++r) {
        const float point[2] = {0.0f, static_cast<float>(static_cast<double>(r) * kTile)};
        Intersect(point, along, &F(camera, p.edgeA), &F(camera, p.edgeADir), a);
        Intersect(point, along, &F(camera, p.edgeB), &F(camera, p.edgeBDir), b);
        const bool swap = !(a[0] < b[0]); // the game's test, NaN included
        const float lowX = swap ? b[0] : a[0];
        const float highX = swap ? a[0] : b[0];
        starts[r] = Floor(perMetre * lowX);
        ends[r] = Ceil(perMetre * highX) + 1;
    }
    const uint32_t columns = Columns();
    int32_t& lowest = I(self, p.first + 0x24);
    int32_t& highest = I(self, p.first + 0x28);
    lowest = static_cast<int32_t>(columns);
    highest = 0;
    for (int32_t r = first; r < end; ++r) {
        if (starts[r] < 0)
            starts[r] = 0;
        else if (static_cast<uint32_t>(starts[r]) > columns)
            starts[r] = static_cast<int32_t>(columns);
        if (static_cast<uint32_t>(ends[r]) > columns)
            ends[r] = static_cast<int32_t>(columns);
        else if (ends[r] < 0)
            ends[r] = 0;
        if (starts[r] < lowest)
            lowest = starts[r];
        if (ends[r] > highest)
            highest = ends[r];
    }
    for (int32_t r = first + 1; r < end; ++r) {
        if (starts[r - 1] > starts[r])
            starts[r - 1] = starts[r];
        if (ends[r - 1] < ends[r])
            ends[r - 1] = ends[r];
    }
}

// 0x00751EE0 / 0x00751FF0: per row, the lowest height the camera sees along
// it (the camera view's plane through the row's first and last columns).
void RowThresholds(char* self, char* context, const Pass& p, float* out)
{
    char* const camera = At<char*>(context, 0x24);
    const float k = static_cast<float>(-1.0f / static_cast<double>(F(camera, p.plane + 4)));
    const double a = F(camera, p.plane), c = F(camera, p.plane + 8), d = F(camera, p.plane + 12);
    const int32_t* const starts = Ints(p.start);
    const int32_t* const ends = Ints(p.end);
    for (int32_t r = I(self, p.first); r < I(self, p.first + 4); ++r) {
        At<uint32_t>(out, 4 * r) = 0x7F7FFFFF;
        if (starts[r] >= ends[r])
            continue;
        const double rowTerm = static_cast<double>(r) * kTile * c;
        const double atStart = ((static_cast<double>(starts[r]) * kTile * a + rowTerm) + d) * k;
        const float atStartF = static_cast<float>(atStart);
        out[r] = atStartF;
        if (ends[r] > 0) {
            const double atEnd = (rowTerm + static_cast<double>(ends[r] - 1) * kTile * a + d) * k;
            if (atEnd < atStartF)
                out[r] = static_cast<float>(atEnd);
        }
    }
}

// 0x007535A0 / 0x007539D0: which rows have land or water above their
// threshold, and the row ranges for drawing them.
void RowVisibility(char* self, char* context, const Pass& p)
{
    float thresholds[257];
    RowThresholds(self, context, p, thresholds);
    int32_t& first = I(self, p.first);
    int32_t& end = I(self, p.first + 4);
    int32_t& last = I(self, p.first + 8);
    int32_t& landFirst = I(self, p.first + 0xC);
    int32_t& landEnd = I(self, p.first + 0x10);
    int32_t& landLast = I(self, p.first + 0x14);
    int32_t& waterFirst = I(self, p.first + 0x18);
    int32_t& waterEnd = I(self, p.first + 0x1C);
    int32_t& waterLast = I(self, p.first + 0x20);
    int32_t anyFirst = end;
    int32_t anyLast = last;
    bool seenAny = false, seenLand = false, seenWater = false;
    landFirst = landEnd = waterFirst = waterEnd = end;
    const int32_t* const starts = Ints(p.start);
    const int32_t* const ends = Ints(p.end);
    uint8_t* const drawn = Bytes(p.drawn);
    uint8_t* const water = Bytes(p.water);
    const char* const land = Global<char*>(addr::STE_LandVertices);
    const char* const sea = Global<char*>(addr::STE_WaterVertices);
    const uint32_t columns = Columns();
    for (int32_t r = first; r < end; ++r) {
        if (starts[r] >= ends[r]) {
            drawn[r] = 0;
            water[r] = 0;
            continue;
        }
        float highest = -3.40282347e+38f, lowest = 3.40282347e+38f, waterHighest = -3.40282347e+38f;
        for (int32_t c = starts[r]; c < ends[r]; ++c) {
            const std::size_t at = (static_cast<std::size_t>(columns) * r + c) * 32 + 4;
            const float y = *reinterpret_cast<const float*>(land + at);
            const float w = *reinterpret_cast<const float*>(sea + at);
            if (highest < y)
                highest = y;
            if (y < lowest)
                lowest = y;
            if (w > waterHighest)
                waterHighest = w;
        }
        drawn[r] = !(highest < thresholds[r]);
        water[r] = !(waterHighest < thresholds[r]) && waterHighest > lowest;
        if (!drawn[r] && !water[r])
            continue;
        const int32_t before = r <= 0 ? 0 : r - 1;
        if (!seenAny) {
            seenAny = true;
            anyFirst = before;
        }
        if (drawn[r]) {
            if (!seenLand) {
                seenLand = true;
                landFirst = before;
            }
            landEnd = r + 2;
        }
        if (water[r]) {
            if (!seenWater) {
                seenWater = true;
                waterFirst = before;
            }
            waterEnd = r + 2;
        }
        if (r < last)
            anyLast = r + 1;
    }
    if (first < anyFirst)
        first = anyFirst;
    if (anyLast < last)
        last = anyLast;
    end = last + 1;
    if (landFirst < first)
        landFirst = first;
    if (end < landEnd)
        landEnd = end;
    if (waterFirst < first)
        waterFirst = first;
    if (end < waterEnd)
        waterEnd = end;
    landLast = landEnd - 1;
    waterLast = waterEnd - 1;
}

// 0x00756750
void UpdateLandRows(char* self, char* context)
{
    RowSpans(self, context, kLand);
    using PairFn = void(__thiscall*)(void* camera, int32_t* a, int32_t* b);
    int32_t* const a = &I(self, 0x5C);
    int32_t* const b = &I(self, 0x60);
    reinterpret_cast<PairFn>(addr::SC4CameraView_RotationPair)(At<void*>(context, 0x24), a, b);
    *a = (6 - *a) % 4;
    *b = (6 - *b) % 4;
    RowVisibility(self, context, kLand);
}

// 0x00751C80: texture coordinates of every land and water vertex, planar in
// x and z, turned with the camera, when the zoom or rotation changed.
void UpdateTextureCoordinates(char* context)
{
    int32_t& lastZoom = Global<int32_t>(addr::STE_TextureZoom);
    int32_t& lastRotation = Global<int32_t>(addr::STE_TextureRotation);
    const int32_t zoom = I(context, 0x14);
    if (lastZoom == zoom && lastRotation == I(context, 0x18))
        return;
    const float* const zoomScales = &Global<float>(addr::STE_TextureZoomScale);
    const float k = static_cast<float>(static_cast<double>(Global<float>(addr::STE_TextureScale)) *
                                       zoomScales[Global<int32_t>(addr::STE_Zoom)]);
    lastZoom = zoom;
    lastRotation = I(context, 0x18);
    const uint32_t rotation = static_cast<uint32_t>(lastRotation);
    if (rotation > 3)
        return;
    const double width = Global<float>(addr::STE_CityWidth);
    const double depth = Global<float>(addr::STE_CityDepth);
    const uint32_t count = Global<uint32_t>(addr::STE_VertexCount);
    for (uint32_t i = 0; i < count; ++i) {
        for (const std::uintptr_t array : {addr::STE_LandVertices, addr::STE_WaterVertices}) {
            char* const v = Global<char*>(array) + static_cast<std::size_t>(i) * 32;
            const double x = F(v, 0), z = F(v, 8);
            float u, w;
            switch (rotation) {
            case 0: u = static_cast<float>(k * x), w = static_cast<float>(k * z); break;
            case 1: u = static_cast<float>(k * z), w = static_cast<float>((width - x) * k); break;
            case 2: u = static_cast<float>((width - x) * k), w = static_cast<float>((depth - z) * k); break;
            default: u = static_cast<float>((depth - z) * k), w = static_cast<float>(k * x); break;
            }
            F(v, 0x10) = u;
            F(v, 0x14) = w;
        }
    }
}

// --- Compare mode ----------------------------------------------------------------------

bool g_compare = false;
int g_depth = 0;

// Memory a function writes: copied before, after ours and after the game's.
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

template <typename Ours, typename Original>
void Compared(const char* id, const std::vector<Region>& regions, Ours&& ours, Original&& original)
{
    std::vector<std::vector<char>> before, mine;
    for (const Region& r : regions)
        before.emplace_back(r.data, r.data + r.size);
    ++g_depth;
    ours();
    for (const Region& r : regions)
        mine.emplace_back(r.data, r.data + r.size);
    for (std::size_t i = 0; i < regions.size(); ++i)
        std::memcpy(regions[i].data, before[i].data(), regions[i].size);
    original();
    --g_depth;
    for (std::size_t i = 0; i < regions.size(); ++i) {
        const Region& r = regions[i];
        if (std::memcmp(r.data, mine[i].data(), r.size) == 0)
            continue;
        std::size_t at = 0;
        while (r.data[at] == mine[i][at])
            ++at;
        at &= ~std::size_t{3};
        char where[48];
        std::snprintf(where, sizeof where, "%s+%zX ", r.name, at);
        const std::size_t n = r.size - at < 8 ? r.size - at : 8;
        compare::Record(id, false, where + Hex(r.data + at, n), where + Hex(mine[i].data() + at, n));
        return;
    }
    compare::Record(id, true, std::string(), std::string());
}

std::vector<Region> RowRegions(char* self, const Pass& p)
{
    const std::size_t rows = Rows();
    return {{"view", self, kViewSize},
            {"start", reinterpret_cast<char*>(Ints(p.start)), rows * 4},
            {"end", reinterpret_cast<char*>(Ints(p.end)), rows * 4},
            {"drawn", reinterpret_cast<char*>(Bytes(p.drawn)), rows},
            {"water", reinterpret_cast<char*>(Bytes(p.water)), rows}};
}

using RowsFn = void(__fastcall*)(char*, void*, char*);
using CoordinatesFn = void(__stdcall*)(char*);
RowsFn g_landRows, g_waterSpans, g_waterVisibility;
CoordinatesFn g_coordinates;

void __fastcall DLandRows(char* self, void*, char* context)
{
    if (g_depth > 0)
        return g_landRows(self, nullptr, context);
    if (!g_compare)
        return UpdateLandRows(self, context);
    Compared("terrain.landrows", RowRegions(self, kLand), [&] { UpdateLandRows(self, context); },
             [&] { g_landRows(self, nullptr, context); });
}

void __fastcall DWaterSpans(char* self, void*, char* context)
{
    if (g_depth > 0)
        return g_waterSpans(self, nullptr, context);
    if (!g_compare)
        return RowSpans(self, context, kWater);
    Compared("terrain.waterspans", RowRegions(self, kWater), [&] { RowSpans(self, context, kWater); },
             [&] { g_waterSpans(self, nullptr, context); });
}

void __fastcall DWaterVisibility(char* self, void*, char* context)
{
    if (g_depth > 0)
        return g_waterVisibility(self, nullptr, context);
    if (!g_compare)
        return RowVisibility(self, context, kWater);
    Compared("terrain.watervisibility", RowRegions(self, kWater), [&] { RowVisibility(self, context, kWater); },
             [&] { g_waterVisibility(self, nullptr, context); });
}

void __fastcall DCoordinates(void*, void*, char* context)
{
    if (g_depth > 0)
        return g_coordinates(context);
    if (!g_compare)
        return UpdateTextureCoordinates(context);
    const std::size_t size = static_cast<std::size_t>(Global<uint32_t>(addr::STE_VertexCount)) * 32;
    Compared("terrain.texturecoordinates",
             {{"land", Global<char*>(addr::STE_LandVertices), size},
              {"water", Global<char*>(addr::STE_WaterVertices), size},
              {"zoom", reinterpret_cast<char*>(&Global<int32_t>(addr::STE_TextureZoom)), 4},
              {"rotation", reinterpret_cast<char*>(&Global<int32_t>(addr::STE_TextureRotation)), 4}},
             [&] { UpdateTextureCoordinates(context); }, [&] { g_coordinates(context); });
}

OPENSC4_THUNK(TLandRows, DLandRows, 4)
OPENSC4_THUNK(TWaterSpans, DWaterSpans, 4)
OPENSC4_THUNK(TWaterVisibility, DWaterVisibility, 4)
OPENSC4_THUNK(TCoordinates, DCoordinates, 4)

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
    const bool ok = Detour(addr::cSTETerrainView3D_UpdateLandRows, &TLandRows, &g_landRows) &&
                    Detour(addr::cSTETerrainView3D_WaterRowSpans, &TWaterSpans, &g_waterSpans) &&
                    Detour(addr::cSTETerrainView3D_WaterRowVisibility, &TWaterVisibility, &g_waterVisibility) &&
                    Detour(addr::cSTETerrainView3D_UpdateTextureCoordinates, &TCoordinates, &g_coordinates);
    if (ok)
        log::Write("terrain view: rows and texture coordinates %s", g_compare ? "compared with the game's" : "ours");
    return ok;
}

const bool g_registered = registry::Register({
    "renderer.terrain.rows",
    "renderer/terrain",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSTETerrainView3D_UpdateLandRows,
    "Our terrain view's rows in view (land and water passes) and texture coordinates (docs/renderer.md, \"The "
    "terrain\"); compare mode runs both on every call",
    true,
    Install,
});

} // namespace
} // namespace opensc4::terrain_view
