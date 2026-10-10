// Our cSC4LandfillZone, second part (build 641; the class map, the fields
// and the groups are in landfillzone_sim.h): the occupants on the cells,
// zoning (the group simulators.landfillzone.tiles), the save
// (simulators.landfillzone.save) and our own object
// (simulators.landfillzone.object).

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

// --- Occupants on the cells ----------------------------------------------------------------

// What cSC4LotBaseTextureOccupant::SetTextureSpecification takes here.
struct TextureInfo {
    uint32_t texture;
    uint8_t x;
    uint8_t z;
    uint8_t zero6;
    uint8_t zero7;
    uint8_t colour[3];
    uint8_t alpha;
    uint8_t one;
    uint8_t zeroD;
    uint8_t unset[2];
};
static_assert(sizeof(TextureInfo) == 0x10);

// 0x006BA470: a base texture (0x1005) on the cell. The two bytes after the
// structure's last are stack the game never writes: zeros here.
void PlaceBaseTexture(char* self, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("PlaceBaseTexture", logged);
    TextureInfo info = {};
    info.texture = 0x1005;
    info.x = static_cast<uint8_t>(x);
    info.z = static_cast<uint8_t>(z);
    info.colour[0] = info.colour[1] = info.colour[2] = 0xFF;
    info.alpha = Virtual<uint8_t>(Global<void*>(addr::SC4_ZoneManager), 0x40);
    info.one = 1;
    void* texture = nullptr;
    void* const com = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
    if (!Virtual<uint8_t>(com, 0xC, kClassBaseTexture, kIidBaseTexture, &texture)) {
        void* made = reinterpret_cast<NewFn>(addr::cSC4LotBaseTextureOccupant_Allocate)(0x60);
        if (made)
            made = reinterpret_cast<SelfFn>(addr::cSC4LotBaseTextureOccupant_cSC4LotBaseTextureOccupant)(made);
        if (made != texture) {
            void* const old = texture;
            if (made)
                AddRef(made);
            texture = made;
            if (old)
                Release(old);
        }
    }
    Virtual<uint32_t>(Virtual<void*>(texture, 0xC), 0xC);
    Virtual<void>(texture, 0x14, 1u);
    Virtual<void>(texture, 0x20, &info, 1u, 0u);
    void* const occupant = Virtual<void*>(texture, 0xC);
    void* const occupants = At<void*>(self, field::kOccupants);
    if (Virtual<uint8_t>(occupant, 0x80, 1u)) {
        Virtual<uint8_t>(occupants, 0x60, occupant, 0u);
        Virtual<void>(occupant, 0x3C, kOccupantMoved, 0u);
    } else {
        Virtual<uint8_t>(occupants, 0x50, occupant, 0u);
    }
    if (texture)
        Release(texture);
}

// A new list head from the game's node allocator, linked to itself.
Node* NewList()
{
    auto* const head = static_cast<Node*>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    head->next = head;
    head->previous = head;
    return head;
}

// The list emptied (SC4List::Clear2) and its head given back.
void DeleteList(Node** list)
{
    reinterpret_cast<void(__thiscall*)(Node**)>(addr::SC4List_Clear2)(list);
    Node* const head = *list;
    Virtual<void>(ListNodeAllocator(), 0x10, head);
}

// 0x006BC630: the cell's occupants of type 0x74758926 removed.
void ClearCellOccupants(char* self, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("ClearCellOccupants", logged);
    Node* list = NewList();
    int32_t cell[2] = {x, z};
    if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0xA4, &list, static_cast<void*>(cell),
                         static_cast<void*>(cell), kTypeCleared, 0xFFFFFFFFu)) {
        for (Node* node = list->next; node != list; node = node->next)
            Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x54, node->value, 1u, 3u);
        Virtual<void>(At<void*>(self, field::kOccupants), 0xC0, &list);
    }
    DeleteList(&list);
}

// 0x006BC930: the cell's base textures of usage 1 removed. The listed
// occupants are released one by one here (no slot 48).
void RemoveBaseTextures(char* self, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("RemoveBaseTextures", logged);
    Node* list = NewList();
    int32_t first[2] = {x, z};
    int32_t last[2] = {x, z};
    if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0xA4, &list, static_cast<void*>(first),
                         static_cast<void*>(last), kTypeBaseTexture, 0xFFFFFFFFu)) {
        Node* const end = list;
        for (Node* node = end->next; node != end; node = node->next) {
            void* const occupant = node->value;
            void* texture = nullptr;
            if (Virtual<uint8_t>(occupant, 0x0, kIidBaseTexture, &texture)) {
                if (Virtual<int32_t>(texture, 0x10) == 1) {
                    void* const part = Virtual<void*>(texture, 0xC);
                    Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x54, part, 1u, 3u);
                }
                Release(texture);
            }
            Release(occupant);
        }
    }
    DeleteList(&list);
}

// --- Zoning --------------------------------------------------------------------------------

// 0x006BC150: the zone takes a region in: a new bit map over both
// rectangles, the new cells levelled one by one, the tile grid rebuilt.
void __fastcall AddRegion(char* self, void*, const Region* added)
{
    static bool logged = false;
    FirstCall("AddRegion", logged);
    RemoveMarker(self);
    const uint32_t oldCount = At<uint32_t>(self, field::kTileCount);
    TileVector old;
    CopyTileVector(&old, &Tiles(self));
    const Region* const region = RegionOf(self);
    const int32_t x0 = region->x0 < added->x0 ? region->x0 : added->x0;
    const int32_t z0 = region->z0 < added->z0 ? region->z0 : added->z0;
    const int32_t x1 = region->x1 > added->x1 ? region->x1 : added->x1;
    const int32_t z1 = region->z1 > added->z1 ? region->z1 : added->z1;
    auto* const merged = static_cast<Region*>(OperatorNew(0x24));
    if (merged) {
        const uint8_t fill = 0;
        merged->x0 = x0;
        merged->z0 = z0;
        merged->x1 = x1;
        merged->z1 = z1;
        ConstructCells(&merged->cells, static_cast<uint32_t>(x1 - x0 + 1), static_cast<uint32_t>(z1 - z0 + 1), &fill);
    }
    // No null test from here on, as in the game.
    for (int32_t x = RegionOf(self)->x0; x <= RegionOf(self)->x1; ++x)
        for (int32_t z = RegionOf(self)->z0; z <= RegionOf(self)->z1; ++z)
            if (CellSet(RegionOf(self), x, z))
                SetCell(merged, x, z, true);
    for (int32_t x = added->x0; x <= added->x1; ++x) {
        for (int32_t z = added->z0; z <= added->z1; ++z) {
            if (!CellSet(added, x, z))
                continue;
            SetCell(merged, x, z, true);
            const int32_t cell[4] = {x, z, x + 1, z + 1};
            GameLevel(self, cell, At<uint32_t>(self, field::kLevel), 0);
        }
    }
    if (Region* const gone = RegionOf(self)) {
        gone->cells.vtable = addr::SC4CellRegion_CellsVtable;
        FreeColumns(gone->cells.columns, gone->cells.width);
        OperatorDelete(gone);
    }
    RegionOf(self) = merged;
    At<int32_t>(self, field::kTileColumns) = merged->x1 - merged->x0;
    At<int32_t>(self, field::kTileRows) = merged->z1 - merged->z0;
    At<int32_t>(self, field::kTileCount) = At<int32_t>(self, field::kTileColumns) * At<int32_t>(self, field::kTileRows);
    // The grid emptied (its storage kept) and filled with blank tiles.
    TileVector& tiles = Tiles(self);
    Tile* const begin = CopyTiles(tiles.end, tiles.end, tiles.begin);
    DestroyTiles(begin, tiles.end);
    tiles.end = begin;
    Tile blank = BlankTile();
    for (int32_t row = 0; row < At<int32_t>(self, field::kTileRows); ++row) {
        for (int32_t column = 0; column < At<int32_t>(self, field::kTileColumns); ++column) {
            blank.index = static_cast<uint32_t>(row);
            blank.x = column;
            blank.z = row;
            PushTile(&Tiles(self), &blank);
        }
    }
    // The old tiles back by their own grid places in the new grid's width:
    // not moved by what the rectangle grew to the west or north, as in the
    // game.
    const Tile* source = old.begin;
    for (uint32_t i = 0; i < oldCount; ++i, ++source)
        AssignTile(Tiles(self).begin + (source->z * At<int32_t>(self, field::kTileColumns) + source->x), source);
    for (int32_t x = RegionOf(self)->x0; x <= RegionOf(self)->x1; ++x)
        for (int32_t z = RegionOf(self)->z0; z <= RegionOf(self)->z1; ++z)
            if (!CellSet(RegionOf(self), x, z))
                DeactivateTilesAt(self, x, z);
    At<uint8_t>(self, field::kCellsDirty) = 1;
    DestroyTiles(old.begin, old.end);
    if (old.begin)
        Free(old.begin);
}

// 0x006BCAA0: a cell out of the zone. With `occupants` its landfill
// occupant and base textures go; with `dezone` the zone manager is told to
// unzone the cell (PlaceZone, zone type 0).
void __fastcall ClearCell(char* self, void*, int32_t x, int32_t z, uint32_t dezone, uint32_t occupants)
{
    static bool logged = false;
    FirstCall("ClearCell", logged);
    RemoveMarker(self);
    if (static_cast<uint8_t>(occupants)) {
        DeactivateCell(self, x, z);
        RemoveBaseTextures(self, x, z);
    }
    SetCell(RegionOf(self), x, z, false);
    DeactivateTilesAt(self, x, z);
    if (static_cast<uint8_t>(dezone)) {
        void* const zones = Virtual<void*>(At<void*>(self, field::kCity), 0x90);
        Region cell = {};
        const uint8_t fill = 1;
        cell.x0 = x;
        cell.z0 = z;
        cell.x1 = x;
        cell.z1 = z;
        ConstructCells(&cell.cells, 1, 1, &fill);
        Virtual<uint8_t>(zones, 0x2C, &cell, 0u, 1u, 1u, 0u, 1u, 0u, 0u, 0u, 0u);
        cell.cells.vtable = addr::SC4CellRegion_CellsVtable;
        DestroyCells(&cell.cells);
    }
    At<uint8_t>(self, field::kCellsDirty) = 1;
}

// 0x006BCBC0: a set cell after the ground under it changed.
void CheckCell(char* self, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("CheckCell", logged);
    if (!CellSet(RegionOf(self), x, z))
        return;
    void* occupant = nullptr;
    if (!Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x84, &occupant, x, z, kTypeLandfillOccupant)) {
        ClearCell(self, nullptr, x, z, 1, 1);
        return;
    }
    void* landfill = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kIidLandfillOccupant, &landfill)) {
        float position[3] = {};
        Virtual<void>(Virtual<void*>(landfill, 0x14), 0x20, static_cast<void*>(position));
        const float ground = static_cast<float>(
            Virtual<double>(At<void*>(self, field::kTerrain), 0x4C, position[0], position[2]));
        const float difference = static_cast<float>(static_cast<double>(position[1]) - static_cast<double>(ground));
        const float sea = static_cast<float>(Virtual<double>(At<void*>(self, field::kTerrain), 0x74));
        // fabs, fcomp 0.25, test 1, jne: nothing while less than 0.25 off
        // (or unordered).
        if ((difference < 0.0f ? -difference : difference) >= 0.25f) {
            if (Virtual<uint8_t>(landfill, 0x24)) {
                ClearCell(self, nullptr, x, z, 1, 1);
            } else if (Global<uint8_t>(addr::LandfillTuning_Flag4A677E5D)) {
                // The ground's height, or the sea level when that is not
                // below it.
                position[1] = sea < ground ? ground : sea;
                Virtual<void>(occupant, 0x24, static_cast<void*>(position));
                Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x60, occupant, 1u);
                Virtual<void>(occupant, 0x3C, kOccupantMoved, 0u);
            }
        }
    }
    // Released whether or not the query succeeded.
    if (landfill)
        Release(landfill);
    Release(occupant);
}

// 0x006BCD70: every set cell checked: one no longer zoned as landfill, or
// with a network on it, is cleared (its tiles left alone); the others get
// a base texture unless they have a landfill occupant or a base texture.
// The tiles around cells that were not set are made inactive.
void __fastcall Refresh(char* self, void*)
{
    static bool logged = false;
    FirstCall("Refresh", logged);
    // The loop bounds from the region as it is on entry.
    const Region* const bounds = RegionOf(self);
    for (int32_t x = bounds->x0; x <= bounds->x1; ++x) {
        for (int32_t z = bounds->z0; z <= bounds->z1; ++z) {
            if (CellSet(RegionOf(self), x, z)) {
                const uint32_t zone = Virtual<uint32_t>(At<void*>(self, field::kZoneManager), 0x30, x, z);
                if (zone != kZoneLandfill || NetworkAt(x, z)) {
                    SetCell(RegionOf(self), x, z, false);
                    continue;
                }
                void* occupant = nullptr;
                if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x84, &occupant, x, z,
                                     kTypeLandfillOccupant)) {
                    Release(occupant);
                } else {
                    occupant = nullptr;
                    if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x84, &occupant, x, z,
                                         kTypeBaseTexture)) {
                        Release(occupant);
                    } else {
                        ClearCellOccupants(self, x, z);
                        PlaceBaseTexture(self, x, z);
                    }
                }
            }
            if (!CellSet(RegionOf(self), x, z))
                DeactivateTilesAt(self, x, z);
        }
    }
}

// 0x006BCF20: the set cells where a rectangle of changed ground meets the
// zone's.
void __fastcall OnTilesChanged(char* self, void*, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("OnTilesChanged", logged);
    int32_t common[4] = {};
    if (!reinterpret_cast<IntersectFn>(addr::SC4Rect_Intersect)(common, rect, &RegionOf(self)->x0))
        return;
    for (int32_t x = common[0]; x <= common[2]; ++x)
        for (int32_t z = common[1]; z <= common[3]; ++z)
            if (CellSet(RegionOf(self), x, z))
                CheckCell(self, x, z);
}

// --- Save ----------------------------------------------------------------------------------

// 0x006BA900: version 4. True unless the stream is in error (slot 18).
uint32_t __fastcall Write(char* self, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    Virtual<uint8_t>(stream, 0x1C, 4u);
    const Region* const rect = RegionOf(self);
    Virtual<uint8_t>(stream, 0x20, rect->x0);
    Virtual<uint8_t>(stream, 0x20, rect->z0);
    Virtual<uint8_t>(stream, 0x20, rect->x1);
    Virtual<uint8_t>(stream, 0x20, rect->z1);
    // One byte per cell, the far edges left out (unsigned compares).
    for (uint32_t x = static_cast<uint32_t>(RegionOf(self)->x0); x < static_cast<uint32_t>(RegionOf(self)->x1); ++x)
        for (uint32_t z = static_cast<uint32_t>(RegionOf(self)->z0); z < static_cast<uint32_t>(RegionOf(self)->z1);
             ++z)
            Virtual<uint8_t>(stream, 0x14,
                             CellSet(RegionOf(self), static_cast<int32_t>(x), static_cast<int32_t>(z)) ? 1u : 0u);
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, field::kTileColumns));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, field::kTileRows));
    for (const Tile* tile = Tiles(self).begin; tile != Tiles(self).end; ++tile) {
        Virtual<uint8_t>(stream, 0x1C, 2u);
        Virtual<uint8_t>(stream, 0x24, tile->x);
        Virtual<uint8_t>(stream, 0x24, tile->z);
        Virtual<uint8_t>(stream, 0x1C, tile->active ? 1u : 0u);
        Virtual<uint8_t>(stream, 0x1C, tile->garbage ? 1u : 0u);
        Virtual<uint8_t>(stream, 0x24, tile->used);
        Virtual<uint8_t>(stream, 0x24, tile->capacity);
    }
    reinterpret_cast<StreamValueFn>(addr::SC4Stream_WriteBool)(stream, At<uint8_t>(self, field::kActive));
    reinterpret_cast<StreamPointerFn>(addr::SC4Stream_WriteReference)(stream, At<void*>(self, field::kMarker));
    return Virtual<uint8_t>(stream, 0x48) == 0;
}

// 0x006BBEF0: any version; 3 has the active flag as a byte, 4 and later a
// bool and the marker's reference. The version and the rectangle go into
// locals the game never initialises (zeros here); the cell bytes into the
// stack slot of its stream argument, whose low byte is 1 by then.
uint8_t __fastcall Read(char* self, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    int32_t rect[4] = {};
    for (int32_t& value : rect)
        Virtual<uint8_t>(stream, 0x20, &value);
    uint8_t byte = static_cast<uint8_t>(reinterpret_cast<std::uintptr_t>(stream));
    auto* const region = static_cast<Region*>(OperatorNew(0x24));
    if (region) {
        region->x0 = rect[0];
        region->z0 = rect[1];
        region->x1 = rect[2];
        region->z1 = rect[3];
        byte = 1;
        ConstructCells(&region->cells, static_cast<uint32_t>(rect[2] - rect[0] + 1),
                       static_cast<uint32_t>(rect[3] - rect[1] + 1), &byte);
    }
    // No null test from here on, as in the game.
    RegionOf(self) = region;
    for (uint32_t x = static_cast<uint32_t>(RegionOf(self)->x0); x < static_cast<uint32_t>(RegionOf(self)->x1); ++x)
        for (uint32_t z = static_cast<uint32_t>(RegionOf(self)->z0); z < static_cast<uint32_t>(RegionOf(self)->z1);
             ++z) {
            Virtual<uint8_t>(stream, 0x14, &byte);
            SetCell(RegionOf(self), static_cast<int32_t>(x), static_cast<int32_t>(z), byte != 0);
        }
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, field::kTileColumns));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, field::kTileRows));
    At<int32_t>(self, field::kTileCount) = At<int32_t>(self, field::kTileRows) * At<int32_t>(self, field::kTileColumns);
    // One tile object read over and over; a tile of another version leaves
    // it as the one before.
    Tile tile = BlankTile();
    for (int32_t i = 0; i < At<int32_t>(self, field::kTileCount); ++i) {
        ReadTile(&tile, stream);
        tile.index = static_cast<uint32_t>(i);
        PushTile(&Tiles(self), &tile);
    }
    if (version == 3) {
        Virtual<uint8_t>(stream, 0x14, &byte);
        At<uint8_t>(self, field::kActive) = byte != 0;
    }
    if (version >= 4) {
        reinterpret_cast<StreamPointerFn>(addr::SC4Stream_ReadBool)(stream, self + field::kActive);
        if (void* const marker = At<void*>(self, field::kMarker)) {
            At<void*>(self, field::kMarker) = nullptr;
            Release(marker);
        }
        reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference)(stream, kIidOccupant,
                                                                        &At<void*>(self, field::kMarker), 0);
    }
    return Virtual<uint8_t>(stream, 0x48) == 0;
}

// --- Installation --------------------------------------------------------------------------

#define OPENSC4_OURS(fn) reinterpret_cast<void*>(&fn)

const Detour kTileDetours[] = {
    {addr::cSC4LandfillZone_AddRegion, OPENSC4_OURS(AddRegion)},
    {addr::cSC4LandfillZone_ClearCell, OPENSC4_OURS(ClearCell)},
    {addr::cSC4LandfillZone_Sub6BCD70, OPENSC4_OURS(Refresh)},
    {addr::cSC4LandfillZone_OnTilesChanged, OPENSC4_OURS(OnTilesChanged)},
};
const Detour kSaveDetours[] = {
    {addr::cSC4LandfillZone_Write, OPENSC4_OURS(Write)},
    {addr::cSC4LandfillZone_Read, OPENSC4_OURS(Read)},
};

bool InstallTiles(registry::Mode) { return InstallDetours(kTileDetours, std::size(kTileDetours), "tiles"); }
bool InstallSave(registry::Mode) { return InstallDetours(kSaveDetours, std::size(kSaveDetours), "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.landfillzone.tiles", "simulators/landfillzone", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4LandfillZone_AddRegion,
                        "Our landfill zone zoning: detours on AddRegion, ClearCell, the cells' refresh and "
                        "OnTilesChanged (src/core/landfillzone_cells.cpp)",
                        false, InstallTiles}),
    registry::Register({"simulators.landfillzone.save", "simulators/landfillzone", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4LandfillZone_Write,
                        "Our landfill zone Write and Read: detours; the zone's part of the landfill developer's "
                        "record (src/core/landfillzone_cells.cpp)",
                        false, InstallSave}),
};

// --- Our object ----------------------------------------------------------------------------

// A copy of the game's 0x00AB1610 (5 slots), taken at the first
// construction, after every PostAppInit slot swap of the other groups.
constexpr int kSlots = 5;
void* g_vtable[kSlots];
bool g_vtableBuilt = false;

// As the game's (0x005DC8A0, shared with two other classes).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    if (iid != kIidUnknown && iid != kIidMessageTarget2)
        return 0;
    *object = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x005F47F0, shared with other classes).
uint32_t __fastcall AddRefZone(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x006BB8C0), Shutdown at the game's address.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    uint32_t& references = At<uint32_t>(self, field::kRefCount);
    if (references > 1)
        return --references;
    GameShutdown(self);
    Virtual<void*>(self, 0x10, 1u);
    return 0;
}

void BuildVtable()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4LandfillZone_vftable), sizeof(g_vtable));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRefZone);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtableBuilt = true;
    log::Write("landfillzone object: vtable copied (slot 3 %p, slot 4 %p)", g_vtable[3], g_vtable[4]);
}

// The fields both constructors set: everything 0 but the dirty flag.
void SetFields(char* self)
{
    if (!g_vtableBuilt)
        BuildVtable();
    At<void**>(self, 0x0) = g_vtable;
    At<uint32_t>(self, field::kRefCount) = 0;
    At<uint8_t>(self, field::kInitialized) = 0;
    for (std::size_t offset = 0xC; offset <= 0x24; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, field::kEmpty) = 0;
    At<uint8_t>(self, field::kActive) = 0;
    At<int32_t>(self, field::kCellCount) = 0;
    At<uint8_t>(self, field::kCellsDirty) = 1;
    for (std::size_t offset = 0x34; offset <= 0x6C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    static int created = 0;
    if (created++ < 3)
        log::Write("landfillzone object: constructed %p", static_cast<void*>(self));
}

// The plain constructor (detour of 0x006BBB60), used when a zone is read.
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    SetFields(self);
    return self;
}

// The constructor from a region (detour of 0x006BBD60): a copy of the
// region and a grid of blank tiles, each with its row as index.
void* __fastcall ConstructFromRegion(char* self, void*, const Region* region)
{
    static bool logged = false;
    FirstCall("ConstructFromRegion", logged);
    SetFields(self);
    auto* copy = static_cast<Region*>(OperatorNew(0x24));
    if (copy)
        copy = CopyRegion(copy, region);
    // No null test in the game either.
    RegionOf(self) = copy;
    At<int32_t>(self, field::kTileColumns) = copy->x1 - copy->x0;
    At<int32_t>(self, field::kTileRows) = copy->z1 - copy->z0;
    At<int32_t>(self, field::kTileCount) = At<int32_t>(self, field::kTileColumns) * At<int32_t>(self, field::kTileRows);
    TileVector& tiles = Tiles(self);
    EraseTiles(&tiles, tiles.begin, tiles.end);
    if (At<int32_t>(self, field::kTileCount) > 0) {
        Tile blank = BlankTile();
        for (int32_t row = 0; row < At<int32_t>(self, field::kTileRows); ++row) {
            for (int32_t column = 0; column < At<int32_t>(self, field::kTileColumns); ++column) {
                blank.index = static_cast<uint32_t>(row);
                blank.x = column;
                blank.z = row;
                PushTile(&Tiles(self), &blank);
            }
        }
    }
    return self;
}

// The destructor (detour of 0x006BBBC0): the marker released, the tiles
// destroyed and their storage freed. The region went in Shutdown.
void __fastcall Destruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destruct", logged);
    if (g_vtableBuilt)
        At<void**>(self, 0x0) = g_vtable;
    else
        At<std::uintptr_t>(self, 0x0) = addr::cSC4LandfillZone_vftable;
    if (void* const marker = At<void*>(self, field::kMarker))
        Release(marker);
    const TileVector& tiles = Tiles(self);
    DestroyTiles(tiles.begin, tiles.end);
    if (Tiles(self).begin)
        Free(Tiles(self).begin);
}

const Detour kObjectDetours[] = {
    {addr::cSC4LandfillZone_cSC4LandfillZone, OPENSC4_OURS(Construct)},
    {addr::cSC4LandfillZone_FromRegion, OPENSC4_OURS(ConstructFromRegion)},
    {addr::cSC4LandfillZone_Destructor, OPENSC4_OURS(Destruct)},
};

#undef OPENSC4_OURS

bool ApplyObject(registry::Mode) { return InstallDetours(kObjectDetours, std::size(kObjectDetours), "object"); }

const bool g_registeredObject = registry::Register({
    "simulators.landfillzone.object",
    "simulators/landfillzone",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LandfillZone_cSC4LandfillZone,
    "Our cSC4LandfillZone object: both constructors and the destructor (detours), identity and Release; methods "
    "from a copy of the (patched) game vtable (src/core/landfillzone_cells.cpp)",
    false,
    ApplyObject,
});

} // namespace

// --- What landfillzone_sim.cpp also calls ---------------------------------------------------

// 0x006BA120: the cell's landfill occupant removed, the cell no active
// landfill for the pollution simulator.
void DeactivateCell(char* self, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("DeactivateCell", logged);
    void* const occupants = At<void*>(self, field::kOccupants);
    void* occupant = nullptr;
    if (Virtual<uint8_t>(occupants, 0x84, &occupant, x, z, kTypeLandfillOccupant)) {
        Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x54, occupant, 1u, 3u);
        Release(occupant);
    }
    Virtual<void>(At<void*>(self, field::kPollution), 0xC8, x, z, 0u);
}

// 0x006BA3C0 (`this` unused): a cell's monthly cost from its landfill
// occupant's property 0xEA54D286; the tuned default without one.
int64_t GetCellCost(int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("GetCellCost", logged);
    int64_t cost = Global<int64_t>(addr::LandfillTuning_Value4A677E5F);
    void* const occupants = Virtual<void*>(Global<void*>(addr::SC4_City), 0x88);
    AddRef(occupants);
    void* occupant = nullptr;
    if (Virtual<uint8_t>(occupants, 0x84, &occupant, x, z, kTypeLandfillOccupant)) {
        int64_t value = Global<int64_t>(addr::LandfillTuning_Value4A677E5F);
        void* const holder = Virtual<void*>(occupant, 0x18);
        if (reinterpret_cast<GetSint64Fn>(addr::SC4_GetPropertySint64)(holder, 0xEA54D286, &value))
            cost = value;
        Release(occupant);
    }
    Release(occupants);
    return cost;
}

// 0x006BC760: a cell's picture. Always true.
uint8_t PlaceTileOccupant(char* self, int32_t x, int32_t z, uint32_t exemplarId, uint32_t rotation)
{
    static bool logged = false;
    FirstCall("PlaceTileOccupant", logged);
    ClearCellOccupants(self, x, z);
    if (exemplarId == Pictures()[0].exemplar) {
        // The plain picture: only a base texture, unless the cell has one.
        void* occupant = nullptr;
        if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x84, &occupant, x, z, kTypeBaseTexture)) {
            Release(occupant);
            return 1;
        }
        PlaceBaseTexture(self, x, z);
        return 1;
    }
    uint32_t key[3] = {0x6534284A, 0x07BDDF1C, exemplarId};
    void* exemplar = nullptr;
    Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, static_cast<void*>(key), 0xA52160F5u, &exemplar,
                     0u, 0u);
    char* object = nullptr;
    if (exemplar) {
        void* made = reinterpret_cast<NewFn>(addr::cSC4LandfillOccupant_Allocate)(0x68);
        if (made)
            made = reinterpret_cast<OccupantFn>(addr::cSC4LandfillOccupant_cSC4LandfillOccupant)(made, exemplarId,
                                                                                                 rotation);
        object = static_cast<char*>(made);
        AddRef(object);
        Virtual<void>(object + 0x20, 0xC, exemplar);
        void* const occupant = object + 0x4;
        Virtual<void>(occupant, 0x74, 4u);
        if (Pictures()[0].exemplar != exemplarId) {
            Virtual<void>(occupant, 0x30, 2u);
            Virtual<void>(At<void*>(self, field::kPollution), 0xC8, x, z, 1u);
            At<uint8_t>(object, 0x64) = 0;
        } else {
            // Not reached: the plain picture returned above.
            Virtual<void>(occupant, 0x30, 1u);
            Virtual<void>(At<void*>(self, field::kPollution), 0xC8, x, z, 0u);
            At<uint8_t>(object, 0x64) = 1;
            PlaceBaseTexture(self, x, z);
        }
        float position[3] = {};
        CellCentre(self, x, z, &position[0], &position[2]);
        position[1] = static_cast<float>(HeightAt(self, position[0], position[2]));
        Virtual<void>(occupant, 0x24, static_cast<void*>(position));
        Virtual<void>(object, 0xC);
        Release(object);
        Release(exemplar);
    }
    // Without the exemplar the game goes on with a null object too.
    void* const part = Virtual<void*>(object, 0x14);
    Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x50, part, 0u);
    return 1;
}

} // namespace opensc4::landfillzone_sim
