// Our cSC4ZoneDeveloper: parcels (build 641; code 0x0072E520 to 0x007306C0):
// the lots of the lot manager and the cutting of one piece of a zoned
// region into them.
//
//   CreateLot (lot manager slot 23, 0x0072E520) makes a cSC4Lot on a
//   rectangle of cells, puts it into the grid and the lot list (at the
//   traffic simulator's insertion point when it has one) and gives it its
//   empty base texture. DeleteLot (slot 24, 0x0072F8E0) takes a lot out of
//   both and shuts it down; during HighlightParcels (+0x99 clear) it only
//   notes the lot's cells in the map +0xE4.
//   ClearLots (0x0072E6A0; DoParcellization for a zone type that is not
//   RCI) deletes the RCI lots under a cell region and offers their ground
//   to 0x0072D440, and with its flag has the streets left in the region
//   demolished.
//   CutIntoPieces (0x0072EC60) splits a region into its connected pieces
//   (0x0072CDA0). CutPieceIntoLots (0x0072EDD0; zone types 1-6) fills the
//   piece +0xA4 with rows of lots back to back from the side +0xCC, choosing
//   each lot's depth from the room left, with streets between the double
//   rows and across them. CutPieceIntoCells (0x0072E9B0; the other RCI
//   types) lays a street every `spacing` cells both ways and makes each
//   cell its own lot (0x0072D200).
//   ExistsNetworkOfType (primary slot 8, 0x0072FE10), its helper
//   FindNetworkOccupant (0x0072FCA0), NetworkLeadsToward (0x0072FE80) and
//   RateCell (0x0072FAB0) are what zone_streets.cpp's street layout asks about a
//   cell. FillPieceTable (0x0072FF80; Init) fills the map +0xF0: for each
//   combination of connections on a cell's four sides the network piece,
//   its rotation and whether it is mirrored.
//
// Stays the game's although in the range: 0x0072F7D0, operator[] of the
// std::map from a cell pair to 12 bytes (+0xD8, +0xE4; lower bound
// 0x0072B950, insert 0x0072C370), and 0x0072F850, operator[] of the map
// from int32 to 8 bytes, which the lot developer shares
// (SC4Sint32PairMap::Index).
//
// Fields used here (offsets from the object's start; names in zone_sim.h
// where it has them). The lot manager's slots get object + 4.
//   0x18 kMessages      the message server; ClearLots sends 0x49D968C4
//                       (data 1 = 1, then 0) around its work (slot 4)
//   0x20 kDevelopment   the building development simulator; slot 36 gives
//                       the lot developer (DeleteLot)
//   0x24 kCity          slots 93 and 94: the city's size in cells
//                       (CutPieceIntoLots clips the rectangle it levels)
//   0x30 kOccupants     the occupant manager: slot 41 the occupants of a
//                       cell rectangle by type into a list, slot 48
//                       releases such a list, slot 21 removes an occupant
//   0x40 kZones         the zone manager; slot 13 IsRCIZone
//   0x44 kTool44        network tool of type 11: slot 9 (x, z, flag 0x800,
//                       uint32* sides) whether it has a network planned at
//                       the cell (ExistsNetworkOfType, NetworkLeadsToward)
//   0x48 kStreetTool    network tool of type 3: the same with flag 8
//   0x50 kLotDeveloper  slot 11 removes a lot's zot prop (DeleteLot), slot
//                       21 levels an area (CutPieceIntoLots)
//   0x54 kLotGrid       columns of lot pointers [x][z] (CreateLot,
//                       DeleteLot, ClearLots)
//   0x60 kLotList       the lot list (CreateLot appends, DeleteLot removes
//                       through 0x0072B990 with the list as `this`)
//   0x68 kLotCount      CreateLot counts the append
//   0x84 lot width, 0x88 lot depth (int32), 0x8C, 0x90 smallest and largest
//                       depth (uint32), 0x94 street spacing (uint32): what
//                       zone_parcellize.cpp's ChooseLotSizes chose (CutPieceIntoLots;
//                       CutPieceIntoCells takes its spacing as an argument)
//   0x98 kByte98        uint8: allows the extra street along the middle row
//                       of a narrow piece (CutPieceIntoLots)
//   0x99 kPlaceStreets  uint8: clear while HighlightParcels runs. DeleteLot
//                       then only records; ExistsNetworkOfType then also
//                       asks the network tools for planned networks;
//                       CutPieceIntoLots levels the lots' ground when set
//   0x9A kOption9A      uint8: streets are laid (CutPieceIntoLots,
//                       CutPieceIntoCells)
//   0xA0 kZoneType      ClearLots keeps the zone type of the lot it is
//                       deleting here for the call
//   0xA4 kRegion        the piece being cut (CutPieceIntoCells frees it and
//                       clears the field)
//   0xBC                int32[4] by side: 1 where the piece has a street on
//                       that side (the facing of the lots against it)
//   0xCC, 0xD0          the side the rows start from and the side the lots
//                       of a row start from (0-3)
//   0xE4 kCellsE4       map from a cell to {lot x, lot z, facing}: the lots
//                       DeleteLot would have deleted during HighlightParcels
//   0xF0 kMapF0         the network piece table (FillPieceTable)
// Globals: 0x00B43DA8 the traffic simulator (slot 22 the lot insertion
// point), 0x00B43D68 the building development simulator, 0x00B43D00 the
// demolition utility (slot 6, 11 arguments), 0x00B207E4 the list node
// allocator.
//
// Every virtual call's argument count was taken from the callee's `ret N`
// (vt.py on the receiver's vtable): the lot 0x00AB19B0, the lot developer
// 0x00AB1B98, the development simulator 0x00AB0AF8, the traffic simulator
// 0x00AB33D0, the occupant manager 0x00A8F7F0, the zone manager 0x00A92E00,
// the city 0x00A87620, the message 0x00ACEC00 and server 0x00A8CAFC, the
// demolition utility 0x00A88004, the network tool 0x00AA8748, the network
// occupant interface 0xA821EF94 (seven vtables, the same counts), the
// terrain 0x00AB3D18 and this class's own two vtables. In DeleteLot the two
// values pushed before the base texture occupant's slot 3 belong to the
// occupant manager's RemoveOccupant (3 arguments) that follows.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/zone_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace opensc4::zone_sim {
namespace {

// Offsets zone_sim.h has no name for.
constexpr std::size_t kLotWidth = 0x84, kLotDepth = 0x88, kMinDepth = 0x8C, kMaxDepth = 0x90, kStreetSpacing = 0x94;
constexpr std::size_t kStreetSides = 0xBC, kRowSide = 0xCC, kLotSide = 0xD0;

constexpr uint32_t kMessageParcels = 0x49D968C4;
constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;
constexpr uint32_t kTypeNetwork = 0xC772BF98;                         // the occupant type FindNetworkOccupant asks for
constexpr uint32_t kTypeBlockerA = 0x278128A0, kTypeBlockerB = 0x2890D4DE; // occupant types RateCell counts as taken

struct Region {
    int32_t x0;
    int32_t z0;
    int32_t x1;
    int32_t z1;
    std::uintptr_t cellsVtable;
    uint32_t width;
    uint32_t depth;
    uint32_t unknown1C;
    uint32_t** rows;
};
static_assert(sizeof(Region) == 0x24);

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

struct Column {
    void** begin;
    void** end;
    void** capacity;
};

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void*(__thiscall*)(void* self);
using CellsFn = void*(__thiscall*)(void* cells, uint32_t width, uint32_t depth, const uint8_t* fill);
using LotFn = void*(__thiscall*)(void* lot, int32_t x, int32_t z, int32_t width, int32_t depth, uint32_t facing);

const auto StlAllocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto StlFree = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto Delete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto RzNew = reinterpret_cast<NewFn>(addr::RZ_operator_new);

// A function of the game by address, `this` first.
template <typename R, typename... Args>
R Game(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

// The game's loops over a rectangle of cells: rows of x inside z, and at
// least one cell (the far corner is raised to the near one first).
template <typename Fn>
void ForEachCell(int32_t x0, int32_t z0, int32_t x1, int32_t z1, Fn fn)
{
    if (x1 < x0)
        x1 = x0;
    if (z1 < z0)
        z1 = z0;
    for (int32_t z = z0; z <= z1; ++z) {
        for (int32_t x = x0; x <= x1; ++x)
            fn(x, z);
    }
}

void*& GridCell(char* self, int32_t x, int32_t z)
{
    return At<Column*>(self, field::kLotGrid)[x].begin[z];
}

// A list of the STL allocator (0x0090CF54): a head node alone.
ListNode* NewStlList()
{
    auto* const head = static_cast<ListNode*>(StlAllocate(0xC));
    head->next = head;
    head->previous = head;
    return head;
}

void StlAppend(ListNode* head, void* value)
{
    auto* const node = static_cast<ListNode*>(StlAllocate(0xC));
    node->value = value;
    ListNode* const last = head->previous;
    node->next = head;
    node->previous = last;
    last->next = node;
    head->previous = node;
}

void StlClear(ListNode* head)
{
    for (ListNode* node = head->next; node != head;) {
        ListNode* const next = node->next;
        StlFree(node);
        node = next;
    }
    head->next = head;
    head->previous = head;
}

// Frees a cell region from operator new as the game does inline: the
// rows, their table, the region.
void DeleteRegion(Region* region)
{
    region->cellsVtable = addr::SC4CellRegion_CellsVtable;
    if (region->rows) {
        for (uint32_t i = 0; i < region->width; ++i)
            Delete(region->rows[i]);
        Delete(region->rows);
    }
    Delete(region);
}

void SendParcelsMessage(char* self, uint32_t data1)
{
    void* const memory = RzNew(0x2C);
    void* const message = memory ? reinterpret_cast<SelfFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, kMessageParcels);
    Virtual<uint32_t>(message, 0x2C, data1);
    Virtual<uint32_t>(At<void*>(self, field::kMessages), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

// The primary slot 8 through the vtable, as the game's own calls go.
bool NetworkAt(char* self, int32_t x, int32_t z, uint32_t types)
{
    return Virtual<uint8_t>(self, 0x20, x, z, types) != 0;
}

// --- The lot manager's CreateLot and DeleteLot -------------------------------

// Lot manager slot 23 (0x0072E520; `this` is object + 4). The lot gets two
// references: the caller's (*out) and the manager's.
uint32_t __fastcall CreateLot(char* manager, void*, int32_t x, int32_t z, int32_t width, int32_t depth,
                              uint32_t facing, void** out)
{
    static bool logged = false;
    FirstCall("CreateLot", logged);
    char* const self = manager - kLotManagerPart;
    void* const memory = reinterpret_cast<NewFn>(addr::cSC4Lot_Allocate)(0x8C);
    void* const lot =
        memory ? reinterpret_cast<LotFn>(addr::cSC4Lot_cSC4Lot)(memory, x, z, width, depth, facing) : nullptr;
    *out = lot;
    Virtual<uint32_t>(lot, 0x4);
    Virtual<uint32_t>(*out, 0x14);
    Virtual<uint32_t>(*out, 0x88, 1u, 0u, 0u);
    Virtual<uint32_t>(*out, 0x4);
    ForEachCell(x, z, x + width - 1, z + depth - 1, [&](int32_t cx, int32_t cz) { GridCell(self, cx, cz) = *out; });
    if (void* const where = Virtual<void*>(Global<void*>(addr::SC4_Service43DA8), 0x58)) {
        Game<void>(addr::cSC4ZoneDeveloper_Fn_0072C810, where, out);
    } else {
        StlAppend(At<ListNode*>(self, field::kLotList), *out);
        ++At<uint32_t>(self, field::kLotCount);
    }
    void* const lotDeveloper = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x90);
    Virtual<uint32_t>(lotDeveloper, 0xAC, *out);
    return *out ? 1 : 0;
}

// Lot manager slot 24 (0x0072F8E0; `this` is object + 4).
void __fastcall DeleteLot(char* manager, void*, void* lot)
{
    static bool logged = false;
    FirstCall("DeleteLot", logged);
    char* const self = manager - kLotManagerPart;
    int32_t rect[4] = {};
    Virtual<uint32_t>(lot, 0x38, rect);
    if (!At<uint8_t>(self, field::kPlaceStreets)) {
        const uint32_t facing = Virtual<uint32_t>(lot, 0x5C);
        const int32_t lotX = rect[0], lotZ = rect[1];
        ForEachCell(rect[0], rect[1], rect[2], rect[3], [&](int32_t x, int32_t z) {
            const int32_t key[2] = {x, z};
            int32_t* const value =
                Game<int32_t*>(addr::cSC4ZoneDeveloper_Fn_0072F7D0, self + field::kCellsE4, &key[0]);
            value[0] = lotX;
            value[1] = lotZ;
            value[2] = static_cast<int32_t>(facing);
        });
        return;
    }
    Virtual<uint32_t>(At<void*>(self, field::kLotDeveloper), 0x2C, lot);
    void* texture = nullptr;
    void* lotDeveloper = Virtual<void*>(At<void*>(self, field::kDevelopment), 0x90);
    if (Virtual<uint8_t>(lotDeveloper, 0xA4, lot, &texture, 0u)) {
        void* const occupant = Virtual<void*>(texture, 0xC);
        Virtual<uint32_t>(At<void*>(self, field::kOccupants), 0x54, occupant, 1u, 0u);
        Virtual<uint32_t>(texture, 0x8);
    }
    ForEachCell(rect[0], rect[1], rect[2], rect[3], [&](int32_t x, int32_t z) { GridCell(self, x, z) = nullptr; });
    Game<void>(addr::cSC4ZoneDeveloper_Fn_0072B990, self + field::kLotList, &lot);
    if (Virtual<void*>(lot, 0x64)) {
        lotDeveloper = Virtual<void*>(At<void*>(self, field::kDevelopment), 0x90);
        Virtual<uint32_t>(lotDeveloper, 0x84, lot);
    }
    Virtual<uint32_t>(lot, 0x1C);
    Virtual<uint32_t>(lot, 0x8);
}

// --- Clearing a region ----------------------------------------------------------

// 0x0072E6A0 (region, flag; ret 8). For each cell set in the region: the
// lot there, when its zone type is RCI or 11 or 12, is deleted and its
// rectangle given to 0x0072D440; with the flag, a cell with a network of
// types 0x808 and none of types 0x1747 is collected, and the collected
// cells go to the demolition utility.
void __fastcall ClearLots(char* self, void*, Region* region, uint32_t flag)
{
    static bool logged = false;
    FirstCall("ClearLots", logged);
    SendParcelsMessage(self, 1);
    Region streets;
    streets.x0 = region->x0;
    streets.z0 = region->z0;
    streets.x1 = region->x1;
    streets.z1 = region->z1;
    const uint8_t fill = 0;
    bool any = false;
    reinterpret_cast<CellsFn>(addr::SC4CellRegion_ConstructCells)(
        &streets.cellsVtable, static_cast<uint32_t>(region->x1 - region->x0 + 1),
        static_cast<uint32_t>(region->z1 - region->z0 + 1), &fill);
    ForEachCell(region->x0, region->z0, region->x1, region->z1, [&](int32_t x, int32_t z) {
        const uint32_t column = static_cast<uint32_t>(x - region->x0);
        const uint32_t row = static_cast<uint32_t>(z - region->z0);
        if (column >= region->width || row >= region->depth)
            return;
        if (!(region->rows[column][row >> 5] & (1u << (row & 31))))
            return;
        if (void* const lot = GridCell(self, x, z)) {
            int32_t rect[4] = {};
            Virtual<uint32_t>(lot, 0x38, rect);
            const uint32_t facing = Virtual<uint32_t>(lot, 0x5C);
            const uint32_t zoneType = Virtual<uint32_t>(lot, 0x7C);
            At<uint32_t>(self, field::kZoneType) = zoneType;
            if (Virtual<uint8_t>(At<void*>(self, field::kZones), 0x34, zoneType) ||
                At<uint32_t>(self, field::kZoneType) == 11 || At<uint32_t>(self, field::kZoneType) == 12) {
                Virtual<uint32_t>(self + kLotManagerPart, 0x60, lot);
                Game<void>(addr::cSC4ZoneDeveloper_Fn_0072D440, self, rect[0], rect[1], rect[2] - rect[0] + 1,
                           rect[3] - rect[1] + 1, facing);
            }
            At<uint32_t>(self, field::kZoneType) = 0;
        }
        if (!static_cast<uint8_t>(flag))
            return;
        if (!NetworkAt(self, x, z, 0x808) || NetworkAt(self, x, z, 0x1747))
            return;
        const uint32_t streetRow = static_cast<uint32_t>(z - streets.z0);
        streets.rows[x - streets.x0][streetRow >> 5] |= 1u << (streetRow & 31);
        any = true;
    });
    if (any) {
        Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D00), 0x18, 1u, &streets, 1u, 1u, 0u, 0u, 0u, 0u, 0u, 0u,
                          0u);
    }
    SendParcelsMessage(self, 0);
    streets.cellsVtable = addr::SC4CellRegion_CellsVtable;
    reinterpret_cast<SelfFn>(addr::SC4CellRegion_DestroyCells)(&streets.cellsVtable);
}

// --- Cutting a region into pieces and a piece into lots -------------------------

// 0x0072EC60 (region, list; ret 8): a copy of the region (0x0072CB90) is
// emptied piece by piece (0x0072CDA0 takes the connected piece of a set
// cell out of it); each piece is fitted (0x0072AF30) and appended to the
// list. When there was none, the copy itself is fitted and appended.
// The loops run over the size of the region at +0xA4.
void __fastcall CutIntoPieces(char* self, void*, Region* source, ListNode** pieces)
{
    static bool logged = false;
    FirstCall("CutIntoPieces", logged);
    Region* copy = Game<Region*>(addr::cSC4ZoneDeveloper_Fn_0072CB90, self, source);
    const Region* const current = At<Region*>(self, field::kRegion);
    const int32_t width = static_cast<int32_t>(current->width);
    const int32_t depth = static_cast<int32_t>(current->depth);
    for (int32_t column = 0; column < width; ++column) {
        for (int32_t row = 0; row < depth; ++row) {
            if (!(copy->rows[column][static_cast<uint32_t>(row) >> 5] & (1u << (row & 31))))
                continue;
            void* piece = Game<void*>(addr::cSC4ZoneDeveloper_Fn_0072CDA0, self, copy, column, row);
            Game<void>(addr::cSC4ZoneDeveloper_Fn_0072AF30, self, &piece);
            StlAppend(*pieces, piece);
        }
    }
    if ((*pieces)->next == *pieces) {
        Game<void>(addr::cSC4ZoneDeveloper_Fn_0072AF30, self, &copy);
        StlAppend(*pieces, copy);
    } else if (copy) {
        DeleteRegion(copy);
    }
}

// 0x0072E9B0 (spacing; ret 4): with streets on, a street across the piece
// (0x0072A2A0) at every `spacing`-th line at least two cells from both
// edges, first along x from the corner the two sides name, then along z;
// then every cell of the piece's rectangle becomes its own lot
// (0x0072D200 with an empty orientation list); the piece is freed.
void __fastcall CutPieceIntoCells(char* self, void*, int32_t spacing)
{
    static bool logged = false;
    FirstCall("CutPieceIntoCells", logged);
    Region* const piece = At<Region*>(self, field::kRegion);
    const uint32_t rowSide = At<uint32_t>(self, kRowSide);
    const uint32_t lotSide = At<uint32_t>(self, kLotSide);
    int32_t startX = spacing, startZ = spacing; // a side above 3: the game's default
    bool upX = false, upZ = false;              // (it then reads two bytes of its frame it never wrote)
    switch (rowSide) {
    case 0:
        upX = lotSide == 3;
        startX = upX ? piece->x0 : piece->x1;
        startZ = piece->z0;
        upZ = true;
        break;
    case 2:
        upX = lotSide == 3;
        startX = upX ? piece->x0 : piece->x1;
        startZ = piece->z1;
        upZ = false;
        break;
    case 1:
    case 3:
        upX = rowSide == 3;
        startX = upX ? piece->x0 : piece->x1;
        upZ = lotSide == 0;
        startZ = upZ ? piece->z0 : piece->z1;
        break;
    default:
        break;
    }
    if (At<uint8_t>(self, field::kOption9A)) {
        int32_t since = 0;
        for (int32_t x = startX; piece->x0 <= x && x <= piece->x1; x += upX ? 1 : -1) {
            if (since >= spacing && x - piece->x0 >= 2 && piece->x1 - x >= 2) {
                since = 0;
                Game<void>(addr::cSC4ZoneDeveloper_Fn_0072A2A0, self, x, piece->z0, x, piece->z1, 0u);
            } else {
                ++since;
            }
        }
        since = 0;
        for (int32_t z = startZ; piece->z0 <= z && z <= piece->z1; z += upZ ? 1 : -1) {
            if (since >= spacing && z - piece->z0 >= 2 && piece->z1 - z >= 2) {
                since = 0;
                Game<void>(addr::cSC4ZoneDeveloper_Fn_0072A2A0, self, piece->x0, z, piece->x1, z, 0u);
            } else {
                ++since;
            }
        }
    }
    for (int32_t x = piece->x0; x <= piece->x1; ++x) {
        for (int32_t z = piece->z0; z <= piece->z1; ++z) {
            void* lot = nullptr;
            ListNode* orientations = NewStlList();
            Game<uint8_t>(addr::cSC4ZoneDeveloper_Fn_0072D200, self, x, z, 1, 1, &orientations, &lot);
            if (lot)
                Virtual<uint32_t>(lot, 0x8);
            StlClear(orientations);
            StlFree(orientations);
        }
    }
    if (Region* const done = At<Region*>(self, field::kRegion))
        DeleteRegion(done);
    At<Region*>(self, field::kRegion) = nullptr;
}

// The depth of the next lot of a row from the room left in it (0x0072F253
// to 0x0072F364): `room` cells are left, `depth` is the chosen lot depth,
// `streets` 1 when streets are laid. `facesFirst`: the lot is the first of
// a back-to-back pair; `farIsSecond`: the piece's far edge wants the second
// facing. Unsigned compares as the game's.
int32_t ChooseDepth(char* self, int32_t room, int32_t depth, uint32_t streets, bool facesFirst, bool farIsSecond)
{
    const uint32_t left = static_cast<uint32_t>(room);
    const uint32_t minDepth = At<uint32_t>(self, kMinDepth);
    const uint32_t maxDepth = At<uint32_t>(self, kMaxDepth);
    const uint32_t deep = static_cast<uint32_t>(depth);
    if (farIsSecond && facesFirst) {
        if (left <= maxDepth || left < minDepth * 2)
            return room;
        if (left < maxDepth * 2 - 1 || left < streets + minDepth * 3)
            return (room + 1) / 2;
        if (left >= streets + deep * 3)
            return depth;
        return (room + 1) / 3;
    }
    if (!farIsSecond && !facesFirst) {
        if (left < streets + minDepth * 2)
            return room > 1 ? room - 1 : depth;
        if (left >= streets + deep * 2)
            return depth;
        if (static_cast<uint32_t>(room - 1) > maxDepth)
            return room / 2;
        return room > 1 ? room - 1 : depth;
    }
    if (left < streets + minDepth * 2)
        return room;
    if (left >= streets + deep * 2)
        return depth;
    if (left <= maxDepth)
        return room;
    return room / 2;
}

// 0x0072EDD0 (no arguments). The piece at +0xA4 is filled with rows of
// lots. The rows run along axis A (x for the row sides 0 and 2, z for 1
// and 3) and follow each other along axis B, each a lot width (+0x84)
// further from the side +0xCC; within a row the lots follow each other a
// lot depth (+0x88) apart from the side +0xD0, alternately facing the two
// ways along A, so that they stand back to back with, when streets are
// laid, a street across the piece after each pair (in the first row's
// pass) and a street along the rows every +0x94 cells of rows. Each lot is
// asked of the lot manager's slot 22; what it refuses goes to 0x0072D440.
// The lots made keep their vertices locked until the piece is done.
void __fastcall CutPieceIntoLots(char* self, void*)
{
    static bool logged = false;
    FirstCall("CutPieceIntoLots", logged);
    const uint32_t rowSide = At<uint32_t>(self, kRowSide);
    const Region* const piece = At<Region*>(self, field::kRegion);
    if (rowSide > 3)
        return;
    const int32_t lotWidth = At<int32_t>(self, kLotWidth);
    const int32_t lotDepth = At<int32_t>(self, kLotDepth);
    const uint32_t lotSide = At<uint32_t>(self, kLotSide);
    const bool alongX = rowSide == 0 || rowSide == 2;
    int32_t x = 0, z = 0; // the cell being worked on; the game reaches them through two pointers too
    int32_t* const a = alongX ? &x : &z;
    int32_t* const b = alongX ? &z : &x;
    const int32_t lowA = alongX ? piece->x0 : piece->z0;
    const int32_t highA = alongX ? piece->x1 : piece->z1;
    const int32_t lowB = alongX ? piece->z0 : piece->x0;
    const int32_t highB = alongX ? piece->z1 : piece->x1;

    // Axis A: from the low edge upwards, or from the high edge downwards.
    const bool upA = alongX ? lotSide == 3 : lotSide == 0;
    int32_t startA, limitA, endA, stepA, streetStepA;
    uint32_t facingFirst, facingSecond;
    if (upA) {
        startA = lowA;
        limitA = highA;
        endA = lowA;
        stepA = lotDepth;
        streetStepA = lotDepth;
        facingFirst = alongX ? 3 : 0;
        facingSecond = alongX ? 1 : 2;
    } else {
        startA = highA - lotDepth + 1;
        if (startA < 0)
            startA = 0;
        limitA = startA;
        endA = lowA - lotDepth + 1;
        stepA = -lotDepth;
        streetStepA = -1;
        facingFirst = alongX ? 1 : 2;
        facingSecond = alongX ? 3 : 0;
    }

    // Axis B.
    const bool upB = rowSide == 0 || rowSide == 3;
    int32_t startB, limitB, endB, stepB, streetStepB;
    if (upB) {
        startB = lowB;
        limitB = highB;
        endB = lowB;
        stepB = lotWidth;
        streetStepB = lotWidth;
    } else {
        startB = highB - lotWidth + 1;
        limitB = startB;
        endB = lowB - lotWidth + 1;
        stepB = -lotWidth;
        streetStepB = -1;
    }

    // The lot's size along x and z before its depth is adjusted.
    const int32_t sizeX0 = alongX ? lotDepth : lotWidth;
    const int32_t sizeZ0 = alongX ? lotWidth : lotDepth;

    const uint32_t rowFacing =
        At<int32_t>(self, kStreetSides + lotSide * 4) == 1 ? facingFirst : facingSecond;
    const uint32_t farFacing =
        At<int32_t>(self, kStreetSides + ((lotSide - 2) & 3) * 4) == 1 ? facingSecond : facingFirst;
    const uint32_t streets = At<uint8_t>(self, field::kOption9A) ? 1 : 0;
    const uint32_t extentB = static_cast<uint32_t>(highB - lowB + 1);
    const int32_t extentA = highA - lowA + 1;

    // The middle row: half as many steps from the first row as there are
    // rows, less one step unless that is the first row.
    int32_t middleB = startB;
    if (endB <= startB) {
        int32_t position = startB;
        bool second = false;
        for (;;) {
            if (position > limitB)
                break;
            position += stepB;
            if (second)
                middleB += stepB;
            second = !second;
            if (!(endB <= position))
                break;
        }
        if (middleB != startB)
            middleB -= stepB;
    }

    ListNode* const lots = NewStlList();
    int32_t sinceStreet = 0;
    bool crossed = false; // a street across the rows was laid
    *b = startB;
    if (endB <= startB) {
        for (;;) {
            if (*b > limitB)
                break;
            uint32_t facing = rowFacing;
            *a = startA;
            if (endA <= startA) {
                for (;;) {
                    const int32_t at = *a;
                    if (at > limitA)
                        break;
                    const int32_t depthNow = At<int32_t>(self, kLotDepth);
                    void* lot = nullptr;
                    int32_t shift = 0;
                    int32_t sizeX = sizeX0, sizeZ = sizeZ0;
                    const int32_t room = stepA > 0 ? highA - at + 1 : at - lowA + depthNow;
                    if (room > 0) {
                        int32_t chosen = ChooseDepth(self, room, depthNow, streets, facing == facingFirst,
                                                     farFacing == facingSecond);
                        // A piece no deeper than the deepest lot and too narrow for two
                        // rows of lots with a street: one lot over its whole depth.
                        if (static_cast<uint32_t>(extentA) <= At<uint32_t>(self, kMaxDepth) &&
                            extentB < streets + At<uint32_t>(self, kLotWidth) * 2)
                            chosen = extentA;
                        if (chosen != depthNow) {
                            if (alongX) {
                                shift = chosen - sizeX;
                                sizeX = chosen;
                            } else {
                                shift = chosen - sizeZ;
                                sizeZ = chosen;
                            }
                            if (stepA < 0)
                                *a = at - shift;
                        }
                    }
                    int32_t rect[4] = {x, z, x + sizeX - 1, z + sizeZ - 1};
                    if (!Virtual<uint8_t>(self + kLotManagerPart, 0x58, x, z, sizeX, sizeZ, facing, &lot, 0u, 0u, 0u))
                        Game<void>(addr::cSC4ZoneDeveloper_Fn_0072D440, self, x, z, sizeX, sizeZ, facing);
                    if (shift != 0 && stepA > 0)
                        *a += shift;
                    if (facing == facingFirst) {
                        facing = facingSecond;
                    } else {
                        facing = facingFirst;
                        if (At<uint8_t>(self, field::kOption9A)) {
                            // After a pair: the next line along A is a street's, laid
                            // across the whole piece while the first row is cut.
                            const int32_t savedA = *a, savedB = *b;
                            int32_t nextA = savedA;
                            const int32_t streetA = savedA + streetStepA;
                            *a = streetA;
                            if (lowA <= streetA && streetA <= highA) {
                                if (savedB == startB) {
                                    *b = lowB;
                                    const int32_t fromX = x, fromZ = z;
                                    *b = highB;
                                    // The game passes a dword whose low byte it set.
                                    const uint32_t atEdge = (*a == lowA || *a == highA) ? 1 : 0;
                                    Game<void>(addr::cSC4ZoneDeveloper_Fn_0072A2A0, self, fromX, fromZ, x, z, atEdge);
                                    crossed = true;
                                }
                                nextA = stepA > 0 ? savedA + 1 : savedA - 1;
                            }
                            *a = nextA;
                            *b = savedB;
                        }
                    }
                    if (At<uint8_t>(self, field::kPlaceStreets)) {
                        if (rect[0] < 0)
                            rect[0] = 0;
                        if (rect[1] < 0)
                            rect[1] = 0;
                        void* const city = At<void*>(self, field::kCity);
                        if (!(rect[2] < Virtual<int32_t>(city, 0x174)))
                            rect[2] = Virtual<int32_t>(city, 0x174) - 1;
                        if (!(rect[3] < Virtual<int32_t>(city, 0x178)))
                            rect[3] = Virtual<int32_t>(city, 0x178) - 1;
                        // AttemptAreaLevelling(rectangle, 0, 1, 1, FLT_MAX, 0).
                        Virtual<uint32_t>(At<void*>(self, field::kLotDeveloper), 0x54, &rect[0], 0u, 1u, 1u,
                                          0x7F7FFFFFu, 0u);
                    }
                    if (lot) {
                        Virtual<uint32_t>(lot, 0x114, 1u);
                        StlAppend(lots, lot);
                    }
                    *a += stepA;
                    if (!(endA <= *a))
                        break;
                }
            }
            if (At<uint8_t>(self, field::kOption9A)) {
                const uint32_t width = At<uint32_t>(self, kLotWidth);
                sinceStreet += static_cast<int32_t>(width);
                const bool middle = At<uint8_t>(self, field::kByte98) && crossed &&
                                    extentB <= At<uint32_t>(self, kStreetSpacing) + width + 1 && *b == middleB;
                if (static_cast<uint32_t>(sinceStreet) >= At<uint32_t>(self, kStreetSpacing) || middle) {
                    const int32_t savedB = *b;
                    int32_t nextB = savedB;
                    const int32_t streetB = streetStepB + savedB;
                    *b = streetB;
                    if (middle || (static_cast<uint32_t>(lowB) + width <= static_cast<uint32_t>(streetB) &&
                                   static_cast<uint32_t>(streetB) <= static_cast<uint32_t>(highB) - width)) {
                        *a = lowA;
                        const int32_t fromX = x, fromZ = z;
                        *a = highA;
                        sinceStreet = 0;
                        const uint32_t atEdge = (*b == lowB || *b == highB) ? 1 : 0;
                        Game<void>(addr::cSC4ZoneDeveloper_Fn_0072A2A0, self, fromX, fromZ, x, z, atEdge);
                        nextB = stepB > 0 ? savedB + 1 : savedB - 1;
                    }
                    *b = nextB;
                }
            }
            *b += stepB;
            if (!(endB <= *b))
                break;
        }
    }
    for (ListNode* node = lots->next; node != lots; node = node->next) {
        void* const lot = node->value;
        Virtual<uint32_t>(lot, 0x114, 0u);
        Virtual<uint32_t>(lot, 0x8);
    }
    StlClear(lots);
    StlFree(lots);
}

// --- What stands on a cell ------------------------------------------------------

// 0x0072FCA0 (x, z, types; ret 0xC): the network occupant of the cell
// (occupant type 0xC772BF98, interface 0xA821EF94) whose slot 9 accepts
// `types`, not referenced; null when there is none.
void* __fastcall FindNetworkOccupant(char* self, void*, int32_t x, int32_t z, uint32_t types)
{
    static bool logged = false;
    FirstCall("FindNetworkOccupant", logged);
    void* found = nullptr;
    void* const allocator = ListNodeAllocator();
    auto* list = Virtual<ListNode*>(allocator, 0xC, 0xCu);
    list->next = list;
    list->previous = list;
    int32_t cell[2] = {x, z};
    void* const occupants = At<void*>(self, field::kOccupants);
    Virtual<uint32_t>(occupants, 0xA4, &list, &cell[0], &cell[0], kTypeNetwork, 0xFFFFFFFFu);
    for (ListNode* node = list->next; node != list; node = node->next) {
        void* network = nullptr;
        if (Virtual<uint8_t>(node->value, 0x0, kIidNetworkOccupant, &network) &&
            Virtual<uint8_t>(network, 0x24, types)) {
            found = network;
            if (network)
                Virtual<uint32_t>(network, 0x8);
            break;
        }
        if (network)
            Virtual<uint32_t>(network, 0x8);
    }
    Virtual<uint32_t>(At<void*>(self, field::kOccupants), 0xC0, &list);
    reinterpret_cast<SelfFn>(addr::SC4List_Clear2)(&list);
    Virtual<uint32_t>(ListNodeAllocator(), 0x10, list);
    return found;
}

// Primary slot 8 (0x0072FE10; ret 0xC). Without a network occupant, and
// only during HighlightParcels (+0x99 clear), the street tool (types bit
// 3) or else the tool of type 11 (bit 11) is asked for a planned one.
uint32_t __fastcall ExistsNetworkOfType(char* self, void*, int32_t x, int32_t z, uint32_t types)
{
    static bool logged = false;
    FirstCall("ExistsNetworkOfType", logged);
    if (FindNetworkOccupant(self, nullptr, x, z, types))
        return 1;
    if (At<uint8_t>(self, field::kPlaceStreets))
        return 0;
    uint32_t sides = types; // the game lends the tool its argument's slot
    if (types & 0x8)
        return Virtual<uint8_t>(At<void*>(self, field::kStreetTool), 0x24, x, z, 0x8u, &sides);
    if (types & 0x800)
        return Virtual<uint8_t>(At<void*>(self, field::kTool44), 0x24, x, z, 0x800u, &sides);
    return 0;
}

// 0x0072FE80 (x, z, types, side, flag; ret 0x14): whether the cell's
// network of `types` has a connection on the side after `side` (side + 1,
// modulo 4): the occupant's slot 47 (that side, flag), or during
// HighlightParcels the planned network's byte for that side.
uint32_t __fastcall NetworkLeadsToward(char* self, void*, int32_t x, int32_t z, uint32_t types, uint32_t side,
                                       uint32_t flag)
{
    static bool logged = false;
    FirstCall("NetworkLeadsToward", logged);
    const uint32_t toward = side <= 3 ? ((side + 1) & 3) : 0xFFFFFFFFu;
    if (void* const network = FindNetworkOccupant(self, nullptr, x, z, types)) {
        if (Virtual<uint8_t>(network, 0xBC, toward, flag))
            return 1;
    }
    if (At<uint8_t>(self, field::kPlaceStreets) || !(types & 0x808))
        return 0;
    uint32_t sides = types;
    if (!Virtual<uint8_t>(At<void*>(self, field::kStreetTool), 0x24, x, z, 0x8u, &sides) &&
        !Virtual<uint8_t>(At<void*>(self, field::kTool44), 0x24, x, z, 0x800u, &sides))
        return 0;
    // The shift is the processor's: its count's low five bits.
    return static_cast<uint8_t>(sides >> ((toward * 8) & 31)) != 0 ? 1 : 0;
}

// 0x0072FAB0 (x, z, level, score; ret 0x10): how much a cell is in the
// way. A network of types 0x1F4F: level 2 and 5 points for types 0x104 or
// 2, else at least level 1 and 30 points for 0x808, 100 for 0x401, 400 for
// 0x40. No network: level 2 and 5 points for a lot, water, or each
// occupant of two types on the cell. A cell left at level 0 scores 1.
void __fastcall RateCell(char* self, void*, int32_t x, int32_t z, int32_t* level, int32_t* score)
{
    static bool logged = false;
    FirstCall("RateCell", logged);
    const auto taken = [&] {
        *level = 2;
        *score += 5;
    };
    const auto atLeastOne = [&](int32_t points) {
        if (*level < 1)
            *level = 1;
        *score += points;
    };
    if (NetworkAt(self, x, z, 0x1F4F)) {
        if (NetworkAt(self, x, z, 0x104))
            taken();
        else if (NetworkAt(self, x, z, 0x808))
            atLeastOne(30);
        else if (NetworkAt(self, x, z, 0x401))
            atLeastOne(100);
        else if (NetworkAt(self, x, z, 0x40))
            atLeastOne(400);
        else if (NetworkAt(self, x, z, 0x2))
            taken();
    } else if (Virtual<void*>(self + kLotManagerPart, 0x18, x, z, 0u)) {
        taken();
    } else if (Virtual<uint8_t>(self, 0x24, x, z)) {
        taken();
    } else {
        int32_t cell[2] = {x, z};
        auto* list = Virtual<ListNode*>(ListNodeAllocator(), 0xC, 0xCu);
        list->next = list;
        list->previous = list;
        Virtual<uint32_t>(At<void*>(self, field::kOccupants), 0xA4, &list, &cell[0], &cell[0], 0u, 0xFFFFFFFFu);
        for (ListNode* node = list->next; node != list; node = node->next) {
            const uint32_t type = Virtual<uint32_t>(node->value, 0x1C);
            if (type == kTypeBlockerA || type == kTypeBlockerB)
                taken();
        }
        Virtual<uint32_t>(At<void*>(self, field::kOccupants), 0xC0, &list);
        reinterpret_cast<SelfFn>(addr::SC4List_Destroy2)(&list);
    }
    if (*level == 0)
        ++*score;
}

// --- The network piece table ----------------------------------------------------

// One entry of the map +0xF0. The key has a hex digit per side of the
// cell (0x1000, 0x0100, 0x0010, 0x0001): 0 nothing, 1 and 2 two kinds of
// connection; the value is the piece, its rotation and whether it is
// mirrored.
struct Piece {
    int32_t key;
    uint32_t piece;
    uint8_t rotation;
    uint8_t mirrored;
};

const Piece kPieces[] = {
    {0x0000, 0x3000, 0, 0}, {0x1000, 0x2000, 0, 0}, {0x0100, 0x2000, 1, 0}, {0x0010, 0x2000, 2, 0},
    {0x0001, 0x2000, 3, 0}, {0x2000, 0x2600, 0, 0}, {0x0200, 0x2600, 1, 0}, {0x0020, 0x2600, 2, 0},
    {0x0002, 0x2600, 3, 0}, {0x1100, 0x2100, 1, 0}, {0x0110, 0x2100, 2, 0}, {0x0011, 0x2100, 3, 0},
    {0x1001, 0x2100, 0, 0}, {0x1200, 0x2400, 1, 0}, {0x0120, 0x2400, 2, 0}, {0x0012, 0x2400, 3, 0},
    {0x2001, 0x2400, 0, 0}, {0x2100, 0x2400, 0, 1}, {0x0210, 0x2400, 3, 1}, {0x0021, 0x2400, 2, 1},
    {0x1002, 0x2400, 1, 1}, {0x1010, 0x2500, 0, 0}, {0x0101, 0x2500, 1, 0}, {0x1020, 0x2900, 2, 0},
    {0x0102, 0x2900, 3, 0}, {0x2010, 0x2900, 0, 0}, {0x0201, 0x2900, 1, 0}, {0x1110, 0x2200, 1, 0},
    {0x0111, 0x2200, 2, 0}, {0x1011, 0x2200, 3, 0}, {0x1101, 0x2200, 0, 0}, {0x1120, 0x2700, 2, 0},
    {0x0112, 0x2700, 3, 0}, {0x2011, 0x2700, 0, 0}, {0x1201, 0x2700, 1, 0}, {0x1210, 0x2800, 1, 0},
    {0x0121, 0x2800, 2, 0}, {0x1012, 0x2800, 3, 0}, {0x2101, 0x2800, 0, 0}, {0x2110, 0x2700, 0, 1},
    {0x0211, 0x2700, 3, 1}, {0x1021, 0x2700, 2, 1}, {0x1102, 0x2700, 1, 1}, {0x1112, 0x2300, 3, 0},
    {0x2111, 0x2300, 0, 0}, {0x1211, 0x2300, 1, 0}, {0x1121, 0x2300, 2, 0},
};

// 0x0072FF80 (no arguments; Init): the 47 entries in the game's order.
// The game stores the two bytes as a dword whose upper half it never
// wrote; ours is zero there.
void __fastcall FillPieceTable(char* self, void*)
{
    static bool logged = false;
    FirstCall("FillPieceTable", logged);
    for (const Piece& piece : kPieces) {
        const int32_t key = piece.key;
        uint32_t* const value = Game<uint32_t*>(addr::SC4Sint32PairMap_Index, self + field::kMapF0, &key);
        value[0] = piece.piece;
        value[1] = static_cast<uint32_t>(piece.rotation) | (static_cast<uint32_t>(piece.mirrored) << 8);
    }
}

// --- Installation ---------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 23, 0x0072E520, CreateLot),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable_4, 24, 0x0072F8E0, DeleteLot),
    OPENSC4_SLOT(addr::cSC4ZoneDeveloper_vftable, 8, 0x0072FE10, ExistsNetworkOfType),
};

#undef OPENSC4_SLOT

// The functions parts d and e and Init call directly. None of their call
// sites keeps a value in ECX, EDX or (for the four without a result) EAX
// across the call, and none passes anything in a register but `this`, so
// plain detours do.
struct Detour {
    std::uintptr_t address;
    void* ours;
    void* original;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn), nullptr}

Detour g_detours[] = {
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072E6A0, ClearLots),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072E9B0, CutPieceIntoCells),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072EC60, CutIntoPieces),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072EDD0, CutPieceIntoLots),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072FAB0, RateCell),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072FCA0, FindNetworkOccupant),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072FE80, NetworkLeadsToward),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072FF80, FillPieceTable),
};

#undef OPENSC4_DETOUR

// Checks that every slot still holds the original, installs the detours,
// then swaps the slots.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : kSlots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("zone parcels: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (Detour& detour : g_detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, detour.ours, &detour.original)) {
            log::Write("zone parcels: no detour at %08X", static_cast<unsigned>(detour.address));
            return false;
        }
    }
    for (const Slot& slot : kSlots) {
        std::uintptr_t* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("zone parcels: %u slots and %u detours are ours", static_cast<unsigned>(std::size(kSlots)),
               static_cast<unsigned>(std::size(g_detours)));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.zone.parcels", "simulators/zone", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSC4ZoneDeveloper_vftable_4,
     "Our cSC4ZoneDeveloper parcels: CreateLot, DeleteLot, clearing a region's lots, cutting a region into pieces and "
     "a piece into lots with streets, the network queries of a cell, the network piece table",
     false, Install});

} // namespace
} // namespace opensc4::zone_sim
