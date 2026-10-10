// Shared between the parts of our cSC4LandfillZone (landfillzone_sim.cpp:
// the tiles, the month, the effects, Init and Shutdown; landfillzone_cells.cpp:
// the occupants on the cells, zoning, the save and our own object).
//
// cSC4LandfillZone (build 641; code 0x006BA120-0x006BD4B0): one object
// per landfill zone, owned by the landfill developer's list
// (src/core/landfill_sim.cpp, which calls twenty zone functions at their
// addresses; the pollution simulator calls none: the zones call its slots
// 21, 29, 50 and 51). A zone keeps the cells zoned as landfill, a grid of
// tiles holding the garbage, the pictures on its cells and the effects
// around it.
//
// The object (0x70 bytes; constructors 0x006BBB60 and 0x006BBD60, destructor
// 0x006BBBC0; vtable 0x00AB1610 with 5 slots: QueryInterface 0x005DC8A0,
// AddRef 0x005F47F0 and DoMessage 0x004FC900 shared with other classes,
// Release 0x006BB8C0 and the deleting destructor 0x006BBD40 its own):
//   +0x04 uint32  reference count (unsigned compare in Release)
//   +0x08 uint8   initialised (Init sets it, Shutdown clears it)
//   +0x0C the region (SC4CellRegion, 0x24 bytes from operator new: x0, z0,
//         x1, z1 inclusive, then the bit map: vtable 0x00A86364, +0x14
//         width, +0x18 height, +0x1C words per column, +0x20 columns)
//   +0x10 int32   tile count = +0x14 * +0x18
//   +0x14 int32   tile rows (z1 - z0), +0x18 int32 tiles per row (x1 - x0):
//                 one less than the cells each way, as the game has it
//   +0x1C vector<Tile> (begin, end, limit; storage from the STL allocator)
//   +0x28 uint8   empty (IsEmpty's cache), +0x29 uint8 active: road access,
//                 set by OnMonth
//   +0x2C int32   the count of set cells, valid while +0x30 (dirty) is 0
//   +0x34 uint32  capacity: active tiles times the capacity per tile
//   +0x38 float   the level the ground was flattened to
//   +0x3C, +0x40  the cell FindRoadAccess found (the effects' place)
//   +0x44 the marker prop placed while the zone has no road access
//   +0x48, +0x4C  automata controllers, +0x50 the visual effect
//   +0x54 the automata controller manager (city slot 47), +0x58 the effects
//         manager (0x00B43D1C), +0x5C the city, +0x60 the terrain, +0x64 the
//         pollution simulator, +0x68 the occupant manager (0x00B43D0C),
//         +0x6C the zone manager (city slot 36)
// A tile (0x20 bytes; vtable 0x00AB15DC, one slot, the deleting destructor):
//   +0x04, +0x08 its place in the grid, +0x0C uint8 active, +0x0D uint8 has
//   garbage (its cells show garbage pictures), +0x10 0, +0x14 capacity,
//   +0x18 use, +0x1C an index (row or sequence number; never read)
//
// Groups (each off by default, installed at PostAppInit):
//   simulators.landfillzone.sim     Release and the deleting destructor in
//                                   the vtable, the tile's destructor slot,
//                                   and detours on DropEffects,
//                                   CountActiveTiles, Level, GetUsage,
//                                   GetFreeCapacity, GetMaintenanceCost,
//                                   IsEmpty, UpdateEffects (0x006BB1D0),
//                                   Init, Shutdown, UpdateCapacity, OnMonth
//   simulators.landfillzone.tiles   detours on AddRegion, ClearCell, Refresh
//                                   (0x006BCD70) and OnTilesChanged
//   simulators.landfillzone.save    detours on Write and Read
//   simulators.landfillzone.object  our own object: detours on the two
//                                   constructors and the destructor,
//                                   identity and Release from a copy of the
//                                   (patched) vtable
// The detoured functions are the twenty the landfill developer calls
// directly (no other code outside the class calls any). One register
// assumption: the game's SpreadUse (0x006BABC0) uses ECX after calling
// GetUsage, so that detour goes through a thunk keeping ECX and EDX. Calls
// from one group into another go through the game's address, so each group
// can be switched alone.

#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::landfillzone_sim {

// Logs "landfillzone: first <name>" once per function.
void FirstCall(const char* name, bool& logged);

constexpr uint32_t kZoneLandfill = 0xE;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidMessageTarget2 = 0x452294AA;
constexpr uint32_t kIidOccupant = 0x26BB767E;
constexpr uint32_t kIidLandfillOccupant = 0x2A15F262;
constexpr uint32_t kIidBaseTexture = 0x097F6D98;
constexpr uint32_t kClassBaseTexture = 0xC97F987C;
constexpr uint32_t kTypeLandfillOccupant = 0x6A11BD2B;
constexpr uint32_t kTypeBaseTexture = 0x497F6D9D;
constexpr uint32_t kTypeCleared = 0x74758926;
constexpr uint32_t kOccupantMoved = 0x99EF1145;
constexpr uint32_t kMarkerProp = 0x107A0000;
constexpr uint32_t kNetworkMask = 0x1F4F;
constexpr uint32_t kRandomPicture = 0x02FA0000;

namespace field {
constexpr std::size_t kRefCount = 0x4;
constexpr std::size_t kInitialized = 0x8;
constexpr std::size_t kRegion = 0xC;
constexpr std::size_t kTileCount = 0x10;
constexpr std::size_t kTileRows = 0x14;
constexpr std::size_t kTileColumns = 0x18;
constexpr std::size_t kTiles = 0x1C;
constexpr std::size_t kEmpty = 0x28;
constexpr std::size_t kActive = 0x29;
constexpr std::size_t kCellCount = 0x2C;
constexpr std::size_t kCellsDirty = 0x30;
constexpr std::size_t kCapacity = 0x34;
constexpr std::size_t kLevel = 0x38;
constexpr std::size_t kAccessX = 0x3C;
constexpr std::size_t kAccessZ = 0x40;
constexpr std::size_t kMarker = 0x44;
constexpr std::size_t kController1 = 0x48;
constexpr std::size_t kController2 = 0x4C;
constexpr std::size_t kEffect = 0x50;
constexpr std::size_t kControllers = 0x54;
constexpr std::size_t kEffects = 0x58;
constexpr std::size_t kCity = 0x5C;
constexpr std::size_t kTerrain = 0x60;
constexpr std::size_t kPollution = 0x64;
constexpr std::size_t kOccupants = 0x68;
constexpr std::size_t kZoneManager = 0x6C;
} // namespace field

// A cell region's bit map (the part at +0x10) and the region.
struct Cells {
    std::uintptr_t vtable;
    uint32_t width;
    uint32_t height;
    uint32_t words;
    uint32_t** columns;
};
struct Region {
    int32_t x0, z0, x1, z1;
    Cells cells;
};
static_assert(sizeof(Region) == 0x24);

struct Tile {
    std::uintptr_t vtable;
    int32_t x;
    int32_t z;
    uint8_t active;
    uint8_t garbage;
    uint8_t pad[2];
    uint32_t unused10;
    uint32_t capacity;
    uint32_t used;
    uint32_t index;
};
static_assert(sizeof(Tile) == 0x20);

struct TileVector {
    Tile* begin;
    Tile* end;
    Tile* limit;
};

struct Node {
    Node* next;
    Node* previous;
    void* value;
};

// An entry of the game's picture table at 0x00B0D2D0.
struct Picture {
    uint32_t index;
    uint32_t exemplar;
    int32_t rotation;
};

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void*(__thiscall*)(void* self);
using ConstructCellsFn = void*(__thiscall*)(Cells* cells, uint32_t width, uint32_t height, const uint8_t* fill);
using DestroyCellsFn = void(__thiscall*)(Cells* cells);
using IntersectFn = uint8_t(__thiscall*)(int32_t* out, const int32_t* a, const int32_t* b);
using GetSint64Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int64_t* value);
using StreamValueFn = void*(__cdecl*)(void* stream, uint32_t value);
using StreamPointerFn = void*(__cdecl*)(void* stream, void* value);
using ReadReferenceFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void** object, uint32_t flags);
using WalkStartFn = void(__thiscall*)(void* walk, int32_t x, int32_t z);
using WalkNextFn = uint8_t(__thiscall*)(void* walk, int32_t* x, int32_t* z);
using OccupantFn = void*(__thiscall*)(void* self, uint32_t exemplar, uint32_t rotation);
using LevelFn = void(__thiscall*)(void* zone, const int32_t* rect, uint32_t levelBits, uint32_t terrain);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto ConstructCells = reinterpret_cast<ConstructCellsFn>(addr::SC4CellRegion_ConstructCells);
const auto DestroyCells = reinterpret_cast<DestroyCellsFn>(addr::SC4CellRegion_DestroyCells);
// The zone functions of other groups, at the game's addresses.
const auto GameLevel = reinterpret_cast<LevelFn>(addr::cSC4LandfillZone_Level);
const auto GameShutdown = reinterpret_cast<SelfFn>(addr::cSC4LandfillZone_Shutdown);
const auto GameDestructor = reinterpret_cast<SelfFn>(addr::cSC4LandfillZone_Destructor);

inline Region*& RegionOf(char* self) { return At<Region*>(self, field::kRegion); }
inline TileVector& Tiles(char* self) { return At<TileVector>(self, field::kTiles); }
inline Picture* Pictures() { return reinterpret_cast<Picture*>(addr::cSC4LandfillZone_Pictures); }

inline uint32_t AddRef(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t Release(void* object) { return Virtual<uint32_t>(object, 0x8); }

// Whether a cell is set: false outside the bit map (unsigned compares on
// the offsets from x0, z0).
inline bool CellSet(const Region* region, int32_t x, int32_t z)
{
    const uint32_t dx = static_cast<uint32_t>(x - region->x0);
    const uint32_t dz = static_cast<uint32_t>(z - region->z0);
    if (dx >= region->cells.width || dz >= region->cells.height)
        return false;
    return (region->cells.columns[dx][dz >> 5] & (1u << (dz & 0x1F))) != 0;
}

// A cell's bit set or cleared with no bounds test, as the game does where
// it knows the cell is inside.
inline void SetCell(Region* region, int32_t x, int32_t z, bool set)
{
    const uint32_t dz = static_cast<uint32_t>(z - region->z0);
    uint32_t& word = region->cells.columns[x - region->x0][dz >> 5];
    if (set)
        word |= 1u << (dz & 0x1F);
    else
        word &= ~(1u << (dz & 0x1F));
}

inline void SetBit(uint32_t* column, uint32_t bit) { column[bit >> 5] |= 1u << (bit & 0x1F); }

// The inline destruction of a bit map's blocks: every column, then the
// table (nothing for no table).
inline void FreeColumns(uint32_t** columns, uint32_t width)
{
    if (!columns)
        return;
    for (uint32_t i = 0; i < width; ++i)
        OperatorDelete(columns[i]);
    OperatorDelete(columns);
}

// The signed half the game takes of two cell coordinates (cdq, sub, sar).
inline int32_t Middle(int32_t a, int32_t b) { return (a + b) / 2; }

// The world position of a cell's centre (city slot 97).
inline void CellCentre(char* self, int32_t x, int32_t z, float* worldX, float* worldZ)
{
    Virtual<void>(At<void*>(self, field::kCity), 0x184, x, z, worldX, worldZ);
}

// The terrain's height at a position (slot 13), as left in ST0.
inline double HeightAt(char* self, float worldX, float worldZ)
{
    return Virtual<double>(At<void*>(self, field::kTerrain), 0x34, worldX, worldZ);
}

// Whether the city's traffic network has something at a cell (city slot 40,
// its slot 8; the city from the global, as the game reads it here).
inline bool NetworkAt(int32_t x, int32_t z)
{
    void* const network = Virtual<void*>(Global<void*>(addr::SC4_City), 0xA0);
    return Virtual<uint32_t>(network, 0x20, x, z, kNetworkMask, 1u) != 0;
}

// The float-to-int rounding the game does by adding 1.5 * 2^23 (the float
// at 0x00AB15D4) and taking the float's bits less those of the constant.
inline uint32_t RoundBits(double value)
{
    const float biased = static_cast<float>(value + 12582912.0);
    uint32_t bits = 0;
    std::memcpy(&bits, &biased, sizeof bits);
    return bits + 0xB4C00000u;
}

// An unsigned 32-bit value as the game loads it (fild, then + 2^32 when
// the sign bit is set).
inline double UnsignedToDouble(uint32_t value) { return static_cast<double>(value); }

// --- Tiles ---------------------------------------------------------------------------------

inline void AssignTile(Tile* destination, const Tile* source)
{
    destination->x = source->x;
    destination->z = source->z;
    destination->active = source->active;
    destination->garbage = source->garbage;
    destination->unused10 = source->unused10;
    destination->capacity = source->capacity;
    destination->used = source->used;
    destination->index = source->index;
}

inline void ConstructTile(Tile* destination, const Tile* source)
{
    destination->vtable = addr::cSC4LandfillZone_TileVftable;
    AssignTile(destination, source);
}

// The tile every grid starts from: active, no garbage, nothing in it.
inline Tile BlankTile()
{
    Tile tile = {};
    tile.vtable = addr::cSC4LandfillZone_TileVftable;
    tile.active = 1;
    return tile;
}

// Every tile of a range destroyed through its vtable (slot 0 (0)).
inline void DestroyTiles(Tile* first, Tile* last)
{
    for (; first != last; ++first)
        Virtual<void*>(first, 0x0, 0u);
}

// --- The parts' functions the other part calls ----------------------------------------------

// landfillzone_sim.cpp
Tile* CopyTiles(Tile* first, Tile* last, Tile* destination);
void CopyTileVector(TileVector* self, const TileVector* other);
Tile* EraseTiles(TileVector* self, Tile* first, Tile* last);
void PushTile(TileVector* tiles, const Tile* value);
uint8_t ReadTile(Tile* tile, void* stream);
Region* CopyRegion(Region* self, const Region* other);
void DeactivateTilesAt(char* self, int32_t x, int32_t z);
void RemoveMarker(char* self);

// landfillzone_cells.cpp
void DeactivateCell(char* self, int32_t x, int32_t z);
int64_t GetCellCost(int32_t x, int32_t z);
uint8_t PlaceTileOccupant(char* self, int32_t x, int32_t z, uint32_t exemplarId, uint32_t rotation);

// A detour on a zone function, and the installation of a table of them
// (false when an address has no database entry or a hook is refused).
struct Detour {
    std::uintptr_t address;
    void* ours;
};
bool InstallDetours(const Detour* detours, std::size_t count, const char* what);

} // namespace opensc4::landfillzone_sim
