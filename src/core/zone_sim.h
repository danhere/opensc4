// Shared between the parts of our cSC4ZoneDeveloper (build 641): the class
// map, the vtables slot by slot and the object's fields as far as they are
// known. The zone developer zones and dezones land and cuts zoned areas
// into parcels (lots) with streets; its +0x4 part is the city's lot manager
// (the grid of lots by cell, the list of lots, CanCreateLot, CreateLot,
// DeleteLot). Written by five parts by address range (zone_object.cpp,
// zone_cells.cpp, zone_parcels.cpp, zone_streets.cpp, zone_parcellize.cpp).
//
// The class: class ID 0x498F9B01, 0xFC bytes, constructor 0x007348D0 (only
// caller the factory 0x00734AF0, registered at 0x0046047E), destructor
// 0x00734680, deleting destructor 0x00734AD0. Code 0x00729E70 to 0x00734B10
// (the function before 0x00729E70 and the one at 0x00734B10 are other
// classes'). The city's slot 75 (+0x12C) gives the object, slot 33 (+0x84)
// and the global 0x00B43D08 its lot manager part (object + 4).
//
// QueryInterface (0x00729EF0): IIDs 0x00000001 and 0xE818ED2B
// (cISC4ZoneDeveloper) the object; 0x681905BD (the lot manager) +0x4;
// 0x452294AA (cIGZMessageTarget2) +0x8; 0xE4FDA3D4 (cIGZSerializable) +0xC.
//
// --- +0x0: cISC4ZoneDeveloper, vtable 0x00AB3830, 11 slots -----------------
//    0 QueryInterface 0x00729EF0        1 AddRef 0x00695330 (shared code)
//    2 Release 0x0072A270               3 Init 0x00734030
//    4 Shutdown 0x007335A0
//    5 HighlightParcels 0x00733F30 (cell region, zone type, the cell under
//      the cursor (int32 x, z; may be null) kept at +0xA8 for the call, and
//      a cell region** that receives a new region or null: an out pointer,
//      not a flag (0x00733770 writes it); ret 0x10)
//    6 DoParcellization 0x00733FA0 (cell region, zone type, flag; ret 0xC)
//    7 SetOptions 0x007347C0 (three bytes: +0x9B, +0x9A, +0x9C; ret 0xC)
//    8 ExistsNetworkOfType 0x0072FE10 (ret 0xC)
//    9 IsCellWater 0x0072A030 (x, z: a tail jump to the terrain's slot 34)
//   10 the deleting destructor 0x00734AD0
//
// --- +0x4: the lot manager, vtable 0x00AB37C0, 28 slots --------------------
// (`this` is object + 4: the disassembly of these reads fields 4 lower.)
//    0-4 thunks: QueryInterface 0x007347F0, AddRef 0x005200D0, Release
//      0x00734820, Init 0x007348C0, Shutdown 0x007348B0
//    5 0x0072A1C0 the lot at a position (const float[3]): the city's slot 98
//      (x, z in the city), slot 95 (the cell), then slot 6; ret 4
//    6 0x0072B410 GetLot(x, z, bool anchorOnly): the lot of the cell, with
//      anchorOnly only when the cell is the lot's own cell (lot slot 21);
//      ret 0xC
//    7 0x0072E080 the lots in a rectangle (list, x0, z0, x1, z1, bool
//      anchorOnly); the count [zone_cells.cpp]
//    8 0x0072A230 (x, z, list, sides): slot 9 for the lot of the cell; ret 0x10
//    9 0x0072A750 (lot, list, sides): slot 10 for the lot's rectangle (lot
//      slot 14); ret 0xC
//   10 0x0072E2B0 the lots beside a rectangle (rectangle, list, sides) [zone_cells.cpp]
//   11 0x0072A790 (rectangle, side, out): the rectangle's edge on the side
//      (1, 2, 4 or 8) as a line of vertices; ret 0xC
//   12 0x0072A820 (rectangle, side, out): the row of cells just outside the
//      rectangle on the side; false when the city ends there; ret 0xC
//   13 0x0072B3C0 a random lot: up to 999 cells drawn from the generator at
//      0x00B4B398 until one has a lot (null when none has)
//   14 0x0072A110 the lot under an occupant (its slot 8 position, to cells
//      of 16 m), through slot 6; ret 4
//   15 0x00734850 a new iterator over the lot list +0x60 (8 bytes from
//      operator new, registered in the list's iterator list); thunk to
//      0x00734860 with the list
//   16 0x00809250 (shared code: returns the dword at this + 0x64, the lot
//      list's size)
//   17 0x0072A8F0 (x, z, except lot, bool* found): the foundation height
//      (lot slot 16) of the first lot around the vertex with flag bit 0
//      (lot slot 12), else the terrain's slot 14 at the vertex; ST0; ret 0x10
//   18 0x0072A9F0 (float x, float z, bool* found): the same at a position,
//      the one lot of the position's cell; else the terrain's slot 13; ret 0xC
//   19 0x0072AAC0 (cell x, cell z, bool* found): the same for a cell, else
//      the terrain's slot 13 at the cell's centre; ret 0xC
//   20 0x0072C2D0 CanCreateLot (7 arguments; 8 = yes) [zone_cells.cpp]
//   21 0x007306C0 (6 arguments) [zone_streets.cpp]
//   22 0x00730A80 (9 arguments) [zone_streets.cpp]
//   23 0x0072E520 CreateLot (6 arguments) [zone_parcels.cpp]
//   24 0x0072F8E0 DeleteLot (1 argument) [zone_parcels.cpp]
//   25 0x0072CAE0 (4 arguments) [zone_cells.cpp]
//   26 0x0072AE20 (x, z, float* a, float* b): 3.4e38 into both, 32.0 when
//      one of the up to four lots around the vertex is in state 2 or more
//      (lot slot 33); ret 0x10
//   27 0x00809240 (shared code: returns the dword at this + 0x70, which is
//      the object's +0x74: the grid kGrid)
//
// --- +0x8: cIGZMessageTarget2, vtable 0x00AB37AC, 4 slots ------------------
//    0-2 thunks 0x00734800, 0x005200F0, 0x00734830; 3 DoMessage 0x007328D0
//
// --- +0xC: cIGZSerializable, vtable 0x00AB3794, 6 slots --------------------
//    0-2 thunks 0x00734810, 0x00520110, 0x00734840; 3 Write 0x0072AB40;
//    4 Read 0x00734380; 5 GetGZCLSID 0x00729EB0
//
// A helper class of the zoning code, a lot configuration filter (base
// destructor 0x00489660; object {vtable, 0, +8, +0xC, +0x10 limits}), has
// its vtable at 0x00AB36FC (5 slots: 0x00489670,
// 0x005F47F0, 0x0076A370, the test 0x0072A080, the deleting destructor
// 0x0072A730 shared with two other filters).
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::zone_sim {

constexpr uint32_t kClassId = 0x498F9B01;
constexpr uint32_t kIidZoneDeveloper = 0xE818ED2B;
constexpr uint32_t kIidLotManager = 0x681905BD;
constexpr std::size_t kObjectSize = 0xFC;

// Slot counts of the four vtables.
constexpr int kSlots = 11, kLotManagerSlots = 28, kMessageSlots = 4, kSerializableSlots = 6;

// The parts of the object.
constexpr std::size_t kLotManagerPart = 0x4;
constexpr std::size_t kMessagePart = 0x8;
constexpr std::size_t kSerializablePart = 0xC;

// Fields of cSC4ZoneDeveloper (offsets from the object's start). Names are
// ours. "ctor" gives the constructor's value where it is not 0; evidence is
// the function that shows the meaning.
namespace field {
constexpr std::size_t kInitialized = 0x10;   // uint8; Init sets it last, Shutdown clears it first
constexpr std::size_t kLoaded = 0x11;        // uint8; Read sets 1; message 0x26D31EC1 then calls each lot's slot 8 (0x007329F2)
constexpr std::size_t kRefCount = 0x14;      // int32 (Release 0x0072A270)
constexpr std::size_t kMessages = 0x18;      // the message server (0x00B43CCC), Init
constexpr std::size_t kView = 0x1C;          // 0x00B43DD0, Init; its slot 10 object's +0x14 goes to +0xD4
constexpr std::size_t kDevelopment = 0x20;   // the building development simulator (0x00B43D68), Init
constexpr std::size_t kCity = 0x24;          // 0x00B43CEC, Init
constexpr std::size_t kCellsX = 0x28;        // int32, the city's slot 93 (Init)
constexpr std::size_t kCellsZ = 0x2C;        // int32, the city's slot 94 (Init)
constexpr std::size_t kOccupants = 0x30;     // the occupant manager (0x00B43D0C), Init
constexpr std::size_t kTerrain = 0x34;       // 0x00B43CF4, Init; IsCellWater is its slot 34
constexpr std::size_t kTerrainPicker = 0x38; // what 0x00602160 returns; taken at message 0x26D31EC1, released by Shutdown
constexpr std::size_t kTraffic = 0x3C;       // the traffic simulator (0x00B43DA8), Init
constexpr std::size_t kZones = 0x40;         // the zone manager (0x00B43D14), Init
constexpr std::size_t kTool44 = 0x44;        // network tool of type 11 (the network manager's slot 5 with (11, 1)), message 0x26D31EC1
constexpr std::size_t kStreetTool = 0x48;    // network tool of type 3, the same message; its slot 7 places the streets (0x0072A0D0)
constexpr std::size_t kTool4C = 0x4C;        // network tool of type 0, the same message
constexpr std::size_t kLotDeveloper = 0x50;  // 0x00B43D20, Init (replaced when it changed)
constexpr std::size_t kLotGrid = 0x54;       // vector<vector<cISC4Lot*>> (begin, end, capacity; 12-byte columns): [x][z], not referenced
constexpr std::size_t kLotList = 0x60;       // list of lots (head node; nodes {next, previous, lot}), one reference each (Read 0x00734380)
constexpr std::size_t kLotIterators = 0x64;  // list of the live iterators over +0x60 (8-byte iterators {node, list}; slot 15)
constexpr std::size_t kLotCount = 0x68;      // uint32, the size of +0x60 (lot manager slot 16)
constexpr std::size_t kUnknown6C = 0x6C;     // two dwords the constructor clears (+0x6C, +0x70); nothing in the class reads them
constexpr std::size_t kGrid = 0x74;          // a cSC4SimGridUint16 (constructor 0x0045F9B0), ID 0x69D5C456, tract size 4, Init; what lot manager slot 27 returns (its this + 0x70)
constexpr std::size_t kTree78 = 0x78;        // map (head, size +0x7C; 0x20-byte nodes: key +0x10, a map at +0x14 of maps); freed by 0x00734600
constexpr std::size_t kTuning84 = 0x84;      // five int32 the constructor sets to 2, 2, 1, 3, 6: lot width +0x84, lot depth +0x88, smallest +0x8C and largest +0x90 depth, street spacing +0x94 (0x00732BF0 writes them, 0x0072EDD0 reads them); +0x80 is not set
constexpr std::size_t kByte98 = 0x98;        // uint8, ctor 0; 0x00731C80 sets it when the main direction is the best side, 0x0072EDD0 reads it
constexpr std::size_t kPlaceStreets = 0x99;  // uint8, ctor 1; HighlightParcels clears it for its call; the street tool's commit argument
constexpr std::size_t kOption9A = 0x9A;      // uint8, ctor 1; SetOptions' second argument
constexpr std::size_t kOption9B = 0x9B;      // uint8; SetOptions' first argument
constexpr std::size_t kOption9C = 0x9C;      // uint8; SetOptions' third argument
constexpr std::size_t kZoneType = 0xA0;      // the zone type of the operation under way, 0 outside one
constexpr std::size_t kRegion = 0xA4;        // the cell region of the operation (0x24 bytes: rectangle, bit rows at +0x10)
constexpr std::size_t kValueA8 = 0xA8;       // HighlightParcels' third argument during its call: a pointer to the cursor's cell (int32 x, z) or null
constexpr std::size_t kRectAC = 0xAC;        // int32[4] the constructor clears last (+0xAC to +0xB8): the scores of the region's sides (north, east, south, west; 0x00731C80). +0xBC to +0xC8, not set by the constructor: int32[4] the sides' classes (0x0072FAB0 raises them, 0x0072EDD0 reads them)
constexpr std::size_t kValueCC = 0xCC;       // two int32, ctor 0: the second direction +0xCC and the main direction +0xD0 of the parcels (side numbers 0 to 3, -1 for none; 0x00731C80 writes them)
constexpr std::size_t kValueD4 = 0xD4;       // Init: +0x14 of the view's slot 10 object; message 0xA6B79602 stores the dword its void 2 points at
constexpr std::size_t kCellsD8 = 0xD8;       // map keyed by a cell pair (head, size +0xDC; 0x24-byte nodes); cleared by 0x0072C0E0
constexpr std::size_t kCellsE4 = 0xE4;       // the same kind of map (head, size +0xE8)
constexpr std::size_t kMapF0 = 0xF0;         // map<int32, ...> (head, size +0xF4; 0x1C-byte nodes: key +0x10, values +0x14, +0x18, +0x19)
} // namespace field

// The cell region a zoning operation works on (+0xA4 and arguments).
namespace region {
constexpr std::size_t kX0 = 0x0, kZ0 = 0x4, kX1 = 0x8, kZ1 = 0xC; // inclusive cells
constexpr std::size_t kCellsVtable = 0x10; // 0x00A86364
constexpr std::size_t kWidth = 0x14;       // uint32 columns
constexpr std::size_t kDepth = 0x18;       // uint32 cells per column
constexpr std::size_t kRows = 0x20;        // uint32** : rows[x - x0][(z - z0) >> 5] bit (z - z0) & 31
} // namespace region

// The lot of a cell in the grid +0x54 (no bounds check, as the game's inline code).
inline void*& GridLot(void* self, int32_t x, int32_t z)
{
    return At<void**>(At<char*>(self, field::kLotGrid) + static_cast<std::ptrdiff_t>(x) * 12, 0)[z];
}

// Whether a cell is in a cell region (the game's inline test and 0x00466610).
inline bool RegionHas(void* cells, int32_t x, int32_t z)
{
    const uint32_t dx = static_cast<uint32_t>(x - At<int32_t>(cells, region::kX0));
    const uint32_t dz = static_cast<uint32_t>(z - At<int32_t>(cells, region::kZ0));
    if (dx >= At<uint32_t>(cells, region::kWidth) || dz >= At<uint32_t>(cells, region::kDepth))
        return false;
    return (At<uint32_t**>(cells, region::kRows)[dx][dz >> 5] & (1u << (dz & 31))) != 0;
}

// Logs a part's first call once ("zone: first %s"; defined in zone_object.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::zone_sim
