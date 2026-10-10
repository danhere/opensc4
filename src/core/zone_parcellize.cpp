// Our cSC4ZoneDeveloper: parcellize (build 641; code 0x00732BF0 to the end of
// the class at 0x00734B10): the entry points of the parcellization, the
// cutting of a zoned cell region into lots.
//
//   HighlightParcels (primary slot 5, 0x00733F30) and DoParcellization
//   (slot 6, 0x00733FA0) set the operation's zone type and, for an RCI
//   zone, run Parcellize (0x00733770); another zone type goes straight to
//   zone_parcels.cpp's 0x0072E6A0.
//   Parcellize either makes one lot of the whole region (option +0x9C), or
//   has the region cut into pieces along streets (zone_cells.cpp, c and d do the
//   cutting and the lots) after ChooseLotSizes, and for HighlightParcels
//   keeps only the pieces at or beside the cell under the cursor.
//   ChooseLotSizes (0x00732BF0) picks the lot width and depth for the
//   operation from what would grow there: the growth developer with the
//   highest desirability at the region's centre, and of the lot
//   configurations that fit it the one whose capacity is nearest its
//   demand.
//   NewLotIterator (0x00734860; lot manager slot 15 behind the thunk
//   0x00734850), ResizeLotGrid (0x00734000; Init's helper) and the erasers
//   of the map +0x78 (0x00734600, 0x00734580; the destructor's helpers).
//
// Not here although in the range: Shutdown, Init, Read (0x00734380), the
// destructor, SetOptions, the constructor, the factory and the interface
// thunks (the object part's), and 0x00733450, a vector fill-insert the
// traffic simulator shares (data/addresses/traffic.toml: stays the game's).
//
// Fields used here (offsets from the object's start; names in zone_sim.h
// where it has them):
//   0x18 kMessages     the message server; Parcellize sends 0x49D968C4
//   0x20 kDevelopment  the building development simulator: slot 32 a growth
//                      developer by ID, slot 19 the building types of a
//                      family (into a hash set), slot 39 a building type's
//                      profile {purpose, wealth, uint16 capacity at +8}
//   0x24 kCity         slots 93 and 94: the city's size in cells
//   0x40 kZones        the zone manager; slot 13 IsRCIZone(zone type)
//   0x60 kLotList, 0x64 kLotIterators  NewLotIterator
//   0x78 kTree78       map of maps of maps (nodes: +0x4 parent, +0x8 left,
//                      +0xC right, +0x14 the inner map's head, +0x18 its
//                      size); 0x00734600 erases it
//   0x84 lot width, 0x88 lot depth (uint32; constructor 2, 2): the size of
//                      the chosen lot configuration (its slot 7), 2 by 2
//                      when none fits
//   0x8C, 0x90         uint32 smallest and largest depth among the fitting
//                      configurations whose width divides +0x84; at least 2
//                      apart
//   0x94               uint32 street spacing: for a developer of purpose 1
//                      depth * 3, * 2.5 or * 2 by density (low to high),
//                      else 7, 6 or 5
//   0x99 kPlaceStreets uint8: HighlightParcels clears it for its call;
//                      when set Parcellize ends with 0x0072B180
//   0x9A kOption9A     uint8: streets are laid into the region (0x00730BA0
//                      and, for regions over 2 cells both ways, 0x00730D30,
//                      each for the network types 11, 3, 10, 0, 6)
//   0x9C kOption9C     uint8: the whole region is one lot
//   0xA0 kZoneType     the operation's zone type, 0 outside one
//   0xA4 kRegion       the cell region being worked on (the whole region's
//                      copy, then each piece), null outside
//   0xA8 kValueA8      HighlightParcels' cell (int32 x, z), null otherwise
//   0xCC, 0xD0         two orientations (0-3); the one-lot path offers
//                      +0xD0, +0xCC and their opposites to 0x0072D200
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/zone_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace opensc4::zone_sim {
namespace {

namespace local {
constexpr std::size_t kLotWidth = 0x84;
constexpr std::size_t kLotDepth = 0x88;
constexpr std::size_t kMinDepth = 0x8C;
constexpr std::size_t kMaxDepth = 0x90;
constexpr std::size_t kStreetSpacing = 0x94;
constexpr std::size_t kOrientationCC = 0xCC;
constexpr std::size_t kOrientationD0 = 0xD0;
} // namespace local

// --- The game's containers and allocators -------------------------------------

// A node and the table of the game's hash sets (STLport): the set's object
// is 0x14 bytes, the buckets at +4/+8, the count at +0x10.
struct HashNode {
    HashNode* next;
    uint32_t value;
};
struct HashSet {
    uint32_t unused;
    HashNode** begin;
    HashNode** end;
    HashNode** capacity;
    uint32_t count;
};
struct HashIterator {
    HashNode* node;
    HashSet* table;
};

// A node of the game's lists: the head is a node without a value.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

void* StlAllocate(std::size_t size)
{
    return reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(size);
}

void StlFree(void* p) { reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(p); }

void* New(std::size_t size) { return reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(size); }

void Delete(void* p) { reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(p); }

void ConstructSet(HashSet* set)
{
    reinterpret_cast<void(__thiscall*)(HashSet*, uint32_t)>(addr::SC4Uint32HashSet_Construct)(set, 0x64);
}

// Frees the nodes, then the buckets, as the game does at the end of a set's
// life.
void DestroySet(HashSet* set)
{
    reinterpret_cast<void(__thiscall*)(HashSet*)>(addr::SC4Uint32HashSet_Clear)(set);
    if (set->begin)
        StlFree(set->begin);
}

HashIterator Begin(HashSet* set)
{
    HashIterator it{nullptr, nullptr};
    reinterpret_cast<HashIterator*(__thiscall*)(HashSet*, HashIterator*)>(addr::SC4HashTable_Begin)(set, &it);
    return it;
}

// The node after `node`: its chain, then the next bucket in use after the
// one `hash` falls in.
HashNode* Next(const HashSet* table, const HashNode* node, uint32_t hash)
{
    if (node->next)
        return node->next;
    const auto buckets = static_cast<uint32_t>(table->end - table->begin);
    uint32_t index = hash % buckets;
    HashNode* found = nullptr;
    while (++index < buckets) {
        found = table->begin[index];
        if (found)
            break;
    }
    return found;
}

// A new empty list head.
ListNode* NewList()
{
    auto* const head = static_cast<ListNode*>(StlAllocate(sizeof(ListNode)));
    head->next = head;
    head->previous = head;
    return head;
}

void PushBack(ListNode* head, void* value)
{
    auto* const node = static_cast<ListNode*>(StlAllocate(sizeof(ListNode)));
    node->value = value;
    ListNode* const last = head->previous;
    node->next = head;
    node->previous = last;
    last->next = node;
    head->previous = node;
}

// Frees every node but the head, which is left empty.
void ClearList(ListNode* head)
{
    for (ListNode* node = head->next; node != head;) {
        ListNode* const dead = node;
        node = node->next;
        StlFree(dead);
    }
    head->next = head;
    head->previous = head;
}

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword
// of the int64. What the FPU cannot convert (infinity, NaN, beyond int64)
// becomes the integer indefinite 0x8000000000000000, whose low dword is 0.
int32_t Ftol(double value)
{
    if (!(std::fabs(value) < 9223372036854775808.0))
        return 0;
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// --- Cell regions -------------------------------------------------------------

// Whether the cell (column, row), counted from the region's corner, is in
// the region's rows and set.
bool HasCell(void* cells, uint32_t column, uint32_t row)
{
    if (column >= At<uint32_t>(cells, region::kWidth) || row >= At<uint32_t>(cells, region::kDepth))
        return false;
    return (At<uint32_t**>(cells, region::kRows)[column][row >> 5] & (1u << (row & 31))) != 0;
}

void* NewRegion(const void* rect)
{
    void* const memory = New(0x24);
    if (!memory)
        return nullptr;
    return reinterpret_cast<void*(__thiscall*)(void*, const void*, uint32_t)>(addr::SC4CellRegion_Construct)(
        memory, rect, 0);
}

// Deletes a region through the game's helper.
void DeleteRegion(void* cells)
{
    At<std::uintptr_t>(cells, region::kCellsVtable) = addr::SC4CellRegion_CellsVtable;
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4CellRegion_DestroyCells)(static_cast<char*>(cells) + 0x10);
    Delete(cells);
}

// --- Other parts' functions, at the game's addresses --------------------------

template <typename R, typename... Args>
R Game(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

// Message 0x49D968C4 with data 1 = `started` (1 before the work, 0 after).
void SendParcellizing(void* self, uint32_t started)
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::RZ_operator_new)(0x2C);
    void* const message =
        memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, 0x49D968C4u);
    Virtual<uint32_t>(message, 0x2C, started);
    Virtual<uint32_t>(At<void*>(self, field::kMessages), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

// --- ChooseLotSizes (0x00732BF0) ----------------------------------------------

struct Best {
    void* developer = nullptr;
    int32_t desirability = 0;
    int32_t demand = 0;
};

// One growth developer (the development simulator's slot 32 by ID): its
// desirability estimate at the cell (slot 29, int16) and its demand's value
// (slot 18, then the demand's slot 12, a float truncated). The first one is
// taken as it is; a later one wins with a higher estimate, or the same
// estimate and a higher demand.
void Consider(void* self, uint32_t id, int32_t x, int32_t z, Best& best, bool first)
{
    void* const developer = Virtual<void*>(At<void*>(self, field::kDevelopment), 0x80, id);
    const int32_t desirability = static_cast<int16_t>(Virtual<uint32_t>(developer, 0x74, x, z));
    const int32_t demand = Ftol(Virtual<float>(Virtual<void*>(developer, 0x48), 0x30));
    if (first || desirability > best.desirability || (desirability == best.desirability && demand > best.demand)) {
        best.developer = developer;
        best.desirability = desirability;
        best.demand = demand;
    }
}

// The smallest and largest capacity (the profile's uint16 at +8) among the
// building types of the configuration's families that are the developer's
// purpose and wealth. False when the configuration has no family list or
// none of its buildings match.
bool CapacityRange(void* self, void* developer, void* info, int32_t& low, int32_t& high)
{
    void* const development = At<void*>(self, field::kDevelopment);
    low = 0x7FFFFFFF;
    high = INT32_MIN;
    const uint32_t* family = At<uint32_t*>(info, 0x28);
    if (family == At<uint32_t*>(info, 0x2C))
        return false;
    do {
        HashSet types{};
        ConstructSet(&types);
        Virtual<uint32_t>(development, 0x4C, &types, *family);
        const HashIterator it = Begin(&types);
        for (HashNode* node = it.node; node; node = Next(it.table, node, node->value)) {
            const auto* const profile = Virtual<const uint32_t*>(development, 0x9C, node->value);
            if (profile[0] == Virtual<uint32_t>(developer, 0x1C) && profile[1] == Virtual<uint32_t>(developer, 0x20)) {
                const int32_t capacity = *reinterpret_cast<const uint16_t*>(profile + 2);
                if (capacity < low)
                    low = capacity;
                if (capacity > high)
                    high = capacity;
            }
        }
        DestroySet(&types);
        ++family;
    } while (family != At<uint32_t*>(info, 0x2C));
    return low != 0x7FFFFFFF;
}

void ChooseLotSizes(void* self, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("ChooseLotSizes", logged);
    const int32_t zoneType = At<int32_t>(self, field::kZoneType);
    const int32_t density = reinterpret_cast<int32_t(__cdecl*)(int32_t)>(addr::SC4_ZoneTypeDensity)(zoneType);
    const int32_t x = (rect[0] + rect[2]) / 2;
    const int32_t z = (rect[1] + rect[3]) / 2;

    // The developer that would grow here. Residential zones (1-3) ask the
    // three residential developers; commercial zones (4-6) the five
    // commercial ones, and the denser two (5, 6) take no lot deeper than 2.
    // Other zone types have no developer.
    Best best;
    bool shallow = false;
    switch (zoneType) {
    case 1:
    case 2:
    case 3:
        Consider(self, 0x1010, x, z, best, true);
        Consider(self, 0x1020, x, z, best, false);
        Consider(self, 0x1030, x, z, best, false);
        break;
    case 5:
    case 6:
        shallow = true;
        [[fallthrough]];
    case 4:
        Consider(self, 0x3110, x, z, best, true);
        Consider(self, 0x3120, x, z, best, false);
        Consider(self, 0x3130, x, z, best, false);
        Consider(self, 0x3320, x, z, best, false);
        Consider(self, 0x3330, x, z, best, false);
        break;
    default:
        break;
    }

    // The third value of the developer's last growth list cell (12 bytes: x,
    // z, value; slot 11, valid by slot 10) whose desirability (the grid of
    // slot 13 at the cell) is above the centre's estimate; 1 without one.
    uint32_t level = 1;
    if (best.developer && Virtual<uint8_t>(best.developer, 0x28)) {
        auto* const list = Virtual<const int32_t* const*>(best.developer, 0x2C);
        for (const int32_t* cell = list[0]; cell != list[1]; cell += 3) {
            void* const grid = Virtual<void*>(best.developer, 0x34);
            const int32_t desirability = static_cast<int16_t>(Virtual<uint32_t>(grid, 0x58, cell[0], cell[1]));
            if (best.desirability >= desirability)
                break;
            level = static_cast<uint32_t>(cell[2]);
        }
    }

    // The lot configurations for the developer's purpose and wealth at that
    // level; the manager adds a reference to each.
    void* const configurations = Global<void*>(addr::SC4_Service43D18);
    HashSet set{};
    ConstructSet(&set);
    if (best.developer) {
        const uint32_t wealth = Virtual<uint32_t>(best.developer, 0x20);
        const uint32_t purpose = Virtual<uint32_t>(best.developer, 0x1C);
        // The game's filter object on the stack: the base (0x00489A50: its
        // vtable 0x00A8EC1C and a zero), then the zone developer's vtable.
        uint32_t filter[5] = {static_cast<uint32_t>(addr::cSC4ZoneDeveloper_LotFilter_vftable), 0, purpose, wealth,
                              level};
        Virtual<uint32_t>(configurations, 0x24, &set, &filter[0]);
    }

    // Of those 2 or 3 deep and at most 3 wide: the one whose capacity for a
    // tract of the developer's grids is nearest the demand (the first found
    // on a tie; the set is ordered by the configurations' addresses).
    int32_t bestDifference = 0x7FFFFFFF;
    void* bestConfiguration = nullptr;
    const HashIterator it = Begin(&set);
    for (HashNode* node = it.node; node; node = Next(it.table, node, node->value >> 2)) {
        void* const configuration = reinterpret_cast<void*>(node->value);
        void* const info = Virtual<void*>(configuration, 0x60);
        if (!info)
            continue;
        uint32_t width = 0, depth = 0;
        Virtual<uint32_t>(configuration, 0x1C, &width, &depth);
        if (depth <= 1 || depth > 3 || width > 3)
            continue;
        if (shallow && depth > 2)
            continue;
        int32_t low = 0, high = 0;
        if (!CapacityRange(self, best.developer, info, low, high))
            continue;
        const uint32_t shift = Virtual<uint32_t>(Virtual<void*>(best.developer, 0x34), 0x2C) & 31;
        const uint32_t area = width * depth;
        const auto tractArea = static_cast<int32_t>((1u << shift) << shift);
        const int32_t middle = (high + low) / 2;
        // x87 at 53 bits: tract cells / lot cells, times the middle capacity.
        const double capacity = static_cast<double>(tractArea) / static_cast<double>(area) * middle;
        int32_t difference = Ftol(capacity) - best.demand;
        if (difference < 0)
            difference = static_cast<int32_t>(0u - static_cast<uint32_t>(difference));
        if (difference < bestDifference) {
            bestDifference = difference;
            bestConfiguration = configuration;
        }
    }

    uint32_t& lotWidth = At<uint32_t>(self, local::kLotWidth);
    uint32_t& lotDepth = At<uint32_t>(self, local::kLotDepth);
    uint32_t& minDepth = At<uint32_t>(self, local::kMinDepth);
    uint32_t& maxDepth = At<uint32_t>(self, local::kMaxDepth);
    if (bestConfiguration) {
        Virtual<uint32_t>(bestConfiguration, 0x1C, &lotWidth, &lotDepth);
    } else {
        lotWidth = 2;
        lotDepth = 2;
    }
    minDepth = lotDepth;
    maxDepth = lotDepth;

    // The depth range over the configurations whose width divides the
    // chosen one; each configuration's reference is given back.
    HashNode* first = nullptr;
    for (HashNode** bucket = set.begin; bucket != set.end; ++bucket) {
        if (*bucket) {
            first = *bucket;
            break;
        }
    }
    for (HashNode* node = first; node;) {
        void* const configuration = reinterpret_cast<void*>(node->value);
        uint32_t width = 0, depth = 0;
        Virtual<uint32_t>(configuration, 0x1C, &width, &depth);
        if (lotWidth % width == 0) { // the game divides by a zero width too
            if (depth < minDepth)
                minDepth = depth;
            if (depth > maxDepth)
                maxDepth = depth;
        }
        Virtual<uint32_t>(configuration, 0x8);
        node = Next(&set, node, node->value >> 2);
    }
    if (maxDepth - minDepth < 2) {
        if (minDepth > 2)
            minDepth = maxDepth - 2;
        else
            minDepth = 1;
        maxDepth = minDepth + 2;
    }

    // The street spacing by density (1 low, 2 medium, 3 high; another value
    // leaves it).
    uint32_t& spacing = At<uint32_t>(self, local::kStreetSpacing);
    if (best.developer && Virtual<uint32_t>(best.developer, 0x1C) == 1) {
        switch (density) {
        case 1:
            spacing = lotDepth * 3;
            break;
        case 2:
            spacing = static_cast<uint32_t>(Ftol(static_cast<double>(lotDepth) * 2.5));
            break;
        case 3:
            spacing = lotDepth * 2;
            break;
        default:
            break;
        }
    } else {
        switch (density) {
        case 1:
            spacing = 7;
            break;
        case 2:
            spacing = 6;
            break;
        case 3:
            spacing = 5;
            break;
        default:
            break;
        }
    }
    DestroySet(&set);
}

// --- Parcellize (0x00733770) --------------------------------------------------

// The lots of the piece at +0xA4: 0x00731C80, then by zone type 0x0072EDD0
// (1-6) or 0x0072E9B0 with 0x10 (7) or 6 (any other).
void ParcelPiece(void* self)
{
    Game<void>(addr::cSC4ZoneDeveloper_Fn_00731C80, self);
    const int32_t zoneType = At<int32_t>(self, field::kZoneType);
    if (zoneType > 0 && zoneType <= 6)
        Game<void>(addr::cSC4ZoneDeveloper_Fn_0072EDD0, self);
    else if (zoneType == 7)
        Game<void>(addr::cSC4ZoneDeveloper_Fn_0072E9B0, self, 0x10u);
    else
        Game<void>(addr::cSC4ZoneDeveloper_Fn_0072E9B0, self, 6u);
}

// Whether the piece at +0xA4 holds the cell or touches it on a side. The
// game's tests, in its order: the cell itself; the cell before in x (when x
// > 0); the cell after in x (when x < the city's width - 1); the cell
// before in z (when z > 0); the cell after in z (when z < the city's depth:
// not depth - 1, as the game has it).
bool PieceIsAtCell(void* self, const int32_t* cell)
{
    void* piece = At<void*>(self, field::kRegion);
    const int32_t x = cell[0];
    const int32_t z = cell[1];
    const auto column = [&] { return static_cast<uint32_t>(x - At<int32_t>(piece, region::kX0)); };
    const auto row = [&] { return static_cast<uint32_t>(z - At<int32_t>(piece, region::kZ0)); };
    if (HasCell(piece, column(), row()))
        return true;
    bool beside = false;
    if (x > 0 && HasCell(piece, column() - 1, row()))
        beside = true;
    void* const city = At<void*>(self, field::kCity);
    if (x < Virtual<int32_t>(city, 0x174) - 1) {
        piece = At<void*>(self, field::kRegion);
        if (HasCell(piece, column() + 1, row()))
            beside = true;
    }
    if (z > 0) {
        piece = At<void*>(self, field::kRegion);
        if (HasCell(piece, column(), row() - 1))
            beside = true;
    }
    if (z < Virtual<int32_t>(city, 0x178)) {
        piece = At<void*>(self, field::kRegion);
        if (HasCell(piece, column(), row() + 1))
            return true;
    }
    return beside;
}

// Clears the cells of the piece at +0xA4 from the highlight region.
void RemovePiece(void* self, void** highlight)
{
    void* const piece = At<void*>(self, field::kRegion);
    const int32_t width = At<int32_t>(piece, region::kWidth);
    const int32_t depth = At<int32_t>(piece, region::kDepth);
    for (int32_t column = 0; column < width; ++column) {
        for (int32_t row = 0; row < depth; ++row) {
            void* const current = At<void*>(self, field::kRegion);
            const auto bit = static_cast<uint32_t>(row);
            if (!(At<uint32_t**>(current, region::kRows)[column][bit >> 5] & (1u << (bit & 31))))
                continue;
            void* const whole = *highlight;
            const auto wholeColumn =
                static_cast<uint32_t>(At<int32_t>(piece, region::kX0) - At<int32_t>(whole, region::kX0) + column);
            const auto wholeRow =
                static_cast<uint32_t>(At<int32_t>(piece, region::kZ0) - At<int32_t>(whole, region::kZ0) + row);
            At<uint32_t**>(whole, region::kRows)[wholeColumn][wholeRow >> 5] &= ~(1u << (wholeRow & 31));
        }
    }
}

// Deletes the piece at +0xA4 as the game's inlined code does: the rows,
// their table, the region.
void DeletePiece(void* self)
{
    void* const piece = At<void*>(self, field::kRegion);
    if (piece) {
        At<std::uintptr_t>(piece, region::kCellsVtable) = addr::SC4CellRegion_CellsVtable;
        if (At<void*>(piece, region::kRows)) {
            for (uint32_t column = 0; column < At<uint32_t>(piece, region::kWidth); ++column)
                Delete(At<void**>(piece, region::kRows)[column]);
            Delete(At<void*>(piece, region::kRows));
        }
        Delete(piece);
    }
    At<void*>(self, field::kRegion) = nullptr;
}

// `cells` is the region to cut into lots; `highlight`, when not null,
// receives a new region: the cells of the lots made (HighlightParcels).
void Parcellize(void* self, void* cells, void** highlight)
{
    static bool logged = false;
    FirstCall("Parcellize", logged);
    const int32_t* const rect = &At<int32_t>(cells, region::kX0);

    if (At<uint8_t>(self, field::kOption9C)) {
        // One lot of the whole region, facing +0xD0, +0xCC or their
        // opposites.
        At<void*>(self, field::kRegion) = cells;
        Game<void>(addr::cSC4ZoneDeveloper_Fn_00731C80, self);
        At<void*>(self, field::kRegion) = nullptr;
        uint32_t value = 0;
        ListNode* orientations = NewList();
        const auto push = [&](uint32_t orientation) {
            PushBack(orientations, reinterpret_cast<void*>(static_cast<std::uintptr_t>(orientation)));
        };
        push(At<uint32_t>(self, local::kOrientationD0));
        push(At<uint32_t>(self, local::kOrientationCC));
        push((At<uint32_t>(self, local::kOrientationD0) - 2) & 3);
        push((At<uint32_t>(self, local::kOrientationCC) - 2) & 3);
        const uint8_t made = Game<uint8_t>(addr::cSC4ZoneDeveloper_Fn_0072D200, self, rect[0], rect[1],
                                           rect[2] - rect[0] + 1, rect[3] - rect[1] + 1, &orientations, &value);
        if (!made && highlight)
            *highlight = NewRegion(rect);
        reinterpret_cast<void(__thiscall*)(ListNode**)>(addr::SC4PairList_Destroy)(&orientations);
        return;
    }

    SendParcellizing(self, 1);
    ListNode* const head = NewList();
    ListNode* pieces = head; // the list object the cutter fills; the game walks its own copy of the head
    void*& current = At<void*>(self, field::kRegion);
    current = Game<void*>(addr::cSC4ZoneDeveloper_Fn_0072CB90, self, cells);
    Game<void>(addr::cSC4ZoneDeveloper_Fn_0072AF30, self, &current);
    void* const whole = current;
    ChooseLotSizes(self, &At<int32_t>(whole, region::kX0));
    if (At<uint8_t>(self, field::kOption9A)) {
        static constexpr uint32_t kNetworks[] = {0xB, 0x3, 0xA, 0x0, 0x6};
        for (const uint32_t network : kNetworks)
            Game<void>(addr::cSC4ZoneDeveloper_Fn_00730BA0, self, whole, network);
        if (At<int32_t>(whole, region::kX1) - At<int32_t>(whole, region::kX0) + 1 > 2 &&
            At<int32_t>(whole, region::kZ1) - At<int32_t>(whole, region::kZ0) + 1 > 2) {
            for (const uint32_t network : kNetworks)
                Game<void>(addr::cSC4ZoneDeveloper_Fn_00730D30, self, whole, network);
        }
    }
    Game<void>(addr::cSC4ZoneDeveloper_Fn_0072EC60, self, current, &pieces);
    if (current)
        DeleteRegion(current);
    current = nullptr;

    // The highlight region starts as a copy of the argument's cells.
    if (highlight) {
        *highlight = NewRegion(rect);
        const int32_t width = At<int32_t>(cells, region::kWidth);
        const int32_t depth = At<int32_t>(cells, region::kDepth);
        for (int32_t column = 0; column < width; ++column) {
            for (int32_t row = 0; row < depth; ++row) {
                const auto bit = static_cast<uint32_t>(row);
                const uint32_t mask = 1u << (bit & 31);
                const bool set = (At<uint32_t**>(cells, region::kRows)[column][bit >> 5] & mask) != 0;
                uint32_t& word = At<uint32_t**>(*highlight, region::kRows)[column][bit >> 5];
                if (set)
                    word |= mask;
                else
                    word &= ~mask;
            }
        }
    }

    // Each piece: all of them, or with a cell only those at or beside it.
    for (ListNode* node = head->next; node != head; node = node->next) {
        current = node->value;
        const int32_t* const at = At<const int32_t*>(self, field::kValueA8);
        if (!at || PieceIsAtCell(self, at))
            ParcelPiece(self);
        else if (highlight)
            RemovePiece(self, highlight);
        DeletePiece(self);
    }
    ClearList(head);

    // With a cell: the highlight region becomes what 0x0072CDA0 makes of it
    // from the cell (null when the cell is not in it).
    if (const int32_t* const at = At<const int32_t*>(self, field::kValueA8); at && highlight) {
        void* const all = *highlight;
        const int32_t column = at[0] - At<int32_t>(all, region::kX0);
        const int32_t row = at[1] - At<int32_t>(all, region::kZ0);
        const auto bit = static_cast<uint32_t>(row);
        void* result = nullptr;
        if (At<uint32_t**>(all, region::kRows)[column][bit >> 5] & (1u << (bit & 31))) { // no bounds test in the game
            result = Game<void*>(addr::cSC4ZoneDeveloper_Fn_0072CDA0, self, all, column, row);
            Game<void>(addr::cSC4ZoneDeveloper_Fn_0072AF30, self, &result);
        }
        if (*highlight)
            DeleteRegion(*highlight);
        *highlight = result;
    }
    if (At<uint8_t>(self, field::kPlaceStreets))
        Game<void>(addr::cSC4ZoneDeveloper_Fn_0072B180, self, cells);
    SendParcellizing(self, 0);
    ClearList(head);
    StlFree(head);
}

// --- The interface ------------------------------------------------------------

// Slot 5 (0x00733F30).
void __fastcall HighlightParcels(void* self, void*, void* cells, uint32_t zoneType, const int32_t* cell,
                                 void** highlight)
{
    static bool logged = false;
    FirstCall("HighlightParcels", logged);
    At<const int32_t*>(self, field::kValueA8) = cell;
    At<uint8_t>(self, field::kPlaceStreets) = 0;
    At<uint32_t>(self, field::kZoneType) = zoneType;
    if (Virtual<uint8_t>(At<void*>(self, field::kZones), 0x34, zoneType)) {
        Game<void>(addr::cSC4ZoneDeveloper_Fn_0072C0E0, self);
        Parcellize(self, cells, highlight);
        Game<void>(addr::cSC4ZoneDeveloper_Fn_0072DB60, self, cells);
    }
    At<uint8_t>(self, field::kPlaceStreets) = 1;
    At<uint32_t>(self, field::kZoneType) = 0;
    At<const int32_t*>(self, field::kValueA8) = nullptr;
}

// Slot 6 (0x00733FA0).
void __fastcall DoParcellization(void* self, void*, void* cells, uint32_t zoneType, uint32_t flag)
{
    static bool logged = false;
    FirstCall("DoParcellization", logged);
    At<uint32_t>(self, field::kZoneType) = zoneType;
    if (Virtual<uint8_t>(At<void*>(self, field::kZones), 0x34, zoneType)) {
        Game<void>(addr::cSC4ZoneDeveloper_Fn_0072C0E0, self);
        Parcellize(self, cells, nullptr);
    } else {
        Game<void>(addr::cSC4ZoneDeveloper_Fn_0072E6A0, self, cells, flag);
    }
    At<uint32_t>(self, field::kZoneType) = 0;
}

// --- Helpers other parts' code calls (detoured) -------------------------------

// 0x00734860 (lot manager slot 15 behind the thunk 0x00734850; `list` is
// the lot list at object + 0x60): a new iterator {the first node, the list}
// appended to the iterator list at object + 0x64 (the object part's insert
// 0x0072BA10: out, before which node, the value's address).
void** __fastcall NewLotIterator(void* list)
{
    static bool logged = false;
    FirstCall("NewLotIterator", logged);
    auto** const iterator = static_cast<void**>(New(8));
    iterator[1] = list;
    if (list)
        iterator[0] = **static_cast<void***>(list);
    void* inserted = nullptr;
    void* value = iterator;
    reinterpret_cast<void**(__stdcall*)(void**, void*, void* const*)>(addr::cSC4ZoneDeveloper_Fn_0072BA10)(
        &inserted, At<void*>(list, 4), &value);
    return iterator;
}

// 0x00734000: the lot grid resized to `count` empty columns.
void __fastcall ResizeLotGrid(void* grid, void*, uint32_t count)
{
    static bool logged = false;
    FirstCall("ResizeLotGrid", logged);
    const uint32_t empty[3] = {0, 0, 0};
    reinterpret_cast<void(__thiscall*)(void*, uint32_t, const uint32_t*)>(addr::SC4LotColumns_Resize)(grid, count,
                                                                                                    empty);
}

// Tree nodes of the map +0x78 and of the maps in it.
namespace tree {
constexpr std::size_t kParent = 0x4, kLeft = 0x8, kRight = 0xC;
constexpr std::size_t kInnerHead = 0x14, kInnerSize = 0x18; // the node's value: a map
} // namespace tree

// The end of both erasers: the node's own map emptied by `erase`, its head
// freed, the node freed.
template <typename Erase>
void* DestroyTreeNode(void* node, Erase erase)
{
    void* const left = At<void*>(node, tree::kLeft);
    void* const inner = static_cast<char*>(node) + tree::kInnerHead;
    if (At<uint32_t>(node, tree::kInnerSize)) {
        erase(inner, At<void*>(At<void*>(inner, 0), tree::kParent));
        At<void*>(At<void*>(inner, 0), tree::kLeft) = At<void*>(inner, 0);
        At<void*>(At<void*>(inner, 0), tree::kParent) = nullptr;
        At<void*>(At<void*>(inner, 0), tree::kRight) = At<void*>(inner, 0);
        At<uint32_t>(inner, 4) = 0;
    }
    if (At<void*>(inner, 0))
        StlFree(At<void*>(inner, 0));
    StlFree(node);
    return left;
}

// 0x00734580: a subtree of a middle map; the innermost maps go through part
// d's 0x00732830.
void EraseTree78Inner(void* map, void* node)
{
    while (node) {
        EraseTree78Inner(map, At<void*>(node, tree::kRight));
        node = DestroyTreeNode(node, [](void* inner, void* root) {
            Game<void>(addr::cSC4ZoneDeveloper_Fn_00732830, inner, root);
        });
    }
}

void EraseTree78Nodes(void* map, void* node)
{
    while (node) {
        EraseTree78Nodes(map, At<void*>(node, tree::kRight));
        node = DestroyTreeNode(node, [](void* inner, void* root) { EraseTree78Inner(inner, root); });
    }
}

// 0x00734600: a subtree of the map +0x78 (the destructor's call).
void __fastcall EraseTree78(void* map, void*, void* node)
{
    static bool logged = false;
    FirstCall("EraseTree78", logged);
    EraseTree78Nodes(map, node);
}

// --- Installation -------------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, addr::cSC4ZoneDeveloper_HighlightParcels, HighlightParcels),
    OPENSC4_SLOT(6, addr::cSC4ZoneDeveloper_DoParcellization, DoParcellization),
};

#undef OPENSC4_SLOT

struct Detour {
    std::uintptr_t address;
    void* ours;
    void* original;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn), nullptr}

Detour g_detours[] = {
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_NewLotIterator, NewLotIterator),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_ResizeLotGrid, ResizeLotGrid),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_EraseTree78, EraseTree78),
};

#undef OPENSC4_DETOUR

// Checks that both slots still hold the originals, installs the detours,
// then swaps the slots.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4ZoneDeveloper_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("zone parcellize: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(addr::cSC4ZoneDeveloper_vftable),
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (Detour& detour : g_detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, detour.ours, &detour.original)) {
            log::Write("zone parcellize: no detour at %08X", static_cast<unsigned>(detour.address));
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
    log::Write("zone parcellize: %u slots and %u detours are ours", static_cast<unsigned>(std::size(kSlots)),
               static_cast<unsigned>(std::size(g_detours)));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.zone.parcellize", "simulators/zone", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSC4ZoneDeveloper_HighlightParcels,
     "Our cSC4ZoneDeveloper parcellization entry: HighlightParcels, DoParcellization, the cutting of a region into "
     "pieces and the choice of lot sizes; the lot iterator, the lot grid's resize and the eraser of the map +0x78",
     false, Install});

} // namespace
} // namespace opensc4::zone_sim
