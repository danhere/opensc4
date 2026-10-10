// Our cSC4LandfillZone, first part (build 641; the class map, the fields and
// the groups are in landfillzone_sim.h): the tiles and their vector, the
// cell regions, the usage and capacity, the level, the road access and the
// effects, Init, Shutdown and Release, the month; the group
// simulators.landfillzone.sim and the installation of every group's
// detours.

#include "core/landfillzone_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>

namespace opensc4::landfillzone_sim {
namespace {

// --- Tiles and their vector ----------------------------------------------------------------

// Slot 0 of the tile's vtable (0x006BA280).
void* __fastcall TileDeletingDestructor(Tile* tile, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("TileDeletingDestructor", logged);
    tile->vtable = addr::cSC4LandfillZone_TileVftable;
    if (flags & 1)
        OperatorDelete(tile);
    return tile;
}

// 0x006BB5C0: std::uninitialized_copy.
Tile* CopyConstructTiles(Tile* first, Tile* last, Tile* destination)
{
    static bool logged = false;
    FirstCall("CopyConstructTiles", logged);
    for (; first != last; ++first, ++destination)
        if (destination)
            ConstructTile(destination, first);
    return destination;
}

// 0x006BB120: std::uninitialized_fill_n.
Tile* FillTiles(Tile* destination, uint32_t count, const Tile* value)
{
    static bool logged = false;
    FirstCall("FillTiles", logged);
    for (; count != 0; --count, ++destination)
        if (destination)
            ConstructTile(destination, value);
    return destination;
}

// 0x006BBC10: the reallocating insert of `count` copies before `position`.
void InsertTiles(TileVector* self, Tile* position, const Tile* value, uint32_t count, bool atEnd)
{
    static bool logged = false;
    FirstCall("InsertTiles", logged);
    const uint32_t size = static_cast<uint32_t>(self->end - self->begin);
    const uint32_t capacity = size + (size < count ? count : size);
    Tile* const storage = capacity != 0 ? static_cast<Tile*>(Allocate(capacity * sizeof(Tile))) : nullptr;
    Tile* end = CopyConstructTiles(self->begin, position, storage);
    if (count == 1) {
        if (end)
            ConstructTile(end, value);
        ++end;
    } else {
        end = FillTiles(end, count, value);
    }
    if (!atEnd)
        end = CopyConstructTiles(position, self->end, end);
    DestroyTiles(self->begin, self->end);
    if (self->begin)
        Free(self->begin);
    self->end = end;
    self->begin = storage;
    self->limit = storage + capacity;
}

// 0x006BA330: a tile's monthly decay: the use less min(use, the rounded
// share clamped to 1 and the tuned most; unsigned compares).
void DecayTile(Tile* tile)
{
    static bool logged = false;
    FirstCall("DecayTile", logged);
    const uint32_t used = tile->used;
    if (used == 0)
        return;
    uint32_t decay = RoundBits(UnsignedToDouble(used) *
                               static_cast<double>(Global<float>(addr::LandfillTuning_DecayRate)));
    if (decay < 1) {
        decay = 1;
    } else {
        const uint32_t most = Global<uint32_t>(addr::LandfillTuning_Value4A677E5E);
        if (decay > most)
            decay = most;
    }
    tile->used = used - (used < decay ? used : decay);
}

// --- Cell regions --------------------------------------------------------------------------

// 0x006BA840: operator= of a bit map.
Cells* AssignCells(Cells* self, const Cells* other)
{
    static bool logged = false;
    FirstCall("AssignCells", logged);
    if (self == other)
        return self;
    DestroyCells(self);
    self->width = other->width;
    self->height = other->height;
    self->words = other->words;
    self->columns = static_cast<uint32_t**>(OperatorNew(self->width << 2));
    for (uint32_t i = 0; i < self->width; ++i) {
        self->columns[i] = static_cast<uint32_t*>(OperatorNew(self->words << 2));
        for (uint32_t j = 0; j < self->height; ++j) {
            const uint32_t mask = 1u << (j & 0x1F);
            if (other->columns[i][j >> 5] & mask)
                self->columns[i][j >> 5] |= mask;
            else
                self->columns[i][j >> 5] &= ~mask;
        }
    }
    return self;
}

// 0x006BAEE0: the region grown by one cell on every side; each set cell
// also sets its four neighbours, and the diagonal ones when `diagonals`
// is 1.
void GrowRegion(Region* self, int32_t diagonals)
{
    static bool logged = false;
    FirstCall("GrowRegion", logged);
    const int32_t x0 = self->x0 - 1, z0 = self->z0 - 1, x1 = self->x1 + 1, z1 = self->z1 + 1;
    Cells grown = {};
    const uint8_t fill = 0;
    ConstructCells(&grown, static_cast<uint32_t>(x1 - x0 + 1), static_cast<uint32_t>(z1 - z0 + 1), &fill);
    const uint32_t offset = static_cast<uint32_t>(self->z0 - z0);
    uint32_t** columns = grown.columns + (self->x0 - x0);
    for (int32_t i = 0; i <= self->x1 - self->x0; ++i, ++columns) {
        for (int32_t j = 0; j <= self->z1 - self->z0; ++j) {
            const uint32_t from = static_cast<uint32_t>(j);
            if (!(self->cells.columns[i][from >> 5] & (1u << (from & 0x1F))))
                continue;
            const uint32_t bit = from + offset;
            SetBit(columns[0], bit);
            SetBit(columns[-1], bit);
            SetBit(columns[1], bit);
            SetBit(columns[0], bit - 1);
            SetBit(columns[0], bit + 1);
            if (diagonals == 1) {
                SetBit(columns[-1], bit - 1);
                SetBit(columns[-1], bit + 1);
                SetBit(columns[1], bit - 1);
                SetBit(columns[1], bit + 1);
            }
        }
    }
    self->x0 = x0;
    self->z0 = z0;
    self->x1 = x1;
    self->z1 = z1;
    const uint32_t oldWidth = self->cells.width;
    uint32_t** const oldColumns = self->cells.columns;
    self->cells.width = grown.width;
    self->cells.height = grown.height;
    self->cells.words = grown.words;
    self->cells.columns = grown.columns;
    FreeColumns(oldColumns, oldWidth);
}

// --- Tiles of the zone ---------------------------------------------------------------------

int32_t ActiveTiles(char* self)
{
    int32_t active = 0;
    const int32_t count = At<int32_t>(self, field::kTileCount);
    const Tile* tile = Tiles(self).begin;
    for (int32_t i = 0; i < count; ++i, ++tile)
        if (tile->active)
            ++active;
    return active;
}

// 0x006BA3A0.
int32_t __fastcall CountActiveTiles(char* self, void*)
{
    static bool logged = false;
    FirstCall("CountActiveTiles", logged);
    return ActiveTiles(self);
}

// 0x006BA7B0: the sums over the tiles (all of them, active or not).
void __fastcall GetUsage(char* self, void*, uint32_t* free, uint32_t* used, uint32_t* capacity)
{
    static bool logged = false;
    FirstCall("GetUsage", logged);
    *used = 0;
    *capacity = 0;
    *free = 0;
    const TileVector& tiles = Tiles(self);
    for (const Tile* tile = tiles.begin; tile != tiles.end; ++tile) {
        *free += tile->capacity - tile->used;
        *capacity += tile->capacity;
        *used += tile->used;
    }
}

// The game's SpreadUse (0x006BABC0) reads [ecx + 0x10] after its call of
// GetUsage: the detour keeps ECX and EDX.
__declspec(naked) void GetUsageThunk()
{
    __asm push ecx
    __asm push edx
    __asm push dword ptr [esp + 20]
    __asm push dword ptr [esp + 20]
    __asm push dword ptr [esp + 20]
    __asm call GetUsage
    __asm pop edx
    __asm pop ecx
    __asm ret 12
}

// 0x006BA810.
int32_t __fastcall GetFreeCapacity(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetFreeCapacity", logged);
    uint32_t sum = 0;
    const TileVector& tiles = Tiles(self);
    for (const Tile* tile = tiles.begin; tile != tiles.end; ++tile)
        sum += tile->capacity - tile->used;
    return static_cast<int32_t>(sum);
}

// 0x006BAA70: the cost of every set cell.
int64_t __fastcall GetMaintenanceCost(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetMaintenanceCost", logged);
    uint64_t total = 0;
    for (int32_t x = RegionOf(self)->x0; x <= RegionOf(self)->x1; ++x)
        for (int32_t z = RegionOf(self)->z0; z <= RegionOf(self)->z1; ++z)
            if (CellSet(RegionOf(self), x, z))
                total += static_cast<uint64_t>(GetCellCost(x, z));
    return static_cast<int64_t>(total);
}

// 0x006BAB30: the count of set cells, recounted when dirty.
int32_t CountCells(char* self)
{
    static bool logged = false;
    FirstCall("CountCells", logged);
    if (At<uint8_t>(self, field::kCellsDirty)) {
        At<int32_t>(self, field::kCellCount) = 0;
        const Region* const region = RegionOf(self);
        for (int32_t x = region->x0; x <= region->x1; ++x)
            for (int32_t z = region->z0; z <= region->z1; ++z)
                if (CellSet(region, x, z))
                    ++At<int32_t>(self, field::kCellCount);
        At<uint8_t>(self, field::kCellsDirty) = 0;
    }
    return At<int32_t>(self, field::kCellCount);
}

// 0x006BABC0: the use spread evenly over the active tiles (signed).
void SpreadUse(char* self)
{
    static bool logged = false;
    FirstCall("SpreadUse", logged);
    uint32_t free = 0, used = 0, capacity = 0;
    GetUsage(self, nullptr, &free, &used, &capacity);
    int32_t left = static_cast<int32_t>(used);
    if (left <= 0)
        return;
    const int32_t active = ActiveTiles(self);
    if (active <= 0)
        return;
    const int32_t share = static_cast<int32_t>(capacity) / active;
    const TileVector& tiles = Tiles(self);
    for (Tile* tile = tiles.begin; tile != tiles.end; ++tile) {
        if (!tile->active)
            continue;
        const int32_t given = share < left ? share : left;
        left -= given;
        tile->used = static_cast<uint32_t>(given);
    }
}

// 0x006BAE30: whether no cell is set, kept in +0x28 (true at once when it
// is set already).
uint8_t __fastcall IsEmpty(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsEmpty", logged);
    if (At<uint8_t>(self, field::kEmpty))
        return At<uint8_t>(self, field::kEmpty);
    const Region* const region = RegionOf(self);
    At<uint8_t>(self, field::kEmpty) = 1;
    for (int32_t x = region->x0; x <= region->x1; ++x)
        for (int32_t z = region->z0; z <= region->z1; ++z)
            if (CellSet(region, x, z)) {
                At<uint8_t>(self, field::kEmpty) = 0;
                return 0;
            }
    return At<uint8_t>(self, field::kEmpty);
}

// 0x006BB180: the capacity per active tile; 0 without one.
uint32_t GetTileCapacity(char* self, int32_t activeTiles)
{
    static bool logged = false;
    FirstCall("GetTileCapacity", logged);
    if (activeTiles <= 0)
        return 0;
    const int32_t cells = CountCells(self);
    const int32_t perCell = Virtual<int32_t>(Global<void*>(addr::SC4_PollutionSimulator), 0x54);
    const uint32_t total = static_cast<uint32_t>(perCell) * static_cast<uint32_t>(cells);
    return RoundBits(UnsignedToDouble(total) / static_cast<double>(activeTiles));
}

// 0x006BB830: each active tile's capacity (0 for the others), the use above
// it given back to the pollution simulator (slot 29), and +0x34.
void __fastcall UpdateCapacity(char* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateCapacity", logged);
    const int32_t active = ActiveTiles(self);
    const uint32_t share = GetTileCapacity(self, active);
    for (Tile* tile = Tiles(self).begin; tile != Tiles(self).end; ++tile) {
        const uint32_t capacity = tile->active ? share : 0;
        const uint32_t used = tile->used;
        tile->capacity = capacity;
        if (used > capacity) {
            Virtual<void>(At<void*>(self, field::kPollution), 0x74, used - capacity);
            tile->used = tile->capacity;
        }
    }
    At<uint32_t>(self, field::kCapacity) = static_cast<uint32_t>(active) * share;
}

// --- Level, road access, effects -----------------------------------------------------------

// 0x006BA6B0: the level kept; with `terrain` the terrain flattens the
// rectangle (clamped to the city; its slot 49), without it the lot
// developer is asked to level the caller's rectangle as it is (slot 21).
void __fastcall Level(char* self, void*, const int32_t* rect, uint32_t levelBits, uint32_t terrain)
{
    static bool logged = false;
    FirstCall("Level", logged);
    At<uint32_t>(self, field::kLevel) = levelBits;
    int32_t clamped[4];
    clamped[0] = rect[0] < 0 ? 0 : rect[0];
    clamped[1] = rect[1] < 0 ? 0 : rect[1];
    // The far edges are clamped to the cell counts, not the last cells; the
    // city is asked again when the edge is beyond.
    void* const city = At<void*>(self, field::kCity);
    clamped[2] = rect[2];
    if (!(rect[2] < Virtual<int32_t>(city, 0x174)))
        clamped[2] = Virtual<int32_t>(At<void*>(self, field::kCity), 0x174);
    clamped[3] = rect[3];
    if (!(rect[3] < Virtual<int32_t>(At<void*>(self, field::kCity), 0x178)))
        clamped[3] = Virtual<int32_t>(At<void*>(self, field::kCity), 0x178);
    if (static_cast<uint8_t>(terrain)) {
        Virtual<void>(At<void*>(self, field::kTerrain), 0xC4, static_cast<void*>(clamped), levelBits, 1u, 5u);
        return;
    }
    void* const development = Global<void*>(addr::SC4_Service43D68);
    AddRef(development);
    void* const lots = Virtual<void*>(development, 0x90);
    AddRef(lots);
    Virtual<uint8_t>(lots, 0x54, rect, 0u, 1u, 1u, 0x7F7FFFFFu, 0u);
    Release(lots);
    Release(development);
}

// 0x006BAC50: the first cell of the ring around the rectangle with a
// network: the row above, the row below, the column before, the column
// after (the corners at the far x and z are never looked at). The
// rectangle's first cell and false without one.
uint8_t FindRoadAccess(char* self, int32_t* cell)
{
    static bool logged = false;
    FirstCall("FindRoadAccess", logged);
    int32_t maxX = Virtual<int32_t>(At<void*>(self, field::kCity), 0x174) - 1;
    int32_t maxZ = Virtual<int32_t>(At<void*>(self, field::kCity), 0x178) - 1;
    const Region* region = RegionOf(self);
    int32_t minX = 0, minZ = 0;
    if (region->x0 > 0)
        minX = region->x0 - 1;
    if (region->x1 < maxX)
        maxX = region->x1 + 1;
    if (region->z0 > 0)
        minZ = region->z0 - 1;
    if (region->z1 < maxZ)
        maxZ = region->z1 + 1;
    if (region->z0 > minZ)
        for (int32_t x = minX; x < maxX; ++x)
            if (NetworkAt(x, minZ)) {
                cell[1] = minZ;
                cell[0] = x;
                return 1;
            }
    if (RegionOf(self)->z1 < maxZ)
        for (int32_t x = minX; x < maxX; ++x)
            if (NetworkAt(x, maxZ)) {
                cell[1] = maxZ;
                cell[0] = x;
                return 1;
            }
    if (RegionOf(self)->x0 > minX)
        for (int32_t z = minZ; z < maxZ; ++z)
            if (NetworkAt(minX, z)) {
                cell[1] = z;
                cell[0] = minX;
                return 1;
            }
    if (RegionOf(self)->x1 < maxX)
        for (int32_t z = minZ; z < maxZ; ++z)
            if (NetworkAt(maxX, z)) {
                cell[0] = maxX;
                cell[1] = z;
                return 1;
            }
    cell[0] = RegionOf(self)->x0;
    cell[1] = RegionOf(self)->z0;
    return 0;
}

// What the visual effect's slot 8 takes: two flags, a rotation, a position
// and a scale. The two bytes after the flags are stack the game never
// writes: zeros here.
struct EffectTransform {
    uint8_t flag0;
    uint8_t flag1;
    uint8_t unset[2];
    float rotation[9];
    float position[3];
    float scale;
};
static_assert(sizeof(EffectTransform) == 0x38);

// 0x006BB1D0: the automata controllers at the road access cell and the
// seagulls 64 above the rectangle's middle, each made when missing.
void __fastcall UpdateEffects(char* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateEffects", logged);
    FindRoadAccess(self, &At<int32_t>(self, field::kAccessX));
    float position[3] = {};
    CellCentre(self, At<int32_t>(self, field::kAccessX), At<int32_t>(self, field::kAccessZ), &position[0],
               &position[2]);
    position[1] = static_cast<float>(HeightAt(self, position[0], position[2]));
    if (At<void*>(self, field::kControllers)) {
        if (!At<void*>(self, field::kController1)) {
            void* const controller =
                Virtual<void*>(At<void*>(self, field::kControllers), 0x14, 0u,
                               reinterpret_cast<void*>(addr::cSC4LandfillZone_ControllerName1),
                               static_cast<void*>(position));
            At<void*>(self, field::kController1) = controller;
            if (controller) {
                AddRef(controller);
                Virtual<void>(At<void*>(self, field::kController1), 0x34, 8u, 1u);
                Virtual<void>(At<void*>(self, field::kController1), 0x34, 0x100u, 1u);
            }
        }
        if (!At<void*>(self, field::kController2)) {
            void* const controller =
                Virtual<void*>(At<void*>(self, field::kControllers), 0x14, 0u,
                               reinterpret_cast<void*>(addr::cSC4LandfillZone_ControllerName2),
                               static_cast<void*>(position));
            At<void*>(self, field::kController2) = controller;
            if (controller) {
                AddRef(controller);
                Virtual<void>(At<void*>(self, field::kController2), 0x34, 8u, 1u);
            }
        }
    }
    if (At<void*>(self, field::kEffect) || !At<void*>(self, field::kEffects))
        return;
    void* effect = nullptr;
    if (!Virtual<uint8_t>(At<void*>(self, field::kEffects), 0x1C,
                          reinterpret_cast<const char*>(addr::cSC4LandfillZone_EffectName), &effect))
        return;
    const Region* const region = RegionOf(self);
    At<void*>(self, field::kEffect) = effect;
    CellCentre(self, Middle(region->x1, region->x0), Middle(region->z1, region->z0), &position[0], &position[2]);
    position[1] = static_cast<float>(HeightAt(self, position[0], position[2]) + 64.0);
    EffectTransform transform = {};
    transform.flag0 = 1;
    transform.flag1 = 1;
    transform.rotation[0] = 1.0f;
    transform.rotation[4] = 1.0f;
    transform.rotation[8] = 1.0f;
    transform.position[0] = position[0];
    transform.position[1] = position[1];
    transform.position[2] = position[2];
    transform.scale = 1.0f;
    Virtual<void>(At<void*>(self, field::kEffect), 0x20, &transform);
    Virtual<void>(At<void*>(self, field::kEffect), 0xC, 0u);
}

// 0x006BA190: the effect stopped and released, the two controllers taken
// out of their manager and released.
void __fastcall DropEffects(char* self, void*)
{
    static bool logged = false;
    FirstCall("DropEffects", logged);
    if (At<void*>(self, field::kEffect)) {
        Virtual<void>(At<void*>(self, field::kEffect), 0x10, 1u);
        Release(At<void*>(self, field::kEffect));
        At<void*>(self, field::kEffect) = nullptr;
    }
    void* const manager = At<void*>(self, field::kControllers);
    if (!manager)
        return;
    AddRef(manager);
    for (const std::size_t offset : {field::kController1, field::kController2}) {
        if (!At<void*>(self, offset))
            continue;
        Virtual<void>(At<void*>(self, offset), 0x48, 0u);
        Virtual<void>(At<void*>(self, field::kControllers), 0x1C, At<void*>(self, offset));
        Release(At<void*>(self, offset));
        At<void*>(self, offset) = nullptr;
    }
    Release(At<void*>(self, field::kControllers));
}

// 0x006BB3B0: the marker prop of a zone without road access, at the first
// set cell of a spiral walk from the rectangle's middle (the middle itself
// when the walk ends without one).
void PlaceMarker(char* self)
{
    static bool logged = false;
    FirstCall("PlaceMarker", logged);
    if (At<void*>(self, field::kMarker))
        return;
    const Region* const region = RegionOf(self);
    int32_t x = Middle(region->x1, region->x0);
    int32_t z = Middle(region->z0, region->z1);
    alignas(8) char walk[0x30] = {};
    const auto Start = reinterpret_cast<WalkStartFn>(addr::SC4SpiralWalk_Start);
    At<int32_t>(walk, 0x10) = INT32_MIN;
    At<int32_t>(walk, 0x14) = INT32_MIN;
    At<int32_t>(walk, 0x18) = INT32_MAX;
    At<int32_t>(walk, 0x1C) = INT32_MAX;
    At<uint8_t>(walk, 0x20) = 1;
    At<int32_t>(walk, 0x2C) = 0;
    Start(walk, 0, 0);
    At<int32_t>(walk, 0x10) = region->x0;
    At<int32_t>(walk, 0x14) = region->z0;
    At<int32_t>(walk, 0x18) = region->x1 < region->x0 ? region->x0 : region->x1;
    At<int32_t>(walk, 0x1C) = region->z1 < region->z0 ? region->z0 : region->z1;
    if (At<uint8_t>(walk, 0x28) != 1)
        At<uint8_t>(walk, 0x21) = 1;
    Start(walk, x, z);
    while (!CellSet(region, x, z)) {
        int32_t nextX = x, nextZ = z;
        if (!reinterpret_cast<WalkNextFn>(addr::SC4SpiralWalk_Next)(walk, &nextX, &nextZ)) {
            x = Middle(region->x1, region->x0);
            z = Middle(region->z0, region->z1);
            break;
        }
        x = nextX;
        z = nextZ;
    }
    float position[3] = {};
    CellCentre(self, x, z, &position[0], &position[2]);
    // The terrain from the global here, and its slot 19.
    position[1] =
        static_cast<float>(Virtual<double>(Global<void*>(addr::SC4_Terrain), 0x4C, position[0], position[2]) + 3.0);
    const uint32_t view = At<uint32_t>(Global<void*>(addr::SC4_CameraView), 0x10);
    void* const made = Virtual<void*>(Global<void*>(addr::SC4_Service43D10), 0x68, kMarkerProp,
                                      static_cast<void*>(position), view);
    void* const old = At<void*>(self, field::kMarker);
    if (made != old) {
        if (made)
            AddRef(made);
        At<void*>(self, field::kMarker) = made;
        if (old)
            Release(old);
    }
    // No null test in the game either.
    Virtual<void>(At<void*>(self, field::kMarker), 0x30, 3u);
}

struct IndexVector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* limit;
};

// 0x006BB8F0: whether a subnetwork next to the zone carries occupants of
// one of the twelve types.
uint8_t HasRoadAccess(char* self)
{
    static bool logged = false;
    FirstCall("HasRoadAccess", logged);
    const Region* const region = RegionOf(self);
    void* const traffic = Global<void*>(addr::SC4_Service43DA8);
    Region around = {};
    around.x0 = region->x0;
    around.z0 = region->z0;
    around.x1 = region->x1;
    around.z1 = region->z1;
    around.cells.vtable = addr::SC4CellRegion_CellsVtable;
    AssignCells(&around.cells, &region->cells);
    GrowRegion(&around, 0);
    IndexVector networks = {};
    IndexVector counts = {};
    Virtual<void>(traffic, 0xA4, &around, &networks);
    const std::ptrdiff_t found = networks.end - networks.begin;
    uint8_t access = 0;
    const auto* const types = reinterpret_cast<const uint32_t*>(addr::cSC4LandfillZone_TripTypes);
    for (int type = 0; type < 12 && !access; ++type) {
        Virtual<void>(traffic, 0xA8, types[type], &counts);
        const uint32_t size = static_cast<uint32_t>(counts.end - counts.begin);
        for (std::ptrdiff_t i = 0; i < found; ++i) {
            const uint32_t network = networks.begin[i];
            if (network < size && counts.begin[network] != 0) {
                access = 1;
                break;
            }
        }
    }
    if (counts.begin)
        Free(counts.begin);
    if (networks.begin)
        Free(networks.begin);
    around.cells.vtable = addr::SC4CellRegion_CellsVtable;
    FreeColumns(around.cells.columns, around.cells.width);
    return access;
}

// --- Init, Shutdown, Release ---------------------------------------------------------------

void Take(char* self, std::size_t offset, void* service)
{
    if (At<void*>(self, offset))
        return;
    At<void*>(self, offset) = service;
    AddRef(service);
}

// 0x006BB620. Always true.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    Take(self, field::kCity, Global<void*>(addr::SC4_City));
    Take(self, field::kOccupants, Global<void*>(addr::SC4_NetworkService));
    Take(self, field::kTerrain, Global<void*>(addr::SC4_Terrain));
    Take(self, field::kPollution, Global<void*>(addr::SC4_PollutionSimulator));
    Take(self, field::kEffects, Global<void*>(addr::SC4_Service43D1C));
    if (!At<void*>(self, field::kControllers))
        Take(self, field::kControllers, Virtual<void*>(At<void*>(self, field::kCity), 0xBC));
    if (!At<void*>(self, field::kZoneManager))
        Take(self, field::kZoneManager, Virtual<void*>(At<void*>(self, field::kCity), 0x90));
    UpdateEffects(self, nullptr);
    // fucompp, test 0x44, jp: only a level of exactly 0 is replaced by the
    // height at the rectangle's middle.
    if (At<float>(self, field::kLevel) == 0.0f) {
        const Region* const region = RegionOf(self);
        float worldX = 0.0f, worldZ = 0.0f;
        CellCentre(self, Middle(region->x1, region->x0), Middle(region->z1, region->z0), &worldX, &worldZ);
        At<float>(self, field::kLevel) = static_cast<float>(HeightAt(self, worldX, worldZ));
    }
    return 1;
}

// 0x006BB750. Always true.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    DropEffects(self, nullptr);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    RemoveMarker(self);
    if (Region* const region = RegionOf(self)) {
        region->cells.vtable = addr::SC4CellRegion_CellsVtable;
        DestroyCells(&region->cells);
        OperatorDelete(region);
        RegionOf(self) = nullptr;
    }
    for (const std::size_t offset : {field::kZoneManager, field::kControllers, field::kEffects, field::kPollution,
                                     field::kTerrain, field::kOccupants, field::kCity}) {
        if (void* const object = At<void*>(self, offset)) {
            Release(object);
            At<void*>(self, offset) = nullptr;
        }
    }
    return 1;
}

// Slot 2 (0x006BB8C0).
uint32_t __fastcall ReleaseZone(char* self, void*)
{
    static bool logged = false;
    FirstCall("Release", logged);
    uint32_t& references = At<uint32_t>(self, field::kRefCount);
    if (references > 1)
        return --references;
    Shutdown(self, nullptr);
    Virtual<void*>(self, 0x10, 1u);
    return 0;
}

// Slot 4 (0x006BBD40): the destructor at the game's address (ours when
// simulators.landfillzone.object is on).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    GameDestructor(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// --- The month -----------------------------------------------------------------------------

// Whether the tile at a grid place has garbage; false outside the grid.
bool HasGarbage(char* self, int32_t column, int32_t row)
{
    const int32_t columns = At<int32_t>(self, field::kTileColumns);
    if (column < 0 || column >= columns || row < 0 || row >= At<int32_t>(self, field::kTileRows))
        return false;
    return Tiles(self).begin[columns * row + column].garbage != 0;
}

// An entry of the picture table; one with the random picture gets a new
// rotation (0, 2, 4 or 6) each time it is looked at.
Picture* LookAt(uint32_t index)
{
    Picture* const picture = Pictures() + index;
    if (picture->exemplar == kRandomPicture)
        picture->rotation = (reinterpret_cast<int(__cdecl*)()>(addr::CRT_rand)() % 4) * 2;
    return picture;
}

// 0x006BCFC0: the pictures of a tile's four cells, from the garbage flags
// of the tile and its eight neighbours.
void UpdateTileOccupants(char* self, const Tile* tile)
{
    static bool logged = false;
    FirstCall("UpdateTileOccupants", logged);
    uint32_t mask[4] = {0, 0, 0, 0};
    if (tile->garbage) {
        mask[0] = 4;
        mask[1] = 2;
        mask[2] = 8;
        mask[3] = 1;
    }
    const int32_t column = tile->x, row = tile->z;
    if (HasGarbage(self, column - 1, row - 1))
        mask[0] |= 1;
    if (HasGarbage(self, column - 1, row)) {
        mask[0] |= 2;
        mask[2] |= 1;
    }
    if (HasGarbage(self, column, row - 1)) {
        mask[0] |= 8;
        mask[1] |= 1;
    }
    if (HasGarbage(self, column + 1, row - 1))
        mask[1] |= 8;
    if (HasGarbage(self, column + 1, row)) {
        mask[3] |= 8;
        mask[1] |= 4;
    }
    if (HasGarbage(self, column - 1, row + 1))
        mask[2] |= 2;
    if (HasGarbage(self, column, row + 1)) {
        mask[2] |= 4;
        mask[3] |= 2;
    }
    if (HasGarbage(self, column + 1, row + 1))
        mask[3] |= 4;
    // The first entry is looked at before the four cells.
    LookAt(0);
    for (int cell = 0; cell < 4; ++cell) {
        // The region and the tile are read again for each cell.
        const int32_t x = RegionOf(self)->x0 + tile->x + (cell & 1);
        const int32_t z = RegionOf(self)->z0 + tile->z + (cell >> 1);
        const Picture* const picture = LookAt(mask[cell]);
        const int32_t rotation = picture->rotation;
        const uint32_t exemplar = picture->exemplar;
        DeactivateCell(self, x, z);
        PlaceTileOccupant(self, x, z, exemplar, static_cast<uint32_t>(rotation));
    }
}

// 0x006BD390: the month: the road access, the marker, then each active
// tile decays and takes garbage from the offer (at most the cells times
// the tuned intake over the zone); the pictures follow tiles that became
// empty or got garbage; the use is spread again.
void __fastcall OnMonth(char* self, void*, int32_t* garbage)
{
    static bool logged = false;
    FirstCall("OnMonth", logged);
    const uint8_t active = HasRoadAccess(self);
    At<uint8_t>(self, field::kActive) = active;
    if (active)
        RemoveMarker(self);
    else
        PlaceMarker(self);
    const int32_t most = CountCells(self) * Global<int32_t>(addr::LandfillTuning_Value4A677E5C);
    int32_t left = *garbage > most ? most : *garbage;
    for (Tile* tile = Tiles(self).begin; tile != Tiles(self).end; ++tile) {
        if (!tile->active)
            continue;
        DecayTile(tile);
        const uint32_t used = tile->used;
        const uint32_t room = tile->capacity - used;
        uint32_t taken = 0;
        if (room != 0 && At<uint8_t>(self, field::kActive) && left > 0) {
            taken = room < static_cast<uint32_t>(left) ? room : static_cast<uint32_t>(left);
            tile->used = used + taken;
            Virtual<void>(At<void*>(self, field::kPollution), 0x74, 0u - taken);
            Virtual<void>(At<void*>(self, field::kPollution), 0xCC, taken);
        }
        *garbage -= static_cast<int32_t>(taken);
        left -= static_cast<int32_t>(taken);
        if (tile->used == 0) {
            if (!tile->garbage)
                continue;
            tile->garbage = 0;
        } else {
            if (tile->garbage)
                continue;
            tile->garbage = 1;
        }
        UpdateTileOccupants(self, tile);
    }
    SpreadUse(self);
}

// --- Installation --------------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_OURS(fn) reinterpret_cast<void*>(&fn)

const Slot kSimSlots[] = {
    {addr::cSC4LandfillZone_vftable, 2, addr::cSC4LandfillZone_Release, OPENSC4_OURS(ReleaseZone)},
    {addr::cSC4LandfillZone_vftable, 4, addr::cSC4LandfillZone_DeletingDestructor, OPENSC4_OURS(DeletingDestructor)},
    {addr::cSC4LandfillZone_TileVftable, 0, addr::cSC4LandfillZone_TileDeletingDestructor,
     OPENSC4_OURS(TileDeletingDestructor)},
};
const Detour kSimDetours[] = {
    {addr::cSC4LandfillZone_DropEffects, OPENSC4_OURS(DropEffects)},
    {addr::cSC4LandfillZone_CountActiveTiles, OPENSC4_OURS(CountActiveTiles)},
    {addr::cSC4LandfillZone_Level, OPENSC4_OURS(Level)},
    {addr::cSC4LandfillZone_GetUsage, OPENSC4_OURS(GetUsageThunk)},
    {addr::cSC4LandfillZone_GetFreeCapacity, OPENSC4_OURS(GetFreeCapacity)},
    {addr::cSC4LandfillZone_GetMaintenanceCost, OPENSC4_OURS(GetMaintenanceCost)},
    {addr::cSC4LandfillZone_IsEmpty, OPENSC4_OURS(IsEmpty)},
    {addr::cSC4LandfillZone_Sub6BB1D0, OPENSC4_OURS(UpdateEffects)},
    {addr::cSC4LandfillZone_Init, OPENSC4_OURS(Init)},
    {addr::cSC4LandfillZone_Shutdown, OPENSC4_OURS(Shutdown)},
    {addr::cSC4LandfillZone_UpdateCapacity, OPENSC4_OURS(UpdateCapacity)},
    {addr::cSC4LandfillZone_OnMonth, OPENSC4_OURS(OnMonth)},
};

#undef OPENSC4_OURS

// The trampolines to the game's functions (never called).
void* g_originals[32];
std::size_t g_originalCount = 0;

// Checks that every slot still holds the original, installs the detours,
// then swaps the slots.
bool InstallSim(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : kSimSlots) {
        const std::uintptr_t held = reinterpret_cast<const std::uintptr_t*>(slot.vtable)[slot.index];
        if (held != slot.original) {
            log::Write("landfillzone sim: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(held),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    if (!InstallDetours(kSimDetours, std::size(kSimDetours), "sim"))
        return false;
    for (const Slot& slot : kSimSlots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("landfillzone sim: %u slots are ours", static_cast<unsigned>(std::size(kSimSlots)));
    return true;
}

const bool g_registered[] = {
    registry::Register({"simulators.landfillzone.sim", "simulators/landfillzone", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LandfillZone_vftable,
                        "Our landfill zone: Release and the destructors' slots; detours on Init, Shutdown, the "
                        "month, the capacity and usage, the level and the effects (src/core/landfillzone_sim.cpp)",
                        false, InstallSim}),
};

} // namespace

// --- What landfillzone_cells.cpp also calls -------------------------------------------------

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("landfillzone: first %s", name);
}

// 0x006BA220: std::copy; nothing when the range is empty or reversed.
Tile* CopyTiles(Tile* first, Tile* last, Tile* destination)
{
    static bool logged = false;
    FirstCall("CopyTiles", logged);
    for (std::ptrdiff_t count = last - first; count > 0; --count, ++first, ++destination)
        AssignTile(destination, first);
    return destination;
}

// 0x006BBAB0: the vector's copy constructor.
void CopyTileVector(TileVector* self, const TileVector* other)
{
    static bool logged = false;
    FirstCall("CopyTileVector", logged);
    const std::ptrdiff_t count = other->end - other->begin;
    self->begin = nullptr;
    self->end = nullptr;
    self->limit = nullptr;
    Tile* const storage =
        count != 0 ? static_cast<Tile*>(Allocate(static_cast<std::size_t>(count) * sizeof(Tile))) : nullptr;
    self->begin = storage;
    self->end = storage;
    self->limit = storage + count;
    self->end = CopyConstructTiles(other->begin, other->end, storage);
}

// 0x006BBB10: erase(first, last).
Tile* EraseTiles(TileVector* self, Tile* first, Tile* last)
{
    static bool logged = false;
    FirstCall("EraseTiles", logged);
    Tile* const end = CopyTiles(last, self->end, first);
    DestroyTiles(end, self->end);
    self->end = end;
    return first;
}

// The inline push_back.
void PushTile(TileVector* tiles, const Tile* value)
{
    if (tiles->end != tiles->limit) {
        if (tiles->end)
            ConstructTile(tiles->end, value);
        ++tiles->end;
    } else {
        InsertTiles(tiles, tiles->end, value, 1, true);
    }
}

// 0x006BA2A0: a tile from the save, version 2 only. The game reads the
// version into the stack slot of a pushed ECX (the tile's address) and the
// two flags into the slot of its stream argument: when a read writes
// nothing the low 16 bits of those are compared.
uint8_t ReadTile(Tile* tile, void* stream)
{
    static bool logged = false;
    FirstCall("ReadTile", logged);
    uint16_t version = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(tile));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 2)
        return 0;
    Virtual<uint8_t>(stream, 0x24, &tile->x);
    Virtual<uint8_t>(stream, 0x24, &tile->z);
    uint16_t flag = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &flag);
    tile->active = flag == 1;
    Virtual<uint8_t>(stream, 0x1C, &flag);
    tile->garbage = flag == 1;
    Virtual<uint8_t>(stream, 0x24, &tile->used);
    Virtual<uint8_t>(stream, 0x24, &tile->capacity);
    return 1;
}

// 0x006BB570: a region's copy constructor (the words per column are unset
// until the assignment, 0 here).
Region* CopyRegion(Region* self, const Region* other)
{
    static bool logged = false;
    FirstCall("CopyRegion", logged);
    self->x0 = other->x0;
    self->z0 = other->z0;
    self->x1 = other->x1;
    self->z1 = other->z1;
    self->cells.vtable = addr::SC4CellRegion_CellsVtable;
    self->cells.width = 0;
    self->cells.height = 0;
    self->cells.columns = nullptr;
    AssignCells(&self->cells, &other->cells);
    return self;
}

// 0x006BA5C0: the four tiles a cell touches made inactive.
void DeactivateTilesAt(char* self, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("DeactivateTilesAt", logged);
    const Region* const region = RegionOf(self);
    const int32_t dx = x - region->x0, dz = z - region->z0;
    const int32_t columns = At<int32_t>(self, field::kTileColumns), rows = At<int32_t>(self, field::kTileRows);
    Tile* const tiles = Tiles(self).begin;
    const auto clear = [&](int32_t i, int32_t j) {
        if (i >= 0 && i < columns && j >= 0 && j < rows)
            tiles[columns * j + i].active = 0;
    };
    clear(dx, dz);
    clear(dx, dz - 1);
    clear(dx - 1, dz);
    clear(dx - 1, dz - 1);
}

// The marker prop taken out of the prop manager (city slot 35, its slot
// 29) and released; the block the game repeats in four functions.
void RemoveMarker(char* self)
{
    if (!At<void*>(self, field::kMarker))
        return;
    if (void* const city = At<void*>(self, field::kCity))
        if (void* const props = Virtual<void*>(city, 0x8C))
            Virtual<void>(props, 0x74, At<void*>(self, field::kMarker));
    if (void* const marker = At<void*>(self, field::kMarker)) {
        At<void*>(self, field::kMarker) = nullptr;
        Release(marker);
    }
}

bool InstallDetours(const Detour* detours, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!addr_check::Find(detours[i].address)) {
            log::Write("landfillzone %s: no address entry for %08X; not installing", what,
                       static_cast<unsigned>(detours[i].address));
            return false;
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (g_originalCount >= std::size(g_originals) ||
            !hook::Install(*addr_check::Find(detours[i].address), detours[i].ours, &g_originals[g_originalCount]))
            return false;
        ++g_originalCount;
    }
    log::Write("landfillzone %s: %u detours are ours", what, static_cast<unsigned>(count));
    return true;
}

} // namespace opensc4::landfillzone_sim
