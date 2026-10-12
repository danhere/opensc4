// Our cSTETerrain normals of a rectangle (build 641), second file of piece 5
// of the city's terrain object (vtable 0x00AB3D18): 0x00743B60, which
// DoPostCityInit (0x007495F9) and the redisplay slot 68 (0x007498CD) call
// after the altitudes changed. Group simulators.terrain.erode_normals, apart
// from simulators.terrain.erode (src/core/terrain_erode.cpp) so that either
// can be switched alone.
//
// What it does, from the disassembly, statement by statement:
// 1. the mark 8 is cleared on every vertex of the rectangle (0x007409B0,
//    the brushes'; called at its address);
// 2. a second rectangle of CELLS is made: the first column and row less one
//    (not below 0), the last ones cut to cells - 1 (unsigned); 0x00740900
//    (the game's) gets it: it sets the triangulation back (slot 93 with x,
//    z, 0) for every vertex of it that carries the flag 0x0800 and clears
//    the flag;
// 3. for each row of cells of that rectangle, top down:
//    a. the two triangles of every cell get their normals, kept in a row of
//       24 bytes per cell (first triangle, second triangle; the vectors
//       +0x118 and +0x124 serve as this row and the row before, swapped
//       after every row; the swap is local, the object keeps its pointers).
//       With h the altitude of the cell's vertex, S, E and SE the
//       differences to the vertex below, to the right and across, and 16 the
//       cell width (0x00AB3B5C): first (16 S - 16 SE, 256, 0 SE - 16 S),
//       second (0 SE - 16 E, 256, 16 E - 16 SE), each term rounded to float
//       as the game stores it, then scaled to length 1 (0x00B0D684 / FSQRT).
//       A triangle whose normal's y is below 0.3 (0x00B0D620) is steep; a
//       cell with a steep triangle is given to 0x00742FC0 (the game's:
//       marks and triangulation of steep cells);
//    b. when the row is not above the rectangle asked for, every vertex of
//       the row in the rectangle's columns gets its normal (+4 of the
//       record): the sum of the normals of the up to six triangles around
//       it (second of the cell to the left; both of the cell up-left; both
//       of the cell here; first of the cell above), x summed unrounded and
//       y, z rounded to float at every step, as the game keeps them; a sum
//       of length 0 becomes (1, 1, 1); scaled to length 1 (x by the
//       unrounded factor, y and z by the factor rounded to float).
// The last row of vertices (z = cells z) is never reached by step 3b (the
// rows end at cells z - 1): the game's, kept.
//
// The detour: both call sites are plain thiscall with the rectangle on the
// stack and load what they use anew after the call (0x007495FE pushes EDI
// and sets ECX from ESI; 0x007498D2 loads EAX from [EDI + 0xE8]).
//
// Floating point as in terrain_erode.cpp. FSQRT only (correctly rounded, as
// std::sqrt); no transcendental instruction. No random draws.
//
// Still the game's and called at their addresses: 0x007409B0 (ours with
// simulators.terrain.brush), 0x00740900 and 0x00742FC0 (only this function
// calls them; not rewritten: 0x00742FC0 is 584 bytes of its own float work).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace opensc4::terrain_sim {

// --- Additions for terrain_sim.h (to be merged there) --------------------------

namespace field {
constexpr std::size_t kFaceRowA = 0x118; // vector of 12-byte normals, 2 x cells x of them: the triangles of the row before (0x00743B60)
constexpr std::size_t kFaceRowB = 0x124; // the same: the triangles of the row at hand
} // namespace field

namespace flag {
constexpr uint16_t kSteepMark = 0x0008; // cleared over the rectangle by 0x00743B60, set by 0x00742FC0
} // namespace flag

namespace {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("terrain: first %s", name);
}

template <typename R, typename... Args>
inline R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

inline float& F(uint32_t address) { return AtAddress<float>(address); }

// A float constant of the game, as a value on the x87 stack.
inline double C(std::uintptr_t address) { return static_cast<double>(Global<float>(address)); }

inline uint32_t U32(void* self, std::size_t offset) { return At<uint32_t>(self, offset); }

inline uint32_t Offset(uint32_t index) { return index * 9u * 4u; }

constexpr uint32_t kFaceSize = 0xC;        // one triangle's normal
constexpr uint32_t kCellFaces = 0x18;      // a cell's two
constexpr uint32_t kFaceY = 0x43800000u;   // 256.0f, stored as a dword (0x00743C4C)

// A triangle's normal scaled to length 1 (0x00743C9F and 0x00743D1B): the
// length from the three floats as stored, (x x + y y) + z z; each component
// times 0x00B0D684 / length, rounded to float.
inline void ScaleFace(uint32_t face)
{
    const double x = static_cast<double>(F(face));
    const double y = static_cast<double>(F(face + 4));
    const double z = static_cast<double>(F(face + 8));
    const double length = std::sqrt((x * x + y * y) + z * z);
    const double scale = C(addr::cSTETerrain_One) / length;
    F(face) = static_cast<float>(scale * static_cast<double>(F(face)));
    F(face + 4) = static_cast<float>(scale * static_cast<double>(F(face + 4)));
    F(face + 8) = static_cast<float>(scale * static_cast<double>(F(face + 8)));
}

// Whether a scaled normal is steep: |y| below 0x00B0D620 (not for a NaN).
inline bool Steep(uint32_t face)
{
    return std::fabs(static_cast<double>(F(face + 4))) < C(addr::cSTETerrain_SteepNormalY);
}

// The two triangles of one cell (0x00743C30 to 0x00743D93): `index` and
// `offset` its vertex, `face` its place in the row.
inline void CellFaces(char* self, uint32_t index, uint32_t offset, uint32_t face)
{
    const uint32_t base = VertexBase(self);
    const uint32_t here = base + offset;
    const uint32_t below = base + Offset(U32(self, field::kVertsX) + index);
    const double south = static_cast<double>(F(below)) - static_cast<double>(F(here)); // stays on the stack
    const float across =
        static_cast<float>(static_cast<double>(F(below + vertex::kSize)) - static_cast<double>(F(here)));
    const float east =
        static_cast<float>(static_cast<double>(F(here + vertex::kSize)) - static_cast<double>(F(here)));
    AtAddress<uint32_t>(face + 4) = kFaceY;
    float wide = static_cast<float>(south * C(addr::cSTETerrain_CellWidthFloat));
    const float wideAcross = static_cast<float>(static_cast<double>(across) * C(addr::cSTETerrain_CellWidthFloat));
    const double x1 = static_cast<double>(wide) - static_cast<double>(wideAcross);
    const double nothing = static_cast<double>(across) * C(addr::SC4_FloatZero); // 0, or a NaN
    const float nothingStored = static_cast<float>(nothing);
    const double z1 = nothing - static_cast<double>(wide);
    F(face) = static_cast<float>(x1);
    F(face + 8) = static_cast<float>(z1);
    ScaleFace(face);
    const bool steep1 = Steep(face);

    const uint32_t second = face + kFaceSize;
    wide = static_cast<float>(static_cast<double>(east) * C(addr::cSTETerrain_CellWidthFloat));
    AtAddress<uint32_t>(second + 4) = kFaceY;
    const double x2 = static_cast<double>(nothingStored) - static_cast<double>(wide);
    const double z2 = static_cast<double>(wide) - static_cast<double>(wideAcross);
    F(second) = static_cast<float>(x2);
    F(second + 8) = static_cast<float>(z2);
    ScaleFace(second);
    const bool steep2 = Steep(second);
    // The game pushes two dwords of which 0x00742FC0 reads the low bytes.
    if (steep1 || steep2)
        This<void>(addr::cSTETerrain_MarkSteepCell, self, index, steep1 ? 1u : 0u, steep2 ? 1u : 0u);
}

// One vertex's normal (0x00743E00 to 0x00743F62). `before` and `current` are
// the game's two row pointers: the row before + x * 24 - 0x14 and this row
// + x * 24 - 4. x and z are tested signed against 0 and unsigned against the
// cell counts.
inline void VertexNormal(char* self, int32_t x, int32_t z, uint32_t offset, uint32_t before, uint32_t current)
{
    double a = C(addr::SC4_FloatZero); // x: on the stack throughout
    float b = 0.0f;                    // y: a float at every step
    float c = 0.0f;                    // z: the same
    if (x > 0) {
        if (static_cast<uint32_t>(z) < U32(self, field::kCellsZ)) {
            // The second triangle of the cell to the left.
            a = static_cast<double>(F(current - 8));
            b = F(current - 4);
            c = F(current);
        }
        if (z > 0) {
            // Both triangles of the cell up-left: the second, then the first.
            a = a + static_cast<double>(F(before + 8));
            b = static_cast<float>(static_cast<double>(b) + static_cast<double>(F(before + 0xC)));
            c = static_cast<float>(static_cast<double>(c) + static_cast<double>(F(before + 0x10)));
            a = a + static_cast<double>(F(before - 4));
            b = static_cast<float>(static_cast<double>(b) + static_cast<double>(F(before)));
            c = static_cast<float>(static_cast<double>(c) + static_cast<double>(F(before + 4)));
        }
    }
    if (static_cast<uint32_t>(x) < U32(self, field::kCellsX)) {
        if (static_cast<uint32_t>(z) < U32(self, field::kCellsZ)) {
            // Both triangles of the cell here.
            a = a + static_cast<double>(F(current + 4));
            b = static_cast<float>(static_cast<double>(b) + static_cast<double>(F(current + 8)));
            c = static_cast<float>(static_cast<double>(c) + static_cast<double>(F(current + 0xC)));
            a = a + static_cast<double>(F(current + 0x10));
            b = static_cast<float>(static_cast<double>(b) + static_cast<double>(F(current + 0x14)));
            c = static_cast<float>(static_cast<double>(c) + static_cast<double>(F(current + 0x18)));
        }
        if (z > 0) {
            // The first triangle of the cell above.
            a = a + static_cast<double>(F(before + 0x14));
            b = static_cast<float>(static_cast<double>(b) + static_cast<double>(F(before + 0x18)));
            c = static_cast<float>(static_cast<double>(c) + static_cast<double>(F(before + 0x1C)));
        }
    }
    double cc = static_cast<double>(c) * static_cast<double>(c);
    double bb = static_cast<double>(b) * static_cast<double>(b);
    double length = std::sqrt((cc + bb) + a * a);
    if (C(addr::SC4_FloatZero) == length) {
        b = 1.0f;
        a = C(addr::SC4_FloatOneA81228);
        c = 1.0f;
    }
    const uint32_t normal = VertexBase(self) + offset + vertex::kNormal;
    cc = static_cast<double>(c) * static_cast<double>(c);
    bb = static_cast<double>(b) * static_cast<double>(b);
    length = std::sqrt((cc + bb) + a * a);
    const double scale = C(addr::cSTETerrain_One) / length;
    const float scaleStored = static_cast<float>(scale);
    const double nx = a * scale;
    const double ny = static_cast<double>(scaleStored) * static_cast<double>(b);
    c = static_cast<float>(static_cast<double>(scaleStored) * static_cast<double>(c));
    F(normal + 8) = c;
    F(normal) = static_cast<float>(nx);
    F(normal + 4) = static_cast<float>(ny);
}

// 0x00743B60 (rect): see the top of the file. The rectangle is of vertices,
// both ends included; the game tests no range beyond the cuts below, and
// reads the rectangle asked for again at every step of 3b.
void __fastcall PrepareRect(char* self, void*, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("PrepareRect", logged);
    uint32_t rowBefore = U32(self, field::kFaceRowA);
    uint32_t rowHere = U32(self, field::kFaceRowB);
    int32_t cells[4];
    const int32_t firstX = rect[0];
    int32_t z = rect[1];
    cells[2] = rect[2];
    cells[3] = rect[3];
    This<void>(addr::cSTETerrain_ClearFlagsInRect, self, static_cast<const void*>(rect),
               static_cast<uint32_t>(flag::kSteepMark));
    cells[0] = firstX - 1;
    if (cells[0] < 0)
        cells[0] = 0;
    --z;
    cells[1] = z;
    if (z < 0) {
        z = 0;
        cells[1] = 0;
    }
    if (static_cast<uint32_t>(cells[2]) > U32(self, field::kLastCellX))
        cells[2] = static_cast<int32_t>(U32(self, field::kLastCellX));
    if (static_cast<uint32_t>(cells[3]) > U32(self, field::kLastCellZ))
        cells[3] = static_cast<int32_t>(U32(self, field::kLastCellZ));
    This<void>(addr::cSTETerrain_UnflipMarkedCells, self, static_cast<void*>(cells));
    if (z > cells[3])
        return;
    do {
        // 3a: the triangles of this row of cells.
        if (cells[0] <= cells[2]) {
            uint32_t index = U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(cells[0]);
            uint32_t face = rowHere + static_cast<uint32_t>(cells[0]) * kCellFaces;
            uint32_t offset = Offset(index);
            int32_t left = cells[2] - cells[0] + 1;
            do {
                CellFaces(self, index, offset, face);
                face += kCellFaces;
                ++index;
                offset += vertex::kSize;
            } while (--left != 0);
        }
        // 3b: the vertices of this row.
        if (z >= rect[1]) {
            int32_t x = rect[0];
            if (x <= rect[2]) {
                uint32_t offset =
                    Offset(U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x));
                uint32_t before = rowBefore + static_cast<uint32_t>(x) * kCellFaces - 0x14u;
                uint32_t current = rowHere + static_cast<uint32_t>(x) * kCellFaces - 4u;
                do {
                    VertexNormal(self, x, z, offset, before, current);
                    ++x;
                    before += kCellFaces;
                    current += kCellFaces;
                    offset += vertex::kSize;
                } while (x <= rect[2]);
            }
        }
        const uint32_t swap = rowBefore;
        rowBefore = rowHere;
        rowHere = swap;
        ++z;
    } while (z <= cells[3]);
}

void* g_original = nullptr;

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSTETerrain_PrepareRect);
    if (!entry) {
        log::Write("terrain normals: no address entry for %08X; not installing",
                   static_cast<unsigned>(addr::cSTETerrain_PrepareRect));
        return false;
    }
    if (!hook::Install(*entry, reinterpret_cast<void*>(&PrepareRect), &g_original)) {
        log::Write("terrain normals: the detour failed (%08X)", static_cast<unsigned>(addr::cSTETerrain_PrepareRect));
        return false;
    }
    log::Write("terrain normals: %08X (the normals of a rectangle) is ours, by a detour",
               static_cast<unsigned>(addr::cSTETerrain_PrepareRect));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.terrain.erode_normals", "simulators/terrain", registry::Kind::Detour, registry::Phase::PostAppInit,
     addr::cSTETerrain_PrepareRect,
     "Our cSTETerrain normals of a rectangle (0x00743B60: the triangles' and the vertices' normals after a change of "
     "altitudes; the steep-cell marking it calls stays the game's), by a detour",
     false, Install});

} // namespace
} // namespace opensc4::terrain_sim
