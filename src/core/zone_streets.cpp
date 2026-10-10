// zone_streets.cpp of our cSC4ZoneDeveloper (build 641; the class map is in
// zone_sim.h): the functions with entries in 0x007306C0-0x00732BF0. They are
// the street side of zoning and two lot manager slots:
//
//   0x007306C0  lot manager slot 21: whether a lot configuration fits a
//               rectangle of cells (water, zones, networks and their
//               connections to the configuration's network objects, slope)
//   0x00730A80  lot manager slot 22: creates a lot on a rectangle when
//               CanCreateLot allows it, or, in a preview (+0x99 clear), only
//               records its cells in the map +0xD8
//   0x00730BA0  extends the dead ends of one network type inside a rectangle
//               with a street to the rectangle's edge
//   0x00730D30  lays a street along a side of a rectangle when a network of
//               the type ends just outside it and nothing road-like runs
//               along that side already
//   0x00731C80  scores the four sides of the operation's region by the
//               networks along them and picks the parcels' two directions
//   0x00731F60  message 0x99EF1142 (occupant added), data 3 = 1 or 2: joins
//               neighbouring street dead ends to a new road-like occupant
//   0x00732450  the same message, data 3 = 3: draws a street across a
//               straight rail beside the new occupant
//   0x00732830  frees a subtree of the map of maps at +0x78
//
// DoMessage (0x007328D0) lies in the range too but is the object part's.
//
// Directions and sides. A network occupant's connections are numbered 0 west
// (x - 1), 1 north (z - 1), 2 east, 3 south (the neighbour table 0x00AB3780
// read by 0x007306C0: {0, -1, 0, 1, 0} as z, x offsets); a connection flags
// dword has one byte per direction. 0x0072FE80's "side" argument is another
// numbering (side s is direction (s + 1) & 3); ours passes it through as the
// game does. The sides of the region in 0x00731C80: 0 north (z0), 1 east
// (x1), 2 south (z1), 3 west (x0).
//
// Fields used here (offsets from the object's start; the two slots get
// object + 4 and read everything 4 lower):
//   0x24  the city (0x00B43CEC): slots 93/94 the cell counts (0x00731108,
//         0x0073189F)
//   0x28, 0x2C  int32 cell counts x, z (0x00730786, 0x00731C96)
//   0x40  the zone manager: slot 12 GetZoneType(x, z) (0x00730942)
//   0x44, 0x48, 0x4C  the network tools of types 11, 3 and 0 (DoMessage
//         0x00732924 fills them): slot 7 (x0, z0, x1, z1, 0, 0, 1, -1) draws
//         a line, slot 9 (x, z, type mask, out) gives the connection flags
//         of a cell the tool has laid out but not committed
//   0x50  the lot developer: slot 29 GetAreaSlope(rectangle, bool)
//   0x98  uint8: 0x00731C80's result, 1 = the main direction is the best
//         side
//   0x99  uint8: 0 during HighlightParcels (a preview): no lots are made and
//         the tools' uncommitted cells count as networks
//   0x9A, 0x9B  uint8 options (SetOptions): 0x9A set makes sides without a
//         road start at class 0 and lets low zone types on big regions turn
//         the directions; 0x9B set swaps the two directions
//   0xA0  int32 zone type of the operation (1 and 2 turn, 0x00731EDC)
//   0xA4  the cell region of the operation, or null (rectangle +0, +4, +8,
//         +0xC inclusive, width +0x14, depth +0x18, +0x20 a row of bits per
//         x: bit z - z0); tested as 0x00466610 does
//   0xAC  int32[4] scores of the sides (north, east, south, west)
//   0xBC  int32[4] classes of the sides (0, 1 or 2; 0x0072FAB0 sets them)
//   0xCC  int32 second direction, 0xD0 int32 main direction (side numbers,
//         -1 for none)
//   0xD8  map keyed by a cell (x, z) whose value is {x, z, facing} of the
//         lot previewed there (0x0072F7D0 is its operator[])
//
// Every virtual call's argument count here was taken from the callee's
// `ret N` (vt.py on the receiver's vtable): the zone developer 0x00AB3830,
// the lot manager part 0x00AB37C0, the lot configuration 0x00A8CEB8, the lot
// developer 0x00AB1B98, the zone manager 0x00A92E00, the city 0x00A87620,
// the seven network occupant vtables (IID 0xA821EF94), the five network tool
// vtables (slots 7 and 9 are the same code in all), the occupant's slot 11
// (0x005E8E20 in all 57 occupant vtables: writes four cell coordinates).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/zone_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace opensc4::zone_sim {
namespace {

namespace local {
constexpr std::size_t kCity = 0x24;
constexpr std::size_t kCellsX = 0x28;
constexpr std::size_t kCellsZ = 0x2C;
constexpr std::size_t kZones = 0x40;
constexpr std::size_t kTool11 = 0x44;
constexpr std::size_t kStreetTool = 0x48;
constexpr std::size_t kRoadTool = 0x4C;
constexpr std::size_t kLotDeveloper = 0x50;
constexpr std::size_t kMainIsBest = 0x98;
constexpr std::size_t kCommit = 0x99;
constexpr std::size_t kOption9A = 0x9A;
constexpr std::size_t kOption9B = 0x9B;
constexpr std::size_t kZoneType = 0xA0;
constexpr std::size_t kRegion = 0xA4;
constexpr std::size_t kSideScores = 0xAC;  // int32[4]
constexpr std::size_t kSideClasses = 0xBC; // int32[4]
constexpr std::size_t kSecondSide = 0xCC;
constexpr std::size_t kMainSide = 0xD0;
constexpr std::size_t kPreviewCells = 0xD8;

constexpr std::size_t kLotManagerPart = 0x4;

// The cell region (+0xA4).
constexpr std::size_t kRegionWidth = 0x14;
constexpr std::size_t kRegionDepth = 0x18;
constexpr std::size_t kRegionRows = 0x20;

// A lot configuration's objects (slot 23 returns the vector): 0x34 bytes
// each, the type in the first byte.
constexpr std::size_t kConfigObjectSize = 0x34;
constexpr uint8_t kConfigNetworkObject = 7;
} // namespace local

constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;

// Network type masks (1 << type): 0 road, 1 rail, 3 street, 6 avenue, 10
// one-way road, 11 the type of the tool at +0x44.
constexpr uint32_t kMaskRoad = 0x1, kMaskRail = 0x2, kMaskStreet = 0x8, kMaskAvenue = 0x40, kMaskOneWay = 0x400,
                   kMask11 = 0x800;
constexpr uint32_t kMaskAnyTransit = 0x1F4F; // types 0-3, 6, 8-12
constexpr uint32_t kMaskStreetLike = 0xC09;  // types 0, 3, 10, 11

struct ObjectVector {
    uint8_t* begin;
    uint8_t* end;
};

// --- The game's functions of the other parts ---------------------------------

using NetworkAtFn = void*(__thiscall*)(void* self, int32_t x, int32_t z, uint32_t mask);
using ConnectsFn = uint8_t(__thiscall*)(void* self, int32_t x, int32_t z, uint32_t mask, int32_t side, int32_t type);
using LayStreetFn = void(__thiscall*)(void* self, int32_t x0, int32_t z0, int32_t x1, int32_t z1, uint32_t whole);
using CheckConfigurationFn = int32_t(__thiscall*)(void* self, int32_t x0, int32_t z0, int32_t x1, int32_t z1,
                                                  int32_t facing, void* configuration);
using ObjectPlacementFn = void(__cdecl*)(void* configuration, const uint8_t* object, const int32_t* rect,
                                         int32_t facing, int32_t* x, int32_t* z, uint32_t* flagsA, uint32_t* flagsB,
                                         uint32_t* type);
using CreateLotsFn = uint8_t(__thiscall*)(void* self, int32_t x, int32_t z, int32_t width, int32_t depth, void* list,
                                          void** lot);
using CellEntryFn = int32_t*(__thiscall*)(void* map, const int32_t* key);
using ScoreSideFn = void(__thiscall*)(void* self, int32_t x, int32_t z, int32_t* sideClass, int32_t* score);
using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using DestroyListFn = void(__thiscall*)(void* list);

// 0x0072FCA0 (zone_parcels.cpp): the network occupant (interface 0xA821EF94, not
// referenced) of a cell with any of the mask's flags, or null.
void* NetworkAt(void* self, int32_t x, int32_t z, uint32_t mask)
{
    return reinterpret_cast<NetworkAtFn>(addr::cSC4ZoneDeveloper_Fn_0072FCA0)(self, x, z, mask);
}

// 0x0072FE80 (zone_parcels.cpp): whether the cell's network of the mask connects on a
// side with a network type, the tools' uncommitted cells included in a
// preview.
bool Connects(void* self, int32_t x, int32_t z, uint32_t mask, int32_t side, int32_t type)
{
    return reinterpret_cast<ConnectsFn>(addr::cSC4ZoneDeveloper_Fn_0072FE80)(self, x, z, mask, side, type);
}

// 0x0072A2A0 (object part): lays a street between two cells.
void LayStreet(void* self, int32_t x0, int32_t z0, int32_t x1, int32_t z1, uint32_t whole)
{
    reinterpret_cast<LayStreetFn>(addr::cSC4ZoneDeveloper_Fn_0072A2A0)(self, x0, z0, x1, z1, whole);
}

// The zone developer's slot 8, through the vtable as the game calls it.
bool ExistsNetwork(void* self, int32_t x, int32_t z, uint32_t mask)
{
    return Virtual<uint8_t>(self, 0x20, x, z, mask);
}

// The table 0x00AB3680 (1 << type for the 13 network types), read as the
// game reads it.
uint32_t TypeMask(uint32_t type)
{
    return reinterpret_cast<const uint32_t*>(addr::cSC4ZoneDeveloper_NetworkTypeMasks)[type];
}

// A network occupant's slot 47: whether it connects in a direction with a
// network type.
bool HasConnection(void* occupant, int32_t direction, int32_t type)
{
    return Virtual<uint8_t>(occupant, 0xBC, direction, type);
}

// A network occupant's slot 22: whether it is of a network type.
bool IsOfType(void* occupant, int32_t type)
{
    return Virtual<uint8_t>(occupant, 0x58, type);
}

// A network tool's slot 7: a line from one cell to another.
void DrawLine(void* tool, int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    Virtual<uint32_t>(tool, 0x1C, x0, z0, x1, z1, 0u, 0u, 1u, -1);
}

// --- Plain copies of the object part's small helpers ---------------------------

// 0x00466610: whether a cell is in a region.
bool RegionHas(void* region, int32_t x, int32_t z)
{
    const uint32_t column = static_cast<uint32_t>(x - At<int32_t>(region, 0x0));
    const uint32_t row = static_cast<uint32_t>(z - At<int32_t>(region, 0x4));
    if (column >= At<uint32_t>(region, local::kRegionWidth) || row >= At<uint32_t>(region, local::kRegionDepth))
        return false;
    const uint32_t* bits = At<uint32_t**>(region, local::kRegionRows)[column];
    return (bits[row >> 5] & (1u << (row & 31))) != 0;
}

// 0x00729EC0: whether two connection flag dwords agree in every direction
// the first one uses (a direction's byte is "used" when its low nibble is
// not 0; the whole byte is then compared).
bool ConnectionsAgree(uint32_t a, uint32_t b)
{
    const uint32_t used = (((a + 0x0F0F0F0Fu) >> 4) & 0x01010101u) * 0xFu;
    return (used & (a ^ b)) == 0;
}

// 0x00729F90 (the occupant comes in ESI, so the game's cannot be called from
// here): a street or type 11 network that is no road or one-way road; with
// the byte 0x00B4B391 set, any of the four.
bool IsStreetLike(void* occupant)
{
    if (Global<uint8_t>(addr::cSC4ZoneDeveloper_AnyRoadIsStreetLike))
        return IsOfType(occupant, 3) || IsOfType(occupant, 11) || IsOfType(occupant, 10) || IsOfType(occupant, 0);
    if (!IsOfType(occupant, 3) && !IsOfType(occupant, 11))
        return false;
    if (IsOfType(occupant, 10))
        return false;
    return !IsOfType(occupant, 0);
}

// 0x0072ACC0: frees a subtree of nodes without owned values.
void FreeSubtree(void* node)
{
    const auto free = reinterpret_cast<FreeFn>(addr::SC4_Free);
    while (node) {
        FreeSubtree(At<void*>(node, 0xC));
        void* const next = At<void*>(node, 0x8);
        free(node);
        node = next;
    }
}

// --- Lot manager slot 21 (0x007306C0) -------------------------------------------

// Whether a lot configuration fits the rectangle with a facing: 8 when every
// cell is free, else the lowest value of a cell: 0 outside the city or the
// operation's region, 1 water (not asked of a configuration whose slot 14 is
// set), 3 a network or a zone of type 15, 7 a zone of type 10 to 14. A cell
// with a network counts 8 when a network object of the configuration lies on
// it, the network is of the object's type alone and its connections agree
// with the object's two flag dwords. 4 when the area is steeper than the
// configuration allows (its slot 8). When all cells pass, the cells around
// each network object that lie outside the rectangle, on the sides the
// object connects to, must not hold a network with flag 0x200000 or
// 0x400000: 3. A configuration with slot 14 set is first checked by
// 0x0072B480, whose answer is returned unless it is 8.
int32_t __fastcall CheckConfiguration(void* part, void*, int32_t x0, int32_t z0, int32_t x1, int32_t z1,
                                      int32_t facing, void* configuration)
{
    static bool logged = false;
    FirstCall("CheckConfiguration", logged);
    void* const self = static_cast<char*>(part) - local::kLotManagerPart;
    // The game hands out the address of its own arguments as the rectangle.
    int32_t rect[4] = {x0, z0, x1, z1};
    int32_t result = 8;
    const bool flag = Virtual<uint8_t>(configuration, 0x38);
    if (flag) {
        result = reinterpret_cast<CheckConfigurationFn>(addr::cSC4ZoneDeveloper_Fn_0072B480)(
            self, rect[0], rect[1], rect[2], rect[3], facing, configuration);
        if (result != 8)
            return result;
    }
    // The game pushes the dword holding the flag byte; its upper bytes are
    // stack memory it never wrote (zeros here).
    const float slope = static_cast<float>(Virtual<double>(At<void*>(self, local::kLotDeveloper), 0x74, rect, flag));
    if (Virtual<double>(configuration, 0x20) < static_cast<double>(slope))
        return 4;

    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            int32_t value;
            void* const region = At<void*>(self, local::kRegion);
            if (x < 0 || x >= At<int32_t>(self, local::kCellsX) || z < 0 || z >= At<int32_t>(self, local::kCellsZ) ||
                (region && !RegionHas(region, x, z))) {
                value = 0;
            } else if (!flag && Virtual<uint8_t>(self, 0x24, x, z)) {
                value = 1;
            } else if (void* const occupant = NetworkAt(self, x, z, kMaskAnyTransit); !occupant) {
                const uint32_t zone = Virtual<uint32_t>(At<void*>(self, local::kZones), 0x30, x, z);
                const uint32_t bit = 1u << (zone & 31);
                value = (bit & 0xFC00) ? static_cast<int32_t>((~(bit >> 13) & 4) | 3) : 8;
            } else if (Virtual<uint8_t>(occupant, 0x20, 0x200000u)) {
                value = 3;
            } else {
                value = 3;
                const ObjectVector* const objects = Virtual<const ObjectVector*>(configuration, 0x5C);
                const uint8_t* const end = objects->end;
                for (const uint8_t* object = objects->begin; object != end; object += local::kConfigObjectSize) {
                    if (*object != local::kConfigNetworkObject)
                        continue;
                    int32_t objectX = 0, objectZ = 0;
                    uint32_t flagsA = 0, flagsB = 0, type = 0;
                    reinterpret_cast<ObjectPlacementFn>(addr::cSC4ZoneDeveloper_Fn_0072AD00)(
                        configuration, object, rect, facing, &objectX, &objectZ, &flagsA, &flagsB, &type);
                    if (objectX != x || objectZ != z)
                        continue;
                    if ((Virtual<uint32_t>(occupant, 0x30) & 0x1FFF) != TypeMask(type))
                        break;
                    const uint32_t connections = Virtual<uint32_t>(occupant, 0x98, type);
                    if (ConnectionsAgree(flagsA, connections) && ConnectionsAgree(connections, flagsB))
                        value = 8;
                }
            }
            if (value < result)
                result = value;
        }
    }
    if (result != 8)
        return result;

    static const int32_t kNeighbourX[4] = {-1, 0, 1, 0}; // 0x00AB3780
    static const int32_t kNeighbourZ[4] = {0, -1, 0, 1};
    const ObjectVector* const objects = Virtual<const ObjectVector*>(configuration, 0x5C);
    const uint8_t* const end = objects->end;
    for (const uint8_t* object = objects->begin; object != end; object += local::kConfigObjectSize) {
        if (*object != local::kConfigNetworkObject)
            continue;
        int32_t objectX = 0, objectZ = 0;
        uint32_t flagsA = 0, flagsB = 0, type = 0;
        reinterpret_cast<ObjectPlacementFn>(addr::cSC4ZoneDeveloper_Fn_0072AD00)(
            configuration, object, rect, facing, &objectX, &objectZ, &flagsA, &flagsB, &type);
        for (int direction = 0; direction < 4; ++direction) {
            const int32_t x = objectX + kNeighbourX[direction];
            const int32_t z = objectZ + kNeighbourZ[direction];
            if (static_cast<uint32_t>(x) >= At<uint32_t>(self, local::kCellsX) ||
                static_cast<uint32_t>(z) >= At<uint32_t>(self, local::kCellsZ))
                continue;
            if (x >= rect[0] && z >= rect[1] && x <= rect[2] && z <= rect[3])
                continue;
            if (!(flagsB & (0xFFu << (8 * direction))))
                continue;
            uint32_t mask = TypeMask(type);
            if (mask & kMaskAnyTransit)
                mask = kMaskAnyTransit;
            void* const occupant = NetworkAt(self, x, z, mask);
            if (!occupant)
                continue;
            if (Virtual<uint8_t>(occupant, 0x24, 0x600000u)) {
                if (result > 3)
                    result = 3;
                break;
            }
        }
    }
    return result;
}

// --- Lot manager slot 22 (0x00730A80) -------------------------------------------

// Creates a lot on a rectangle of cells when CanCreateLot (slot 20, with the
// two arguments after the lot pointer and 0) answers 8. Committing (+0x99):
// through CreateLot (slot 23) with the facing, or for facing -1 through
// 0x0072D200 with an empty list; the answer is whether *lot was set. In a
// preview every cell of the rectangle gets {x, z, facing} in the map +0xD8
// and the answer is true. The ninth argument is not read.
bool __fastcall CreateLotOrPreview(void* part, void*, int32_t x, int32_t z, int32_t width, int32_t depth,
                                   int32_t facing, void** lot, int32_t a7, int32_t a8, int32_t)
{
    static bool logged = false;
    FirstCall("CreateLotOrPreview", logged);
    void* const self = static_cast<char*>(part) - local::kLotManagerPart;
    *lot = nullptr;
    if (Virtual<int32_t>(part, 0x50, x, z, width, depth, a7, a8, 0) != 8)
        return *lot != nullptr;
    if (At<uint8_t>(self, local::kCommit)) {
        if (facing == -1) {
            void* head = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0xC);
            At<void*>(head, 0x0) = head;
            At<void*>(head, 0x4) = head;
            reinterpret_cast<CreateLotsFn>(addr::cSC4ZoneDeveloper_Fn_0072D200)(self, x, z, width, depth, &head, lot);
            reinterpret_cast<DestroyListFn>(addr::SC4PairList_Destroy)(&head);
        } else {
            Virtual<uint32_t>(part, 0x5C, x, z, width, depth, facing, lot);
        }
        return *lot != nullptr;
    }
    for (int32_t i = 0; i < width; ++i) {
        int32_t key[2];
        key[0] = i + x;
        for (int32_t j = 0; j < depth; ++j) {
            key[1] = j + z;
            int32_t* const entry = reinterpret_cast<CellEntryFn>(addr::cSC4ZoneDeveloper_Fn_0072F7D0)(
                static_cast<char*>(self) + local::kPreviewCells, key);
            entry[0] = x;
            entry[1] = z;
            entry[2] = facing;
        }
    }
    return true;
}

// --- 0x00730BA0 -------------------------------------------------------------------

// For each cell of the rectangle with a network of the type that connects
// (to a road, one-way road, street, type 11 or, on cells with x + z odd, an
// avenue) on exactly one side: a street from the cell straight to the
// rectangle's edge on the other end (side 0 to z1, 1 to x0, 2 to z0, 3 to
// x1), unless the cell is on that edge.
void __fastcall ExtendDeadEnds(void* self, void*, const int32_t* rect, int32_t networkType)
{
    static bool logged = false;
    FirstCall("ExtendDeadEnds", logged);
    const uint32_t mask = TypeMask(static_cast<uint32_t>(networkType));
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            if (!ExistsNetwork(self, x, z, mask))
                continue;
            int32_t count = 0;
            int32_t last = -1;
            for (int32_t side = 0; side <= 3; ++side) {
                if (Connects(self, x, z, mask, side, 0) || Connects(self, x, z, mask, side, 10) ||
                    Connects(self, x, z, mask, side, 3) || Connects(self, x, z, mask, side, 11) ||
                    (((x + z) & 1) && Connects(self, x, z, mask, side, 6))) {
                    if (++count > 1)
                        break;
                    last = side;
                }
            }
            if (count != 1)
                continue;
            switch (last) {
            case 0:
                if (z != rect[3])
                    LayStreet(self, x, z, x, rect[3], 0);
                break;
            case 1:
                if (x != rect[0])
                    LayStreet(self, x, z, rect[0], z, 0);
                break;
            case 2:
                if (z != rect[1])
                    LayStreet(self, x, z, x, rect[1], 0);
                break;
            case 3:
                if (x != rect[2])
                    LayStreet(self, x, z, rect[2], z, 0);
                break;
            default:
                break;
            }
        }
    }
}

// --- 0x00730D30 -------------------------------------------------------------------

// Whether the cell's network of a mask connects in a direction with a type.
bool NetworkConnects(void* self, int32_t x, int32_t z, uint32_t mask, int32_t direction, int32_t type)
{
    void* const occupant = NetworkAt(self, x, z, mask);
    return occupant && HasConnection(occupant, direction, type);
}

// In a preview: whether the street tool (mask 8) or the type 11 tool (mask
// 0x800) has an uncommitted cell there that connects in a direction. The
// tools' slot 9 writes the flags whenever it answers true.
bool PendingConnects(void* self, int32_t x, int32_t z, int32_t direction)
{
    if (At<uint8_t>(self, local::kCommit))
        return false;
    uint32_t flags = 0;
    if (!Virtual<uint8_t>(At<void*>(self, local::kStreetTool), 0x24, x, z, kMaskStreet, &flags) &&
        !Virtual<uint8_t>(At<void*>(self, local::kTool11), 0x24, x, z, kMask11, &flags))
        return false;
    return ((flags >> (8 * direction)) & 0xFF) != 0;
}

// One cell of the line outside a side. Sets `ends` when the network of the
// mask there connects with the type on exactly one side and that side is
// `endSide`. Returns whether an avenue, road, one-way road, street or type
// 11 network there runs along the side (connects in either of the two
// directions with its own type).
bool RunsAlongSide(void* self, int32_t x, int32_t z, uint32_t mask, int32_t networkType, int32_t endSide,
                   int32_t directionA, int32_t directionB, bool& ends)
{
    if (!ExistsNetwork(self, x, z, mask))
        return false;
    int32_t count = 0;
    int32_t last = -1;
    for (int32_t side = 0; side <= 3; ++side) {
        if (Connects(self, x, z, mask, side, networkType)) {
            if (++count > 1)
                break;
            last = side;
        }
    }
    if (count == 1 && last == endSide)
        ends = true;

    static const struct {
        uint32_t mask;
        int32_t type;
    } kPlain[] = {{kMaskAvenue, 6}, {kMaskRoad, 0}, {kMaskOneWay, 10}},
      kWithPending[] = {{kMaskStreet, 3}, {kMask11, 11}};
    for (const auto& network : kPlain) {
        if (!ExistsNetwork(self, x, z, network.mask))
            continue;
        if (NetworkConnects(self, x, z, network.mask, directionA, network.type) ||
            NetworkConnects(self, x, z, network.mask, directionB, network.type))
            return true;
    }
    for (const auto& network : kWithPending) {
        if (!ExistsNetwork(self, x, z, network.mask))
            continue;
        if (NetworkConnects(self, x, z, network.mask, directionA, network.type) ||
            PendingConnects(self, x, z, directionA) ||
            NetworkConnects(self, x, z, network.mask, directionB, network.type) ||
            PendingConnects(self, x, z, directionB))
            return true;
    }
    return false;
}

// The four sides in turn (north, south, west, east; a side at the city's
// edge is skipped): the line of cells just outside the side is walked until
// a cell has a road-like network running along the side. When none has and
// some cell's network of the type ends there (one connection, on side 0 for
// north, 2 south, 3 west, 1 east), a street is laid along the rectangle's
// own edge cells of that side.
void __fastcall LayEdgeStreets(void* self, void*, const int32_t* rect, int32_t networkType)
{
    static bool logged = false;
    FirstCall("LayEdgeStreets", logged);
    const uint32_t mask = TypeMask(static_cast<uint32_t>(networkType));

    if (rect[1] > 0) {
        const int32_t z = rect[1] - 1;
        bool ends = false, blocked = false;
        for (int32_t x = rect[0]; x <= rect[2]; ++x) {
            if (RunsAlongSide(self, x, z, mask, networkType, 0, 0, 2, ends)) {
                blocked = true;
                break;
            }
        }
        if (ends && !blocked)
            LayStreet(self, rect[0], rect[1], rect[2], rect[1], 1);
    }

    {
        const int32_t edge = rect[3];
        if (edge < Virtual<int32_t>(At<void*>(self, local::kCity), 0x178) - 1) {
            const int32_t z = edge + 1;
            bool ends = false, blocked = false;
            for (int32_t x = rect[0]; x <= rect[2]; ++x) {
                if (RunsAlongSide(self, x, z, mask, networkType, 2, 0, 2, ends)) {
                    blocked = true;
                    break;
                }
            }
            if (ends && !blocked)
                LayStreet(self, rect[0], rect[3], rect[2], rect[3], 1);
        }
    }

    if (rect[0] > 0) {
        const int32_t x = rect[0] - 1;
        bool ends = false, blocked = false;
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            if (RunsAlongSide(self, x, z, mask, networkType, 3, 1, 3, ends)) {
                blocked = true;
                break;
            }
        }
        if (ends && !blocked)
            LayStreet(self, rect[0], rect[1], rect[0], rect[3], 1);
    }

    {
        const int32_t edge = rect[2];
        if (edge < Virtual<int32_t>(At<void*>(self, local::kCity), 0x174) - 1) {
            const int32_t x = edge + 1;
            bool ends = false, blocked = false;
            for (int32_t z = rect[1]; z <= rect[3]; ++z) {
                if (RunsAlongSide(self, x, z, mask, networkType, 1, 1, 3, ends)) {
                    blocked = true;
                    break;
                }
            }
            if (ends && !blocked)
                LayStreet(self, rect[2], rect[1], rect[2], rect[3], 1);
        }
    }
}

// --- 0x00731C80 -------------------------------------------------------------------

// Scores the sides of the operation's region: every region cell on an edge
// of the rectangle sends the cell just outside (clamped to the city) to
// 0x0072FAB0, which raises the side's class and score by the network there.
// The best side (highest score; the first of equals) is the main direction
// +0xD0, the better of the two sides across it the second +0xCC (north and
// south: east unless west scores at least as much; east and west: north
// only when it beats south), and +0x98 is set. With the option +0x9A, zone
// type 1 or 2 and a region more than two cells each way, the two are
// exchanged and +0x98 cleared; the option +0x9B exchanges them (again) and
// flips +0x98.
void __fastcall ChooseDirections(void* self, void*)
{
    static bool logged = false;
    FirstCall("ChooseDirections", logged);
    const auto score = reinterpret_cast<ScoreSideFn>(addr::cSC4ZoneDeveloper_Fn_0072FAB0);
    int32_t* const region = At<int32_t*>(self, local::kRegion);
    int32_t* const scores = &At<int32_t>(self, local::kSideScores);
    int32_t* const classes = &At<int32_t>(self, local::kSideClasses);

    int32_t west = region[0];
    if (west > 0)
        --west;
    int32_t east = region[2];
    if (east < At<int32_t>(self, local::kCellsX) - 1)
        ++east;
    int32_t north = region[1];
    if (north > 0)
        --north;
    int32_t south = region[3];
    if (south < At<int32_t>(self, local::kCellsZ) - 1)
        ++south;

    const int32_t firstClass = At<uint8_t>(self, local::kOption9A) == 0 ? 1 : 0;
    for (int i = 0; i < 4; ++i)
        scores[i] = 0;
    for (int i = 0; i < 4; ++i)
        classes[i] = firstClass;

    const auto rows = [self]() { return At<uint32_t**>(At<void*>(self, local::kRegion), local::kRegionRows); };
    for (int32_t x = region[0]; x <= region[2]; ++x) {
        if (rows()[x - region[0]][0] & 1)
            score(self, x, north, &classes[0], &scores[0]);
        const uint32_t last = static_cast<uint32_t>(region[3] - region[1]);
        if (rows()[x - region[0]][last >> 5] & (1u << (last & 31)))
            score(self, x, south, &classes[2], &scores[2]);
    }
    for (int32_t z = region[1]; z <= region[3]; ++z) {
        const uint32_t row = static_cast<uint32_t>(z - region[1]);
        if (rows()[0][row >> 5] & (1u << (row & 31)))
            score(self, west, z, &classes[3], &scores[3]);
        const uint32_t row2 = static_cast<uint32_t>(z - region[1]);
        if (rows()[region[2] - region[0]][row2 >> 5] & (1u << (row2 & 31)))
            score(self, east, z, &classes[1], &scores[1]);
    }

    int32_t best = INT_MIN;
    int32_t main = -1;
    int32_t second = -1;
    for (int i = 0; i < 4; ++i) {
        if (scores[i] > best) {
            best = scores[i];
            main = i;
        }
    }
    if (main == 0 || main == 2)
        second = scores[1] <= scores[3] ? 3 : 1;
    else if (main == 1 || main == 3)
        second = scores[0] > scores[2] ? 0 : 2;

    At<uint8_t>(self, local::kMainIsBest) = 1;
    At<int32_t>(self, local::kMainSide) = main;
    At<int32_t>(self, local::kSecondSide) = second;
    const int32_t zoneType = At<int32_t>(self, local::kZoneType);
    if (At<uint8_t>(self, local::kOption9A) && zoneType > 0 && zoneType <= 2 && region[2] - region[0] + 1 > 2 &&
        region[3] - region[1] + 1 > 2) {
        At<uint8_t>(self, local::kMainIsBest) = 0;
        At<int32_t>(self, local::kSecondSide) = main;
        At<int32_t>(self, local::kMainSide) = second;
    }
    if (At<uint8_t>(self, local::kOption9B)) {
        const int32_t a = At<int32_t>(self, local::kSecondSide);
        const int32_t b = At<int32_t>(self, local::kMainSide);
        At<uint8_t>(self, local::kMainIsBest) = At<uint8_t>(self, local::kMainIsBest) == 0 ? 1 : 0;
        At<int32_t>(self, local::kSecondSide) = b;
        At<int32_t>(self, local::kMainSide) = a;
    }
}

// --- 0x00731F60 -------------------------------------------------------------------

// Whether a street-like occupant has exactly one connection to a street,
// type 11 or (with the byte 0x00B4B391) road or one-way road, and it points
// in `direction`.
bool OnlyConnectionPoints(void* occupant, int32_t direction)
{
    int32_t streets = 0, type11s = 0, others = 0;
    int32_t streetDirection = -1, type11Direction = -1, otherDirection = -1;
    for (int32_t d = 0; d < 4; ++d) {
        if (HasConnection(occupant, d, 3)) {
            ++streets;
            streetDirection = d;
        } else if (HasConnection(occupant, d, 11)) {
            ++type11s;
            type11Direction = d;
        } else if (Global<uint8_t>(addr::cSC4ZoneDeveloper_AnyRoadIsStreetLike)) {
            if (HasConnection(occupant, d, 0) || (Global<uint8_t>(addr::cSC4ZoneDeveloper_AnyRoadIsStreetLike) &&
                                                  HasConnection(occupant, d, 10))) {
                ++others;
                otherDirection = d;
            }
        }
    }
    if (others + streets + type11s != 1)
        return false;
    return type11Direction == direction || streetDirection == direction || otherDirection == direction;
}

// A new occupant with the network occupant interface that is a road, one-way
// road (the road tool +0x4C), street (+0x48) or type 11 (+0x44) and whose
// slot 17 (flag 0x400000) is clear: each of the four neighbours (west,
// north, east, south) that is street-like and whose only connection points
// away from the new cell is joined by a line of the tool from the new cell.
void __fastcall JoinDeadEnds(void* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("JoinDeadEnds", logged);
    void* network = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kIidNetworkOccupant, &network)) {
        void* tool = nullptr;
        bool known = true;
        if (IsOfType(network, 0))
            tool = At<void*>(self, local::kRoadTool);
        else if (IsOfType(network, 10))
            tool = At<void*>(self, local::kRoadTool);
        else if (IsOfType(network, 3))
            tool = At<void*>(self, local::kStreetTool);
        else if (IsOfType(network, 11))
            tool = At<void*>(self, local::kTool11);
        else
            known = false;
        if (known && tool && !Virtual<uint8_t>(network, 0x44)) {
            int32_t cell[4] = {};
            Virtual<uint32_t>(occupant, 0x2C, cell);
            const int32_t x = cell[0];
            const int32_t z = cell[1];
            static const int32_t kX[4] = {-1, 0, 1, 0};
            static const int32_t kZ[4] = {0, -1, 0, 1};
            for (int32_t direction = 0; direction < 4; ++direction) {
                const int32_t nx = x + kX[direction];
                const int32_t nz = z + kZ[direction];
                void* const neighbour = NetworkAt(self, nx, nz, kMaskStreetLike);
                if (neighbour && IsStreetLike(neighbour) && OnlyConnectionPoints(neighbour, direction))
                    DrawLine(tool, x, z, nx, nz);
            }
        }
    }
    if (network)
        Virtual<uint32_t>(network, 0x8);
}

// --- 0x00732450 -------------------------------------------------------------------

// A new network occupant (slot 17 clear), with the tools +0x44 and +0x48
// there: for each neighbour (west, north, east, south) that is rail alone
// and runs straight past (connections to rail only across the direction to
// the new cell), when the cell beyond it holds a street (the street tool) or
// else a type 11 network (the tool +0x44), a line of that tool from the new
// cell to the cell beyond.
void __fastcall CrossRail(void* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("CrossRail", logged);
    void* network = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kIidNetworkOccupant, &network) && At<void*>(self, local::kTool11) &&
        At<void*>(self, local::kStreetTool) && !Virtual<uint8_t>(network, 0x44)) {
        int32_t cell[4] = {};
        Virtual<uint32_t>(occupant, 0x2C, cell);
        const int32_t x = cell[0];
        const int32_t z = cell[1];
        static const int32_t kX[4] = {-1, 0, 1, 0};
        static const int32_t kZ[4] = {0, -1, 0, 1};
        for (int32_t direction = 0; direction < 4; ++direction) {
            const int32_t nx = x + kX[direction];
            const int32_t nz = z + kZ[direction];
            void* const rail = NetworkAt(self, nx, nz, kMaskRail);
            if (!rail || (Virtual<uint32_t>(rail, 0x30) & kMaskAnyTransit) != kMaskRail)
                continue;
            // West and east neighbours: rail north and south only; north
            // and south neighbours: rail west and east only.
            const bool alongX = (direction & 1) != 0;
            if (HasConnection(rail, 0, 1) != alongX)
                continue;
            if (HasConnection(rail, 1, 1) == alongX)
                continue;
            if (HasConnection(rail, 2, 1) != alongX)
                continue;
            if (HasConnection(rail, 3, 1) == alongX)
                continue;
            const int32_t fx = x + 2 * kX[direction];
            const int32_t fz = z + 2 * kZ[direction];
            void* tool;
            void* beyond = NetworkAt(self, fx, fz, kMaskStreet);
            if (beyond && IsOfType(beyond, 3)) {
                tool = At<void*>(self, local::kStreetTool);
            } else {
                beyond = NetworkAt(self, fx, fz, kMask11);
                if (!beyond || !IsOfType(beyond, 11))
                    continue;
                tool = At<void*>(self, local::kTool11);
            }
            DrawLine(tool, x, z, fx, fz);
        }
    }
    if (network)
        Virtual<uint32_t>(network, 0x8);
}

// --- 0x00732830 -------------------------------------------------------------------

// Frees a subtree of a map whose values are maps (node: +0x8 left, +0xC
// right, +0x14 the value's head, +0x18 its size): the right subtree first,
// then the value's nodes when it has any (and its head reset), its head, the
// node; on to the left child.
void FreeMapSubtree(void* node)
{
    const auto free = reinterpret_cast<FreeFn>(addr::SC4_Free);
    while (node) {
        FreeMapSubtree(At<void*>(node, 0xC));
        void* const next = At<void*>(node, 0x8);
        void*& head = At<void*>(node, 0x14);
        if (At<uint32_t>(node, 0x18) != 0) {
            FreeSubtree(At<void*>(head, 0x4));
            At<void*>(head, 0x8) = head;
            At<void*>(head, 0x4) = nullptr;
            At<void*>(head, 0xC) = head;
            At<uint32_t>(node, 0x18) = 0;
        }
        if (head)
            free(head);
        free(node);
        node = next;
    }
}

void __fastcall FreeMapSubtreeEntry(void*, void*, void* node)
{
    static bool logged = false;
    FirstCall("FreeMapSubtree", logged);
    FreeMapSubtree(node);
}

// --- Installation ---------------------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

// Slots of the lot manager part's vtable (0x00AB37C0).
const Slot kSlots[] = {
    OPENSC4_SLOT(21, 0x007306C0, CheckConfiguration),
    OPENSC4_SLOT(22, 0x00730A80, CreateLotOrPreview),
};

#undef OPENSC4_SLOT

// The functions other code calls directly. Their call sites keep nothing in
// ECX, EDX or EAX across the call and pass everything on the stack
// (0x00733968-0x007339D9 and 0x00733795, 0x00733C6F in zone_parcellize.cpp's code,
// 0x00732B9F and 0x00732BB4 in DoMessage, 0x007345B5 in 0x00734580), so
// plain detours do.
struct Detour {
    std::uintptr_t address;
    void* ours;
    void* original;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn), nullptr}

Detour g_detours[] = {
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_00730BA0, ExtendDeadEnds),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_00730D30, LayEdgeStreets),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_00731C80, ChooseDirections),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_00731F60, JoinDeadEnds),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_00732450, CrossRail),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_00732830, FreeMapSubtreeEntry),
};

#undef OPENSC4_DETOUR

// Checks that every slot still holds the original, installs the detours,
// then swaps the slots.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4ZoneDeveloper_vftable_4);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("zone streets: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(addr::cSC4ZoneDeveloper_vftable_4),
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (Detour& detour : g_detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, detour.ours, &detour.original)) {
            log::Write("zone streets: no detour at %08X", static_cast<unsigned>(detour.address));
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
    log::Write("zone streets: %u slots and %u detours are ours", static_cast<unsigned>(std::size(kSlots)),
               static_cast<unsigned>(std::size(g_detours)));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.zone.streets", "simulators/zone", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSC4ZoneDeveloper_vftable_4,
     "Our cSC4ZoneDeveloper streets: the lot configuration fit test and lot creation with preview (lot manager "
     "slots 21, 22), streets from dead ends and along zone edges, the parcel directions, joining new roads to "
     "street dead ends and across rails",
     false, Install});

} // namespace
} // namespace opensc4::zone_sim
