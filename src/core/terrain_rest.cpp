// Our cSTETerrain leftovers (build 641), piece 7 of the city's terrain
// object (vtable 0x00AB3D18, the service 0x00B43CF4): the small pieces of
// terrain logic no earlier piece replaced. Group simulators.terrain.rest.
//
//   0x00740900  UnflipMarkedCells     cells flipped for steepness set back
//   0x00742FC0  MarkSteepCell         a steep cell: marks, perhaps a flip
//   0x007428E0  MarkShoreVertices     the mark 0x10 on every shore vertex
//   0x0074B570  CollectShore          one shore line into a deque
//   0x00740980  ClearFlagsEverywhere  flags cleared on every vertex
//   0x00740F10  RectAround            the square around a place, cut to the grid
//   0x00741E30  GrowRect              a rectangle widened, cut to the grid
//   0x00744CA0  RaiseVertex           a vertex raised by an amount
//
// All from the disassembly, statement by statement.
//
// How the group reaches every caller: each is called directly by game code
// that is not ours or not always ours, so each gets a detour at its entry.
// The call sites (scan of every direct call in the exe) are listed at the
// installation; every one is a plain thiscall with the arguments on the
// stack that loads what it uses anew after the call: no thunks.
//
// What stays the game's after all seven pieces is listed in docs and in
// the piece's report: the drawing side (the view, lighting, the texture
// weights of 0x007490B0 and its helpers, the wave producers), the static
// tuning and brush table loaders, the water object's three functions at
// the start of the range, and shared container code.
//
// Floating point. MarkSteepCell follows the x87 code: the altitude
// differences as the game keeps them (one on the stack, the others stored
// as floats), products and sums in double, the square root as FSQRT
// (correctly rounded, as std::sqrt is). No transcendental instruction, no
// random draws. Constants in .data are read from the game's memory at each
// use.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
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

// --- Additions for terrain_sim.h (to be merged there) --------------------------

namespace flag {
constexpr uint16_t kSteep = 0x0008;        // set on a steep cell's vertices by 0x00742FC0 (flag::kSteepMark of terrain_erode_2.cpp)
constexpr uint16_t kShoreVertex = 0x0010;  // set by 0x007428E0 (flag::kShore of terrain_edit.cpp)
constexpr uint16_t kFlippedSteep = 0x0800; // the cell was flipped by 0x00742FC0; 0x00740900 flips it back
} // namespace flag

namespace slot {
constexpr std::size_t kSetFlipped1 = 0x174; // slot 93 (x, z, bool)
} // namespace slot

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

// --- Steep cells -------------------------------------------------------------

// 0x00740900 (rect): for every vertex of a rectangle (rows first, both
// ends included, signed; the far ends read again at every step) that
// carries the flag 0x0800: slot 93 with (x, z, 0), then that flag cleared.
// No range test.
void __fastcall UnflipMarkedCells(char* self, void*, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("UnflipMarkedCells", logged);
    int32_t z = rect[1];
    if (z > rect[3])
        return;
    do {
        int32_t x = rect[0];
        uint32_t offset = Offset(U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x));
        if (x <= rect[2]) {
            do {
                if (AtAddress<uint8_t>(VertexBase(self) + offset + 0x23) & (flag::kFlippedSteep >> 8)) {
                    Virtual<void>(self, slot::kSetFlipped1, x, z, 0u);
                    AtAddress<uint8_t>(VertexBase(self) + offset + 0x23) &= static_cast<uint8_t>(~(flag::kFlippedSteep >> 8));
                }
                ++x;
                offset += vertex::kSize;
            } while (x <= rect[2]);
        }
        ++z;
    } while (z <= rect[3]);
}

// Whether a triangle whose normal has these two horizontal components (and
// 256 upward) is steep: |256 x (0x00B0D684 / length)| below 0x00B0D620
// (not for a NaN); the length is the square root of a x a + b x b + 65536.
inline bool SteepNormal(double a, double b)
{
    const double aa = a * a;
    const double bb = b * b;
    double squares = aa + bb;
    squares = squares + 65536.0; // 0x00AB3CAC
    double y = C(addr::cSTETerrain_One) / std::sqrt(squares);
    y = y * 256.0; // 0x00A9422C
    return std::fabs(y) < C(addr::cSTETerrain_SteepNormalY);
}

// 0x00742FC0 (index, first, second): a cell (by its first vertex) whose
// first and/or second triangle is steep, as 0x00743B60 found. With both:
// the two triangles of the OTHER diagonal are weighed from the altitudes
// (the vertex below as the base); unless both of those are steep too, the
// cell is flipped (slot 93 with x, z, 1; the flag 0x0800 on its vertex) and
// only the vertices of the triangles that are steep on the new diagonal get
// the mark 8. Otherwise the vertices of each steep triangle get the mark 8.
// Only the low bytes of the two arguments are read.
void __fastcall MarkSteepCell(char* self, void*, uint32_t index, uint32_t firstArgument, uint32_t secondArgument)
{
    const uint8_t first = static_cast<uint8_t>(firstArgument);
    const uint8_t second = static_cast<uint8_t>(secondArgument);
    const uint32_t here = Offset(index);
    const auto mark = [self](uint32_t offset) { AtAddress<uint8_t>(VertexBase(self) + offset + vertex::kFlags) |= static_cast<uint8_t>(flag::kSteep); };
    if (first) {
        if (second) {
            const uint32_t base = VertexBase(self);
            const uint32_t below = base + Offset(U32(self, field::kVertsX) + index);
            const double south = static_cast<double>(F(below));              // stays on the stack
            const double drop = static_cast<double>(F(base + here)) - south; // stays on the stack
            const float east = static_cast<float>(static_cast<double>(F(base + here + vertex::kSize)) - south);
            const float across = static_cast<float>(static_cast<double>(F(below + vertex::kSize)) - south);
            // -16.0 is 0x00A98FB0, 16.0 0x00AB3B5C (.rdata).
            const float long1 = static_cast<float>(static_cast<double>(east) * -16.0 - drop * -16.0);
            const float nothing = static_cast<float>(static_cast<double>(east) * C(addr::SC4_FloatZero)); // 0, or a NaN
            const double wide1 = drop * C(addr::cSTETerrain_CellWidthFloat) - static_cast<double>(nothing);
            // The sum is wide x wide + long x long.
            const bool steep1 = SteepNormal(wide1, static_cast<double>(long1));
            const double long2 = static_cast<double>(across) * -16.0 - static_cast<double>(nothing);
            const double wide2 = static_cast<double>(east) * C(addr::cSTETerrain_CellWidthFloat) -
                                 static_cast<double>(across) * C(addr::cSTETerrain_CellWidthFloat);
            const bool steep2 = SteepNormal(wide2, long2);
            if (!(steep1 && steep2)) {
                const uint32_t x = index % U32(self, field::kVertsX);
                const uint32_t z = index / U32(self, field::kVertsX);
                Virtual<void>(self, slot::kSetFlipped1, x, z, 1u);
                AtAddress<uint8_t>(VertexBase(self) + here + 0x23) |= static_cast<uint8_t>(flag::kFlippedSteep >> 8);
                if (steep1) {
                    mark(here);
                    mark(Offset(U32(self, field::kVertsX) + index));
                    mark(here + vertex::kSize);
                }
                if (!steep2)
                    return;
                mark(here + vertex::kSize);
                mark(Offset(U32(self, field::kVertsX) + index));
                mark(Offset(U32(self, field::kVertsX) + index) + vertex::kSize);
                return;
            }
        }
        mark(here);
        mark(Offset(U32(self, field::kVertsX) + index));
        mark(Offset(U32(self, field::kVertsX) + index) + vertex::kSize);
    }
    if (!second)
        return;
    mark(here);
    mark(Offset(U32(self, field::kVertsX) + index) + vertex::kSize);
    mark(here + vertex::kSize);
}

// --- The shore ---------------------------------------------------------------

// 0x007428E0: the mark 0x10 cleared over the whole map (0x007409B0 with 0,
// 0, cells x, cells z), then set on every shore vertex (0x00741DE0 with the
// callback 0x00741B80). Both are called at their addresses (ours with
// simulators.terrain.brush).
void __fastcall MarkShoreVertices(char* self, void*)
{
    static bool logged = false;
    FirstCall("MarkShoreVertices", logged);
    int32_t rect[4];
    rect[2] = static_cast<int32_t>(U32(self, field::kCellsX));
    const uint32_t cellsZ = U32(self, field::kCellsZ);
    rect[0] = 0;
    rect[1] = 0;
    rect[3] = static_cast<int32_t>(cellsZ);
    This<void>(addr::cSTETerrain_ClearFlagsInRect, self, static_cast<void*>(rect), uint32_t{flag::kShoreVertex});
    This<void>(addr::cSTETerrain_FlagVerticesWhere, self, static_cast<uint32_t>(addr::cSTETerrain_IsShoreVertex), 0u,
               uint32_t{flag::kShoreVertex});
}

// A std::deque<uint32_t> of the game (STLport; blocks of 0x80 bytes).
struct IndexDeque {
    uint32_t startCurrent, startFirst, startLast, startNode;
    uint32_t finishCurrent, finishFirst, finishLast, finishNode;
    uint32_t map, mapSize;
};

// 0x0074B570 (index, queue, mask): the queue emptied (0x006DB440), the
// vertex pushed onto it (0x006266E0 when the last block is full) and given
// the mask's flags, then the shore line followed from it both ways
// (0x0074AD20, 0x0074AB60, at their addresses: ours with
// simulators.terrain.brush).
void __fastcall CollectShore(char* self, void*, uint32_t index, IndexDeque* queue, uint32_t mask)
{
    static bool logged = false;
    FirstCall("CollectShore", logged);
    This<void>(addr::SC4CellDeque_Clear, queue);
    if (queue->finishCurrent != queue->finishLast - 4u) {
        if (queue->finishCurrent)
            AtAddress<uint32_t>(queue->finishCurrent) = index;
        queue->finishCurrent += 4u;
    } else {
        This<void>(addr::SC4Uint32Deque_PushBackSlow, queue, &index);
    }
    AtAddress<uint16_t>(VertexBase(self) + Offset(index) + vertex::kFlags) |= static_cast<uint16_t>(mask);
    This<void>(addr::cSTETerrain_ShoreCollect1, self, index, static_cast<void*>(queue), mask);
    This<void>(addr::cSTETerrain_ShoreCollect2, self, index, static_cast<void*>(queue), mask);
}

// 0x00740980 (mask): the mask's bits cleared in every vertex's flags word.
void __fastcall ClearFlagsEverywhere(char* self, void*, uint32_t mask)
{
    static bool logged = false;
    FirstCall("ClearFlagsEverywhere", logged);
    const uint16_t keep = static_cast<uint16_t>(~mask);
    uint32_t offset = 0;
    for (uint32_t index = 0; index < U32(self, field::kVertexCount); ++index, offset += vertex::kSize)
        AtAddress<uint16_t>(VertexBase(self) + offset + vertex::kFlags) &= keep;
}

// --- Rectangles --------------------------------------------------------------

// 0x00740F10 (out, x, z, reach): out = x - reach, z - reach, x + reach, z +
// reach; a near end below 0 (signed) becomes 0, a far end above the cell
// count (unsigned) the cell count. Answers `out`.
int32_t* __fastcall RectAround(char* self, void*, int32_t* out, int32_t x, int32_t z, int32_t reach)
{
    static bool logged = false;
    FirstCall("RectAround", logged);
    out[0] = static_cast<int32_t>(static_cast<uint32_t>(x) - static_cast<uint32_t>(reach));
    if (out[0] < 0)
        out[0] = 0;
    const uint32_t farX = static_cast<uint32_t>(x) + static_cast<uint32_t>(reach);
    out[2] = static_cast<int32_t>(farX);
    if (farX > U32(self, field::kCellsX))
        out[2] = static_cast<int32_t>(U32(self, field::kCellsX));
    out[1] = static_cast<int32_t>(static_cast<uint32_t>(z) - static_cast<uint32_t>(reach));
    if (out[1] < 0)
        out[1] = 0;
    const uint32_t farZ = static_cast<uint32_t>(reach) + static_cast<uint32_t>(z);
    out[3] = static_cast<int32_t>(farZ);
    if (farZ > U32(self, field::kCellsZ))
        out[3] = static_cast<int32_t>(U32(self, field::kCellsZ));
    return out;
}

// 0x00741E30 (rect, by): the rectangle widened by `by` on every side; a
// near end below 0 becomes 0, a far end above the cell count (signed here)
// the cell count. Answers the rectangle.
int32_t* __fastcall GrowRect(char* self, void*, int32_t* rect, int32_t by)
{
    static bool logged = false;
    FirstCall("GrowRect", logged);
    const int32_t x0 = static_cast<int32_t>(static_cast<uint32_t>(rect[0]) - static_cast<uint32_t>(by));
    const int32_t z0 = static_cast<int32_t>(static_cast<uint32_t>(rect[1]) - static_cast<uint32_t>(by));
    const int32_t x1 = static_cast<int32_t>(static_cast<uint32_t>(rect[2]) + static_cast<uint32_t>(by));
    const int32_t z1 = static_cast<int32_t>(static_cast<uint32_t>(rect[3]) + static_cast<uint32_t>(by));
    rect[0] = x0;
    rect[1] = z0;
    rect[2] = x1;
    rect[3] = z1;
    if (z0 < 0)
        rect[1] = 0;
    if (x0 < 0)
        rect[0] = 0;
    if (x1 > static_cast<int32_t>(U32(self, field::kCellsX)))
        rect[2] = static_cast<int32_t>(U32(self, field::kCellsX));
    if (z1 > static_cast<int32_t>(U32(self, field::kCellsZ)))
        rect[3] = static_cast<int32_t>(U32(self, field::kCellsZ));
    return rect;
}

// --- A vertex raised ---------------------------------------------------------

// 0x00744CA0 (index, amount): nothing for a locked vertex (0x4000). Else
// the altitude + the amount, stored rounded; when the sum before rounding
// is above the greatest allowed height (0x00B0D5DC; not for a NaN) that
// height instead; the water depth becomes sea level - altitude (also a
// negative one). No range test. The counterpart of 0x00744AF0.
void __fastcall RaiseVertex(char* self, void*, uint32_t index, float amount)
{
    const uint32_t offset = Offset(index);
    const uint32_t record = VertexBase(self) + offset;
    if (AtAddress<uint8_t>(record + 0x23) & 0x40)
        return;
    const double raised = static_cast<double>(amount) + static_cast<double>(F(record));
    const uint32_t wet = WaterBase(self) + offset;
    F(record) = static_cast<float>(raised);
    if (raised > C(addr::cSTETerrain_MaxAllowedHeightValue))
        AtAddress<uint32_t>(record) = Global<uint32_t>(addr::cSTETerrain_MaxAllowedHeightValue);
    F(wet) = static_cast<float>(static_cast<double>(At<float>(self, field::kSeaLevel)) - static_cast<double>(F(record)));
}

// --- Installation ------------------------------------------------------------

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Every direct call site of each (scan of all direct calls in the exe),
// checked for what it uses after the call:
// 0x00740900: 0x00743BDD (0x00743B60; EDI and the stack next).
// 0x00742FC0: 0x00743D8A (0x00743B60; EDX loaded from the stack next).
// 0x007428E0: 0x0074BBA4 (slot 64; EBP next), 0x00761BA2 (ECX loaded from
//   [ESI+0x14] next).
// 0x0074B570: 0x0074BC36 (slot 64), 0x00761C2F: the stack next.
// 0x00740980: 0x00761C9E (the stack next).
// 0x00740F10: 0x0075F958 (reads the rectangle through EDI next, not EAX).
// 0x00741E30: 0x007479F3, 0x00748374, 0x00748A14, 0x007490CF, 0x007498C1,
//   0x0075F10E, 0x0075F846: each loads ECX and what else it uses anew.
// 0x00744CA0: 0x00745C4D, 0x00745C96, 0x00745D7D (the brushes' per-vertex
//   routines: AL set and a return next), 0x0075F7DA (EBX and the stack
//   next).
// Arguments on the stack only; nothing is kept in ECX, EDX or EAX.
const Detour kDetours[] = {
    OPENSC4_DETOUR(addr::cSTETerrain_UnflipMarkedCells, UnflipMarkedCells),
    OPENSC4_DETOUR(addr::cSTETerrain_MarkSteepCell, MarkSteepCell),
    OPENSC4_DETOUR(addr::cSTETerrain_MarkShoreVertices, MarkShoreVertices),
    OPENSC4_DETOUR(addr::cSTETerrain_CollectShore, CollectShore),
    OPENSC4_DETOUR(addr::cSTETerrain_ClearFlagsEverywhere, ClearFlagsEverywhere),
    OPENSC4_DETOUR(addr::cSTETerrain_RectAround, RectAround),
    OPENSC4_DETOUR(addr::cSTETerrain_GrowRect, GrowRect),
    OPENSC4_DETOUR(addr::cSTETerrain_RaiseVertex, RaiseVertex),
};

#undef OPENSC4_DETOUR

void* g_originals[std::size(kDetours)];

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : kDetours) {
        if (!addr_check::Find(detour.address)) {
            log::Write("terrain rest: no address entry for %08X; not installing", static_cast<unsigned>(detour.address));
            return false;
        }
    }
    for (std::size_t i = 0; i < std::size(kDetours); ++i) {
        if (!hook::Install(*addr_check::Find(kDetours[i].address), kDetours[i].ours, &g_originals[i])) {
            log::Write("terrain rest: detour %u of %u failed (%08X); the earlier ones stay", static_cast<unsigned>(i),
                       static_cast<unsigned>(std::size(kDetours)), static_cast<unsigned>(kDetours[i].address));
            return false;
        }
    }
    log::Write("terrain rest: %u functions (detours) are ours", static_cast<unsigned>(std::size(kDetours)));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.terrain.rest", "simulators/terrain", registry::Kind::Detour, registry::Phase::PostAppInit,
     addr::cSTETerrain_UnflipMarkedCells,
     "Our cSTETerrain leftovers: steep cells (unflip, mark and flip), the shore marks and one shore line, flags "
     "cleared everywhere, the two rectangle helpers and a vertex raised, by detours",
     false, Install});

} // namespace
} // namespace opensc4::terrain_sim
