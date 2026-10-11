// Our cSTETerrain queries (build 641): the slots of the city's terrain object
// (vtable 0x00AB3D18, the service 0x00B43CF4) that only read it: sizes, index
// conversions, altitudes, water levels and depths, normals, the sea and water
// tests, the hidden-triangle tests, the cell flags and the brush table
// getters. Every function is a replacement on the original object (54 slots
// of the primary vtable; the fields are in terrain_sim.h). No code outside
// the class calls any of the class's own query functions directly, and no
// other vtable holds them (scan of every direct call and of every data
// pointer in the exe), so there are no detours; the slots that are 4-byte
// getters shared with other classes through code folding (5, 7, 8, 29, 42,
// 54, 83) are swapped in this vtable only.
//
// These are called tens of thousands of times a game day, so nothing here
// logs per call; the group logs once when it installs.
//
// Floating point. The game computes on the x87 at 53-bit precision; our code
// is compiled for SSE. So every value the game keeps on the FPU stack is a
// double here, and a float exactly where the game stores one to memory
// (FSTP dword) and loads it again; operations are in the game's order. A
// result the game leaves in ST0 unrounded is returned as a double (the
// caller's ST0 then holds the same value); one it loads from a float is
// returned as a float. The constants in .rdata are literals (0.0, 0.5, 16.0,
// 256.0, 65536.0, 32.0, 12582912.0, 4294967296.0); the ones in .data are read
// from the game's memory at each use, as the game does (1/16 at 0x00B0D688,
// 1.0 at 0x00B0D684, the allowed heights, the raise/lower amount).
//
// Range tests are the game's and no more: where it reads outside the grid
// (GetAltitudes, GetWaterDepths and NumberOfWaterVertices for a rectangle
// outside it; GetLerpedNormal and GetLerpedWaterLevel on the far edges;
// GetFaceNormal for a NaN; GetBrushExemplarID past its table), so do we, with
// the same 32-bit wrapping address arithmetic.
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace opensc4::terrain_sim {
namespace {

// 16.0, the float at 0x00AB3B5C (.rdata): the width of a cell.
constexpr double kCellWidth = 16.0;

inline float FloatAt(uint32_t address) { return AtAddress<float>(address); }

inline uint32_t U32(void* self, std::size_t offset) { return At<uint32_t>(self, offset); }

// Slot 11 through the object's vtable, as the game's functions call it.
inline bool InBounds(char* self, float x, float z)
{
    return Virtual<uint8_t>(self, slot::kLocationIsInBounds, x, z) != 0;
}

// __ftol2 (0x009EEF04): truncation toward zero to 64 bits; the x87 "integer
// indefinite" for what does not fit and for a NaN.
int64_t Ftol64(double value)
{
    if (!(value > -9.2233720368547758e18 && value < 9.2233720368547758e18))
        return INT64_MIN;
    return static_cast<int64_t>(value);
}

// The walk over a rectangle of records that GetAltitudeCheckSum (0x007414A0)
// and NumberOfWaterVertices(rect) (0x00744B40) share: rows z0 to z1 (the
// caller has tested z0 <= z1), in each the records x0 to x1 in ascending
// order: first in groups of four when there are at least four, then one by
// one. Indices are 32-bit and wrap as the game's do.
template <typename Visit>
void WalkRows(uint32_t stride, const int32_t* rect, Visit visit)
{
    const uint32_t x0 = static_cast<uint32_t>(rect[0]), z0 = static_cast<uint32_t>(rect[1]);
    const uint32_t x1 = static_cast<uint32_t>(rect[2]), z1 = static_cast<uint32_t>(rect[3]);
    uint32_t start = x0 + stride * z0;
    const uint32_t span = x1 - x0;
    uint32_t rows = z1 - z0 + 1u;
    do {
        const uint32_t last = span + start;
        uint32_t current = start;
        if (static_cast<int32_t>(last - start + 1u) >= 4) {
            uint32_t groups = ((span - 3u) >> 2) + 1u;
            current = start + groups * 4u;
            uint32_t index = start;
            do {
                visit(index);
                visit(index + 1u);
                visit(index + 2u);
                visit(index + 3u);
                index += 4u;
            } while (--groups);
        }
        if (static_cast<int32_t>(current) <= static_cast<int32_t>(last)) {
            uint32_t left = last - current + 1u;
            do {
                visit(current);
                ++current;
            } while (--left);
        }
        start += stride;
    } while (--rows);
}

// What GetAltitudes (0x00742A00) and GetWaterDepths (0x007443C0) share: the
// first dword of each record of a rectangle, copied to out[row * rowStep +
// column * columnStep]. No range test; the record array is read anew for each
// element, the row length (+0x38) for each row.
template <typename Base>
void CopyRect(char* self, const int32_t* rect, uint32_t out, int32_t rowStep, int32_t columnStep, Base base)
{
    uint32_t index = U32(self, field::kVertsX) * static_cast<uint32_t>(rect[1]) + static_cast<uint32_t>(rect[0]);
    const int32_t dx = static_cast<int32_t>(static_cast<uint32_t>(rect[2]) - static_cast<uint32_t>(rect[0]));
    int32_t dz = static_cast<int32_t>(static_cast<uint32_t>(rect[3]) - static_cast<uint32_t>(rect[1]));
    if (dx < 0 || dz < 0)
        return;
    const uint32_t rowBytes = static_cast<uint32_t>(rowStep) << 2;
    const uint32_t columnBytes = static_cast<uint32_t>(columnStep) << 2;
    for (;;) {
        uint32_t to = out;
        for (uint32_t column = 0; column <= static_cast<uint32_t>(dx); ++column) {
            AtAddress<uint32_t>(to) = AtAddress<uint32_t>(RecordAddress(base(), index + column));
            to += columnBytes;
        }
        out += rowBytes;
        index += U32(self, field::kVertsX);
        if (dz-- == 0)
            break;
    }
}

// --- Sizes and conversions ---------------------------------------------------

// Slot 5 (0x005B9D30, shared): the byte at +0xC.
uint8_t __fastcall IsInitialized(char* self, void*) { return At<uint8_t>(self, field::kInitialized); }

// Slot 6 (0x0074C5C0): 16.0 in ST0.
float __fastcall CellWidth(char*, void*) { return 16.0f; }

// Slots 7 and 8 (0x0060FB80, 0x004ABAD0, shared).
uint32_t __fastcall CellCountX(char* self, void*) { return U32(self, field::kCellsX); }
uint32_t __fastcall CellCountZ(char* self, void*) { return U32(self, field::kCellsZ); }

// Slot 9 (0x0074C5D0): no range test.
uint32_t __fastcall GetVertIndex(char* self, void*, uint32_t x, uint32_t z)
{
    return U32(self, field::kVertsX) * z + x;
}

// Slot 10 (0x0074C5E0): z (the quotient) is written first. The game leaves
// the x pointer in EAX.
uint32_t* __fastcall GetVertex(char* self, void*, uint32_t index, uint32_t* x, uint32_t* z)
{
    *z = index / U32(self, field::kVertsX);
    *x = index % U32(self, field::kVertsX);
    return x;
}

// Slot 11 (0x0074C610): both ends inside; false for a NaN.
uint32_t __fastcall LocationIsInBounds(char* self, void*, float x, float z)
{
    if (!(x >= 0.0f))
        return 0;
    if (!(x <= At<float>(self, field::kSizeX)))
        return 0;
    if (!(z >= 0.0f))
        return 0;
    if (!(z <= At<float>(self, field::kSizeZ)))
        return 0;
    return 1;
}

// Slot 25 (0x0074C6B0): z is converted first; truncation, no range test.
uint32_t __fastcall GetTerrainGridIndex(char* self, void*, float x, float z)
{
    const uint32_t row =
        static_cast<uint32_t>(Ftol64(static_cast<double>(InverseCellWidth()) * static_cast<double>(z)));
    const uint32_t column =
        static_cast<uint32_t>(Ftol64(static_cast<double>(InverseCellWidth()) * static_cast<double>(x)));
    return column + row * U32(self, field::kVertsX);
}

// --- Altitudes ---------------------------------------------------------------

// Slot 12 (0x007413C0).
float __fastcall GetAltitudeAtIndex(char* self, void*, uint32_t index)
{
    if (index >= U32(self, field::kVertexCount))
        return 0.0f;
    return FloatAt(RecordAddress(VertexBase(self), index));
}

// Slot 13 (0x00741260): the height at a position, on the plane of the
// triangle it is in. All in double precision; ST0 is not rounded.
double __fastcall GetAltitude(char* self, void*, float x, float z)
{
    if (!InBounds(self, x, z))
        return 0.0;
    uint32_t cx = FloorCell(static_cast<double>(InverseCellWidth()) * static_cast<double>(x));
    uint32_t cz = FloorCell(static_cast<double>(InverseCellWidth()) * static_cast<double>(z));
    if (cx > U32(self, field::kLastCellX))
        cx = U32(self, field::kLastCellX);
    if (cz > U32(self, field::kLastCellZ))
        cz = U32(self, field::kLastCellZ);
    const uint32_t stride = U32(self, field::kVertsX);
    const uint32_t base = VertexBase(self);
    const uint32_t nearRow = RecordAddress(base, stride * cz + cx);
    const double a00 = FloatAt(nearRow);
    const double fx = static_cast<double>(x) - static_cast<double>(cx) * kCellWidth;
    const double fz = static_cast<double>(z) - static_cast<double>(cz) * kCellWidth;
    const uint32_t farRow = RecordAddress(base, (cz + 1u) * stride + cx);
    const double a11 = FloatAt(farRow + vertex::kSize);
    double sum;
    if (fx <= fz) {
        const double a01 = FloatAt(farRow);
        const double first = (a11 - a01) * fx;
        const double second = (a01 - a00) * fz;
        sum = first + second;
    } else { // also when unordered
        const double a10 = FloatAt(nearRow + vertex::kSize);
        const double first = (a11 - a10) * fz;
        const double second = (a10 - a00) * fx;
        sum = first + second;
    }
    const double scaled = sum * static_cast<double>(InverseCellWidth());
    return a00 + scaled;
}

// Slot 14 (0x007413E0).
float __fastcall GetAltitudeAtVertex(char* self, void*, uint32_t x, uint32_t z)
{
    const uint32_t stride = U32(self, field::kVertsX);
    if (x >= stride || z >= U32(self, field::kVertsZ))
        return 0.0f;
    return FloatAt(RecordAddress(VertexBase(self), stride * z + x));
}

// Slot 15 (0x00742A00).
void __fastcall GetAltitudes(char* self, void*, const int32_t* rect, uint32_t out, int32_t rowStep,
                             int32_t columnStep)
{
    CopyRect(self, rect, out, rowStep, columnStep, [self] { return VertexBase(self); });
}

// Slot 16 (0x007443C0).
void __fastcall GetWaterDepths(char* self, void*, const int32_t* rect, uint32_t out, int32_t rowStep,
                               int32_t columnStep)
{
    CopyRect(self, rect, out, rowStep, columnStep, [self] { return WaterBase(self); });
}

// Slot 17 (0x007415F0): (x, z), (x, z+1), (x+1, z+1), (x+1, z); the fields
// are read anew for each corner.
void __fastcall GetCellCornerAltitudes(char* self, void*, uint32_t x, uint32_t z, uint32_t* out)
{
    if (x >= U32(self, field::kCellsX) || z >= U32(self, field::kCellsZ))
        return;
    const uint32_t index = U32(self, field::kVertsX) * z + x;
    out[0] = AtAddress<uint32_t>(RecordAddress(VertexBase(self), index));
    out[1] = AtAddress<uint32_t>(RecordAddress(VertexBase(self), U32(self, field::kVertsX) + index));
    out[2] = AtAddress<uint32_t>(RecordAddress(VertexBase(self), U32(self, field::kVertsX) + index + 1u));
    out[3] = AtAddress<uint32_t>(RecordAddress(VertexBase(self), index) + vertex::kSize);
}

// Slot 18 (0x00744620): the highest altitude + depth of a cell's corners (a
// negative depth counts as 0; a NaN depth is added as it is). The first
// corner's sum stays as computed (double); a later one that is greater
// replaces it rounded to float, as the game stores and reloads it.
double __fastcall GetMaxSurfaceLevel(char* self, void*, uint32_t x, uint32_t z)
{
    if (x >= U32(self, field::kCellsX) || z >= U32(self, field::kCellsZ))
        return 0.0;
    const uint32_t stride = U32(self, field::kVertsX);
    const uint32_t index = stride * z + x;
    const uint32_t depths = WaterBase(self);
    const uint32_t altitudes = VertexBase(self);
    const auto Level = [depths, altitudes](uint32_t at) {
        float depth = FloatAt(RecordAddress(depths, at));
        if (depth < 0.0f)
            depth = 0.0f;
        return static_cast<double>(FloatAt(RecordAddress(altitudes, at))) + static_cast<double>(depth);
    };
    double best = Level(index);
    for (const uint32_t at : {index + stride, index + stride + 1u, index + 1u}) {
        const double level = Level(at);
        const float stored = static_cast<float>(level);
        if (level > best)
            best = stored;
    }
    return best;
}

// Slot 19 (0x00741420): the vertex nearest to a position. The game adds
// 12582912.0, stores a float and uses its bits; the constants cancel in the
// 32-bit address arithmetic, kept here as it is.
float __fastcall GetAltitudeAtNearestGrid(char* self, void*, float x, float z)
{
    if (!InBounds(self, x, z))
        return 0.0f;
    const float storedX =
        static_cast<float>(static_cast<double>(InverseCellWidth()) * static_cast<double>(x) + 12582912.0);
    const uint32_t column = Bits(storedX);
    const float storedZ =
        static_cast<float>(static_cast<double>(InverseCellWidth()) * static_cast<double>(z) + 12582912.0);
    const uint32_t row = Bits(storedZ) + 0xB4C00000u;
    const uint32_t index = row * U32(self, field::kVertsX) + column;
    return FloatAt(VertexBase(self) + index * 9u * 4u + 0x6B000000u);
}

// Slot 20 (0x007414A0): the sum stays on the FPU stack (double) through the
// whole walk and is returned unrounded.
double __fastcall GetAltitudeCheckSum(char* self, void*, const int32_t* rect)
{
    const uint32_t stride = U32(self, field::kVertsX);
    if (static_cast<uint32_t>(rect[0]) >= stride)
        return 0.0;
    const uint32_t depth = U32(self, field::kVertsZ);
    if (static_cast<uint32_t>(rect[1]) >= depth || static_cast<uint32_t>(rect[2]) >= stride ||
        static_cast<uint32_t>(rect[3]) >= depth)
        return 0.0;
    double sum = 0.0;
    if (rect[1] > rect[3])
        return sum;
    WalkRows(U32(self, field::kVertsX), rect,
             [self, &sum](uint32_t index) { sum += static_cast<double>(FloatAt(RecordAddress(VertexBase(self), index))); });
    return sum;
}

// Slots 21 to 24 (0x0074C670, 0x0074C680, 0x0074C690, 0x0074C6A0).
float __fastcall MinHeight(char* self, void*) { return At<float>(self, field::kMinHeight); }
float __fastcall MaxHeight(char* self, void*) { return At<float>(self, field::kMaxHeight); }
float __fastcall MinAllowedHeight(char*, void*) { return Global<float>(addr::cSTETerrain_MinAllowedHeightValue); }
float __fastcall MaxAllowedHeight(char*, void*) { return Global<float>(addr::cSTETerrain_MaxAllowedHeightValue); }

// Slot 47 (0x0074C660).
float __fastcall GetDefaultTerrainRaiseLowerAmount(char*, void*)
{
    return Global<float>(addr::cSTETerrain_DefaultRaiseLowerAmount);
}

// --- Normals -----------------------------------------------------------------

// Slot 26 (0x00742B10): the normal of the triangle a position is in. Returns
// with nothing written when x < 0, x >= the width, z < 0 or z >= the depth; a
// NaN passes all four tests, as in the game. The cell is not clamped.
void __fastcall GetFaceNormal(char* self, void*, float x, float z, float* out)
{
    if (x < 0.0f)
        return;
    if (x >= At<float>(self, field::kSizeX))
        return;
    if (z < 0.0f)
        return;
    if (z >= At<float>(self, field::kSizeZ))
        return;
    const uint32_t cx = FloorCell(static_cast<double>(InverseCellWidth()) * static_cast<double>(x));
    const uint32_t cz = FloorCell(static_cast<double>(InverseCellWidth()) * static_cast<double>(z));
    const uint32_t stride = U32(self, field::kVertsX);
    const uint32_t base = VertexBase(self);
    const uint32_t nearRow = RecordAddress(base, stride * cz + cx);
    const uint32_t farRow = RecordAddress(base, (cz + 1u) * stride + cx);
    const double a00 = FloatAt(nearRow);
    const float diagonal = static_cast<float>(static_cast<double>(FloatAt(farRow + vertex::kSize)) - a00);
    const double fx = static_cast<double>(x) - static_cast<double>(cx) * kCellWidth;
    const double fz = static_cast<double>(z) - static_cast<double>(cz) * kCellWidth;
    float first;
    double third;
    if (fz >= fx) {
        const double edge = (static_cast<double>(FloatAt(farRow)) - a00) * kCellWidth;
        const double scaled = static_cast<double>(diagonal) * kCellWidth;
        first = static_cast<float>(edge - scaled);
        const double nothing = static_cast<double>(diagonal) * 0.0;
        third = nothing - edge;
    } else { // also when unordered
        const double edge = (static_cast<double>(FloatAt(nearRow + vertex::kSize)) - a00) * kCellWidth;
        const double nothing = static_cast<double>(diagonal) * 0.0;
        first = static_cast<float>(nothing - edge);
        const double scaled = static_cast<double>(diagonal) * kCellWidth;
        third = edge - scaled;
    }
    out[1] = 256.0f;
    out[0] = first;
    out[2] = static_cast<float>(third);
    const double c = out[2];
    const double a = out[0];
    const double aa = a * a;
    const double cc = c * c;
    const double squares = aa + cc;
    const double length = std::sqrt(squares + 65536.0);
    const double scale = static_cast<double>(Global<float>(addr::cSTETerrain_One)) / length;
    out[0] = static_cast<float>(scale * static_cast<double>(out[0]));
    out[1] = static_cast<float>(256.0 * scale);
    out[2] = static_cast<float>(scale * static_cast<double>(out[2]));
}

// Slot 27 (0x00742CF0): the corners' normals interpolated along x on both
// edges, then along z, then made unit length. The mix of float and double
// steps is the game's: each `float` below is a value it stores to its frame
// and loads again. The z coordinate is rounded to float before its floor; x
// is not. The cell is not clamped (on the far edges this reads the records
// after the row, or after the array).
void __fastcall GetLerpedNormal(char* self, void*, float x, float z, float* out)
{
    if (!InBounds(self, x, z))
        return;
    const double tx = static_cast<double>(InverseCellWidth()) * static_cast<double>(x);
    const float tz = static_cast<float>(static_cast<double>(InverseCellWidth()) * static_cast<double>(z));
    const uint32_t cx = FloorCell(tx);
    const uint32_t cz = FloorCell(static_cast<double>(tz));
    const double u = tx - static_cast<double>(cx);
    const double v = static_cast<double>(tz) - static_cast<double>(cz);
    const uint32_t stride = U32(self, field::kVertsX);
    const uint32_t index = stride * cz + cx;
    const uint32_t base = VertexBase(self);
    const uint32_t p0 = RecordAddress(base, index);
    const uint32_t p1 = RecordAddress(base, index + stride);
    const float n0x = FloatAt(p0 + 0x4), n0y = FloatAt(p0 + 0x8), n0z = FloatAt(p0 + 0xC);
    const float n10x = FloatAt(p0 + 0x28), n10y = FloatAt(p0 + 0x2C), n10z = FloatAt(p0 + 0x30);
    const float n01x = FloatAt(p1 + 0x4), n01y = FloatAt(p1 + 0x8), n01z = FloatAt(p1 + 0xC);
    const float n11x = FloatAt(p1 + 0x28), n11y = FloatAt(p1 + 0x2C), n11z = FloatAt(p1 + 0x30);

    // The near edge: n0 towards n10 by u.
    const float dx10 = static_cast<float>(static_cast<double>(n10x) - static_cast<double>(n0x));
    const double dy10 = static_cast<double>(n10y) - static_cast<double>(n0y);
    const double dz10 = static_cast<double>(n10z) - static_cast<double>(n0z);
    const float t1 = static_cast<float>(static_cast<double>(dx10) * u);
    const float t2 = static_cast<float>(dy10 * u);
    const float t3 = static_cast<float>(dz10 * u);
    const float ax = static_cast<float>(static_cast<double>(t1) + static_cast<double>(n0x));
    const float ay = static_cast<float>(static_cast<double>(t2) + static_cast<double>(n0y));
    const float az = static_cast<float>(static_cast<double>(t3) + static_cast<double>(n0z));

    // The far edge: n01 towards n11 by u.
    const double ex = static_cast<double>(n11x) - static_cast<double>(n01x);
    const double ey = static_cast<double>(n11y) - static_cast<double>(n01y);
    const float ez = static_cast<float>(static_cast<double>(n11z) - static_cast<double>(n01z));
    const float s1 = static_cast<float>(ex * u);
    const double eyu = ey * u;
    const double ezu = static_cast<double>(ez) * u;
    const float bx = static_cast<float>(static_cast<double>(s1) + static_cast<double>(n01x));
    const float by = static_cast<float>(eyu + static_cast<double>(n01y));
    const double bz = ezu + static_cast<double>(n01z);

    // Between the edges by v.
    const double dxx = static_cast<double>(bx) - static_cast<double>(ax);
    const float dyy = static_cast<float>(static_cast<double>(by) - static_cast<double>(ay));
    const float dzz = static_cast<float>(bz - static_cast<double>(az));
    const float px = static_cast<float>(dxx * v);
    const float py = static_cast<float>(static_cast<double>(dyy) * v);
    const double pz = static_cast<double>(dzz) * v;
    const double rx = static_cast<double>(px) + static_cast<double>(ax);
    const float ry = static_cast<float>(static_cast<double>(py) + static_cast<double>(ay));
    const double rzWide = pz + static_cast<double>(az);
    const float rz = static_cast<float>(rzWide);

    const double zz = rzWide * static_cast<double>(rz);
    const double yy = static_cast<double>(ry) * static_cast<double>(ry);
    const double zzyy = zz + yy;
    const double xx = rx * rx;
    const double squares = zzyy + xx;
    const float scale = static_cast<float>(static_cast<double>(Global<float>(addr::cSTETerrain_One)) / std::sqrt(squares));
    const double ox = rx * static_cast<double>(scale);
    const double oy = static_cast<double>(ry) * static_cast<double>(scale);
    const double oz = static_cast<double>(rz) * static_cast<double>(scale);
    out[0] = static_cast<float>(ox);
    out[1] = static_cast<float>(oy);
    out[2] = static_cast<float>(oz);
}

// Slot 28 (0x007417D0).
void __fastcall GetVertexNormal(char* self, void*, uint32_t x, uint32_t z, uint32_t* out)
{
    const uint32_t stride = U32(self, field::kVertsX);
    if (x >= stride || z >= U32(self, field::kVertsZ))
        return;
    const uint32_t normal = RecordAddress(VertexBase(self), stride * z + x) + vertex::kNormal;
    out[0] = AtAddress<uint32_t>(normal);
    out[1] = AtAddress<uint32_t>(normal + 4);
    out[2] = AtAddress<uint32_t>(normal + 8);
}

// --- Sea and water -----------------------------------------------------------

// Slot 29 (0x00741760, shared).
float __fastcall GetSeaLevel(char* self, void*) { return At<float>(self, field::kSeaLevel); }

// Slot 30 (0x007449B0): depth + altitude of the vertex at the floor of the
// position, unrounded.
double __fastcall GetWaterLevel(char* self, void*, float x, float z)
{
    if (!InBounds(self, x, z))
        return 0.0;
    const uint32_t cx = FloorCell(static_cast<double>(InverseCellWidth()) * static_cast<double>(x));
    const uint32_t cz = FloorCell(static_cast<double>(InverseCellWidth()) * static_cast<double>(z));
    const uint32_t index = U32(self, field::kVertsX) * cz + cx;
    const uint32_t depths = WaterBase(self);
    const uint32_t altitudes = VertexBase(self);
    return static_cast<double>(FloatAt(RecordAddress(depths, index))) +
           static_cast<double>(FloatAt(RecordAddress(altitudes, index)));
}

// Slot 31 (0x00744980).
double __fastcall GetWaterLevelAtIndex(char* self, void*, uint32_t index)
{
    if (index >= U32(self, field::kVertexCount))
        return 0.0;
    const uint32_t depths = WaterBase(self);
    const uint32_t altitudes = VertexBase(self);
    return static_cast<double>(FloatAt(RecordAddress(depths, index))) +
           static_cast<double>(FloatAt(RecordAddress(altitudes, index)));
}

// Slot 32 (0x007444E0): depth + altitude of the four corners, interpolated
// along x on both edges and then along z. The far edge's first level is
// rounded to float (stored), the others stay double; z is rounded to float
// before its floor. The cell is not clamped.
double __fastcall GetLerpedWaterLevel(char* self, void*, float x, float z)
{
    if (!InBounds(self, x, z))
        return 0.0;
    const double tx = static_cast<double>(InverseCellWidth()) * static_cast<double>(x);
    const float tz = static_cast<float>(static_cast<double>(InverseCellWidth()) * static_cast<double>(z));
    const uint32_t cx = FloorCell(tx);
    const uint32_t cz = FloorCell(static_cast<double>(tz));
    const uint32_t stride = U32(self, field::kVertsX);
    const uint32_t depths = WaterBase(self);
    const uint32_t offset = (stride * cz + cx) * vertex::kSize;
    const uint32_t land = VertexBase(self) + offset;
    const uint32_t wet = depths + offset;
    const double u = tx - static_cast<double>(cx);
    const uint32_t row = stride * vertex::kSize;
    const double l00 = static_cast<double>(FloatAt(wet)) + static_cast<double>(FloatAt(land));
    const float l01 =
        static_cast<float>(static_cast<double>(FloatAt(wet + row)) + static_cast<double>(FloatAt(land + row)));
    const double l10 = static_cast<double>(FloatAt(wet + 0x24)) + static_cast<double>(FloatAt(land + 0x24));
    const double nearStep = (l10 - l00) * u;
    const double nearLevel = l00 + nearStep;
    const double l11 =
        static_cast<double>(FloatAt(land + row + 0x24)) + static_cast<double>(FloatAt(wet + row + 0x24));
    const double farStep = (l11 - static_cast<double>(l01)) * u;
    const double farLevel = farStep + static_cast<double>(l01);
    const double across = farLevel - nearLevel;
    const double v = static_cast<double>(tz) - static_cast<double>(cz);
    const double step = across * v;
    return step + nearLevel;
}

// Slot 33 (0x00744AB0).
float __fastcall GetWaterDepthAtVertex(char* self, void*, uint32_t x, uint32_t z)
{
    const uint32_t stride = U32(self, field::kVertsX);
    if (x >= stride || z >= U32(self, field::kVertsZ))
        return 0.0f;
    return FloatAt(RecordAddress(WaterBase(self), stride * z + x));
}

// Slot 92 (0x00744A80).
float __fastcall GetWaterDepthAtIndex(char* self, void*, uint32_t index)
{
    if (index >= U32(self, field::kVertexCount))
        return 0.0f;
    return FloatAt(RecordAddress(WaterBase(self), index));
}

// The corners of a cell in the order slots 34 and 35 test them: (x, z),
// (x+1, z), (x, z+1), (x+1, z+1).
template <typename Test>
uint32_t AnyCorner(char* self, uint32_t x, uint32_t z, Test test)
{
    if (x >= U32(self, field::kCellsX) || z >= U32(self, field::kCellsZ))
        return 0;
    const uint32_t stride = U32(self, field::kVertsX);
    const uint32_t index = stride * z + x;
    const uint32_t depths = WaterBase(self);
    const uint32_t nearRow = RecordAddress(depths, index);
    if (test(FloatAt(nearRow)) || test(FloatAt(nearRow + 0x24)))
        return 1;
    const uint32_t farRow = RecordAddress(depths, stride + index);
    if (test(FloatAt(farRow)) || test(FloatAt(farRow + 0x24)))
        return 1;
    return 0;
}

// Slot 34 (0x00743FA0): a corner with a depth above 0.
uint32_t __fastcall CellHasWater(char* self, void*, uint32_t x, uint32_t z)
{
    return AnyCorner(self, x, z, [](float depth) { return depth > 0.0f; });
}

// Slot 35 (0x00744030): a corner with a depth of 0 or less (not a NaN).
uint32_t __fastcall CellHasLand(char* self, void*, uint32_t x, uint32_t z)
{
    return AnyCorner(self, x, z, [](float depth) { return depth <= 0.0f; });
}

// Slot 36 (0x00747680).
uint32_t __fastcall IsLandVertex(char* self, void*, uint32_t x, uint32_t z)
{
    const uint32_t stride = U32(self, field::kVertsX);
    if (x >= stride || z >= U32(self, field::kVertsZ))
        return 0;
    return FloatAt(RecordAddress(WaterBase(self), stride * z + x)) <= 0.0f ? 1u : 0u;
}

// Slot 37 (0x007476D0).
uint32_t __fastcall IsWaterVertex(char* self, void*, uint32_t x, uint32_t z)
{
    const uint32_t stride = U32(self, field::kVertsX);
    if (x >= stride || z >= U32(self, field::kVertsZ))
        return 0;
    return FloatAt(RecordAddress(WaterBase(self), stride * z + x)) > 0.0f ? 1u : 0u;
}

// Slot 38 (0x00741B30): below the sea level.
uint32_t __fastcall IsSeaVertex(char* self, void*, uint32_t x, uint32_t z)
{
    const uint32_t stride = U32(self, field::kVertsX);
    if (x >= stride || z >= U32(self, field::kVertsZ))
        return 0;
    return FloatAt(RecordAddress(VertexBase(self), stride * z + x)) < At<float>(self, field::kSeaLevel) ? 1u : 0u;
}

// Slot 39 (0x00740750): every vertex of the rectangle below the sea level;
// true for an empty one inside the grid.
uint32_t __fastcall AreSeaVertices(char* self, void*, const int32_t* rect)
{
    const uint32_t stride = U32(self, field::kVertsX);
    const int32_t x0 = rect[0];
    if (static_cast<uint32_t>(x0) >= stride)
        return 0;
    const uint32_t depth = U32(self, field::kVertsZ);
    int32_t z = rect[1];
    if (static_cast<uint32_t>(z) >= depth)
        return 0;
    const int32_t x1 = rect[2];
    if (static_cast<uint32_t>(x1) >= stride)
        return 0;
    const int32_t z1 = rect[3];
    if (static_cast<uint32_t>(z1) >= depth)
        return 0;
    if (z > z1)
        return 1;
    uint32_t rowOffset = (stride * static_cast<uint32_t>(z) + static_cast<uint32_t>(x0)) * vertex::kSize;
    const uint32_t rowStep = stride * vertex::kSize;
    do {
        if (x0 <= x1) {
            uint32_t at = VertexBase(self) + rowOffset;
            int32_t x = x0;
            do {
                if (!(FloatAt(at) < At<float>(self, field::kSeaLevel)))
                    return 0;
                at += vertex::kSize;
                ++x;
            } while (x <= x1);
        }
        ++z;
        rowOffset += rowStep;
    } while (z <= z1);
    return 1;
}

// Slot 53 (0x00744B40): no range test.
uint32_t __fastcall NumberOfWaterVerticesInRect(char* self, void*, const int32_t* rect)
{
    uint32_t count = 0;
    if (rect[1] > rect[3])
        return count;
    WalkRows(U32(self, field::kVertsX), rect, [self, &count](uint32_t index) {
        if (FloatAt(RecordAddress(WaterBase(self), index)) > 0.0f)
            ++count;
    });
    return count;
}

// Slot 54 (0x0074C6E0, shared).
uint32_t __fastcall NumberOfWaterVertices(char* self, void*) { return U32(self, field::kWaterVertices); }

// Slot 55 (0x007448C0).
float __fastcall GetSedimentValue(char* self, void*, float x, float z)
{
    if (!InBounds(self, x, z))
        return 0.0f;
    const uint32_t cx = FloorCell(static_cast<double>(InverseCellWidth()) * static_cast<double>(x));
    const uint32_t cz = FloorCell(static_cast<double>(InverseCellWidth()) * static_cast<double>(z));
    const uint32_t index = U32(self, field::kVertsX) * cz + cx;
    return FloatAt(RecordAddress(WaterBase(self), index) + water::kSediment);
}

// --- Type, brushes -----------------------------------------------------------

// Slot 42 (0x0040CF10, shared).
uint32_t __fastcall TerrainType(char* self, void*) { return U32(self, field::kTerrainType); }

// Slot 58 (0x007415D0): no range test of the type. AL is whether the ID is
// not 0; the rest of EAX is the ID's, as the game leaves it.
uint32_t __fastcall GetBrushExemplarID(char*, void*, uint32_t type, uint32_t* id)
{
    const uint32_t value = AtAddress<uint32_t>(static_cast<uint32_t>(addr::cSTETerrain_BrushExemplarIDs) + type * 4u);
    *id = value;
    return (value & 0xFFFFFF00u) | (value != 0 ? 1u : 0u);
}

// The brush's node in the map at 0x00B4BD58 through the game's find
// (0x00497520); null for none (the game compares with the map's header).
char* FindBrush(uint32_t brush)
{
    using FindFn = void(__thiscall*)(void* map, char** out, const uint32_t* key);
    char* node = nullptr;
    reinterpret_cast<FindFn>(addr::SC4Uint32Map_Find)(reinterpret_cast<void*>(addr::cSTETerrain_Brushes), &node,
                                                      &brush);
    return node == Global<char*>(addr::cSTETerrain_Brushes) ? nullptr : node;
}

// Slot 61 (0x00747AE0): 32.0 for an unknown brush.
float __fastcall BrushRadius(char* self, void*, uint32_t brush)
{
    char* const node = FindBrush(brush);
    if (!node)
        return 32.0f;
    return FloatAt(At<uint32_t>(node, 0x20) + U32(self, field::kBrushStep) * 4u);
}

// Slot 62 (0x00747C90): 32 for an unknown brush; EDX:EAX.
int64_t __fastcall GetCostPerBrushApplication(char* self, void*, uint32_t brush)
{
    char* const node = FindBrush(brush);
    if (!node)
        return 0x20;
    const uint32_t address =
        static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(node)) + U32(self, field::kBrushStep) * 4u + 0x38u;
    return Ftol64(static_cast<double>(FloatAt(address)));
}

// Slot 63 (0x00747D60).
uint8_t __fastcall GetUIElementsForBrush(char*, void*, uint32_t brush, uint32_t* elements)
{
    char* const node = FindBrush(brush);
    if (!node)
        return 0;
    for (int i = 0; i < 10; ++i)
        elements[i] = At<uint32_t>(node, 0x4C + 4 * static_cast<std::size_t>(i));
    return 1;
}

// --- Flags and display state -------------------------------------------------

// The flags word of a cell's vertex; false when the cell is outside the grid.
inline bool CellFlags(char* self, uint32_t x, uint32_t z, uint32_t& flags)
{
    if (x >= U32(self, field::kCellsX) || z >= U32(self, field::kCellsZ))
        return false;
    flags = AtAddress<uint16_t>(RecordAddress(VertexBase(self), U32(self, field::kVertsX) * z + x) + vertex::kFlags);
    return true;
}

// Slot 70 (0x00741C20).
uint32_t __fastcall IsTriangulationFlipped(char* self, void*, uint32_t x, uint32_t z)
{
    uint32_t flags = 0;
    if (!CellFlags(self, x, z, flags))
        return 0;
    return (flags & (flag::kFlipped1 | flag::kFlipped2)) != 0 ? 1u : 0u;
}

// Slot 74 (0x00741AB0).
uint32_t __fastcall IsEvenTriangleHidden(char* self, void*, uint32_t x, uint32_t z)
{
    uint32_t flags = 0;
    if (!CellFlags(self, x, z, flags))
        return 0;
    return (flags >> 5) & 1u;
}

// Slot 75 (0x00741AF0).
uint32_t __fastcall IsOddTriangleHidden(char* self, void*, uint32_t x, uint32_t z)
{
    uint32_t flags = 0;
    if (!CellFlags(self, x, z, flags))
        return 0;
    return (flags >> 6) & 1u;
}

// Slot 76 (0x00741C60).
uint32_t __fastcall IsWholeCellHidden(char* self, void*, uint32_t x, uint32_t z)
{
    uint32_t flags = 0;
    if (!CellFlags(self, x, z, flags))
        return 0;
    return (flags & flag::kOddHidden) != 0 && (flags & flag::kEvenHidden) != 0 ? 1u : 0u;
}

// Slot 77 (0x00741CB0): no bounds test; the cell is clamped to the last one
// (a negative coordinate's floor is a large unsigned number, so it clamps
// too). The parts of the position inside the cell are rounded to float. The
// vertex index comes from the object's own slot 9.
uint32_t __fastcall IsPointHidden(char* self, void*, float x, float z)
{
    uint32_t cx = FloorCell(static_cast<double>(InverseCellWidth()) * static_cast<double>(x));
    uint32_t cz = FloorCell(static_cast<double>(InverseCellWidth()) * static_cast<double>(z));
    if (cx > U32(self, field::kLastCellX))
        cx = U32(self, field::kLastCellX);
    if (cz > U32(self, field::kLastCellZ))
        cz = U32(self, field::kLastCellZ);
    const float fx = static_cast<float>(static_cast<double>(x) - static_cast<double>(cx) * kCellWidth);
    const float fz = static_cast<float>(static_cast<double>(z) - static_cast<double>(cz) * kCellWidth);
    const uint32_t index = Virtual<uint32_t>(self, slot::kGetVertIndex, cx, cz);
    const uint32_t flags = AtAddress<uint16_t>(VertexBase(self) + index * 9u * 4u + vertex::kFlags);
    bool odd;
    if ((flags & (flag::kFlipped1 | flag::kFlipped2)) == 0)
        odd = !(fx <= fz); // also when unordered
    else
        odd = static_cast<double>(fz) + static_cast<double>(fx) > kCellWidth;
    return (flags >> (odd ? 6 : 5)) & 1u;
}

// Slot 80 (0x0074C6F0).
uint8_t __fastcall IsAccumulatingTextureUpdates(char* self, void*) { return At<uint8_t>(self, field::kAccumulating); }

// Slot 82 (0x00741770).
uint32_t __fastcall GetTerrainCellFlags(char* self, void*, uint32_t x, uint32_t z)
{
    uint32_t flags = 0;
    if (!CellFlags(self, x, z, flags))
        return 0;
    uint32_t result = 0;
    if (flags & flag::kEvenHidden)
        result = 1;
    if (flags & flag::kOddHidden)
        result |= 2;
    if (flags & flag::kCell4)
        result |= 4;
    if (flags & flag::kFlipped2)
        result |= 8;
    return result;
}

// Slot 83 (0x00741740, shared).
uint8_t __fastcall GetWaterSurfaceDisplayState(char* self, void*) { return At<uint8_t>(self, field::kShowWater); }

// Slot 85 (0x00741750): a global, not a field.
uint8_t __fastcall GetPlumbingInfoDisplayState(char*, void*) { return Global<uint8_t>(addr::STE_DebugCells); }

// --- Installation ------------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x005B9D30, IsInitialized),
    OPENSC4_SLOT(6, 0x0074C5C0, CellWidth),
    OPENSC4_SLOT(7, 0x0060FB80, CellCountX),
    OPENSC4_SLOT(8, 0x004ABAD0, CellCountZ),
    OPENSC4_SLOT(9, 0x0074C5D0, GetVertIndex),
    OPENSC4_SLOT(10, 0x0074C5E0, GetVertex),
    OPENSC4_SLOT(11, 0x0074C610, LocationIsInBounds),
    OPENSC4_SLOT(12, 0x007413C0, GetAltitudeAtIndex),
    OPENSC4_SLOT(13, 0x00741260, GetAltitude),
    OPENSC4_SLOT(14, 0x007413E0, GetAltitudeAtVertex),
    OPENSC4_SLOT(15, 0x00742A00, GetAltitudes),
    OPENSC4_SLOT(16, 0x007443C0, GetWaterDepths),
    OPENSC4_SLOT(17, 0x007415F0, GetCellCornerAltitudes),
    OPENSC4_SLOT(18, 0x00744620, GetMaxSurfaceLevel),
    OPENSC4_SLOT(19, 0x00741420, GetAltitudeAtNearestGrid),
    OPENSC4_SLOT(20, 0x007414A0, GetAltitudeCheckSum),
    OPENSC4_SLOT(21, 0x0074C670, MinHeight),
    OPENSC4_SLOT(22, 0x0074C680, MaxHeight),
    OPENSC4_SLOT(23, 0x0074C690, MinAllowedHeight),
    OPENSC4_SLOT(24, 0x0074C6A0, MaxAllowedHeight),
    OPENSC4_SLOT(25, 0x0074C6B0, GetTerrainGridIndex),
    OPENSC4_SLOT(26, 0x00742B10, GetFaceNormal),
    OPENSC4_SLOT(27, 0x00742CF0, GetLerpedNormal),
    OPENSC4_SLOT(28, 0x007417D0, GetVertexNormal),
    OPENSC4_SLOT(29, 0x00741760, GetSeaLevel),
    OPENSC4_SLOT(30, 0x007449B0, GetWaterLevel),
    OPENSC4_SLOT(31, 0x00744980, GetWaterLevelAtIndex),
    OPENSC4_SLOT(32, 0x007444E0, GetLerpedWaterLevel),
    OPENSC4_SLOT(33, 0x00744AB0, GetWaterDepthAtVertex),
    OPENSC4_SLOT(34, 0x00743FA0, CellHasWater),
    OPENSC4_SLOT(35, 0x00744030, CellHasLand),
    OPENSC4_SLOT(36, 0x00747680, IsLandVertex),
    OPENSC4_SLOT(37, 0x007476D0, IsWaterVertex),
    OPENSC4_SLOT(38, 0x00741B30, IsSeaVertex),
    OPENSC4_SLOT(39, 0x00740750, AreSeaVertices),
    OPENSC4_SLOT(42, 0x0040CF10, TerrainType),
    OPENSC4_SLOT(47, 0x0074C660, GetDefaultTerrainRaiseLowerAmount),
    OPENSC4_SLOT(53, 0x00744B40, NumberOfWaterVerticesInRect),
    OPENSC4_SLOT(54, 0x0074C6E0, NumberOfWaterVertices),
    OPENSC4_SLOT(55, 0x007448C0, GetSedimentValue),
    OPENSC4_SLOT(58, 0x007415D0, GetBrushExemplarID),
    OPENSC4_SLOT(61, 0x00747AE0, BrushRadius),
    OPENSC4_SLOT(62, 0x00747C90, GetCostPerBrushApplication),
    OPENSC4_SLOT(63, 0x00747D60, GetUIElementsForBrush),
    OPENSC4_SLOT(70, 0x00741C20, IsTriangulationFlipped),
    OPENSC4_SLOT(74, 0x00741AB0, IsEvenTriangleHidden),
    OPENSC4_SLOT(75, 0x00741AF0, IsOddTriangleHidden),
    OPENSC4_SLOT(76, 0x00741C60, IsWholeCellHidden),
    OPENSC4_SLOT(77, 0x00741CB0, IsPointHidden),
    OPENSC4_SLOT(80, 0x0074C6F0, IsAccumulatingTextureUpdates),
    OPENSC4_SLOT(82, 0x00741770, GetTerrainCellFlags),
    OPENSC4_SLOT(83, 0x00741740, GetWaterSurfaceDisplayState),
    OPENSC4_SLOT(85, 0x00741750, GetPlumbingInfoDisplayState),
    OPENSC4_SLOT(92, 0x00744A80, GetWaterDepthAtIndex),
};

#undef OPENSC4_SLOT

// Checks that every slot still holds the original, then swaps them all.
bool SwapSlots()
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSTETerrain_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("terrain queries: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(addr::cSTETerrain_vftable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : kSlots) {
        std::uintptr_t* const entry = vtable + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("terrain queries: %u slots of %08X are ours", static_cast<unsigned>(std::size(kSlots)),
               static_cast<unsigned>(addr::cSTETerrain_vftable));
    return true;
}

bool Install(registry::Mode) { return SwapSlots(); }

const bool g_registered = registry::Register(
    {"simulators.terrain.queries", "simulators/terrain", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSTETerrain_vftable,
     "Our cSTETerrain queries: sizes, index conversions, altitudes, normals, water levels and depths, sea and "
     "water tests, hidden triangles, cell flags, brush getters (54 slots of 0x00AB3D18)",
     false, Install});

} // namespace
} // namespace opensc4::terrain_sim
