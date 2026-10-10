// Our cSC4ZoneDeveloper (build 641; the class map is in zone_sim.h), the
// object part's functions with entries in 0x00729E70-0x0072C200: the lot
// manager's queries by cell, position and vertex (vtable slots), and the
// zoning code's helpers that the other parts call directly (detours): the
// street drawing, the region trimming, the dezoning of a region, the
// water/land test of a lot configuration's records and small tests.
//
// Fields used (offsets from the object; the lot manager's slots get
// object + 4, so their disassembly reads 4 less):
//   +0x24 city: slot 95 (0x17C) PositionToCell(float x, float z, int* cx,
//         int* cz), slot 98 (0x188) (float x, float z) in the city, slot 99
//         (0x18C) (cell x, cell z) in the city
//   +0x28, +0x2C int32  the city's cells in x and z (bounds of slot 6)
//   +0x34 terrain: slot 13 (0x34) altitude at a position (float x, float z),
//         slot 14 (0x38) altitude at a vertex (int, int), both ST0; slots 34
//         and 35 (0x88, 0x8C) cell tests (0x0072B480)
//   +0x38 the terrain picker (slot 9 called by 0x0072C0E0)
//   +0x40 zone manager: slot 11 (0x2C) PlaceZone, 10 arguments; slot 12
//         (0x30) the cell's zone type; slot 13 (0x34) whether a type is RCI
//   +0x48 the street tool (network tool of type 3): slot 7 (0x1C) with
//         (x0, z0, x1, z1, 0xC49, the tool, the byte +0x99, -1), true when
//         it placed; slot 5 (0x14) by 0x0072C0E0, also for the tool +0x4C
//   +0x50 lot developer: slot 46 (0xB8) "force empty lot textures", slot 43
//         (0xAC) with a lot (0x0072B180)
//   +0x54 the lot grid: columns of 12 bytes, [x][z] lot pointers
//   +0x99 uint8  place for real (1) or only highlight (0)
//   +0xA0 the operation's zone type; +0xA4 the operation's cell region
//   +0xD8, +0xE4 maps keyed by a cell pair (node: key +0x10, +0x14; head
//         +0x0, size +0x4); +0xF0 map<int32, {uint32 +0x14, uint8 +0x18,
//         uint8 +0x19}>
// A lot (cISC4Lot, vtable 0x00AB19B0): slot 12 (0x30) flags, 14 (0x38)
// rectangle, 16 (0x40) altitude ST0, 21 (0x54) its own cell (int* x, int*
// z), 31 (0x7C) zone type, 32 (0x80) update zone type, 33 (0x84) state.
// A lot configuration (vtable 0x00A8CEB8): slot 7 (0x1C) size (two out
// values), 11 (0x2C) a byte, 16 and 17 (0x40, 0x44) tests with one
// argument, 23 (0x5C) its vector of 0x34-byte records (+0x0 type byte, +0x2
// facing byte, +0x10/+0x14 the far corner and +0x18/+0x1C the near corner
// in metres, +0x28 a vector of dwords).
//
// Left the game's: the container code of the range (0x00729E70 the cell
// pair order, 0x0072ABE0/0x0072AC20/0x0072ACC0 tree erasers, 0x0072AC60
// find, 0x0072B950 lower bound, 0x0072B990 the lot list's erase, 0x0072BAE0
// and 0x0072BBD0 inserts; plain copies here where our code needs them) and
// the instantiations other classes share (0x00729EC0, 0x0072BA10,
// 0x0072BA50, 0x0072BC80, 0x0072BD50, the deleting destructor 0x0072A730).
#include "core/zone_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::zone_sim {
namespace {

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using CellsFn = void*(__thiscall*)(void* cells, uint32_t width, uint32_t depth, const uint8_t* fill);
using SelfFn = void(__thiscall*)(void* self);
using RotateFn = void(__cdecl*)(int32_t facing, int32_t width, int32_t depth, const int32_t* rect, int32_t* out);
using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t count);
using FindFn = void(__thiscall*)(void* map, char** out, const int32_t* key);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto ConstructCells = reinterpret_cast<CellsFn>(addr::SC4CellRegion_ConstructCells);
const auto DestroyCells = reinterpret_cast<SelfFn>(addr::SC4CellRegion_DestroyCells);
const auto RotateRectangle = reinterpret_cast<RotateFn>(addr::SC4_RotateRectangle);

char* LotManager(char* self) { return self + kLotManagerPart; }

// (v - 0.5) + 1.5 * 2^23 stored as a float, its bits less 0x4B400000, one
// more when v is that whole number: the game's inline floor (0x0072A125).
int32_t Floor(double v)
{
    const float magic = static_cast<float>((v - 0.5) + 12582912.0);
    const int32_t n = static_cast<int32_t>(std::bit_cast<uint32_t>(magic) + 0xB4C00000u);
    return v == static_cast<double>(n + 1) ? n + 1 : n;
}

// --- The lot manager's slots (this = object + 4) -----------------------------------

// Slot 6 (0x0072B410).
void* __fastcall GetLot(char* part, void*, int32_t x, int32_t z, uint32_t anchorOnly)
{
    static bool logged = false;
    FirstCall("GetLot", logged);
    char* const self = part - kLotManagerPart;
    if (x < 0 || x >= At<int32_t>(self, field::kCellsX) || z < 0 || z >= At<int32_t>(self, field::kCellsZ))
        return nullptr;
    void* const lot = GridLot(self, x, z);
    if (anchorOnly & 0xFF) {
        if (!lot)
            return nullptr;
        // The game reads the lot's cell into its own argument slots.
        int32_t lotX = static_cast<int32_t>(anchorOnly), lotZ = x;
        Virtual<void>(lot, 0x54, &lotX, &lotZ);
        if (x != lotX || z != lotZ)
            return nullptr;
    }
    return lot;
}

// Slot 5 (0x0072A1C0).
void* __fastcall GetLotAtPosition(char* part, void*, const float* position)
{
    static bool logged = false;
    FirstCall("GetLotAtPosition", logged);
    char* const self = part - kLotManagerPart;
    const float x = position[0], z = position[2];
    if (!Virtual<uint8_t>(At<void*>(self, field::kCity), 0x188, x, z))
        return nullptr;
    // The out values start as the position's bits, as the game's stack slots.
    int32_t cellX = static_cast<int32_t>(std::bit_cast<uint32_t>(x));
    int32_t cellZ = static_cast<int32_t>(std::bit_cast<uint32_t>(z));
    Virtual<void>(At<void*>(self, field::kCity), 0x17C, position[0], position[2], &cellX, &cellZ);
    return Virtual<void*>(part, 0x18, cellX, cellZ, 0u);
}

// Slot 14 (0x0072A110).
void* __fastcall GetOccupantLot(char* part, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("GetOccupantLot", logged);
    float position[3] = {};
    Virtual<void>(occupant, 0x20, position);
    const int32_t x = Floor(static_cast<double>(position[0]) * 0.0625);
    const int32_t z = Floor(static_cast<double>(position[2]) * 0.0625);
    return Virtual<void*>(part, 0x18, x, z, 0u);
}

// Slot 9 (0x0072A750).
uint32_t __fastcall GetNeighbours(char* part, void*, void* lot, void* list, uint32_t sides)
{
    static bool logged = false;
    FirstCall("GetNeighbours", logged);
    int32_t rect[4] = {};
    Virtual<void>(lot, 0x38, rect);
    return Virtual<uint32_t>(part, 0x28, rect, list, sides);
}

// Slot 8 (0x0072A230).
uint32_t __fastcall GetNeighboursOfCell(char* part, void*, int32_t x, int32_t z, void* list, uint32_t sides)
{
    static bool logged = false;
    FirstCall("GetNeighboursOfCell", logged);
    void* const lot = Virtual<void*>(part, 0x18, x, z, 0u);
    if (!lot)
        return 0;
    return Virtual<uint32_t>(part, 0x24, lot, list, sides);
}

// Slot 11 (0x0072A790). Nothing is written for a side without one of the
// four bits; the lowest bit set decides.
void __fastcall GetSideLine(char*, void*, const int32_t* rect, uint32_t side, int32_t* out)
{
    static bool logged = false;
    FirstCall("GetSideLine", logged);
    if (side & 1) {
        out[0] = rect[0];
        out[1] = rect[1];
        out[2] = rect[2] + 1;
        out[3] = rect[1];
    } else if (side & 2) {
        out[0] = rect[2] + 1;
        out[1] = rect[1];
        out[2] = rect[2] + 1;
        out[3] = rect[3] + 1;
    } else if (side & 4) {
        out[0] = rect[0];
        out[1] = rect[3] + 1;
        out[2] = rect[2] + 1;
        out[3] = rect[3] + 1;
    } else if (side & 8) {
        out[0] = rect[0];
        out[1] = rect[1];
        out[2] = rect[0];
        out[3] = rect[3] + 1;
    }
}

// Slot 12 (0x0072A820).
uint8_t __fastcall GetSideCells(char* part, void*, const int32_t* rect, uint32_t side, int32_t* out)
{
    static bool logged = false;
    FirstCall("GetSideCells", logged);
    char* const self = part - kLotManagerPart;
    out[2] = -1;
    out[3] = -1;
    out[0] = 0;
    out[1] = 0;
    if (side & 1) {
        if (rect[1] > 0) {
            out[2] = rect[2];
            out[3] = rect[1] - 1;
            out[0] = rect[0];
            out[1] = rect[1] - 1;
        }
    } else if (side & 2) {
        if (rect[2] < At<int32_t>(self, field::kCellsX) - 1) {
            out[2] = rect[2] + 1;
            out[3] = rect[3];
            out[0] = rect[2] + 1;
            out[1] = rect[1];
        }
    } else if (side & 4) {
        if (rect[3] < At<int32_t>(self, field::kCellsZ) - 1) {
            out[0] = rect[0];
            out[1] = rect[3] + 1;
            out[2] = rect[2];
            out[3] = rect[3] + 1;
        }
    } else if (side & 8) {
        if (rect[0] > 0) {
            out[2] = rect[0] - 1;
            out[3] = rect[3];
            out[0] = rect[0] - 1;
            out[1] = rect[1];
        }
    }
    return out[2] >= out[0];
}

// Slot 13 (0x0072B3C0): x then z drawn for each try.
void* __fastcall GetRandomLot(char* part, void*)
{
    static bool logged = false;
    FirstCall("GetRandomLot", logged);
    char* const self = part - kLotManagerPart;
    const auto Below = reinterpret_cast<BelowFn>(addr::SC4Random_BelowLcg);
    void* const random = reinterpret_cast<void*>(addr::cSC4ZoneDeveloper_Random);
    void* lot = nullptr;
    for (int tries = 1; tries < 1000 && !lot; ++tries) {
        const uint32_t x = Below(random, At<uint32_t>(self, field::kCellsX));
        const uint32_t z = Below(random, At<uint32_t>(self, field::kCellsZ));
        lot = GridLot(self, static_cast<int32_t>(x), static_cast<int32_t>(z));
    }
    return lot;
}

// A lot that counts for the altitude queries: there, not `except`, flag bit 0.
bool Flagged(void* lot, void* except)
{
    return lot && lot != except && (Virtual<uint8_t>(lot, 0x30) & 1);
}

// Slot 17 (0x0072A8F0): the four cells around the vertex in the game's
// order; ST0.
double __fastcall GetVertexAltitude(char* part, void*, int32_t x, int32_t z, void* except, uint8_t* found)
{
    static bool logged = false;
    FirstCall("GetVertexAltitude", logged);
    char* const self = part - kLotManagerPart;
    if (found)
        *found = 1;
    void* lot;
    if (x > 0 && z > 0 && Flagged(lot = GridLot(self, x - 1, z - 1), except))
        return Virtual<double>(lot, 0x40);
    if (x < At<int32_t>(self, field::kCellsX) && z > 0 && Flagged(lot = GridLot(self, x, z - 1), except))
        return Virtual<double>(lot, 0x40);
    if (x > 0 && z < At<int32_t>(self, field::kCellsZ) && Flagged(lot = GridLot(self, x - 1, z), except))
        return Virtual<double>(lot, 0x40);
    if (x < At<int32_t>(self, field::kCellsX) && z < At<int32_t>(self, field::kCellsZ) &&
        Flagged(lot = GridLot(self, x, z), except))
        return Virtual<double>(lot, 0x40);
    if (found)
        *found = 0;
    return Virtual<double>(At<void*>(self, field::kTerrain), 0x38, x, z);
}

// The cell of a coordinate for slot 18: (0.0625 (the float at 0x00B0D4AC)
// * v - 0.499) rounded by the float trick, clamped to the city unless it is
// inside already (the unsigned test first, as the game).
int32_t ClampedCell(float v, int32_t count)
{
    const double scaled = static_cast<double>(Global<float>(addr::cSC4ZoneDeveloper_CellsPerMetre)) *
                              static_cast<double>(v) -
                          static_cast<double>(std::bit_cast<float>(0x3EFF7CEEu));
    int32_t cell = static_cast<int32_t>(std::bit_cast<uint32_t>(static_cast<float>(scaled + 12582912.0)) + 0xB4C00000u);
    if (static_cast<uint32_t>(cell) >= static_cast<uint32_t>(count)) {
        if (cell < 0)
            cell = 0;
        if (cell >= count)
            cell = count - 1;
    }
    return cell;
}

// Slot 18 (0x0072A9F0); ST0.
double __fastcall GetAltitudeAtPosition(char* part, void*, float x, float z, uint8_t* found)
{
    static bool logged = false;
    FirstCall("GetAltitudeAtPosition", logged);
    char* const self = part - kLotManagerPart;
    const int32_t cellX = ClampedCell(x, At<int32_t>(self, field::kCellsX));
    const int32_t cellZ = ClampedCell(z, At<int32_t>(self, field::kCellsZ));
    void* const lot = GridLot(self, cellX, cellZ);
    if (Flagged(lot, nullptr)) {
        if (found)
            *found = 1;
        return Virtual<double>(lot, 0x40);
    }
    if (found)
        *found = 0;
    return Virtual<double>(At<void*>(self, field::kTerrain), 0x34, x, z);
}

// Slot 19 (0x0072AAC0); ST0. The cell is not checked against the city.
double __fastcall GetCellAltitude(char* part, void*, int32_t x, int32_t z, uint8_t* found)
{
    static bool logged = false;
    FirstCall("GetCellAltitude", logged);
    char* const self = part - kLotManagerPart;
    void* const lot = GridLot(self, x, z);
    if (Flagged(lot, nullptr)) {
        if (found)
            *found = 1;
        return Virtual<double>(lot, 0x40);
    }
    if (found)
        *found = 0;
    const float centreZ = static_cast<float>((static_cast<double>(z) + 0.5) * 16.0);
    const float centreX = static_cast<float>((static_cast<double>(x) + 0.5) * 16.0);
    return Virtual<double>(At<void*>(self, field::kTerrain), 0x34, centreX, centreZ);
}

// Slot 26 (0x0072AE20): the distinct lots of the up to four cells around
// the vertex (x - 1 to x, z - 1 to z, clamped to the city).
void __fastcall GetVertexLimits(char* part, void*, int32_t x, int32_t z, uint32_t* a, uint32_t* b)
{
    static bool logged = false;
    FirstCall("GetVertexLimits", logged);
    char* const self = part - kLotManagerPart;
    void* lots[4] = {};
    int32_t count = 0;
    int32_t x0 = x - 1, z0 = z - 1, x1 = x, z1 = z;
    if (x0 < 0)
        x0 = 0;
    if (z0 < 0)
        z0 = 0;
    if (x1 >= At<int32_t>(self, field::kCellsX))
        x1 = At<int32_t>(self, field::kCellsX) - 1;
    if (z1 >= At<int32_t>(self, field::kCellsZ))
        z1 = At<int32_t>(self, field::kCellsZ) - 1;
    for (int32_t cx = x0; cx <= x1; ++cx)
        for (int32_t cz = z0; cz <= z1; ++cz) {
            void* const lot = GridLot(self, cx, cz);
            if (!lot)
                continue;
            int32_t i = 0;
            while (i < count && lots[i] != lot)
                ++i;
            if (i == count)
                lots[count++] = lot;
        }
    *a = 0x7F7FFFFF;
    *b = 0x7F7FFFFF;
    for (int32_t i = 0; i < count; ++i)
        if (Virtual<int32_t>(lots[i], 0x84) >= 2) {
            *a = 0x42000000;
            *b = 0x42000000;
        }
}

// --- Primary slots -----------------------------------------------------------------

// Slot 7 (0x007347C0).
void __fastcall SetOptions(char* self, void*, uint32_t a, uint32_t b, uint32_t c)
{
    static bool logged = false;
    FirstCall("SetOptions", logged);
    At<uint8_t>(self, field::kOption9B) = static_cast<uint8_t>(a);
    At<uint8_t>(self, field::kOption9A) = static_cast<uint8_t>(b);
    At<uint8_t>(self, field::kOption9C) = static_cast<uint8_t>(c);
}

// Slot 9 (0x0072A030): the game jumps to the terrain's slot 34; all of EAX
// is passed on.
uint32_t __fastcall IsCellWater(char* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("IsCellWater", logged);
    return Virtual<uint32_t>(At<void*>(self, field::kTerrain), 0x88, x, z);
}

// The lot filter's slot 3 (0x0072A080): filter +0x8, +0xC, +0x10.
uint8_t __fastcall LotFilterTest(char* filter, void*, void* configuration)
{
    static bool logged = false;
    FirstCall("LotFilterTest", logged);
    if (!Virtual<uint8_t>(configuration, 0x44, At<uint32_t>(filter, 0x8)))
        return 0;
    if (!Virtual<uint8_t>(configuration, 0x40, At<uint32_t>(filter, 0xC)))
        return 0;
    return static_cast<int32_t>(Virtual<uint8_t>(configuration, 0x2C)) <= At<int32_t>(filter, 0x10);
}

// --- Helpers the other parts call (detours) ----------------------------------------

// 0x00729F90: the game passes the network occupant in ESI (its whole
// program build's convention) and tests AL. Its slot 22 (0x58) says whether
// it has a network type.
uint32_t __cdecl IsStreetLike(void* occupant)
{
    static bool logged = false;
    FirstCall("IsStreetLike", logged);
    const auto Has = [&](uint32_t type) { return Virtual<uint8_t>(occupant, 0x58, type) != 0; };
    if (Global<uint8_t>(addr::cSC4ZoneDeveloper_AnyRoadIsStreetLike))
        return Has(3) || Has(11) || Has(10) || Has(0);
    if (!Has(3) && !Has(11))
        return 0;
    return !Has(10) && !Has(0);
}

__declspec(naked) void IsStreetLikeThunk()
{
    __asm push esi
    __asm call IsStreetLike
    __asm add esp, 4
    __asm ret
}

// 0x0072A010: all of EAX passed on from slot 8.
uint32_t __fastcall AnyNetworkAt(char* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("AnyNetworkAt", logged);
    return Virtual<uint32_t>(self, 0x20, x, z, 0x1F4Fu);
}

// 0x0072A040.
uint32_t __fastcall CellHasZone(char* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("CellHasZone", logged);
    const uint32_t type = Virtual<uint32_t>(At<void*>(self, field::kZones), 0x30, x, z);
    if (const uint32_t wanted = At<uint32_t>(self, field::kZoneType))
        return type == wanted;
    return Virtual<uint32_t>(At<void*>(self, field::kZones), 0x34, type);
}

// The street tool's slot 7; the tool and the byte +0x99 are read at each call.
uint8_t Street(char* self, int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    void* const tool = At<void*>(self, field::kStreetTool);
    return Virtual<uint8_t>(tool, 0x1C, x0, z0, x1, z1, 0xC49u, tool,
                            static_cast<uint32_t>(At<uint8_t>(self, field::kPlaceStreets)), 0xFFFFFFFFu);
}

// 0x0072A2A0: a street from (x, z) to (x1, z1) (a straight or diagonal
// line), over the runs of cells inside the operation's region only.
void __fastcall DrawStreet(char* self, void*, int32_t x, int32_t z, int32_t x1, int32_t z1, uint32_t cellByCellArg)
{
    static bool logged = false;
    FirstCall("DrawStreet", logged);
    const bool cellByCell = (cellByCellArg & 0xFF) != 0;
    if (x == x1 && z == z1) {
        if (RegionHas(At<void*>(self, field::kRegion), x, z))
            Street(self, x, z, x1, z1);
        return;
    }
    const int32_t dx = x1 > x ? 1 : x1 < x ? -1 : 0;
    const int32_t dz = z1 > z ? 1 : z1 < z ? -1 : 0;
    for (;;) {
        void* const cells = At<void*>(self, field::kRegion);
        // To the next cell of the region, or the end.
        bool in = RegionHas(cells, x, z);
        while (!in && !(x == x1 && z == z1)) {
            x += dx;
            z += dz;
            in = RegionHas(cells, x, z);
        }
        if (x == x1 && z == z1)
            break;
        // The run's last cell.
        int32_t endX = x, endZ = z;
        bool inside = true;
        while (inside && !(endX == x1 && endZ == z1)) {
            endX += dx;
            endZ += dz;
            inside = RegionHas(cells, endX, endZ);
        }
        if (!inside) {
            endX -= dx;
            endZ -= dz;
        }
        if (!(cellByCell && x == endX && z == endZ)) {
            // The run; else from a cell on to the line's end; else that
            // with the end a cell short, from here or from a cell on.
            if (!Street(self, x, z, endX, endZ) && !Street(self, x + dx, z + dz, x1, z1)) {
                if (!Street(self, x, z, x1 - dx, z1 - dz))
                    Street(self, x + dx, z + dz, x1 - dx, z1 - dz);
            }
        }
        if (cellByCell) {
            while (!(x == endX && z == endZ)) {
                Street(self, x, z, x, z);
                x += dx;
                z += dz;
            }
            Street(self, endX, endZ, endX, endZ);
        }
        if (endX == x1 && endZ == z1)
            return;
        x = endX + dx;
        z = endZ + dz;
    }
    // The line's last cell on its own.
    if (!RegionHas(At<void*>(self, field::kRegion), x, z))
        return;
    if (Street(self, x, z, x1, z1))
        return;
    if (Street(self, x + dx, z + dz, x1, z1))
        return;
    if (Street(self, x, z, x1 - dx, z1 - dz))
        return;
    // 0x0072A0D0, the same call.
    Street(self, x + dx, z + dz, x1 - dx, z1 - dz);
}

// 0x0072AD00 (cdecl, 9 arguments): where a record of a lot configuration
// lies for a lot at origin with a facing, and its three dwords; the last
// two are rotated left by 8 bits per quarter turn of (record facing +
// facing) % 4 (the shifts by the low 5 bits, as the game's).
void __cdecl PlaceRecordCells(void* configuration, char* record, const int32_t* origin, int32_t facing, int32_t* x,
                              int32_t* z, uint32_t* a, uint32_t* b, uint32_t* c)
{
    static bool logged = false;
    FirstCall("PlaceRecordCells", logged);
    int32_t width = 0, depth = 0;
    Virtual<void>(configuration, 0x1C, &width, &depth);
    const float centreZ =
        static_cast<float>((static_cast<double>(At<float>(record, 0x1C)) + At<float>(record, 0x14)) * 0.5);
    const float centreX =
        static_cast<float>((static_cast<double>(At<float>(record, 0x18)) + At<float>(record, 0x10)) * 0.5);
    // The game reads the cell's x into the slot of its first argument.
    int32_t cellX = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(configuration)), cellZ = 0;
    Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, centreX, centreZ, &cellX, &cellZ);
    const int32_t in[4] = {cellX, cellZ, cellX, cellZ};
    int32_t out[4] = {};
    RotateRectangle(facing, width, depth, in, out);
    *x = out[0] + origin[0];
    *z = out[1] + origin[1];
    const uint32_t* const values = At<const uint32_t*>(record, 0x28);
    *c = values[0];
    *a = values[1];
    *b = values[2];
    const int32_t turns = (static_cast<int32_t>(At<uint8_t>(record, 0x2)) + facing) % 4;
    const uint32_t left = static_cast<uint32_t>(turns * 8) & 31;
    const uint32_t right = static_cast<uint32_t>(32 - turns * 8) & 31;
    *a = (*a >> right) | (*a << left);
    *b = (*b >> right) | (*b << left);
}

// The cells object at +0x10 of a region freed as the game's inline code
// does (0x0072B12C), then the region.
void DeleteRegion(char* cells)
{
    At<std::uintptr_t>(cells, region::kCellsVtable) = addr::SC4CellRegion_CellsVtable;
    if (void** const rows = At<void**>(cells, region::kRows)) {
        for (uint32_t i = 0; i < At<uint32_t>(cells, region::kWidth); ++i)
            OperatorDelete(At<void**>(cells, region::kRows)[i]);
        OperatorDelete(rows);
    }
    OperatorDelete(cells);
}

// 0x0072AF30 (`this` is not used): the region behind the pointer replaced
// by one of the bounding rectangle of its set cells when that is smaller
// (an empty region becomes its first cell).
void __fastcall TrimRegion(void*, void*, char** regionPointer)
{
    static bool logged = false;
    FirstCall("TrimRegion", logged);
    char* const old = *regionPointer;
    const int32_t width = At<int32_t>(old, region::kWidth);
    const int32_t depth = At<int32_t>(old, region::kDepth);
    int32_t minX = 0x7FFFFFFF, minZ = 0x7FFFFFFF;
    int32_t maxX = static_cast<int32_t>(0x80000000), maxZ = static_cast<int32_t>(0x80000000);
    bool empty = true;
    for (int32_t i = 0; i < width; ++i)
        for (int32_t j = 0; j < At<int32_t>(old, region::kDepth); ++j) {
            if (!(At<uint32_t**>(old, region::kRows)[i][static_cast<uint32_t>(j) >> 5] & (1u << (j & 31))))
                continue;
            if (i < minX)
                minX = i;
            if (i > maxX)
                maxX = i;
            if (j < minZ)
                minZ = j;
            if (j > maxZ)
                maxZ = j;
            empty = false;
        }
    if (empty)
        minX = minZ = maxX = maxZ = 0;
    if (minX <= 0 && minZ <= 0 && maxX >= width - 1 && maxZ >= depth - 1)
        return;
    const int32_t x0 = At<int32_t>(old, region::kX0) + minX;
    const int32_t z0 = At<int32_t>(old, region::kZ0) + minZ;
    const int32_t x1 = At<int32_t>(old, region::kX0) + maxX;
    const int32_t z1 = At<int32_t>(old, region::kZ0) + maxZ;
    // The game goes on with a null region when the allocation fails.
    char* const fresh = static_cast<char*>(OperatorNew(0x24));
    At<int32_t>(fresh, region::kX0) = x0;
    At<int32_t>(fresh, region::kZ0) = z0;
    At<int32_t>(fresh, region::kX1) = x1;
    At<int32_t>(fresh, region::kZ1) = z1;
    const uint8_t fill = 1;
    ConstructCells(fresh + region::kCellsVtable, static_cast<uint32_t>(x1 - x0 + 1),
                   static_cast<uint32_t>(z1 - z0 + 1), &fill);
    const int32_t shiftX = At<int32_t>(fresh, region::kX0) - At<int32_t>(old, region::kX0);
    const int32_t shiftZ = At<int32_t>(fresh, region::kZ0) - At<int32_t>(old, region::kZ0);
    const int32_t freshWidth = At<int32_t>(fresh, region::kWidth);
    const int32_t freshDepth = At<int32_t>(fresh, region::kDepth);
    for (int32_t i = 0; i < freshWidth; ++i)
        for (int32_t j = 0; j < freshDepth; ++j) {
            const uint32_t from = static_cast<uint32_t>(shiftZ + j);
            const bool set = (At<uint32_t**>(*regionPointer, region::kRows)[shiftX + i][from >> 5] &
                              (1u << (from & 31))) != 0;
            uint32_t& word = At<uint32_t**>(fresh, region::kRows)[i][static_cast<uint32_t>(j) >> 5];
            if (set)
                word |= 1u << (j & 31);
            else
                word &= ~(1u << (j & 31));
        }
    if (char* const previous = *regionPointer)
        DeleteRegion(previous);
    *regionPointer = fresh;
}

// 0x0072B180: the lots of a region take their zone type again (lot slot
// 32; a lot whose type changed gets the lot developer's slot 43 unless it
// is in state 2 or more and textures are not forced), and the region's
// cells without any lot are zoned 0 through the zone manager.
void __fastcall ClearZones(char* self, void*, char* cells)
{
    static bool logged = false;
    FirstCall("ClearZones", logged);
    const int32_t x0 = At<int32_t>(cells, region::kX0), z0 = At<int32_t>(cells, region::kZ0);
    int32_t x1 = At<int32_t>(cells, region::kX1), z1 = At<int32_t>(cells, region::kZ1);
    // The loop's limits are raised to the start; the copy keeps the region's own.
    struct {
        int32_t rect[4];
        char part[0x14];
    } bare{};
    bare.rect[0] = x0;
    bare.rect[1] = z0;
    bare.rect[2] = x1;
    bare.rect[3] = z1;
    const uint8_t fill = 0;
    ConstructCells(bare.part, static_cast<uint32_t>(x1 - x0 + 1), static_cast<uint32_t>(z1 - z0 + 1), &fill);
    if (x1 < x0)
        x1 = x0;
    if (z1 < z0)
        z1 = z0;
    char* const local = reinterpret_cast<char*>(&bare);
    bool any = false;
    const uint8_t forced = Virtual<uint8_t>(At<void*>(self, field::kLotDeveloper), 0xB8);
    char* const lots = LotManager(self);
    for (int32_t z = z0; z <= z1; ++z)
        for (int32_t x = x0; x <= x1; ++x) {
            if (!RegionHas(cells, x, z))
                continue;
            if (void* const lot = Virtual<void*>(lots, 0x18, x, z, 1u)) {
                const uint32_t before = Virtual<uint32_t>(lot, 0x7C);
                Virtual<void>(lot, 0x80);
                if (Virtual<uint32_t>(lot, 0x7C) == before)
                    continue;
                if (!forced && Virtual<int32_t>(lot, 0x84) >= 2)
                    continue;
                Virtual<void>(At<void*>(self, field::kLotDeveloper), 0xAC, lot);
            } else if (!Virtual<void*>(lots, 0x18, x, z, 0u)) {
                const uint32_t bit = static_cast<uint32_t>(z - bare.rect[1]);
                At<uint32_t**>(local, region::kRows)[x - bare.rect[0]][bit >> 5] |= 1u << (bit & 31);
                any = true;
            }
        }
    if (any)
        Virtual<void>(At<void*>(self, field::kZones), 0x2C, local, 0u, 1u, 1u, 0u, 1u, 0u, 0u, 0u, 0u);
    At<std::uintptr_t>(local, region::kCellsVtable) = addr::SC4CellRegion_CellsVtable;
    DestroyCells(bare.part);
}

// A record's rectangle of cells for a lot at (x, z) with a facing: the
// near corner (+0x18, +0x1C) and the far corner (+0x10, +0x14), each 8 m
// further, as cells (the far one less a cell), turned and moved.
void RecordCells(char* self, const char* record, int32_t x, int32_t z, int32_t facing, int32_t width, int32_t depth,
                 int32_t* out)
{
    const auto Shifted = [&](std::size_t offset) {
        return static_cast<float>(static_cast<double>(At<float>(const_cast<char*>(record), offset)) + 8.0);
    };
    int32_t in[4] = {};
    Virtual<void>(At<void*>(self, field::kCity), 0x17C, Shifted(0x18), Shifted(0x1C), &in[0], &in[1]);
    Virtual<void>(At<void*>(self, field::kCity), 0x17C, Shifted(0x10), Shifted(0x14), &in[2], &in[3]);
    --in[2];
    --in[3];
    RotateRectangle(facing, width, depth, in, out);
    out[0] += x;
    out[1] += z;
    out[2] += x;
    out[3] += z;
}

// 0x0072B480: 8, or 6 when a type 6 record has a water cell, or 5 when a
// type 5 record's cells are not all water of the right kind (terrain slot
// 34 true and slot 35 false) or, for a record whose first dword (+0x28) is
// 1, the two rows beyond its far side are not all in the city and open
// water (0x00B43CF0 slot 8). A row's test stops at its first failing cell.
int32_t __fastcall CheckRecords(char* self, void*, int32_t x, int32_t z, int32_t, int32_t, int32_t facing,
                                void* configuration)
{
    static bool logged = false;
    FirstCall("CheckRecords", logged);
    char** const records = Virtual<char**>(configuration, 0x5C);
    int32_t width = 0, depth = 0;
    Virtual<void>(configuration, 0x1C, &width, &depth);
    bool notWater = false, water = false;
    const auto Count = [&] { return static_cast<uint32_t>((records[1] - records[0]) / 0x34); };
    if (!Count())
        return 8;
    for (uint32_t index = 0; index < Count(); ++index) {
        char* const record = records[0] + static_cast<std::ptrdiff_t>(index) * 0x34;
        const uint8_t type = At<uint8_t>(record, 0x0);
        if (type == 5) {
            const uint32_t* const first = At<const uint32_t*>(record, 0x28);
            const uint32_t kind = first == At<const uint32_t*>(record, 0x2C) ? 0 : *first;
            int32_t rect[4];
            RecordCells(self, record, x, z, facing, width, depth, rect);
            void* const terrain = At<void*>(self, field::kTerrain);
            for (int32_t cz = rect[1]; cz <= rect[3]; ++cz)
                for (int32_t cx = rect[0]; cx <= rect[2]; ++cx)
                    if (!Virtual<uint8_t>(At<void*>(self, field::kTerrain), 0x88, cx, cz) ||
                        Virtual<uint8_t>(terrain, 0x8C, cx, cz)) {
                        notWater = true;
                        break;
                    }
            if (kind != 1)
                continue;
            int32_t fx0 = rect[0], fx1 = rect[2], fz0 = rect[1], fz1 = rect[3];
            // The record's facing byte is read through the vector again.
            const int32_t side =
                (static_cast<int32_t>(At<uint8_t>(records[0] + static_cast<std::ptrdiff_t>(index) * 0x34, 0x2)) +
                 facing + 2) %
                4;
            switch (side) {
            case 0:
                fz0 = rect[1] - 2;
                fz1 = rect[1] - 1;
                break;
            case 1:
                fx0 = rect[2] + 1;
                fx1 = rect[2] + 2;
                break;
            case 2:
                fz0 = rect[3] + 1;
                fz1 = rect[3] + 2;
                break;
            case 3:
                fx0 = rect[0] - 2;
                fx1 = rect[0] - 1;
                break;
            default: // a negative remainder: the whole rectangle
                break;
            }
            for (int32_t cz = fz0; cz <= fz1; ++cz)
                for (int32_t cx = fx0; cx <= fx1; ++cx)
                    if (!Virtual<uint8_t>(At<void*>(self, field::kCity), 0x18C, cx, cz) ||
                        !Virtual<uint8_t>(Global<void*>(addr::SC4_Service43CF0), 0x20, cx, cz, 0u, 0u)) {
                        notWater = true;
                        break;
                    }
        } else if (type == 6) {
            int32_t rect[4];
            RecordCells(self, record, x, z, facing, width, depth, rect);
            for (int32_t cz = rect[1]; cz <= rect[3]; ++cz)
                for (int32_t cx = rect[0]; cx <= rect[2]; ++cx)
                    if (Virtual<uint8_t>(self, 0x24, cx, cz)) {
                        water = true;
                        break;
                    }
        }
    }
    if (water)
        return 6;
    return notWater ? 5 : 8;
}

// 0x0072AC60: the node of a cell pair in one of the maps +0xD8/+0xE4, or
// the head.
char* FindCell(char* map, const int32_t* key)
{
    const auto Less = [](const int32_t* a, const int32_t* b) { return a[0] != b[0] ? a[0] < b[0] : a[1] < b[1]; };
    char* const head = At<char*>(map, 0x0);
    char* found = head;
    for (char* node = At<char*>(head, 0x4); node;) {
        if (Less(&At<int32_t>(node, 0x10), key)) {
            node = At<char*>(node, 0xC);
        } else {
            found = node;
            node = At<char*>(node, 0x8);
        }
    }
    if (found == head || Less(key, &At<int32_t>(found, 0x10)))
        return head;
    return found;
}

// 0x0072C050: when only highlighting, a cell in the map +0xE4 is not asked
// for its lot.
uint32_t __fastcall CellIsTaken(char* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("CellIsTaken", logged);
    const int32_t key[2] = {x, z};
    bool askLot = true;
    if (!At<uint8_t>(self, field::kPlaceStreets)) {
        char* const map = self + field::kCellsE4;
        askLot = FindCell(map, key) == At<char*>(map, 0x0);
    }
    if (askLot && Virtual<void*>(LotManager(self), 0x18, x, z, 0u))
        return 1;
    if (!At<uint8_t>(self, field::kPlaceStreets)) {
        char* const map = self + field::kCellsD8;
        if (FindCell(map, key) != At<char*>(map, 0x0))
            return 1;
    }
    return 0;
}

// 0x0072ABE0: a subtree's nodes freed (the values own nothing).
void EraseNodes(char* node)
{
    while (node) {
        EraseNodes(At<char*>(node, 0xC));
        char* const left = At<char*>(node, 0x8);
        Free(node);
        node = left;
    }
}

void ClearTree(char* tree)
{
    if (!At<uint32_t>(tree, 0x4))
        return;
    EraseNodes(At<char*>(At<char*>(tree, 0x0), 0x4));
    char* const head = At<char*>(tree, 0x0);
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0xC) = head;
    At<uint32_t>(tree, 0x4) = 0;
}

// 0x0072C0E0: the two cell maps emptied, the tools +0x48 and +0x4C reset
// (slot 5) and the terrain picker's slot 9 (a tail jump in the game: all of
// EAX is its result).
uint32_t __fastcall ClearCellMaps(char* self, void*)
{
    static bool logged = false;
    FirstCall("ClearCellMaps", logged);
    ClearTree(self + field::kCellsD8);
    ClearTree(self + field::kCellsE4);
    if (void* const tool = At<void*>(self, field::kStreetTool))
        Virtual<void>(tool, 0x14);
    if (void* const tool = At<void*>(self, field::kTool4C))
        Virtual<void>(tool, 0x14);
    if (void* const picker = At<void*>(self, field::kTerrainPicker))
        return Virtual<uint32_t>(picker, 0x24);
    return 0;
}

// 0x0072C180: the entry of the map +0xF0 for a key of four values (each
// shifted 4 bits over the next), the entry for 0 when it has none; when
// that is missing too the head node is read, as the game does.
void __fastcall LookupStyle(char* self, void*, const int32_t* values, uint32_t* a, uint8_t* b, uint8_t* c)
{
    static bool logged = false;
    FirstCall("LookupStyle", logged);
    const auto Find = reinterpret_cast<FindFn>(addr::SC4Sint32Map_Find);
    char* const map = self + field::kMapF0;
    int32_t key = static_cast<int32_t>(
        (((((static_cast<uint32_t>(values[0]) << 4) + static_cast<uint32_t>(values[1])) << 4) +
           static_cast<uint32_t>(values[2]))
          << 4) +
        static_cast<uint32_t>(values[3]));
    char* node = nullptr;
    Find(map, &node, &key);
    if (node == At<char*>(map, 0x0)) {
        key = 0;
        Find(map, &node, &key);
    }
    *a = At<uint32_t>(node, 0x14);
    *b = At<uint8_t>(node, 0x18);
    *c = At<uint8_t>(node, 0x19);
}

// --- Installation --------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 5, addr::cSC4ZoneDeveloper_LotManager_GetLotAtPosition,
                 GetLotAtPosition),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 6, addr::cSC4ZoneDeveloper_LotManager_GetLot, GetLot),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 8, addr::cSC4ZoneDeveloper_LotManager_GetNeighboursOfCell,
                 GetNeighboursOfCell),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 9, addr::cSC4ZoneDeveloper_LotManager_GetNeighbours,
                 GetNeighbours),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 11, addr::cSC4ZoneDeveloper_LotManager_GetSideLine, GetSideLine),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 12, addr::cSC4ZoneDeveloper_LotManager_GetSideCells,
                 GetSideCells),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 13, addr::cSC4ZoneDeveloper_LotManager_GetRandomLot,
                 GetRandomLot),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 14, addr::cSC4ZoneDeveloper_LotManager_GetOccupantLot,
                 GetOccupantLot),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 17, addr::cSC4ZoneDeveloper_LotManager_GetVertexAltitude,
                 GetVertexAltitude),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 18, addr::cSC4ZoneDeveloper_LotManager_GetAltitudeAtPosition,
                 GetAltitudeAtPosition),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 19, addr::cSC4ZoneDeveloper_LotManager_GetCellAltitude,
                 GetCellAltitude),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 26, addr::cSC4ZoneDeveloper_LotManager_GetVertexLimits,
                 GetVertexLimits),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable, 7, addr::cSC4ZoneDeveloper_SetOptions, SetOptions),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable, 9, addr::cSC4ZoneDeveloper_IsCellWater, IsCellWater),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_LotFilter_vftable, 3, addr::cSC4ZoneDeveloper_LotFilterTest, LotFilterTest),
};

#undef OPENSC4_SLOT

bool InstallSlots(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : kSlots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("zone slots: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : kSlots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("zone slots: %u slots are ours", static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])));
    return true;
}

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// The originals are not called again; MinHook's trampolines are kept here.
const Detour kDetours[] = {
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_IsStreetLike, IsStreetLikeThunk),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072A010, AnyNetworkAt),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072A040, CellHasZone),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072A2A0, DrawStreet),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072AD00, PlaceRecordCells),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072AF30, TrimRegion),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072B180, ClearZones),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072B480, CheckRecords),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072C050, CellIsTaken),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072C0E0, ClearCellMaps),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072C180, LookupStyle),
};

#undef OPENSC4_DETOUR

void* g_originals[sizeof(kDetours) / sizeof(kDetours[0])];

bool InstallHelpers(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    bool ok = true;
    for (std::size_t i = 0; i < sizeof(kDetours) / sizeof(kDetours[0]); ++i) {
        const addr::Entry* const entry = addr_check::Find(kDetours[i].address);
        ok = entry && hook::Install(*entry, kDetours[i].ours, &g_originals[i]) && ok;
    }
    return ok;
}

const bool g_registered[] = {
    registry::Register({"simulators.zone.slots", "simulators/zone", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ZoneDeveloper_vftable_4,
                        "Our lot manager queries (slots 5, 6, 8, 9, 11-14, 17-19, 26), the zone developer's "
                        "SetOptions and IsCellWater and the lot filter's test (src/core/zone_slots.cpp)",
                        false, InstallSlots}),
    registry::Register({"simulators.zone.helpers", "simulators/zone", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4ZoneDeveloper_Fn_0072A2A0,
                        "Our zoning helpers the rest of the class calls: the street drawing, region trimming, "
                        "dezoning, record tests and cell tests (detours; src/core/zone_slots.cpp)",
                        false, InstallHelpers}),
};

} // namespace
} // namespace opensc4::zone_sim
