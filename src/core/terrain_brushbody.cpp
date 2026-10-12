// Our cSTETerrain brush body (build 641), piece 6 of the city's terrain
// object (vtable 0x00AB3D18, the service 0x00B43CF4): what slot 57
// (ApplyBrush, terrain_brush.cpp) runs for a brush record, and everything
// only it reaches:
//
//   0x0074D730  ApplyBrushLayers   the record's rotation, the layer loop
//   0x00742180  WalkPicture        a layer's picture laid over the vertices
//   0x00742440  WalkSquare         the same without a picture
//   0x00740AB0  QuadCorners        the four corners of a turned rectangle
//   0x00740BB0  QuadRows           its rows of vertices (scan conversion)
//   0x00745C60  kind 0  RaiseLower    up or down by the picture's value
//   0x00745A40  kind 1  PlantFlora    flora at a random spot of the cell
//   0x00745DE0  kind 2  SmoothVertex  slopes to the eight neighbours limited
//   0x00749130  kind 3  SplatTexture  a decal at a random spot of the cell
//   0x00749310  kind 4  StartEffect   an effect at a random spot of the cell
//   0x00745BA0  kind 5  LevelVertex   towards the parameters' altitude
//   0x007479B0  kind 6  ErodeLayer    the erosion pass over a square
//   0x00740820  kind 7  MarkVertex    the mark 0x0400, then 0x00748040
//   0x00745CD0  kind 8  ShapeVertex   towards altitude + the picture's height
//   kind 9, kind 10: the water object's (0x0073EAB0, 0x0073D4D0), called
//   0x0074B5E0  CollectFloraTypes  (before a kind 1 layer)
//   0x00748040  AddPendingRect     (after a kind 7 layer)
//
// All from the disassembly, statement by statement. A brush record's
// eleventh kind (11) is accepted by the loader and does nothing here, as in
// the game (the jump table at 0x0074DB64 has eleven entries).
//
// How the group reaches every caller. 0x0074D730 has one direct call site,
// slot 57's (0x0074F140; ours in terrain_brush.cpp calls it at its address
// too): a plain thiscall with two stack arguments whose caller goes on with
// ESI, EBX and EDI only: a detour at its entry. 0x00740BB0 is also called by
// the wave producers' counter (0x007425BB in 0x00742590, the game's, which
// loads everything anew from its stack afterwards): a detour too. The
// walkers, the corner function, the per-vertex routines, 0x007479B0,
// 0x00748040 and 0x0074B5E0 are reached from 0x0074D730 only (no other
// direct call, and their addresses occur nowhere else in the image but in
// 0x0074D730's own instructions): plain functions here. Our per-vertex
// routines are handed to the walkers as the game hands its own: a function
// and an adjustment of `this`; kind 9's comes from the water object and is
// called the same way.
//
// Still the game's and called at their addresses: the erosion pass
// 0x00746AD0 (ours in simulators.terrain.erode), the list's push_back
// 0x00747FF0 (a container helper the map window's code calls too), the
// water object's two functions, the map helpers 0x00497520 and 0x004FB6C0,
// the vector's resize 0x006BDF10, the game's memmove, the random generator's
// draws, and every virtual call (the resource manager, the picture, the
// flora simulator, the effects manager, the view's decals, the sound
// service, the city, the terrain's own slots 9, 13 and 50).
//
// Floating point. As in the other terrain files: what the game keeps on the
// x87 stack (53-bit precision) is a double here, a float exactly where the
// game stores one. Three transcendental instructions are in this code; each
// is executed by a small assembly function below on the game's operands,
// up to the game's first store, so that no value passes through a rounding
// the game does not make:
//   FPATAN (0x0074D84D): the brush's direction; the two differences, the
//     instruction, the addition of the float 0x00AB3CA8 and the store as a
//     float are all in X87Direction.
//   FCOS and FSIN in 0x00742180: both stored as floats at once (X87SinCos).
//   FCOS and FSIN in 0x00740AB0: the cosine is stored as a float at once;
//     the sine stays on the x87 stack at full width and is multiplied by
//     eight floats (the corners' coordinates), each product being rounded by
//     the multiplication itself. X87SinProducts does the cosine's store and
//     those eight multiplications on the unrounded sine and hands the
//     products out as doubles (they are 53-bit values already); everything
//     after is additions and float stores in C++.
// The two FISTP (0x0074D9CF, 0x00747A03) are executed as such (X87Fistp).
//
// Random numbers, all from the terrain's generator 0x00B4BC78 through the
// game's functions and in its order: per application of a rotating brush
// one draw below 5 (0x0074D7CF); per vertex of a kind 1 layer whose picture
// value is 10 or more one draw below 100, and when that is 30 or less and
// the vertex is dry and inside the cells two uniform draws (the place) and
// one draw below the flora type count (its result is not used: the game's);
// per vertex of a kind 3 layer a uniform draw for the turn (when the layer
// has one) and two for the place; per vertex of a kind 4 layer two for the
// place.
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

namespace field {
constexpr std::size_t kNeighbourOffsets = 0x84; // int32[8]: as in terrain_brush.cpp
constexpr std::size_t kDecals = 0xF8;           // the view's object for decals on the land (Init); kind 3 calls its slot 5
constexpr std::size_t kEffects = 0xFC;          // the city's effects manager (city slot 85); kind 4 calls its slot 6
constexpr std::size_t kFlora = 0x104;           // the city's flora simulator (city slot 59); kind 1 calls its slots 7, 9 and 17
constexpr std::size_t kFloraTypeCount = 0x108;  // uint32: the flora types a brush may plant (0: not gathered yet)
constexpr std::size_t kFloraTypes = 0x10C;      // vector<uint32_t> of those types
constexpr std::size_t kPendingRects = 0x1B0;    // std::list of 0x18-byte records (rect, two dwords): kind 7 adds one
constexpr std::size_t kScrubbed = 0x1D4;        // std::map<vertex index, count>: as in terrain_edit.cpp
} // namespace field

namespace flag {
constexpr uint8_t kMarkedHigh = 0x04; // 0x0400 of the flags word (its high byte): kind 7 sets it
} // namespace flag

namespace slot {
constexpr std::size_t kGetAltitude = 0x34;      // slot 13 (x, z): the unrounded altitude in ST0
constexpr std::size_t kSetAltitudeIndex = 0xC8; // slot 50 (index, altitude)
} // namespace slot

// tBrushParameters (0x38 bytes) as the body reads it.
namespace parameters {
constexpr std::size_t kFromX = 0x0;      // float: where the stroke came from
constexpr std::size_t kFromZ = 0x4;
constexpr std::size_t kX = 0x8;          // float: where the brush is applied
constexpr std::size_t kZ = 0xC;
constexpr std::size_t kStep = 0x10;      // int32: the index into the layers' arrays; below 0: the terrain's +0xBC
constexpr std::size_t kStrength = 0x18;  // float: multiplies each layer's strength
constexpr std::size_t kRadiusScale = 0x1C; // float: multiplies each layer's width
constexpr std::size_t kApplied = 0x24;   // int32[4]: joined with each layer's rectangle
constexpr std::size_t kAltitude = 0x34;  // float: the target of kinds 5 and 8
} // namespace parameters

// A brush record (the value of a node of the map 0x00B4BD58, at node + 0x14).
// Filled by the loader 0x0074FC70 (0x00747EC0, 0x0074F4A0, 0x00747DE0).
namespace brush {
constexpr std::size_t kTurns = 0x4;      // uint8: the brush turns at random with each application
constexpr std::size_t kTurnStep = 0x8;   // float: by this angle
constexpr std::size_t kLayers = 0x18;    // vector of layers: begin
constexpr std::size_t kLayersEnd = 0x1C;
constexpr std::size_t kTurn = 0x68;      // float: the angle now
constexpr std::size_t kDirection = 0x6C; // float: the stroke's direction + pi / 2
} // namespace brush

// A layer of a brush record (0x4C bytes).
namespace layer {
constexpr std::size_t kPictureID = 0x0;  // uint32: instance of the picture (type 0x7AB50E44, group 0x891B0E1A); 0: none
constexpr std::size_t kKind = 0x4;       // uint32: 0 to 11
constexpr std::size_t kPicture = 0x8;    // the picture's resource, fetched at the first use
constexpr std::size_t kFollows = 0xC;    // uint8: turned by the stroke's direction instead of the brush's turn
constexpr std::size_t kWidths = 0x10;    // vector<float>: the width in metres per step
constexpr std::size_t kStrengths = 0x1C; // vector<float>: the strength per step
constexpr std::size_t kNeutral = 0x28;   // uint8: the picture value that changes nothing (kinds 0, 4, 8)
constexpr std::size_t kMaxCount = 0x2C;  // uint32: kinds 3 and 4 act on a vertex while its scrub count is not above this
constexpr std::size_t kTextureID = 0x30; // uint32: kind 3's decal (type 0x7AB50E44, group 0x891B0E1A)
constexpr std::size_t kMaxTurn = 0x34;   // float: kind 3 turns its decal by a random angle below this
constexpr std::size_t kEffect = 0x38;    // uint32[2]: kind 4's effect, the two arguments of the effects manager's slot 6
constexpr uint32_t kSize = 0x4C;
} // namespace layer

// A picture as the walker reads it (what slot 3 of the resource answers).
namespace picture {
constexpr std::size_t kWidth = 0x8;
constexpr std::size_t kHeight = 0xC;
constexpr std::size_t kRows = 0x1C; // a pointer to the pointer to the bytes, one per point, rows first
} // namespace picture

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

template <typename R, typename... Args>
inline R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

inline float F(uint32_t address) { return AtAddress<float>(address); }

inline uint32_t U32(void* self, std::size_t offset) { return At<uint32_t>(self, offset); }

inline float Sea(char* self) { return At<float>(self, field::kSeaLevel); }

inline uint32_t Offset(uint32_t index) { return index * 9u * 4u; }

inline bool Locked(uint32_t record) { return (AtAddress<uint8_t>(record + 0x23) & 0x40) != 0; }

inline void* Random() { return reinterpret_cast<void*>(addr::cSTETerrain_Random); }

inline uint32_t Below(uint32_t count) { return This<uint32_t>(addr::cRZRandom_Below, Random(), count); }

// low + a unit draw * (high - low), as the game leaves it in ST0.
inline double Uniform(double low, double high) { return This<double>(addr::cRZRandom_Uniform, Random(), low, high); }

// __ftol2 (0x009EEF04): truncation toward zero to 64 bits; the x87 "integer
// indefinite" for what does not fit and for a NaN. Callers take the low
// dword.
int64_t Ftol64(double value)
{
    if (!(value > -9.2233720368547758e18 && value < 9.2233720368547758e18))
        return INT64_MIN;
    return static_cast<int64_t>(value);
}

inline int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<uint32_t>(Ftol64(value))); }

// --- The x87 instructions C++ cannot repeat ----------------------------------

// FLD angle, FLD ST(0), FCOS, FSTP float, FSIN, FSTP float: 0x0074220D to
// 0x0074221E.
__declspec(naked) void __cdecl X87SinCos(float /*angle*/, float* /*cosine*/, float* /*sine*/)
{
    __asm fld dword ptr [esp + 4]
    __asm fld st(0)
    __asm fcos
    __asm mov eax, [esp + 8]
    __asm fstp dword ptr [eax]
    __asm fsin
    __asm mov eax, [esp + 12]
    __asm fstp dword ptr [eax]
    __asm ret
}

// 0x00740AB0's use of the two: the cosine stored as a float; the sine kept
// on the stack and multiplied by each of eight floats (FLD ST(0), FMUL
// float), every product stored as a double (it is rounded to 53 bits by the
// multiplication, as the game's is).
__declspec(naked) void __cdecl X87SinProducts(float /*angle*/, const float* /*factors*/, double* /*products*/,
                                              float* /*cosine*/)
{
    __asm fld dword ptr [esp + 4]
    __asm fld st(0)
    __asm fcos
    __asm mov eax, [esp + 16]
    __asm fstp dword ptr [eax]
    __asm fsin
    __asm mov eax, [esp + 8]
    __asm mov ecx, [esp + 12]
    __asm fld st(0)
    __asm fmul dword ptr [eax]
    __asm fstp qword ptr [ecx]
    __asm fld st(0)
    __asm fmul dword ptr [eax + 4]
    __asm fstp qword ptr [ecx + 8]
    __asm fld st(0)
    __asm fmul dword ptr [eax + 8]
    __asm fstp qword ptr [ecx + 16]
    __asm fld st(0)
    __asm fmul dword ptr [eax + 12]
    __asm fstp qword ptr [ecx + 24]
    __asm fld st(0)
    __asm fmul dword ptr [eax + 16]
    __asm fstp qword ptr [ecx + 32]
    __asm fld st(0)
    __asm fmul dword ptr [eax + 20]
    __asm fstp qword ptr [ecx + 40]
    __asm fld st(0)
    __asm fmul dword ptr [eax + 24]
    __asm fstp qword ptr [ecx + 48]
    __asm fld st(0)
    __asm fmul dword ptr [eax + 28]
    __asm fstp qword ptr [ecx + 56]
    __asm fstp st(0)
    __asm ret
}

// 0x0074D842 to 0x0074D855: FLD z, FSUB fromZ, FLD x, FSUB fromX, FPATAN,
// FADD float, FSTP float.
__declspec(naked) void __cdecl X87Direction(float /*z*/, float /*fromZ*/, float /*x*/, float /*fromX*/, float /*add*/,
                                            float* /*out*/)
{
    __asm fld dword ptr [esp + 4]
    __asm fsub dword ptr [esp + 8]
    __asm fld dword ptr [esp + 12]
    __asm fsub dword ptr [esp + 16]
    __asm fpatan
    __asm fadd dword ptr [esp + 20]
    __asm mov eax, [esp + 24]
    __asm fstp dword ptr [eax]
    __asm ret
}

// FLD float, FISTP dword (the rounding of the control word in force).
__declspec(naked) int32_t __cdecl X87Fistp(float /*value*/)
{
    __asm fld dword ptr [esp + 4]
    __asm fistp dword ptr [esp + 4]
    __asm mov eax, [esp + 4]
    __asm ret
}

// --- What a walker hands a per-vertex routine --------------------------------

// The block 0x0074D730 keeps on its stack for a layer (0x2C bytes); the
// water object's kind 9 gets it too, so the layout is the game's.
struct Context {
    float x, z;         // the parameters' +0x8, +0xC
    float fromX, fromZ; // the parameters' +0x0, +0x4
    uint32_t step;      // the index into the layers' arrays
    uint8_t value;      // the picture's byte at the vertex (0x00742180 sets it)
    uint8_t unused[3];
    float strength;     // the layer's strength at the step x the parameters' strength
    float altitude;     // the parameters' +0x34
    char* layer;
    char* params;
    char* record;
};
static_assert(sizeof(Context) == 0x2C);
static_assert(offsetof(Context, value) == 0x14);
static_assert(offsetof(Context, layer) == 0x20);

// A per-vertex routine: thiscall on (the terrain + an adjustment) with the
// vertex index and the block; the walkers test AL.
using PerVertex = uint32_t(__thiscall*)(void*, uint32_t, Context*);

inline bool Visit(uint32_t function, void* self, uint32_t index, Context* context)
{
    return (reinterpret_cast<PerVertex>(static_cast<std::uintptr_t>(function))(self, index, context) & 0xFFu) != 0;
}

// --- Raising and lowering one vertex -----------------------------------------

inline void WaterFromSea(char* self, uint32_t wet, uint32_t record)
{
    AtAddress<float>(wet) = static_cast<float>(static_cast<double>(Sea(self)) - static_cast<double>(F(record)));
}

// 0x00744AF0 (index, amount): an unlocked vertex lowered, not below the
// minimum allowed height (the clamp tests the difference before it is
// rounded); the water depth becomes sea level - altitude.
void LowerVertex(char* self, uint32_t index, float amount)
{
    const uint32_t offset = Offset(index);
    const uint32_t record = VertexBase(self) + offset;
    if (Locked(record))
        return;
    const double lowered = static_cast<double>(F(record)) - static_cast<double>(amount);
    const uint32_t wet = WaterBase(self) + offset;
    AtAddress<float>(record) = static_cast<float>(lowered);
    if (lowered < static_cast<double>(Global<float>(addr::cSTETerrain_MinAllowedHeightValue)))
        AtAddress<uint32_t>(record) = Global<uint32_t>(addr::cSTETerrain_MinAllowedHeightValue);
    WaterFromSea(self, wet, record);
}

// 0x00744CA0 (index, amount): raised, not above the maximum allowed height.
void RaiseVertex(char* self, uint32_t index, float amount)
{
    const uint32_t offset = Offset(index);
    const uint32_t record = VertexBase(self) + offset;
    if (Locked(record))
        return;
    const double raised = static_cast<double>(amount) + static_cast<double>(F(record));
    const uint32_t wet = WaterBase(self) + offset;
    AtAddress<float>(record) = static_cast<float>(raised);
    if (raised > static_cast<double>(Global<float>(addr::cSTETerrain_MaxAllowedHeightValue)))
        AtAddress<uint32_t>(record) = Global<uint32_t>(addr::cSTETerrain_MaxAllowedHeightValue);
    WaterFromSea(self, wet, record);
}

inline void SetAltitude(char* self, uint32_t index, float altitude)
{
    Virtual<void>(self, slot::kSetAltitudeIndex, index, altitude);
}

// The neighbours a vertex at (x, z) has inside the grid, one bit each in the
// order of the +0x84 table (inlined in 0x00745DE0 as in 0x00745020).
inline uint8_t NeighbourBits(char* self, uint32_t x, uint32_t z)
{
    uint8_t bits = 0xFF;
    if (static_cast<int32_t>(x) < 1)
        bits = 0x7C;
    if (x >= U32(self, field::kCellsX))
        bits &= 0xC7;
    if (static_cast<int32_t>(z) < 1)
        bits &= 0xF1;
    if (z >= U32(self, field::kCellsZ))
        bits &= 0x1F;
    return bits;
}

// --- The per-vertex routines -------------------------------------------------

// Kind 0 (0x00745C60): nothing for the layer's neutral value; above it the
// vertex is raised by (value - neutral) x strength, below it lowered by
// (neutral - value) x strength (rounded to float).
uint32_t __fastcall RaiseLower(char* self, void*, uint32_t index, Context* context)
{
    const uint8_t neutral = At<uint8_t>(context->layer, layer::kNeutral);
    const uint8_t value = context->value;
    if (value == neutral)
        return 0;
    if (value > neutral) {
        const float amount = static_cast<float>(static_cast<double>(static_cast<int32_t>(value) - static_cast<int32_t>(neutral)) *
                                                static_cast<double>(context->strength));
        RaiseVertex(self, index, amount);
    } else {
        const float amount = static_cast<float>(static_cast<double>(static_cast<int32_t>(neutral) - static_cast<int32_t>(value)) *
                                                static_cast<double>(context->strength));
        LowerVertex(self, index, amount);
    }
    return 1;
}

// Kind 1 (0x00745A40): for a picture value of 10 or more and a draw below
// 100 of 30 or less. Under water (depth above 0) the sound service gets
// (1, 0xAA62C9D5, 0, 0, 0) through its slot 11. Else, for a vertex that is
// the corner of a cell: a place drawn inside that cell (x, then z), one more
// draw below the flora type count whose result the game does not use, the
// altitude there (slot 13, rounded to float), and the flora simulator's slot
// 17 with (0, the place, 0); then, when the city's slot 22 answers, the
// service 0x00B43D60's slot 6 with (x, z, 1). Answers 0 always, so the layer
// adds nothing to the applied rectangle.
uint32_t __fastcall PlantFlora(char* self, void*, uint32_t index, Context* context)
{
    if (context->value < 10)
        return 0;
    if (Below(100u) > 0x1Eu)
        return 0;
    const float depth = F(WaterBase(self) + Offset(index));
    if (depth > Global<float>(addr::SC4_FloatZero)) {
        void* const sound = Global<void*>(addr::SC4_Sound43CB8);
        if (!sound)
            return 0;
        Virtual<void>(sound, 0x2C, 1u, 0xAA62C9D5u, 0u, 0u, 0u);
        return 0;
    }
    const uint32_t x = index % U32(self, field::kVertsX);
    const uint32_t z = index / U32(self, field::kVertsX);
    if (x >= U32(self, field::kCellsX))
        return 0;
    if (z >= U32(self, field::kCellsZ))
        return 0;
    const double width = static_cast<double>(Global<float>(addr::cSTETerrain_CellWidthFloat));
    const double lowX = static_cast<double>(static_cast<int32_t>(x)) * width;
    const float lowZ = static_cast<float>(static_cast<double>(static_cast<int32_t>(z)) * width);
    const double highX = width + lowX;
    float place[3];
    place[0] = static_cast<float>(Uniform(lowX, highX));
    const double highZ = static_cast<double>(lowZ) + width;
    place[2] = static_cast<float>(Uniform(static_cast<double>(lowZ), highZ));
    Below(U32(self, field::kFloraTypeCount)); // drawn and dropped (0x00745B3C)
    place[1] = static_cast<float>(Virtual<double>(self, slot::kGetAltitude, place[0], place[2]));
    Virtual<void>(At<void*>(self, field::kFlora), 0x44, 0u, static_cast<void*>(place), 0u);
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x58))
        Virtual<void>(Global<void*>(addr::SC4_Service43D60), 0x18, place[0], place[2], 1u);
    return 0;
}

// Kind 2 (0x00745DE0): nothing for a picture value of 0. The amount is
// value x strength / 256 (two multiplications, rounded to float). For each
// neighbour inside the grid, in the +0x84 table's order: where the vertex is
// higher than the neighbour by more than the limit (0x00B4BCF4 straight,
// 0x00B4BCF0 diagonal), the excess x the amount (not rounded) is taken from
// the vertex and added to the neighbour, each unless locked, with the clamps
// and water depths of 0x00744AF0 and 0x00744CA0. The changed altitudes are
// used at once. Answers 1 when any neighbour was lower by more than the
// limit (locked or not).
uint32_t __fastcall SmoothVertex(char* self, void*, uint32_t index, Context* context)
{
    const uint8_t value = context->value;
    if (value == 0)
        return 0;
    const double weighted = static_cast<double>(static_cast<int32_t>(value)) * static_cast<double>(context->strength);
    const float amount = static_cast<float>(weighted * static_cast<double>(Global<float>(addr::cSTETerrain_PictureScale)));
    const uint32_t stride = U32(self, field::kVertsX);
    const uint8_t bits = NeighbourBits(self, index % stride, index / stride);
    const uint32_t offset = Offset(index);
    uint32_t changed = 0;
    for (uint32_t k = 0; k < 8; ++k) {
        if (!(bits & (1u << k)))
            continue;
        const uint32_t other = static_cast<uint32_t>(At<int32_t>(self, field::kNeighbourOffsets + 4 * k)) + index;
        const uint32_t otherOffset = Offset(other);
        const uint32_t base = VertexBase(self);
        const double difference = static_cast<double>(F(base + offset)) - static_cast<double>(F(base + otherOffset));
        const double excess = difference - static_cast<double>((k & 1u) ? Global<float>(addr::cSTETerrain_SmoothDiagonalLimit)
                                                                        : Global<float>(addr::cSTETerrain_SmoothStraightLimit));
        if (!(excess > static_cast<double>(Global<float>(addr::SC4_FloatZero))))
            continue;
        const double moved = excess * static_cast<double>(amount);
        const uint32_t record = base + offset;
        if (!Locked(record)) {
            const double lowered = static_cast<double>(F(record)) - moved;
            const uint32_t wet = WaterBase(self) + offset;
            AtAddress<float>(record) = static_cast<float>(lowered);
            if (lowered < static_cast<double>(Global<float>(addr::cSTETerrain_MinAllowedHeightValue)))
                AtAddress<uint32_t>(record) = Global<uint32_t>(addr::cSTETerrain_MinAllowedHeightValue);
            WaterFromSea(self, wet, record);
        }
        const uint32_t otherRecord = VertexBase(self) + otherOffset;
        if (!Locked(otherRecord)) {
            const double raised = moved + static_cast<double>(F(otherRecord));
            const uint32_t wet = WaterBase(self) + otherOffset;
            AtAddress<float>(otherRecord) = static_cast<float>(raised);
            if (raised > static_cast<double>(Global<float>(addr::cSTETerrain_MaxAllowedHeightValue)))
                AtAddress<uint32_t>(otherRecord) = Global<uint32_t>(addr::cSTETerrain_MaxAllowedHeightValue);
            WaterFromSea(self, wet, otherRecord);
        }
        changed = 1;
    }
    return changed;
}

// The scrub count of a vertex (the map +0x1D4), made 0 when the vertex is
// not in the map yet: 0x00497520, then 0x004FB6C0 with (index, 0).
uint32_t* ScrubCount(char* self, uint32_t index)
{
    void* const map = self + field::kScrubbed;
    uint32_t key = index;
    char* node = nullptr;
    This<void>(addr::SC4Uint32Map_Find, map, &node, &key);
    if (node == At<char*>(self, field::kScrubbed)) {
        uint32_t pair[2] = {index, 0};
        uint32_t result[4] = {0, 0, 0, 0}; // the node and whether it was inserted
        This<void*>(addr::SC4FloraMap_Insert, map, static_cast<void*>(result), static_cast<void*>(pair));
        node = reinterpret_cast<char*>(static_cast<std::uintptr_t>(result[0]));
    }
    return &At<uint32_t>(node, 0x14);
}

// The cell a vertex is the first corner of, as kinds 3 and 4 draw a place in
// it: x between x * 16 and x * 16 + 16 (not beyond the terrain's size), z
// between the same of z (both ends rounded to float). Two uniform draws,
// x first.
inline void PlaceInCell(char* self, uint32_t x, uint32_t z, float& placeX, float& placeZ)
{
    const double width = static_cast<double>(Global<float>(addr::cSTETerrain_CellWidthFloat));
    const double lowX = static_cast<double>(static_cast<int32_t>(x)) * width;
    const float lowZ = static_cast<float>(static_cast<double>(static_cast<int32_t>(z)) * width);
    double highX = width + lowX;
    float highZ = static_cast<float>(static_cast<double>(lowZ) + width);
    if (highX > static_cast<double>(At<float>(self, field::kSizeX)))
        highX = static_cast<double>(At<float>(self, field::kSizeX));
    if (highZ > At<float>(self, field::kSizeZ))
        highZ = At<float>(self, field::kSizeZ);
    placeX = static_cast<float>(Uniform(lowX, highX));
    placeZ = static_cast<float>(Uniform(static_cast<double>(lowZ), static_cast<double>(highZ)));
}

// Kind 3 (0x00749130): with the view's decal object, a layer and a picture
// value other than 0, for a vertex that is the corner of a cell and whose
// scrub count is not above the layer's limit: the count goes up by one; a
// turn is drawn between 0 and the layer's angle (when that is above 0; else
// 0); a place is drawn in the cell; the decal object's slot 5 gets the
// texture's key (type 0x7AB50E44, group 0x891B0E1A, the layer's instance),
// the place (x, z), the strength and the turn. Answers 0 always.
uint32_t __fastcall SplatTexture(char* self, void*, uint32_t index, Context* context)
{
    if (!At<void*>(self, field::kDecals))
        return 0;
    if (!context->layer)
        return 0;
    if (context->value == 0)
        return 0;
    const uint32_t x = index % U32(self, field::kVertsX);
    const uint32_t z = index / U32(self, field::kVertsX);
    if (x >= U32(self, field::kCellsX))
        return 0;
    if (z >= U32(self, field::kCellsZ))
        return 0;
    uint32_t* const count = ScrubCount(self, U32(self, field::kVertsX) * z + x);
    if (*count > At<uint32_t>(context->layer, layer::kMaxCount))
        return 0;
    *count = *count + 1u;
    // The place and, right behind it, the key, as on the game's stack.
    struct {
        float place[2];
        uint32_t key[3];
    } what = {{0.0f, 0.0f}, {0x7AB50E44u, 0x891B0E1Au, At<uint32_t>(context->layer, layer::kTextureID)}};
    float turn = 0.0f;
    if (At<float>(context->layer, layer::kMaxTurn) > Global<float>(addr::SC4_FloatZero))
        turn = static_cast<float>(
            Uniform(Global<double>(addr::SC4_DoubleZero), static_cast<double>(At<float>(context->layer, layer::kMaxTurn))));
    PlaceInCell(self, x, z, what.place[0], what.place[1]);
    Virtual<void>(At<void*>(self, field::kDecals), 0x14, static_cast<void*>(what.key), static_cast<void*>(what.place),
                  context->strength, turn);
    return 0;
}

// What kind 4 hands the effect (0x38 bytes on the game's stack): two bytes,
// a 3 x 3 unit matrix, a position, 1.0. The two bytes behind the first two
// are not written by the game; zero here.
struct EffectPlace {
    uint8_t count;
    uint8_t flags;
    uint8_t unwritten[2];
    float matrix[9];
    float position[3];
    float scale;
    uint32_t spare[2]; // not part of the game's block; room for a reader that looks further
};
static_assert(offsetof(EffectPlace, position) == 0x28);

// Kind 4 (0x00749310): with a layer and a picture value other than the
// layer's neutral one, for a vertex whose scrub count is not above the
// layer's limit (no test that it is the corner of a cell: the game's): the
// effects manager's slot 6 makes the layer's effect; when it does, the count
// goes up by one, a place is drawn in the cell, its height is the altitude
// there (slot 13) or the sea level when that is higher, and the effect gets
// the place (slot 8), is started (slot 3 with 0) and released. Answers 0
// always.
uint32_t __fastcall StartEffect(char* self, void*, uint32_t index, Context* context)
{
    char* const layer = context->layer;
    if (!layer)
        return 0;
    if (context->value == At<uint8_t>(layer, layer::kNeutral))
        return 0;
    const uint32_t x = index % U32(self, field::kVertsX);
    const uint32_t z = index / U32(self, field::kVertsX);
    uint32_t* const count = ScrubCount(self, U32(self, field::kVertsX) * z + x);
    if (*count > At<uint32_t>(context->layer, layer::kMaxCount))
        return 0;
    // The game hands the stack slot of its index argument as the out pointer.
    void* effect = reinterpret_cast<void*>(static_cast<std::uintptr_t>(index));
    char* const now = context->layer;
    if (!Virtual<uint8_t>(At<void*>(self, field::kEffects), 0x18, At<uint32_t>(now, layer::kEffect),
                          At<uint32_t>(now, layer::kEffect + 4), &effect))
        return 0;
    *count = *count + 1u;
    EffectPlace where{};
    where.matrix[0] = 1.0f;
    where.matrix[4] = 1.0f;
    where.matrix[8] = 1.0f;
    where.scale = 1.0f;
    float placeX = 0.0f, placeZ = 0.0f;
    PlaceInCell(self, x, z, placeX, placeZ);
    const double altitude = Virtual<double>(self, slot::kGetAltitude, placeX, placeZ);
    where.position[1] = altitude < static_cast<double>(Sea(self)) ? Sea(self) : static_cast<float>(altitude);
    where.position[2] = placeZ;
    where.flags |= 1;
    where.position[0] = placeX;
    where.count = static_cast<uint8_t>(where.count + 1);
    Virtual<void>(effect, 0x20, static_cast<void*>(&where));
    Virtual<void>(effect, 0xC, 0u);
    Virtual<void>(effect, 0x8);
    return 0;
}

// Kind 5 (0x00745BA0): nothing for a picture value of 0. The step is
// value x strength / 256 (rounded to float). A vertex above the parameters'
// altitude is lowered by the step, or set to the altitude (slot 50) when it
// is no more than the step above; one below is raised by the step, or set
// when it is no more than the step below; one at the altitude (or a NaN)
// answers 0. The comparisons use the unrounded difference.
uint32_t __fastcall LevelVertex(char* self, void*, uint32_t index, Context* context)
{
    const uint8_t value = context->value;
    if (value == 0)
        return 0;
    const double weighted = static_cast<double>(static_cast<int32_t>(value)) * static_cast<double>(context->strength);
    const float step = static_cast<float>(weighted * static_cast<double>(Global<float>(addr::cSTETerrain_PictureScale)));
    const double above =
        static_cast<double>(F(VertexBase(self) + Offset(index))) - static_cast<double>(context->altitude);
    const double zero = static_cast<double>(Global<float>(addr::SC4_FloatZero));
    if (above > zero) {
        if (above <= static_cast<double>(step))
            SetAltitude(self, index, context->altitude);
        else
            LowerVertex(self, index, step); // also for a NaN step, as the game's test goes
        return 1;
    }
    if (!(above < zero))
        return 0;
    if (above >= -static_cast<double>(step))
        SetAltitude(self, index, context->altitude);
    else
        RaiseVertex(self, index, step);
    return 1;
}

// Kind 7 (0x00740820): nothing for a picture value of 0; else the vertex
// gets the mark 0x0400.
uint32_t __fastcall MarkVertex(char* self, void*, uint32_t index, Context* context)
{
    if (context->value == 0)
        return 0;
    AtAddress<uint8_t>(VertexBase(self) + Offset(index) + 0x23) |= flag::kMarkedHigh;
    return 1;
}

// Kind 8 (0x00745CD0): the height is (value - neutral) x the layer's own
// strength at the step (the parameters' strength is not in it); nothing for
// 0. The target is the parameters' altitude + the height (rounded to
// float), the step |height| / 16 (rounded to float). A vertex below the
// target that is not under water (depth 0 or less) is raised by the step, or
// set to the target (slot 50) when it is less than the step below; one below
// the target under water answers 0; one above is lowered by the step, or set
// when it is less than the step above; one at the target answers 0.
uint32_t __fastcall ShapeVertex(char* self, void*, uint32_t index, Context* context)
{
    char* const layer = context->layer;
    const int32_t steps = static_cast<int32_t>(context->value) - static_cast<int32_t>(At<uint8_t>(layer, layer::kNeutral));
    const double height = static_cast<double>(steps) *
                          static_cast<double>(F(At<uint32_t>(layer, layer::kStrengths) + 4u * context->step));
    const double zero = static_cast<double>(Global<float>(addr::SC4_FloatZero));
    if (height == zero)
        return 0;
    const float target = static_cast<float>(height + static_cast<double>(context->altitude));
    const float step = static_cast<float>(std::fabs(height * static_cast<double>(Global<float>(addr::SC4_FloatSixteenth))));
    const uint32_t offset = Offset(index);
    const double below = static_cast<double>(target) - static_cast<double>(F(VertexBase(self) + offset));
    if (below > zero) {
        const float depth = F(WaterBase(self) + offset);
        if (depth < Global<float>(addr::SC4_FloatZero) || depth == Global<float>(addr::SC4_FloatZero)) {
            if (below < static_cast<double>(step))
                SetAltitude(self, index, target);
            else
                RaiseVertex(self, index, step);
            return 1;
        }
        return 0;
    }
    if (!(below < zero))
        return 0;
    if (-below < static_cast<double>(step))
        SetAltitude(self, index, target);
    else
        LowerVertex(self, index, step);
    return 1;
}

// --- The turned rectangle ----------------------------------------------------

// 0x00740AB0 (out, x, z, width, height, angle; cdecl): the corners of a
// rectangle of that size around (x, z), turned by the angle: half sizes are
// the sizes x -0.5 (0x00A949FC), rounded to float; the corners (hw, hh),
// (hw, -hh), (-hw, -hh), (-hw, hh) become
//   x' = cos x cx - sin x cy + x,  z' = sin x cx + cos x cy + z
// with the cosine a float and the sine at the x87's full width (see
// X87SinProducts), each sum rounded to float at the end.
void QuadCorners(float* out, float x, float z, float width, float height, float angle)
{
    const double half = static_cast<double>(Global<float>(addr::SC4_FloatMinusHalf));
    const double halfWidth = static_cast<double>(width) * half;
    const float hw = static_cast<float>(halfWidth);
    const double halfHeight = static_cast<double>(height) * half;
    const float hh = static_cast<float>(halfHeight);
    const float nhh = static_cast<float>(-halfHeight);
    const float corners[8] = {hw, hh, hw, nhh, -hw, nhh, -hw, hh};
    double sine[8];
    float cosine = 0.0f;
    X87SinProducts(angle, corners, sine, &cosine);
    for (uint32_t i = 0; i < 4; ++i) {
        const double sum = sine[2 * i] + static_cast<double>(cosine) * static_cast<double>(corners[2 * i + 1]);
        const double difference = static_cast<double>(cosine) * static_cast<double>(corners[2 * i]) - sine[2 * i + 1];
        out[2 * i] = static_cast<float>(difference + static_cast<double>(x));
        out[2 * i + 1] = static_cast<float>(sum + static_cast<double>(z));
    }
}

// Rows of vertices a walker keeps on its stack: one first and one last
// column per row. The game's arrays hold 257 rows (0x404 bytes each, at
// 0x00742277 / 0x0074227F and 0x00742499 / 0x0074249E): a city of more than
// 256 cells along z would overrun them there, and here.
constexpr uint32_t kMaxRows = 257;

// 0x00740BB0 (corners, first columns, last columns, first row, last row):
// the rows the quadrilateral covers and the columns in each. The rows: the
// corners' z / 16 truncated, the least not below 0, the greatest not above
// the cell count (unsigned, so a negative greatest becomes the cell count).
// For each row, at z = row x 16: each edge (corner i to corner i + 1) whose
// z range holds it (the lower end included, the upper not) gives a column:
// x of the edge there truncated, x 1/16, truncated again (an edge with equal
// x at both ends: that x / 16 truncated); the first column is the least of
// them, not below 0; the last the greatest + 1, not above the cell count
// (unsigned). A row no edge crosses keeps 0x7FFFFFFF as its first column.
void __fastcall QuadRows(char* self, void*, const float* corners, int32_t* first, int32_t* last, int32_t* firstRow,
                         int32_t* lastRow)
{
    static bool logged = false;
    FirstCall("QuadRows", logged);
    *firstRow = 0x7FFFFFFF;
    *lastRow = INT32_MIN;
    for (uint32_t i = 0; i < 4; ++i) {
        const int32_t row = Ftol(static_cast<double>(InverseCellWidth()) * static_cast<double>(corners[2 * i + 1]));
        if (row > *lastRow)
            *lastRow = row;
        if (row < *firstRow)
            *firstRow = row;
    }
    if (*firstRow < 0)
        *firstRow = 0;
    if (static_cast<uint32_t>(*lastRow) > U32(self, field::kCellsZ))
        *lastRow = static_cast<int32_t>(U32(self, field::kCellsZ));
    int32_t row = *firstRow;
    if (row > *lastRow)
        return;
    do {
        first[row] = 0x7FFFFFFF;
        last[row] = INT32_MIN;
        const double z = static_cast<double>(row) * static_cast<double>(Global<float>(addr::cSTETerrain_CellWidthFloat));
        for (uint32_t i = 0; i < 4; ++i) {
            const uint32_t j = (i + 1u) & 3u;
            const double zi = static_cast<double>(corners[2 * i + 1]);
            const double zj = static_cast<double>(corners[2 * j + 1]);
            bool crosses = false;
            if (z >= zi && z < zj)
                crosses = true;
            else if (z >= zj && z < zi)
                crosses = true;
            if (!crosses)
                continue;
            double scaled;
            if (corners[2 * j] == corners[2 * i]) {
                scaled = static_cast<double>(InverseCellWidth()) * static_cast<double>(corners[2 * i]);
            } else {
                const double run = static_cast<double>(corners[2 * j]) - static_cast<double>(corners[2 * i]);
                const double rise = zj - zi;
                const double slope = run / rise;
                const double along = z - zi;
                const double offset = slope * along;
                const double x = offset + static_cast<double>(corners[2 * i]);
                scaled = static_cast<double>(Ftol(x)) * static_cast<double>(InverseCellWidth());
            }
            const int32_t column = Ftol(scaled);
            if (column < first[row])
                first[row] = column;
            if (column > last[row])
                last[row] = column;
        }
        if (first[row] < 0)
            first[row] = 0;
        last[row] = static_cast<int32_t>(static_cast<uint32_t>(last[row]) + 1u);
        if (static_cast<uint32_t>(last[row]) > U32(self, field::kCellsX))
            last[row] = static_cast<int32_t>(U32(self, field::kCellsX));
        ++row;
    } while (row <= *lastRow);
}

inline void EmptyRect(int32_t* rect)
{
    rect[3] = INT32_MIN;
    rect[2] = INT32_MIN;
    rect[1] = 0x7FFFFFFF;
    rect[0] = 0x7FFFFFFF;
}

inline void JoinVertex(int32_t* rect, int32_t x, int32_t z)
{
    if (x < rect[0])
        rect[0] = x;
    if (x > rect[2])
        rect[2] = x;
    if (z < rect[1])
        rect[1] = z;
    if (z > rect[3])
        rect[3] = z;
}

// --- The walkers -------------------------------------------------------------

// 0x00742180 (picture, x, z, width, angle, routine, adjustment, block,
// rect): lays the picture, `width` metres wide and as high as its own shape
// makes it, turned by the angle, around the vertex (x, z) and calls the
// routine for every vertex of its rows (0x00740BB0) that falls on a point of
// the picture, with that point's byte in the block. The rectangle becomes
// the bounds of the vertices the routine answered for (empty for none).
//
// The picture's point for a vertex: with scale = 16 / (width / picture
// width) (rounded to float) and the vertex's offsets from (x, z) in
// vertices dx, dz:
//   u = (dz x scale) x sin + (dx x scale) x cos + picture width / 2
//   v = cos x (dz x scale) - (dx x scale) x sin + picture height / 2
// computed at the first vertex of each row and stored as floats; along the
// row cos x scale is added to u and -(sin x scale) to v, each sum rounded to
// float again (so the error builds up along a row exactly as the game's).
// Sine and cosine are floats here.
void WalkPicture(char* self, char* image, int32_t x, int32_t z, float width, float angle, uint32_t routine,
                 uint32_t adjust, Context* context, int32_t* rect)
{
    const uint32_t imageWidth = At<uint32_t>(image, picture::kWidth);
    const uint32_t imageHeight = At<uint32_t>(image, picture::kHeight);
    const double twoTo32 = static_cast<double>(Global<float>(addr::SC4_FloatTwoTo32));
    const double halfOf = static_cast<double>(Global<float>(addr::SC4_FloatHalf));
    const double cell = static_cast<double>(Global<float>(addr::cSTETerrain_CellWidthFloat));
    double across = static_cast<double>(static_cast<int32_t>(imageWidth));
    if (static_cast<int32_t>(imageWidth) < 0)
        across = across + twoTo32;
    const float halfAcross = static_cast<float>(halfOf * across);
    double down = static_cast<double>(static_cast<int32_t>(imageHeight));
    if (static_cast<int32_t>(imageHeight) < 0)
        down = down + twoTo32;
    const float downFloat = static_cast<float>(down);
    const uint32_t bytes = AtAddress<uint32_t>(At<uint32_t>(image, picture::kRows));
    const float halfDown = static_cast<float>(down * halfOf);
    const double perPoint = static_cast<double>(width) / across;
    const float scale = static_cast<float>(cell / perPoint);
    float cosine = 0.0f, sine = 0.0f;
    X87SinCos(angle, &cosine, &sine);
    const float centreX = static_cast<float>(static_cast<double>(x) * cell);
    const float centreZ = static_cast<float>(static_cast<double>(z) * cell);
    const float height = static_cast<float>(static_cast<double>(downFloat) * perPoint);
    float corners[8];
    QuadCorners(corners, centreX, centreZ, width, height, angle);
    int32_t first[kMaxRows], last[kMaxRows];
    int32_t firstRow = 0, lastRow = 0;
    QuadRows(self, nullptr, corners, first, last, &firstRow, &lastRow);
    const float stepU = static_cast<float>(static_cast<double>(cosine) * static_cast<double>(scale));
    EmptyRect(rect);
    const float stepV = static_cast<float>(-(static_cast<double>(sine) * static_cast<double>(scale)));
    int32_t row = firstRow;
    if (row > lastRow)
        return;
    int32_t rowOffset = static_cast<int32_t>(static_cast<uint32_t>(row) - static_cast<uint32_t>(z));
    do {
        int32_t column = first[row];
        uint32_t index = U32(self, field::kVertsX) * static_cast<uint32_t>(row) + static_cast<uint32_t>(column);
        const double dx =
            static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(column) - static_cast<uint32_t>(x))) *
            static_cast<double>(scale);
        const double dz = static_cast<double>(rowOffset) * static_cast<double>(scale);
        float v = static_cast<float>(static_cast<double>(cosine) * dz - dx * static_cast<double>(sine));
        float u = static_cast<float>((dz * static_cast<double>(sine) + dx * static_cast<double>(cosine)) +
                                     static_cast<double>(halfAcross));
        v = static_cast<float>(static_cast<double>(v) + static_cast<double>(halfDown));
        if (column <= last[row]) {
            do {
                const uint32_t pointX = static_cast<uint32_t>(Ftol(static_cast<double>(u)));
                const uint32_t pointY = static_cast<uint32_t>(Ftol(static_cast<double>(v)));
                if (pointX < imageWidth && pointY < imageHeight) {
                    context->value = AtAddress<uint8_t>(pointY * imageWidth + pointX + bytes);
                    if (Visit(routine, self + adjust, index, context))
                        JoinVertex(rect, column, row);
                }
                u = static_cast<float>(static_cast<double>(u) + static_cast<double>(stepU));
                ++column;
                ++index;
                v = static_cast<float>(static_cast<double>(v) + static_cast<double>(stepV));
            } while (column <= last[row]);
        }
        ++row;
        ++rowOffset;
    } while (row <= lastRow);
}

// 0x00742440 (x, z, width, angle, routine, adjustment, times, block, rect):
// the same for a layer without a picture: a square of `width` metres around
// the vertex (x, z), turned by the angle; the routine is called for every
// vertex of its rows, `times` times over, with the block as it is (its
// picture byte is not set here). The rectangle as above.
void WalkSquare(char* self, int32_t x, int32_t z, float width, float angle, uint32_t routine, uint32_t adjust,
                uint32_t times, Context* context, int32_t* rect)
{
    const double cell = static_cast<double>(Global<float>(addr::cSTETerrain_CellWidthFloat));
    const float centreZ = static_cast<float>(static_cast<double>(z) * cell);
    const float centreX = static_cast<float>(static_cast<double>(x) * cell);
    float corners[8];
    QuadCorners(corners, centreX, centreZ, width, width, angle);
    int32_t first[kMaxRows], last[kMaxRows];
    int32_t firstRow = 0, lastRow = 0;
    QuadRows(self, nullptr, corners, first, last, &firstRow, &lastRow);
    EmptyRect(rect);
    if (times == 0)
        return;
    do {
        int32_t row = firstRow;
        if (row <= lastRow) {
            do {
                int32_t column = first[row];
                uint32_t index = U32(self, field::kVertsX) * static_cast<uint32_t>(row) + static_cast<uint32_t>(column);
                if (column <= last[row]) {
                    do {
                        if (Visit(routine, self + adjust, index, context))
                            JoinVertex(rect, column, row);
                        ++column;
                        ++index;
                    } while (column <= last[row]);
                }
                ++row;
            } while (row <= lastRow);
        }
    } while (--times != 0);
}

// --- What some kinds do around their walk ------------------------------------

// 0x00741E30 (rect, by), our copy: the rectangle grown on every side and cut
// to the cells (z first, then x, at the near ends; signed).
inline void GrowRect(char* self, int32_t* rect, int32_t by)
{
    const uint32_t amount = static_cast<uint32_t>(by);
    rect[0] = static_cast<int32_t>(static_cast<uint32_t>(rect[0]) - amount);
    rect[1] = static_cast<int32_t>(static_cast<uint32_t>(rect[1]) - amount);
    rect[2] = static_cast<int32_t>(static_cast<uint32_t>(rect[2]) + amount);
    rect[3] = static_cast<int32_t>(static_cast<uint32_t>(rect[3]) + amount);
    const int32_t farX = rect[2], farZ = rect[3];
    if (rect[1] < 0)
        rect[1] = 0;
    if (rect[0] < 0)
        rect[0] = 0;
    if (farX > At<int32_t>(self, field::kCellsX))
        rect[2] = At<int32_t>(self, field::kCellsX);
    if (farZ > At<int32_t>(self, field::kCellsZ))
        rect[3] = At<int32_t>(self, field::kCellsZ);
}

// 0x0074B5E0: once (while the count +0x108 is 0): the flora simulator's
// slot 9 with (0, the count's address) gives the number of its types; the
// vector +0x10C is resized to that (0x006BDF10, zeros) and slot 9 with (the
// vector's array, the count's address) fills it; then every type whose
// record (slot 7) has not exactly the flag 2 of the flags 1 and 2 in its
// byte +0x16C is taken out of the vector (the game's memmove) and the count
// goes down.
void CollectFloraTypes(char* self)
{
    if (U32(self, field::kFloraTypeCount) != 0)
        return;
    uint32_t* const count = &At<uint32_t>(self, field::kFloraTypeCount);
    void* const types = self + field::kFloraTypes;
    const uint32_t found = Virtual<uint32_t>(At<void*>(self, field::kFlora), 0x24, 0u, count);
    uint32_t zero = 0;
    *count = found;
    This<void>(addr::SC4Uint32Vector_Resize, types, found, &zero);
    Virtual<uint32_t>(At<void*>(self, field::kFlora), 0x24, At<uint32_t>(types, 0), count);
    uint32_t i = 0;
    if (*count == 0)
        return;
    do {
        const uint32_t at = i * 4u;
        char* const type =
            Virtual<char*>(At<void*>(self, field::kFlora), 0x1C, AtAddress<uint32_t>(At<uint32_t>(types, 0) + at));
        const uint8_t flags = At<uint8_t>(type, 0x16C);
        if (!(flags & 1u) && (flags & 2u)) {
            ++i;
        } else {
            const uint32_t here = At<uint32_t>(types, 0) + at;
            const uint32_t end = At<uint32_t>(types, 4);
            const uint32_t next = here + 4u;
            if (end != next)
                Cdecl<void*>(addr::SC4_memmove, here, next, end - next);
            At<uint32_t>(types, 4) -= 4u;
            *count = *count - 1u;
        }
    } while (i < *count);
}

// 0x007479B0 (block, size, rect), kind 6: the rectangle is the vertex at the
// block's place (x / 16 and z / 16 truncated) grown by size / 2 on every
// side and cut to the cells; the erosion pass (0x00746AD0) runs over it as
// many times as the block's strength rounded to an integer (FISTP), each
// time with the iteration count the table 0x00B4BD4C has for the step.
void ErodeLayer(char* self, Context* context, int32_t size, int32_t* rect)
{
    const int32_t x = Ftol(static_cast<double>(InverseCellWidth()) * static_cast<double>(context->x));
    const int32_t z = Ftol(static_cast<double>(InverseCellWidth()) * static_cast<double>(context->z));
    rect[3] = z;
    rect[1] = z;
    rect[2] = x;
    rect[0] = x;
    GrowRect(self, rect, size / 2);
    int32_t times = X87Fistp(context->strength);
    if (times <= 0)
        return;
    do {
        const int32_t iterations =
            AtAddress<int32_t>(Global<uint32_t>(addr::cSTETerrain_ErodeIterationsPerStep) + 4u * context->step);
        This<void>(addr::cSTETerrain_ErodePass, self, static_cast<void*>(rect), iterations);
    } while (--times != 0);
}

// 0x00748040 (rect), after a kind 7 layer: nothing for an empty rectangle.
// Else the rectangle joins the list +0x1B0: every record of the list that
// overlaps it (as it has grown so far) is merged into it and taken out of
// the list (the node freed), and one record (the rectangle and two zero
// dwords) is added at the end (0x00747FF0).
void AddPendingRect(char* self, const int32_t* rect)
{
    if (rect[0] > rect[2])
        return;
    if (rect[1] > rect[3])
        return;
    struct {
        int32_t x0, z0, x1, z1;
        uint32_t zero[2];
    } record = {rect[0], rect[1], rect[2], rect[3], {0, 0}};
    void* const list = self + field::kPendingRects;
    const uint32_t head = At<uint32_t>(list, 0);
    uint32_t node = AtAddress<uint32_t>(head);
    if (node != head) {
        int32_t x1 = record.x1, z1 = record.z1;
        do {
            const int32_t nodeX0 = AtAddress<int32_t>(node + 0x8), nodeZ0 = AtAddress<int32_t>(node + 0xC);
            const int32_t nodeX1 = AtAddress<int32_t>(node + 0x10), nodeZ1 = AtAddress<int32_t>(node + 0x14);
            if (nodeX1 < record.x0 || nodeX0 > x1 || nodeZ1 < record.z0 || nodeZ0 > z1) {
                node = AtAddress<uint32_t>(node);
            } else {
                if (record.x0 >= nodeX0)
                    record.x0 = nodeX0;
                if (record.z0 >= nodeZ0)
                    record.z0 = nodeZ0;
                if (x1 <= nodeX1)
                    x1 = nodeX1;
                if (z1 <= nodeZ1)
                    z1 = nodeZ1;
                const uint32_t gone = node;
                const uint32_t next = AtAddress<uint32_t>(gone);
                const uint32_t previous = AtAddress<uint32_t>(gone + 4u);
                node = next;
                AtAddress<uint32_t>(previous) = next;
                AtAddress<uint32_t>(next + 4u) = previous;
                Cdecl<void>(addr::SC4_Free, gone);
            }
        } while (node != At<uint32_t>(list, 0));
        record.z1 = z1;
        record.x1 = x1;
    }
    This<void>(addr::SC4RectList_PushBack, list, static_cast<void*>(&record));
}

// --- 0x0074D730 --------------------------------------------------------------

// 0x0074D730 (record, parameters): applies a brush once.
//
// Nothing when the parameters' place (x / 16 and z / 16 truncated) is not a
// vertex. A brush that turns draws below 5: for anything but 0 its angle
// goes up by its step, less 2 pi (the float 0x00AB3B74) when that is reached;
// for 0 it goes down by the step, plus 2 pi when below 0 (the wrap is made
// on the unrounded sum); a brush that does not turn has the angle 0. When
// the stroke has moved (from != place) the direction becomes the angle of
// the move (FPATAN of dz, dx) + the float 0x00AB3CA8 (pi / 2).
//
// Then the layers in order. A layer with a picture ID whose picture is not
// fetched yet asks the resource manager for it (slot 3; type 0x7AB50E44,
// group 0x891B0E1A, interface 0x1AB50C3F); a failure ends the whole
// application. The layer's angle is the direction or the brush's turn (the
// layer's byte +0xC); its strength the layer's at the step x the
// parameters'; its width the layer's at the step x the parameters' scale.
// By kind:
//   0, 2, 3, 4, 5, 7, 8: the routine of the kind over the picture
//      (0x00742180) or, without one, the square (0x00742440). Kind 2's
//      strength is cut to 1; afterwards its rectangle grows by one vertex
//      (cut to the cells). After kind 7: 0x00748040 with the rectangle.
//   1: the flora types gathered first (0x0074B5E0); the routine only with
//      at least one type.
//   6: no walk: the erosion (0x007479B0) with the width / 16 rounded to an
//      integer.
//   9: the water object's 0x0073EAB0 with (the vertex index of the place,
//      the block, the rectangle, the strength) names the routine, or does
//      the work itself and names none.
//   10: the water object's 0x0073D4D0 with the vertex index of the place.
//   above 10: nothing.
// Each layer's rectangle is joined into the parameters' applied rectangle
// (a layer that walks nothing leaves the rectangle of the layer before it,
// or the place itself: the game's).
//
// The block's picture byte is stack memory the game never writes before a
// layer without a picture reads it (only a layer whose picture ID is 0 can
// do that; every kind with a routine must have the ID property): zero here.
void __fastcall ApplyBrushLayers(char* self, void*, char* record, char* params)
{
    static bool logged = false;
    FirstCall("ApplyBrushLayers", logged);
    const int32_t x =
        Ftol(static_cast<double>(InverseCellWidth()) * static_cast<double>(At<float>(params, parameters::kX)));
    const int32_t z =
        Ftol(static_cast<double>(InverseCellWidth()) * static_cast<double>(At<float>(params, parameters::kZ)));
    if (static_cast<uint32_t>(x) >= U32(self, field::kVertsX))
        return;
    if (static_cast<uint32_t>(z) >= U32(self, field::kVertsZ))
        return;
    int32_t rect[4] = {x, z, x, z};
    Context context{};
    context.x = At<float>(params, parameters::kX);
    context.z = At<float>(params, parameters::kZ);
    context.fromX = At<float>(params, parameters::kFromX);
    context.fromZ = At<float>(params, parameters::kFromZ);
    context.altitude = At<float>(params, parameters::kAltitude);
    context.step = At<int32_t>(params, parameters::kStep) >= 0 ? At<uint32_t>(params, parameters::kStep)
                                                              : U32(self, field::kBrushStep);
    if (At<uint8_t>(record, brush::kTurns)) {
        const double twoPi = static_cast<double>(Global<float>(addr::cSTETerrain_TwoPiFloat));
        if (Below(5u) != 0) {
            const double turned =
                static_cast<double>(At<float>(record, brush::kTurnStep)) + static_cast<double>(At<float>(record, brush::kTurn));
            At<float>(record, brush::kTurn) = static_cast<float>(turned);
            if (turned >= twoPi)
                At<float>(record, brush::kTurn) = static_cast<float>(turned - twoPi);
        } else {
            const double turned =
                static_cast<double>(At<float>(record, brush::kTurn)) - static_cast<double>(At<float>(record, brush::kTurnStep));
            At<float>(record, brush::kTurn) = static_cast<float>(turned);
            if (turned < static_cast<double>(Global<float>(addr::SC4_FloatZero)))
                At<float>(record, brush::kTurn) = static_cast<float>(turned + twoPi);
        }
    } else {
        At<uint32_t>(record, brush::kTurn) = 0;
    }
    if (!(At<float>(params, parameters::kX) == At<float>(params, parameters::kFromX)) ||
        !(At<float>(params, parameters::kZ) == At<float>(params, parameters::kFromZ)))
        X87Direction(At<float>(params, parameters::kZ), At<float>(params, parameters::kFromZ),
                     At<float>(params, parameters::kX), At<float>(params, parameters::kFromX),
                     Global<float>(addr::cSTETerrain_HalfPiFloat), &At<float>(record, brush::kDirection));

    char* layer = At<char*>(record, brush::kLayers);
    char* const end = At<char*>(record, brush::kLayersEnd);
    for (; layer != end; layer += layer::kSize) {
        context.layer = layer;
        if (At<uint32_t>(layer, layer::kPictureID) != 0 && !At<void*>(layer, layer::kPicture)) {
            uint32_t key[3] = {0x7AB50E44u, 0x891B0E1Au, At<uint32_t>(layer, layer::kPictureID)};
            if (!Virtual<uint8_t>(Global<void*>(addr::cSTETerrain_ResourceManager), 0xC, static_cast<void*>(key), 0x1AB50C3Fu,
                                  static_cast<void*>(layer + layer::kPicture), 0u, 0u))
                return;
        }
        char* const image = At<void*>(layer, layer::kPicture) ? Virtual<char*>(At<void*>(layer, layer::kPicture), 0xC) : nullptr;
        const float angle = At<uint8_t>(layer, layer::kFollows) ? At<float>(record, brush::kDirection)
                                                               : At<float>(record, brush::kTurn);
        context.strength = static_cast<float>(static_cast<double>(F(At<uint32_t>(layer, layer::kStrengths) + 4u * context.step)) *
                                              static_cast<double>(At<float>(params, parameters::kStrength)));
        const float width = static_cast<float>(static_cast<double>(F(At<uint32_t>(layer, layer::kWidths) + 4u * context.step)) *
                                               static_cast<double>(At<float>(params, parameters::kRadiusScale)));
        context.params = params;
        context.record = record;
        const uint32_t kind = At<uint32_t>(layer, layer::kKind);
        if (kind == 1) {
            CollectFloraTypes(self);
        } else if (kind == 2) {
            if (context.strength > Global<float>(addr::SC4_FloatOneA81228))
                context.strength = 1.0f;
        }
        uint32_t routine = 0, adjust = 0;
        switch (At<uint32_t>(layer, layer::kKind)) {
        case 0:
            routine = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&RaiseLower));
            break;
        case 1:
            if (U32(self, field::kFloraTypeCount) != 0)
                routine = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&PlantFlora));
            break;
        case 2:
            routine = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&SmoothVertex));
            break;
        case 3:
            routine = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&SplatTexture));
            break;
        case 4:
            routine = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&StartEffect));
            break;
        case 5:
            routine = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&LevelVertex));
            break;
        case 6: {
            const float cells = static_cast<float>(static_cast<double>(InverseCellWidth()) * static_cast<double>(width));
            ErodeLayer(self, &context, X87Fistp(cells), rect);
            break;
        }
        case 7:
            routine = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&MarkVertex));
            break;
        case 8:
            routine = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&ShapeVertex));
            break;
        case 9: {
            const uint32_t index = Virtual<uint32_t>(self, slot::kGetVertIndex, x, z);
            const uint64_t named = This<uint64_t>(addr::STE_WaterBrushRoutine, At<void*>(self, field::kWater), index,
                                                  static_cast<void*>(&context), static_cast<void*>(rect), context.strength);
            routine = static_cast<uint32_t>(named);
            adjust = static_cast<uint32_t>(named >> 32);
            break;
        }
        case 10: {
            const uint32_t index = Virtual<uint32_t>(self, slot::kGetVertIndex, x, z);
            This<void>(addr::STE_WaterBrushPoint, At<void*>(self, field::kWater), index);
            break;
        }
        default:
            break;
        }
        if (routine != 0) {
            if (image)
                WalkPicture(self, image, x, z, width, angle, routine, adjust, &context, rect);
            else
                WalkSquare(self, x, z, width, angle, routine, adjust, 1u, &context, rect);
        }
        const uint32_t after = At<uint32_t>(layer, layer::kKind);
        if (after == 2) {
            rect[1] = static_cast<int32_t>(static_cast<uint32_t>(rect[1]) - 1u);
            rect[0] = static_cast<int32_t>(static_cast<uint32_t>(rect[0]) - 1u);
            rect[2] = static_cast<int32_t>(static_cast<uint32_t>(rect[2]) + 1u);
            rect[3] = static_cast<int32_t>(static_cast<uint32_t>(rect[3]) + 1u);
            if (rect[1] < 0)
                rect[1] = 0;
            if (rect[0] < 0)
                rect[0] = 0;
            if (rect[2] > At<int32_t>(self, field::kCellsX))
                rect[2] = At<int32_t>(self, field::kCellsX);
            if (rect[3] > At<int32_t>(self, field::kCellsZ))
                rect[3] = At<int32_t>(self, field::kCellsZ);
        } else if (after == 7) {
            AddPendingRect(self, rect);
        }
        int32_t* const applied = &At<int32_t>(params, parameters::kApplied);
        if (applied[0] >= rect[0])
            applied[0] = rect[0];
        if (applied[1] >= rect[1])
            applied[1] = rect[1];
        if (applied[2] <= rect[2])
            applied[2] = rect[2];
        if (applied[3] <= rect[3])
            applied[3] = rect[3];
    }
}

// --- Installation ------------------------------------------------------------

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Every direct call site of each (scan of all direct calls in the exe),
// checked for what it uses after the call:
// 0x0074D730: 0x0074F140 (slot 57, the game's: pushes the parameters and the
//   record, ECX from EDI; afterwards it reads through ESI, EBX and EDI) and
//   our slot 57 (terrain_brush.cpp, through the address). Arguments on the
//   stack only; nothing kept in EAX, ECX or EDX.
// 0x00740BB0: 0x007425BB (the wave producers' counter 0x00742590: five
//   pushes, ECX from EBP; afterwards EBX from its stack, EAX zeroed, ECX
//   and EDX loaded anew), 0x0074228B and 0x007424AD (the game's walkers,
//   which our body no longer runs; both load what they use from the stack
//   afterwards). Arguments on the stack only.
const Detour kDetours[] = {
    OPENSC4_DETOUR(addr::cSTETerrain_ApplyBrushLayers, ApplyBrushLayers),
    OPENSC4_DETOUR(addr::cSTETerrain_QuadRows, QuadRows),
};

#undef OPENSC4_DETOUR

void* g_originals[std::size(kDetours)];

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : kDetours) {
        if (!addr_check::Find(detour.address)) {
            log::Write("terrain brushbody: no address entry for %08X; not installing",
                       static_cast<unsigned>(detour.address));
            return false;
        }
    }
    for (std::size_t i = 0; i < std::size(kDetours); ++i) {
        if (!hook::Install(*addr_check::Find(kDetours[i].address), kDetours[i].ours, &g_originals[i])) {
            log::Write("terrain brushbody: detour %u of %u failed (%08X); the earlier ones stay", static_cast<unsigned>(i),
                       static_cast<unsigned>(std::size(kDetours)), static_cast<unsigned>(kDetours[i].address));
            return false;
        }
    }
    log::Write("terrain brushbody: the brush body %08X and %u more function (detours) are ours",
               static_cast<unsigned>(addr::cSTETerrain_ApplyBrushLayers), static_cast<unsigned>(std::size(kDetours) - 1));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.terrain.brushbody", "simulators/terrain", registry::Kind::Detour, registry::Phase::PostAppInit,
     addr::cSTETerrain_ApplyBrushLayers,
     "Our cSTETerrain brush body: the brush's turn and direction, the layer loop, the two walkers and their turned "
     "rectangle, the per-vertex routines of the layer kinds (raise and lower, flora, smooth, decal, effect, level, mark, "
     "shape), the erosion layer and the pending rectangles, by detours",
     false, Install});

} // namespace
} // namespace opensc4::terrain_sim
