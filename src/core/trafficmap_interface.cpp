// Our cSC4TrafficNetworkMap, the query part (docs/traffic-map.md): identity
// (QueryInterface, Release and their thunks on the other parts), the cell
// queries (records, occupants, heights, neighbours), the per-record lane and
// path entries, the automata lists of the records, the lane round robins and
// snapping positions onto the network. Swapped into the game's vtables.
#include "core/trafficmap.h"

#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace opensc4::trafficmap {
namespace {

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using ListFn = void(__thiscall*)(void* list);
using LanesResizeFn = void(__thiscall*)(GameVector* lanes, uint32_t count, const GameVector* fill);
using LanePushFn = void(__thiscall*)(GameVector* entries, const void* entry);
using InsertFn = void(__thiscall*)(GameVector* vector, void* where, const void* value, void* allocator, uint32_t count,
                                   uint32_t atEnd);
using AssignFn = void*(__thiscall*)(GameVector* vector, const GameVector* other);

// A lane entry of a cell record (16 bytes; record +0x4 keeps one vector of
// them per travel type).
struct LaneEntry {
    uint32_t key;    // byte 3, byte 1 and byte 0 of the slots' arguments (byte 2 always 0)
    uint32_t value4; // slot 19
    uint32_t value8; // slot 21
    uint16_t count;  // slot 23: automata of that kind (slots 30, 31)
    uint16_t pad;
};
static_assert(sizeof(LaneEntry) == 16);

// A path entry of a cell record (12 bytes, record +0x10).
struct PathEntry {
    uint32_t key;
    float value;     // slot 20
    uint16_t count;  // slot 22: automata of that kind (slots 30, 31)
    uint16_t pad;
};
static_assert(sizeof(PathEntry) == 12);

constexpr float kCellsPerUnit = 0.0625f; // 0x00A8FDF0: 16 world units per cell
constexpr float kInset = 0.01f;          // 0x00A823A4
constexpr float kMinMove = 1e-5f;        // 0x00A8FE64

// __ftol2: truncation toward zero, the low 32 bits.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The cell of a world coordinate as slots 9 and 18 round it: x / 16 - 0.5
// rounded to nearest (ties to even) through 1.5 * 2^23, then one up when x / 16
// is exactly that plus one (the floor for |x / 16| < 2^22).
int32_t CellOf(float coordinate)
{
    const double cells = static_cast<double>(coordinate) * kCellsPerUnit;
    const float magic = static_cast<float>((cells - 0.5) + 12582912.0);
    uint32_t bits;
    std::memcpy(&bits, &magic, sizeof(bits));
    const uint32_t cell = bits + 0xB4C00000u;
    return static_cast<int32_t>(cells == static_cast<double>(static_cast<int32_t>(cell + 1)) ? cell + 1 : cell);
}

int32_t Times16(int32_t value)
{
    return static_cast<int32_t>(static_cast<uint32_t>(value) << 4);
}

float FloatBits(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// Calls through the map's own vtable, as the game does.
CellRecord* RecordsAt(void* self, int32_t x, int32_t z)
{
    return Virtual<CellRecord*>(self, 0x38, x, z);
}

CellRecord* RecordOfOccupant(void* self, void* occupant)
{
    return Virtual<CellRecord*>(self, 0x40, occupant);
}

bool Matches(CellRecord* record, uint32_t key)
{
    return record && record->occupant && Virtual<uint8_t>(record->occupant, 0x24, key);
}

// --- Identity ----------------------------------------------------------------

// 0 (0x004AE350): the map, cIGZUnknown (+0x4), the message target (+0x8) or
// the serializable (+0xC), with a reference; anything else is refused.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    switch (iid) {
    case 0xCA0F81B9:
        *object = self;
        break;
    case 1:
        *object = self ? self + 0x4 : nullptr;
        break;
    case 0x452294AA:
        *object = self ? self + 0x8 : nullptr;
        break;
    case 0xE4FDA3D4:
        *object = self ? self + 0xC : nullptr;
        break;
    default:
        return 0;
    }
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// 2 (0x004AFAA0): drops a reference; the last one shuts the map down (slot 4)
// and deletes it (slot 45).
uint32_t __fastcall Release(char* self, void*)
{
    uint32_t& count = At<uint32_t>(self, field::kRefCount);
    if (count > 1)
        return --count;
    Virtual<uint32_t>(self, 0x10);
    Virtual<uint32_t>(self, 0xB4, 1u);
    return 0;
}

// The other parts' slots 0 and 2 (0x004B3AE0/0x004B3AF0/0x004B3B00 and
// 0x004B3B10/0x004B3B20/0x004B3B30): back to the object's start.
uint8_t __fastcall QueryInterface4(char* self, void*, uint32_t iid, void** object)
{
    return QueryInterface(self - 0x4, nullptr, iid, object);
}

uint8_t __fastcall QueryInterface8(char* self, void*, uint32_t iid, void** object)
{
    return QueryInterface(self - 0x8, nullptr, iid, object);
}

uint8_t __fastcall QueryInterfaceC(char* self, void*, uint32_t iid, void** object)
{
    return QueryInterface(self - 0xC, nullptr, iid, object);
}

uint32_t __fastcall Release4(char* self, void*) { return Release(self - 0x4, nullptr); }
uint32_t __fastcall Release8(char* self, void*) { return Release(self - 0x8, nullptr); }
uint32_t __fastcall ReleaseC(char* self, void*) { return Release(self - 0xC, nullptr); }

// --- Cells and occupants -----------------------------------------------------

// 7 (0x004AE190): slot 8 at the cell of a world position (truncated).
void* __fastcall FindOccupantAtPosition(void* self, void*, const float* position, uint32_t key, uint32_t objectsToo)
{
    static bool logged = false;
    FirstCall("FindOccupantAtPosition", logged);
    const int32_t z = Ftol(static_cast<double>(position[2]) * kCellsPerUnit);
    const int32_t x = Ftol(static_cast<double>(position[0]) * kCellsPerUnit);
    return Virtual<void*>(self, 0x20, x, z, key, objectsToo);
}

// 8 (0x004B0B20): the first occupant on a cell that accepts `key` (its slot
// 9); with `objectsToo`, then the first object waiting on the cell that does.
void* __fastcall FindOccupantInCell(void* self, void*, int32_t x, int32_t z, uint32_t key, uint32_t objectsToo)
{
    static bool logged = false;
    FirstCall("FindOccupantInCell", logged);
    for (CellRecord* record = RecordsAt(self, x, z); record; record = record->next) {
        void* const occupant = record->occupant;
        if (occupant && Virtual<uint8_t>(occupant, 0x24, key))
            return occupant;
    }
    if (!static_cast<uint8_t>(objectsToo))
        return nullptr;
    const uint32_t cell = (static_cast<uint32_t>(x) << 16) | static_cast<uint32_t>(z);
    auto* const node = Find<CellObjectsNode>(Table(self, field::kCellObjects), cell);
    if (!node)
        return nullptr;
    for (auto** it = static_cast<void**>(node->objects.begin); it != node->objects.end; ++it) {
        void* const object = *it;
        if (object && Virtual<uint8_t>(object, 0x24, key))
            return object;
    }
    return nullptr;
}

// 9 (0x004AF970): the first occupant at the cell of a world position (rounded
// down), or null for no position.
void* __fastcall OccupantAtPosition(void* self, void*, const float* position)
{
    static bool logged = false;
    FirstCall("OccupantAtPosition", logged);
    if (!position)
        return nullptr;
    const int32_t x = CellOf(position[0]);
    const int32_t z = CellOf(position[2]);
    return Virtual<void*>(self, 0x2C, x, z);
}

// 10 (0x004AE160): the occupant of slot 12's record.
void* __fastcall OccupantAtHeight(void* self, void*, uint32_t x, uint32_t z, uint32_t low, uint32_t high)
{
    static bool logged = false;
    FirstCall("OccupantAtHeight", logged);
    auto* const record = Virtual<CellRecord*>(self, 0x30, x, z, low, high);
    return record ? record->occupant : nullptr;
}

// 11 (0x004AE140): the occupant of a cell's first record.
void* __fastcall OccupantAt(void* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("OccupantAt", logged);
    auto* const record = RecordsAt(self, x, z);
    return record ? record->occupant : nullptr;
}

// 12 (0x004AFA10): the first record on a cell whose occupant's position
// (its slot 48 object, slot 8) has a height in [low, high]; null before Init.
CellRecord* __fastcall RecordAtHeight(void* self, void*, int32_t x, int32_t z, float low, float high)
{
    static bool logged = false;
    FirstCall("RecordAtHeight", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return nullptr;
    const uint32_t fill = Global<uint32_t>(addr::SC4_ZeroVector3Value);
    uint32_t position[3] = {fill, fill, fill};
    CellRecord* record = RecordsAt(self, x, z);
    for (; record; record = record->next) {
        void* const object = Virtual<void*>(record->occupant, 0xC0);
        Virtual<uint32_t>(object, 0x20, position);
        const float y = FloatBits(position[1]);
        if (y >= low && y <= high)
            break;
    }
    return record;
}

// 13 (0x004B3AC0): a cell's record list, unchecked.
CellRecord* __fastcall CellRecords(void* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("CellRecords", logged);
    return CellAt(self, x, z);
}

// 14 (0x004AE0C0): a cell's record list (slot 13), or null outside the city.
CellRecord* __fastcall CheckedCellRecords(void* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("CheckedCellRecords", logged);
    if (x < 0 || x >= At<int32_t>(self, field::kWidth) || z < 0 || z >= At<int32_t>(self, field::kHeight))
        return nullptr;
    return Virtual<CellRecord*>(self, 0x34, x, z);
}

// 15 (0x004AF7C0): the records of the cell next to an occupant in a direction
// (0 -x, 1 -z, 2 +x, 3 +z); with no records there, those of the cell of the
// occupant's other end (interface 0x8A4BD52B, +0x1D0) if that lies that way.
// With `connected`, the first record whose occupant joins this one in that
// direction under `mask`; otherwise the first that takes `mask` (any record
// for mask 0).
CellRecord* __fastcall NeighborRecord(void* self, void*, void* occupant, uint32_t direction, uint32_t connected,
                                      uint32_t mask)
{
    static bool logged = false;
    FirstCall("NeighborRecord", logged);
    if (!occupant)
        return nullptr;
    int32_t x = 0;
    int32_t z = 0;
    Virtual<uint32_t>(occupant, 0xEC, &x, &z);
    const uint8_t way = static_cast<uint8_t>(direction);
    const auto* const offsets = reinterpret_cast<const int16_t*>(addr::cSC4TrafficNetworkMap_NeighbourOffsets);
    CellRecord* record = RecordsAt(self, x + offsets[way * 2], z + offsets[way * 2 + 1]);
    if (!record) {
        void* end = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, 0x8A4BD52Bu, &end)) {
            if (void* const other = At<void*>(end, 0x1D0)) {
                int32_t x2 = 0;
                int32_t z2 = 0;
                Virtual<uint32_t>(other, 0xEC, &x2, &z2);
                if (way <= 3) {
                    const uint32_t ux = static_cast<uint32_t>(x), uz = static_cast<uint32_t>(z);
                    const uint32_t ux2 = static_cast<uint32_t>(x2), uz2 = static_cast<uint32_t>(z2);
                    bool beyond = false;
                    switch (way) {
                    case 0: beyond = ux2 < ux; break;
                    case 1: beyond = uz2 < uz; break;
                    case 2: beyond = ux2 > ux; break;
                    default: beyond = uz2 > uz; break;
                    }
                    if (beyond)
                        record = RecordsAt(self, x2, z2);
                }
            }
            Virtual<uint32_t>(end, 0x8);
        }
    }
    if (static_cast<uint8_t>(connected)) {
        for (; record; record = record->next) {
            void* const neighbor = record->occupant;
            if (Virtual<uint8_t>(neighbor, 0x4C) && Virtual<uint8_t>(occupant, 0xD4, 0u, 0xFFu, direction) &&
                Virtual<uint8_t>(neighbor, 0xD4, mask, direction, 0xFFu))
                return record;
        }
        return nullptr;
    }
    if (!mask)
        return record;
    for (; record; record = record->next) {
        void* const neighbor = record->occupant;
        if (Virtual<uint8_t>(neighbor, 0x4C) && Virtual<uint8_t>(neighbor, 0xD4, mask, 0xFFu, 0xFFu))
            return record;
    }
    return nullptr;
}

// 16 (0x004AE0F0): an occupant's record on its own cell (its slot 59 cell).
CellRecord* __fastcall RecordOf(void* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("RecordOf", logged);
    int32_t x = 0;
    int32_t z = 0;
    Virtual<uint32_t>(occupant, 0xEC, &x, &z);
    CellRecord* record = RecordsAt(self, x, z);
    while (record && record->occupant != occupant)
        record = record->next;
    return record;
}

// 18 (0x004AF630): the ground height at a point: 0 outside the city; on a
// network occupant of kind 0x1F4F, the occupant's own height (its slot 56)
// unless it is of kind 0x304 (then its model's height at the point, slot 20,
// when the model answers); otherwise the terrain's.
double __fastcall HeightAt(void* self, void*, float x, float z)
{
    static bool logged = false;
    FirstCall("HeightAt", logged);
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x188, x, z))
        return 0.0f;
    const int32_t cellX = CellOf(x);
    const int32_t cellZ = CellOf(z);
    if (void* const occupant = Virtual<void*>(self, 0x20, cellX, cellZ, 0x1F4Fu, 1u)) {
        if (!Virtual<uint8_t>(occupant, 0x24, 0x304u))
            return Virtual<double>(occupant, 0xE0, x, z);
        const uint32_t fill = Global<uint32_t>(addr::SC4_ZeroVector3Value);
        float point[3] = {FloatBits(fill), FloatBits(fill), FloatBits(fill)};
        void* const object = Virtual<void*>(occupant, 0xC0);
        Virtual<uint32_t>(object, 0x20, point);
        point[0] = x;
        point[2] = z;
        if (void* const model = Virtual<void*>(occupant, 0xC4)) {
            if (Virtual<int32_t>(model, 0x50, point, 0u, 0u) != -1)
                return point[1];
        }
    }
    void* const terrain = Virtual<void*>(Global<void*>(addr::SC4_City), 0x150);
    return Virtual<double>(terrain, 0x34, x, z);
}

// --- Lane and path entries of a record ---------------------------------------

// The lane entry with `key` of travel type `type` (1-12; others are 1) of a
// record, appended (zeroed) if missing.
LaneEntry* LaneEntryOf(CellRecord* record, uint32_t type, uint32_t key)
{
    uint32_t index = type - 1;
    if (index >= 12)
        index = 0;
    GameVector* const lanes = &record->lanes;
    const int32_t bytes = static_cast<int32_t>(static_cast<char*>(lanes->end) - static_cast<char*>(lanes->begin));
    if (static_cast<uint32_t>(bytes / 12) <= index) {
        const GameVector empty{};
        reinterpret_cast<LanesResizeFn>(addr::SC4LaneVectors_Resize)(lanes, index + 1, &empty);
    }
    GameVector* const entries = static_cast<GameVector*>(lanes->begin) + index;
    const LaneEntry probe{key, 0, 0, 0, 0};
    auto* entry = static_cast<LaneEntry*>(entries->begin);
    auto* const end = static_cast<LaneEntry*>(entries->end);
    while (entry != end && entry->key != key)
        ++entry;
    if (entry != end)
        return entry;
    reinterpret_cast<LanePushFn>(addr::SC4LaneEntryVector_PushBack)(entries, &probe);
    entry = static_cast<LaneEntry*>(entries->begin);
    while (entry != entries->end && entry->key != key)
        ++entry;
    return entry;
}

uint32_t LaneKey(uint32_t high, uint32_t middle, uint32_t low)
{
    return (((high << 16) | static_cast<uint8_t>(middle)) << 8) | static_cast<uint8_t>(low);
}

// 19 (0x004B4AF0): the first value of a lane entry of an occupant's record
// (made if missing); a zeroed static without a record.
uint32_t* __fastcall LaneValue4(void* self, void*, void* occupant, uint32_t type, uint32_t high, uint32_t middle,
                                uint32_t low)
{
    static bool logged = false;
    FirstCall("LaneValue4", logged);
    CellRecord* const record = RecordOfOccupant(self, occupant);
    if (!record) {
        Global<uint32_t>(addr::cSC4TrafficNetworkMap_NoLaneValue4) = 0;
        return &Global<uint32_t>(addr::cSC4TrafficNetworkMap_NoLaneValue4);
    }
    return &LaneEntryOf(record, type, LaneKey(high, middle, low))->value4;
}

// 21 (0x004B4BF0): the second value of a lane entry, as slot 19 (the static is
// not zeroed).
uint32_t* __fastcall LaneValue8(void* self, void*, void* occupant, uint32_t type, uint32_t high, uint32_t middle,
                                uint32_t low)
{
    static bool logged = false;
    FirstCall("LaneValue8", logged);
    CellRecord* const record = RecordOfOccupant(self, occupant);
    if (!record)
        return &Global<uint32_t>(addr::cSC4TrafficNetworkMap_NoLaneValue8);
    return &LaneEntryOf(record, type, LaneKey(high, middle, low))->value8;
}

// 23 (0x004B4CF0): the count of a lane entry (key with byte 2 cleared), as
// slot 19.
uint16_t* __fastcall LaneCount(void* self, void*, void* occupant, uint32_t type, uint32_t key)
{
    static bool logged = false;
    FirstCall("LaneCount", logged);
    CellRecord* const record = RecordOfOccupant(self, occupant);
    if (!record) {
        Global<uint16_t>(addr::cSC4TrafficNetworkMap_NoLaneCount) = 0;
        return &Global<uint16_t>(addr::cSC4TrafficNetworkMap_NoLaneCount);
    }
    return &LaneEntryOf(record, type, key & 0xFF00FFFFu)->count;
}

// 24 (0x004AE1D0): slot 23 with the key from its three parts.
uint16_t* __fastcall LaneCountOf(void* self, void*, void* occupant, uint32_t type, uint32_t high, uint32_t middle,
                                 uint32_t low)
{
    static bool logged = false;
    FirstCall("LaneCountOf", logged);
    return Virtual<uint16_t*>(self, 0x5C, occupant, type, LaneKey(high, middle, low));
}

// The path entry with `key` of an occupant's record, appended (zeroed) if
// missing. The game has no null check (a missing record faults there too).
PathEntry* PathEntryOf(CellRecord* record, uint32_t key)
{
    GameVector* const paths = &record->paths;
    for (auto* entry = static_cast<PathEntry*>(paths->begin); entry != paths->end; ++entry)
        if (entry->key == key)
            return entry;
    PathEntry probe{key, 0.0f, 0, 0};
    if (paths->end != paths->capacity) {
        if (paths->end)
            *static_cast<PathEntry*>(paths->end) = probe;
        paths->end = static_cast<PathEntry*>(paths->end) + 1;
    } else {
        uint32_t allocator = 0;
        reinterpret_cast<InsertFn>(addr::SC4Uint32x3Vector_Insert)(paths, paths->end, &probe, &allocator, 1, 1);
    }
    return static_cast<PathEntry*>(paths->end) - 1;
}

// 20 (0x004B2970): the value of a path entry of an occupant's record.
float* __fastcall PathValue(void* self, void*, void* occupant, uint32_t key)
{
    static bool logged = false;
    FirstCall("PathValue", logged);
    return &PathEntryOf(RecordOfOccupant(self, occupant), key)->value;
}

// 22 (0x004B28C0): the count of a path entry of an occupant's record.
uint16_t* __fastcall PathCount(void* self, void*, void* occupant, uint32_t key)
{
    static bool logged = false;
    FirstCall("PathCount", logged);
    return &PathEntryOf(RecordOfOccupant(self, occupant), key)->count;
}

// --- Occupants near a rectangle ----------------------------------------------

// A list of the allocator at 0x00B207E4 (an empty circular list).
void* NewList()
{
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    return sentinel;
}

// 25 (0x004B2A20): fills `out` (cleared) with the network occupants
// (0xA821EF94, referenced) of the occupant manager's box query over the cells
// `rect` (x0, z0, x1, z1) grown by `margin`, that take `key` and flag
// 0x10000000; with `inner` > 0, leaving out those overlapping the rect grown by
// `inner`. True if any.
uint8_t __fastcall OccupantsNearRect(void* self, void*, GameVector* out, const int32_t* rect, int32_t margin,
                                     int32_t inner, uint32_t key)
{
    static bool logged = false;
    FirstCall("OccupantsNearRect", logged);
    void* head = NewList();
    const auto edge = [](int32_t a, int32_t b) {
        return static_cast<float>(static_cast<int32_t>((static_cast<uint32_t>(a) + static_cast<uint32_t>(b)) << 4));
    };
    const float low[3] = {edge(rect[0], -margin), -std::numeric_limits<float>::max(), edge(rect[1], -margin)};
    const float high[3] = {edge(rect[2], margin + 1), std::numeric_limits<float>::max(), edge(rect[3], margin + 1)};
    if (!Virtual<uint8_t>(At<void*>(self, field::kOccupantManager), 0x90, &head, low, high,
                          At<void*>(self, field::kFilterA), -1)) {
        reinterpret_cast<ListFn>(addr::SC4List_Destroy2)(&head);
        return 0;
    }
    out->end = out->begin;
    void** const sentinel = static_cast<void**>(head);
    for (auto** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        void* const occupant = node[2];
        void* network = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, 0xA821EF94u, &network)) {
            bool take = true;
            if (inner > 0) {
                int32_t bounds[4];
                Virtual<uint32_t>(occupant, 0x2C, bounds);
                if (bounds[2] > rect[0] - inner && bounds[3] > rect[1] - inner && bounds[0] < rect[2] + inner &&
                    bounds[1] < rect[3] + inner)
                    take = false;
            }
            if (take && Virtual<uint8_t>(network, 0x24, key) && Virtual<uint8_t>(network, 0x20, 0x10000000u)) {
                Virtual<uint32_t>(network, 0x4);
                void* value = network;
                if (out->end != out->capacity) {
                    if (out->end)
                        *static_cast<void**>(out->end) = value;
                    out->end = static_cast<void**>(out->end) + 1;
                } else {
                    void* allocator = network;
                    reinterpret_cast<InsertFn>(addr::SC4PtrVector_InsertN)(out, out->end, &value, &allocator, 1, 1);
                }
            }
        }
        if (network)
            Virtual<uint32_t>(network, 0x8);
    }
    Virtual<uint32_t>(At<void*>(self, field::kOccupantManager), 0xC0, &head);
    const uint8_t any = out->begin != out->end;
    reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&head);
    Virtual<uint32_t>(ListNodeAllocator(), 0x10, head);
    return any;
}

// 26 (0x004B2D10): slot 25 around an occupant's lot (the lot manager's lot
// rect) or, without a lot, its own cells.
uint8_t __fastcall OccupantsNearOccupant(void* self, void*, GameVector* out, void* occupant, int32_t margin,
                                         int32_t inner, uint32_t key)
{
    static bool logged = false;
    FirstCall("OccupantsNearOccupant", logged);
    void* head = NewList();
    void* const lots = Virtual<void*>(Global<void*>(addr::SC4_City), 0x84);
    int32_t rect[4];
    if (void* const lot = Virtual<void*>(lots, 0x38, occupant))
        Virtual<uint32_t>(lot, 0x38, rect);
    else
        Virtual<uint32_t>(occupant, 0x2C, rect);
    const uint8_t any = Virtual<uint8_t>(self, 0x64, out, rect, margin, inner, key);
    reinterpret_cast<ListFn>(addr::SC4List_Destroy2)(&head);
    return any;
}

// --- Automata on a record ----------------------------------------------------

// 27 (0x004B0000): how many automata on an occupant's record are not flagged
// (their slot 85) and of `kind` (their slot 136).
uint16_t __fastcall CountAutomata(void* self, void*, void* occupant, uint32_t kind)
{
    static bool logged = false;
    FirstCall("CountAutomata", logged);
    uint32_t count = 0;
    CellRecord* const record = RecordOfOccupant(self, occupant);
    if (!record)
        return 0;
    for (ListNode* node = record->automata->next; node != record->automata; node = node->next) {
        void* const automaton = node->value;
        if (!Virtual<uint8_t>(automaton, 0x154) && Virtual<uint32_t>(automaton, 0x220) == kind)
            ++count;
    }
    return static_cast<uint16_t>(count);
}

// 28 (0x004AE090): an occupant's slot 53 (mask, a, b); false for none.
uint8_t __fastcall OccupantAllows(void*, void*, void* occupant, uint32_t a, uint32_t b, uint32_t mask)
{
    static bool logged = false;
    FirstCall("OccupantAllows", logged);
    if (!occupant)
        return 0;
    return Virtual<uint8_t>(occupant, 0xD4, mask, a, b) != 0;
}

// 29 (0x004AE030): slot 28 for each record of a cell until one allows.
uint8_t __fastcall CellAllows(void* self, void*, int32_t x, int32_t z, uint32_t a, uint32_t b, uint32_t mask)
{
    static bool logged = false;
    FirstCall("CellAllows", logged);
    for (CellRecord* record = RecordsAt(self, x, z); record; record = record->next)
        if (Virtual<uint8_t>(self, 0x70, record->occupant, a, b, mask))
            return 1;
    return 0;
}

// 30 (0x004B1C00): adds an automaton (referenced) to an occupant's record
// unless there already, and counts it in its lane entry (slot 23 with its
// +0x1E4 and kind) and path entry (slot 22, kind bits 24-27), saturating.
uint8_t __fastcall AddAutomaton(void* self, void*, void* automaton, void* occupant)
{
    static bool logged = false;
    FirstCall("AddAutomaton", logged);
    if (!automaton || !occupant)
        return 0;
    CellRecord* const record = RecordOfOccupant(self, occupant);
    if (!record)
        return 0;
    ListNode* const sentinel = record->automata;
    for (ListNode* node = sentinel->next; node != sentinel; node = node->next)
        if (node->value == automaton)
            return 0;
    // 0x004B0BE0: push_back, the node from the STL allocator.
    auto* const node = static_cast<ListNode*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0xC));
    node->value = automaton;
    node->next = sentinel;
    node->prev = sentinel->prev;
    sentinel->prev->next = node;
    sentinel->prev = node;
    Virtual<uint32_t>(automaton, 0x4);
    const uint32_t kind = Virtual<uint32_t>(automaton, 0x220);
    const uint32_t lane = At<uint32_t>(automaton, 0x1E4);
    uint16_t* const laneCount = Virtual<uint16_t*>(self, 0x5C, occupant, lane, kind);
    if (*laneCount < 0xFFFF)
        ++*laneCount;
    uint16_t* const pathCount = Virtual<uint16_t*>(self, 0x58, occupant, (kind >> 24) & 0xF);
    if (*pathCount < 0xFFFF)
        ++*pathCount;
    return 1;
}

// 31 (0x004B02A0): removes every entry of an automaton from an occupant's
// record, each time uncounting it (as slot 30, with the given kind) and
// releasing it. True if any.
uint8_t __fastcall RemoveAutomaton(void* self, void*, void* automaton, void* occupant, uint32_t kind)
{
    static bool logged = false;
    FirstCall("RemoveAutomaton", logged);
    if (!automaton || !occupant)
        return 0;
    CellRecord* const record = RecordOfOccupant(self, occupant);
    if (!record)
        return 0;
    uint8_t removed = 0;
    ListNode* node = record->automata->next;
    while (node != record->automata) {
        if (node->value != automaton) {
            node = node->next;
            continue;
        }
        ListNode* const next = node->next;
        node->prev->next = next;
        next->prev = node->prev;
        reinterpret_cast<FreeFn>(addr::SC4_Free)(node);
        node = next;
        const uint32_t lane = At<uint32_t>(automaton, 0x1E4);
        uint16_t* const laneCount = Virtual<uint16_t*>(self, 0x5C, occupant, lane, kind);
        if (*laneCount)
            --*laneCount;
        uint16_t* const pathCount = Virtual<uint16_t*>(self, 0x58, occupant, (kind >> 24) & 0xF);
        if (*pathCount)
            --*pathCount;
        Virtual<uint32_t>(automaton, 0x8);
        removed = 1;
    }
    return removed;
}

// 34 (0x004AE000): an occupant's model's (its slot 49) slot 13 with 0; false
// without either.
uint32_t __fastcall ModelCheck(void*, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("ModelCheck", logged);
    if (!occupant)
        return 0;
    void* const model = Virtual<void*>(occupant, 0xC4);
    if (!model)
        return 0;
    return Virtual<uint32_t>(model, 0x34, 0u);
}

// --- Lane round robins -------------------------------------------------------

LaneWay* WayOf(LaneNode* node, uint32_t way)
{
    // The game does not check the way (a byte).
    return reinterpret_cast<LaneWay*>(reinterpret_cast<char*>(node) + 8 + static_cast<uint8_t>(way) * 16);
}

// 40 (0x004B0930): the current value of an occupant's lane way; 0 if none.
uint32_t __fastcall CurrentLaneValue(void* self, void*, uint32_t occupant, uint32_t way)
{
    static bool logged = false;
    FirstCall("CurrentLaneValue", logged);
    auto* const node = Find<LaneNode>(Table(self, field::kLanes), occupant);
    if (!node)
        return 0;
    LaneWay* const lane = WayOf(node, way);
    if (lane->values.begin == lane->values.end)
        return 0;
    return static_cast<uint32_t*>(lane->values.begin)[lane->index];
}

// 41 (0x004B1B60): copies an occupant's lane way values into `out` (cleared)
// and its current index into `index` (0 if none); returns the count.
int32_t __fastcall CopyLaneValues(void* self, void*, uint32_t occupant, uint32_t way, GameVector* out,
                                  uint16_t* index)
{
    static bool logged = false;
    FirstCall("CopyLaneValues", logged);
    out->end = out->begin;
    *index = 0;
    auto* const node = Find<LaneNode>(Table(self, field::kLanes), occupant);
    if (!node)
        return 0;
    LaneWay* const lane = WayOf(node, way);
    if (lane->values.begin == lane->values.end)
        return 0;
    reinterpret_cast<AssignFn>(addr::SC4Uint32Vector_CopyAssign)(out, &lane->values);
    *index = lane->index;
    return static_cast<int32_t>(static_cast<char*>(out->end) - static_cast<char*>(out->begin)) >> 2;
}

// 42 (0x004B0990): moves each of an occupant's four lane ways on to its next
// value (round robin). False without lanes.
uint8_t __fastcall AdvanceLanes(void* self, void*, uint32_t occupant)
{
    static bool logged = false;
    FirstCall("AdvanceLanes", logged);
    auto* const node = Find<LaneNode>(Table(self, field::kLanes), occupant);
    if (!node)
        return 0;
    for (LaneWay& lane : node->ways) {
        if (lane.values.begin == lane.values.end)
            continue;
        const auto count = static_cast<uint32_t>(
            static_cast<int32_t>(static_cast<char*>(lane.values.end) - static_cast<char*>(lane.values.begin)) >> 2);
        lane.index = static_cast<uint16_t>((static_cast<uint32_t>(lane.index) + 1) % count);
    }
    return 1;
}

// --- Positions onto the network ----------------------------------------------

// 43 (0x004AF000): true if the cell under `position` has an occupant taking
// `key` (its slot 9); otherwise moves the position to the nearest point of
// such a cell within two cells (first nearest, z outer, x inner), at that
// point's height (slot 18). False if none.
uint8_t __fastcall SnapToNetwork(void* self, void*, float* position, uint32_t key)
{
    static bool logged = false;
    FirstCall("SnapToNetwork", logged);
    const float fill = FloatBits(Global<uint32_t>(addr::SC4_ZeroVector3Value));
    float best[3] = {fill, fill, fill};
    float bestDistance = std::numeric_limits<float>::max();
    CellRecord* bestRecord = nullptr;
    const int32_t cellX = Ftol(static_cast<double>(position[0]) * kCellsPerUnit);
    const int32_t cellZ = Ftol(static_cast<double>(position[2]) * kCellsPerUnit);
    if (Matches(RecordsAt(self, cellX, cellZ), key))
        return 1;
    const int32_t lastZ = cellZ + 2;
    const int32_t lastX = cellX + 2;
    for (int32_t z = cellZ - 2; z <= lastZ; ++z) {
        for (int32_t x = cellX - 2; x <= lastX; ++x) {
            CellRecord* const record = RecordsAt(self, x, z);
            if (!Matches(record, key))
                continue;
            const double lowX = Times16(x);
            const float highX = static_cast<float>(Times16(x) + 16);
            const float px = position[0];
            const float clampX = px < lowX ? static_cast<float>(lowX) : (px > highX ? highX : px);
            const float clampY = position[1];
            const double lowZ = Times16(z);
            const float highZ = static_cast<float>(Times16(z) + 16);
            const float pz = position[2];
            const double clampZ = pz < lowZ ? lowZ : static_cast<double>(pz > highZ ? highZ : pz);
            const double dx = static_cast<double>(position[0]) - clampX;
            const double dy = static_cast<double>(position[1]) - clampY;
            const double dz = static_cast<double>(position[2]) - clampZ;
            const double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
            if (distance < bestDistance) {
                bestDistance = static_cast<float>(distance);
                best[0] = clampX;
                best[1] = clampY;
                best[2] = static_cast<float>(clampZ);
                bestRecord = record;
            }
        }
    }
    if (bestRecord) {
        position[1] = best[1];
        position[0] = best[0];
        position[2] = best[2];
        position[1] = static_cast<float>(Virtual<double>(self, 0x48, position[0], position[2]));
    }
    return bestRecord != nullptr;
}

// 44 (0x004AF260): true if the cell under `to` has an occupant taking `key`.
// Otherwise, moving from `from` (less than 1e-5 away: slot 43 on `to`), tries
// the points where the move leaves `from`'s cell (an x edge, shifted along z by
// the overshoot and the move's z sign; then a z edge, likewise): the first on
// a taking cell becomes `to` (true); with none, slot 43; else `to` is clamped
// into `from`'s cell (0.01 inside its far edges) (false). `to` gets its height
// (slot 18) except where slot 43 decides.
uint8_t __fastcall ClampMoveToNetwork(void* self, void*, const float* from, float* to, uint32_t key)
{
    static bool logged = false;
    FirstCall("ClampMoveToNetwork", logged);
    const int32_t cellX = Ftol(static_cast<double>(to[0]) * kCellsPerUnit);
    const int32_t cellZ = Ftol(static_cast<double>(to[2]) * kCellsPerUnit);
    if (Matches(RecordsAt(self, cellX, cellZ), key))
        return 1;
    const double dx = static_cast<double>(to[0]) - from[0];
    const double dy = static_cast<double>(to[1]) - from[1];
    const double dz = static_cast<double>(to[2]) - from[2];
    const float dzStored = static_cast<float>(dz);
    const double length = std::sqrt((dz * dzStored + dy * dy) + dx * dx);
    if (!(length > kMinMove))
        return Virtual<uint8_t>(self, 0xAC, to, key);

    const int32_t fromX = Ftol(static_cast<double>(from[0]) * kCellsPerUnit);
    const int32_t fromZ = Ftol(static_cast<double>(from[2]) * kCellsPerUnit);
    const float x0 = static_cast<float>(Times16(fromX));
    const float z0 = static_cast<float>(Times16(fromZ));
    const float x1 = static_cast<float>(Times16(fromX) + 16);
    const float z1 = static_cast<float>(Times16(fromZ) + 16);
    const double signX = dx < 0.0 ? -1.0 : 1.0;
    const double signZ = dzStored < 0.0f ? -1.0 : 1.0;

    const float fill = FloatBits(Global<uint32_t>(addr::SC4_ZeroVector3Value));
    float points[2][3] = {{fill, fill, fill}, {fill, fill, fill}};
    int count = 0;
    if (to[0] < x0) {
        points[0][0] = x0;
        points[0][1] = to[1];
        points[0][2] = static_cast<float>((static_cast<double>(x0) - to[0]) * signZ + to[2]);
        count = 1;
    } else if (to[0] >= x1) {
        points[0][1] = to[1];
        points[0][2] = static_cast<float>((static_cast<double>(to[0]) - x1) * signZ + to[2]);
        points[0][0] = static_cast<float>(static_cast<double>(x1) - kInset);
        count = 1;
    }
    if (to[2] < z0) {
        points[count][0] = to[0];
        points[count][1] = to[1];
        points[count][2] = to[2];
        points[count][0] = static_cast<float>((static_cast<double>(z0) - to[2]) * signX + points[count][0]);
        points[count][2] = z0;
        ++count;
    } else if (to[2] >= z1) {
        points[count][0] = to[0];
        points[count][1] = to[1];
        points[count][2] = to[2];
        points[count][0] = static_cast<float>((static_cast<double>(to[2]) - z1) * signX + points[count][0]);
        points[count][2] = static_cast<float>(static_cast<double>(z1) - kInset);
        ++count;
    }
    if (count == 0)
        return Virtual<uint8_t>(self, 0xAC, to, key);

    uint8_t found = 0;
    for (int i = 0; i < count; ++i) {
        const int32_t x = Ftol(static_cast<double>(points[i][0]) * kCellsPerUnit);
        const int32_t z = Ftol(static_cast<double>(points[i][2]) * kCellsPerUnit);
        if (Matches(RecordsAt(self, x, z), key)) {
            to[0] = points[i][0];
            to[1] = points[i][1];
            to[2] = points[i][2];
            found = 1;
            break;
        }
    }
    if (!found) {
        const double limitX = static_cast<double>(x1) - kInset;
        const float tx = to[0];
        to[0] = tx < x0 ? x0 : (tx > limitX ? static_cast<float>(limitX) : tx);
        const double limitZ = static_cast<double>(z1) - kInset;
        const float tz = to[2];
        to[2] = tz < z0 ? z0 : (tz > limitZ ? static_cast<float>(limitZ) : tz);
    }
    to[1] = static_cast<float>(Virtual<double>(self, 0x48, to[0], to[2]));
    return found;
}

// --- Install -----------------------------------------------------------------

constexpr const char* kGroup = "simulators.trafficmap.interface";

const Slot kMainSlots[] = {
    OPENSC4_TM_SLOT(0, 0x004AE350, QueryInterface),
    OPENSC4_TM_SLOT(2, 0x004AFAA0, Release),
    OPENSC4_TM_SLOT(7, 0x004AE190, FindOccupantAtPosition),
    OPENSC4_TM_SLOT(8, 0x004B0B20, FindOccupantInCell),
    OPENSC4_TM_SLOT(9, 0x004AF970, OccupantAtPosition),
    OPENSC4_TM_SLOT(10, 0x004AE160, OccupantAtHeight),
    OPENSC4_TM_SLOT(11, 0x004AE140, OccupantAt),
    OPENSC4_TM_SLOT(12, 0x004AFA10, RecordAtHeight),
    OPENSC4_TM_SLOT(13, 0x004B3AC0, CellRecords),
    OPENSC4_TM_SLOT(14, 0x004AE0C0, CheckedCellRecords),
    OPENSC4_TM_SLOT(15, 0x004AF7C0, NeighborRecord),
    OPENSC4_TM_SLOT(16, 0x004AE0F0, RecordOf),
    OPENSC4_TM_SLOT(18, 0x004AF630, HeightAt),
    OPENSC4_TM_SLOT(19, 0x004B4AF0, LaneValue4),
    OPENSC4_TM_SLOT(20, 0x004B2970, PathValue),
    OPENSC4_TM_SLOT(21, 0x004B4BF0, LaneValue8),
    OPENSC4_TM_SLOT(22, 0x004B28C0, PathCount),
    OPENSC4_TM_SLOT(23, 0x004B4CF0, LaneCount),
    OPENSC4_TM_SLOT(24, 0x004AE1D0, LaneCountOf),
    OPENSC4_TM_SLOT(25, 0x004B2A20, OccupantsNearRect),
    OPENSC4_TM_SLOT(26, 0x004B2D10, OccupantsNearOccupant),
    OPENSC4_TM_SLOT(27, 0x004B0000, CountAutomata),
    OPENSC4_TM_SLOT(28, 0x004AE090, OccupantAllows),
    OPENSC4_TM_SLOT(29, 0x004AE030, CellAllows),
    OPENSC4_TM_SLOT(30, 0x004B1C00, AddAutomaton),
    OPENSC4_TM_SLOT(31, 0x004B02A0, RemoveAutomaton),
    OPENSC4_TM_SLOT(34, 0x004AE000, ModelCheck),
    OPENSC4_TM_SLOT(40, 0x004B0930, CurrentLaneValue),
    OPENSC4_TM_SLOT(41, 0x004B1B60, CopyLaneValues),
    OPENSC4_TM_SLOT(42, 0x004B0990, AdvanceLanes),
    OPENSC4_TM_SLOT(43, 0x004AF000, SnapToNetwork),
    OPENSC4_TM_SLOT(44, 0x004AF260, ClampMoveToNetwork),
};

const Slot kUnknownSlots[] = {
    OPENSC4_TM_SLOT(0, 0x004B3AE0, QueryInterface4),
    OPENSC4_TM_SLOT(2, 0x004B3B10, Release4),
};

const Slot kMessageSlots[] = {
    OPENSC4_TM_SLOT(0, 0x004B3AF0, QueryInterface8),
    OPENSC4_TM_SLOT(2, 0x004B3B20, Release8),
};

const Slot kSerializableSlots[] = {
    OPENSC4_TM_SLOT(0, 0x004B3B00, QueryInterfaceC),
    OPENSC4_TM_SLOT(2, 0x004B3B30, ReleaseC),
};

struct VtablePatch {
    std::uintptr_t vtable;
    int size;
    const Slot* slots;
    std::size_t count;
};

const VtablePatch kTables[] = {
    {addr::cSC4TrafficNetworkMap_vftable, 52, kMainSlots, std::size(kMainSlots)},
    {addr::cSC4TrafficNetworkMap_vftable_0x4, 3, kUnknownSlots, std::size(kUnknownSlots)},
    {addr::cSC4TrafficNetworkMap_vftable_0x8, 4, kMessageSlots, std::size(kMessageSlots)},
    {addr::cSC4TrafficNetworkMap_vftable_0xC, 6, kSerializableSlots, std::size(kSerializableSlots)},
};

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    // Check every table first so a mismatch leaves all four untouched.
    for (const VtablePatch& table : kTables) {
        const auto* const entries = reinterpret_cast<const std::uintptr_t*>(table.vtable);
        for (std::size_t i = 0; i < table.count; ++i) {
            if (entries[table.slots[i].index] != table.slots[i].original) {
                log::Write("%s: slot %d of %08X holds %08X, expected %08X; not installing", kGroup,
                           table.slots[i].index, static_cast<unsigned>(table.vtable),
                           static_cast<unsigned>(entries[table.slots[i].index]),
                           static_cast<unsigned>(table.slots[i].original));
                return false;
            }
        }
    }
    for (const VtablePatch& table : kTables)
        if (!PatchSlots(kGroup, table.vtable, table.size, table.slots, table.count))
            return false;
    return true;
}

const bool g_registered = registry::Register({
    kGroup,
    "simulators/trafficmap",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficNetworkMap_vftable,
    "Our cSC4TrafficNetworkMap queries: identity, cells, occupants, heights, lane and path entries, automata lists, "
    "lane round robins, snapping onto the network (docs/traffic-map.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::trafficmap
