// Shared between the parts of our cSC4TrafficNetworkMap (build 641;
// docs/traffic-map.md): the object's fields, the records it keeps and the
// game's container layouts its code walks.
//
// The map is the automata's view of the transport networks: a grid with one
// list of records per city cell (one record per network occupant on the
// cell), the traffic signals of intersections, the lanes of multi-way
// occupants and the objects waiting on cells. Vehicles, trains and
// pedestrians ask it where they can go; the transit simulation itself is
// cSC4TrafficSimulator (docs/traffic.md).
//
// Interfaces (offsets from the object's start): +0x0 cISC4TrafficNetworkMap
// (vtable 0x00A8FED8, 52 slots), +0x4 cIGZUnknown (0x00A8FEC8), +0x8
// cIGZMessageTarget2 (0x00A8FEB8, DoMessage in slot 3), +0xC
// cIGZSerializable (0x00A8FEA0: Write 3, Read 4, GetGZCLSID 5).
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::trafficmap {

// Fields of cSC4TrafficNetworkMap (offsets from the object's start; 0xA90
// bytes, made by 0x004B42E0 / constructor 0x004B3EB0).
namespace field {
constexpr std::size_t kInitialized = 0x10;   // uint8: Init ran
constexpr std::size_t kRefCount = 0x14;
constexpr std::size_t kGrid = 0x18;          // CellRecord** [width x height], row z at z << shift
constexpr std::size_t kWidth = 0x1C;         // int32 cells (the city's, slot 0x174)
constexpr std::size_t kHeight = 0x20;        // int32 cells (slot 0x178)
constexpr std::size_t kShift = 0x24;         // uint8: 1 << shift == width
constexpr std::size_t kFlag25 = 0x25;        // uint8: Init re-registers tunnel occupants (0x004ADF50)
constexpr std::size_t kSignals = 0x28;       // Hashtable of SignalNode, keyed by occupant
constexpr std::size_t kSignalsChanged = 0x3C;// uint8: slot 32 refreshes the waiting lists (0x004B2830)
constexpr std::size_t kLanes = 0x40;         // Hashtable of LaneNode, keyed by occupant
constexpr std::size_t kUnknown54 = 0x54;     // int32, zeroed by the constructor
constexpr std::size_t kUnknown58 = 0x58;     // int32, zeroed by the constructor
constexpr std::size_t kRectDirty = 0x5C;     // uint8: message 0x69247DC7 (traffic map changed)
constexpr std::size_t kRect = 0x60;          // int32 x0, z0, x1, z1: the cells around the view
constexpr std::size_t kViewPoint = 0x70;     // 3 floats, copied on message 0xA6B79612
constexpr std::size_t kRandom = 0x7C;        // the map's own Mersenne twister (0x9C8 bytes, seeded -1)
constexpr std::size_t kView = 0xA44;         // the global at 0x00B43DD8 (Init)
constexpr std::size_t kOccupantManager = 0xA48; // the global at 0x00B43D0C (Init, referenced)
constexpr std::size_t kFilterA = 0xA4C;      // occupant filter, vtables 0x00A8FE14/0x00A8FDFC
constexpr std::size_t kFilterB = 0xA50;      // occupant filter, vtables 0x00A8FE48/0x00A8FE30
constexpr std::size_t kPaused = 0xA54;       // uint8: messages 0xCAD61E00 (set) / 0xEAD61E16 (cleared)
constexpr std::size_t kWalkX = 0xA58;        // int32, -1 at first: slot 32's cell walk
constexpr std::size_t kWalkZ = 0xA5C;        // int32
constexpr std::size_t kBlinkTimerA = 0xA60;  // float
constexpr std::size_t kBlinkA = 0xA64;       // uint8, 1 at first
constexpr std::size_t kBlinkTimerB = 0xA68;  // float
constexpr std::size_t kBlinkB = 0xA6C;       // uint8, 1 at first
constexpr std::size_t kCellObjects = 0xA70;  // Hashtable of CellObjectsNode, keyed by x << 16 | z
constexpr std::size_t kUpdateX = 0xA84;      // int32, -1 at first: 0x004AE6D0's cell walk
constexpr std::size_t kUpdateZ = 0xA88;      // int32, -1 at first
constexpr std::size_t kUpdateTimer = 0xA8C;  // float
constexpr std::size_t kSize = 0xA90;
} // namespace field

// The game's std::vector (begin, end, end of storage).
struct GameVector {
    void* begin;
    void* end;
    void* capacity;
};

// The game's (SGI) hash table: the empty hasher and key-equal in the first
// word, a vector of bucket heads, the element count. Nodes start with the
// next pointer and the key.
struct Hashtable {
    uint32_t functors;
    void** buckets;
    void** bucketsEnd;
    void** bucketsCapacity;
    uint32_t count;
};

inline uint32_t BucketCount(const Hashtable& table)
{
    return static_cast<uint32_t>(table.bucketsEnd - table.buckets);
}

// The node with `key`, or null (the game's inline find).
template <typename Node>
inline Node* Find(const Hashtable& table, uint32_t key)
{
    for (auto* node = static_cast<Node*>(table.buckets[key % BucketCount(table)]); node; node = node->next)
        if (node->key == key)
            return node;
    return nullptr;
}

// The first node in bucket order (0x004B0390), or null.
template <typename Node>
inline Node* First(const Hashtable& table)
{
    for (uint32_t i = 0, n = BucketCount(table); i < n; ++i)
        if (table.buckets[i])
            return static_cast<Node*>(table.buckets[i]);
    return nullptr;
}

// The node after `node` in bucket order (the game's iterator ++), or null.
template <typename Node>
inline Node* Next(const Hashtable& table, Node* node)
{
    if (node->next)
        return node->next;
    for (uint32_t i = node->key % BucketCount(table) + 1, n = BucketCount(table); i < n; ++i)
        if (table.buckets[i])
            return static_cast<Node*>(table.buckets[i]);
    return nullptr;
}

// The game's circular list (sentinel node of 0xC bytes from SC4::stl_allocate).
struct ListNode {
    ListNode* next;
    ListNode* prev;
    void* value;
};

// One network occupant on one cell (0x58 bytes, from the pool at 0x00B0958C;
// slot 48 makes one per cell the occupant covers).
struct CellRecord {
    void* occupant;               // +0x00 cISC4NetworkOccupant, referenced
    GameVector lanes;             // +0x04 12 GameVectors of 16-byte entries, by travel type - 1 (slots 21, 23)
    GameVector paths;             // +0x10 12-byte entries {uint32 key, float, uint16} (slots 20, 22)
    ListNode* automata;           // +0x1C list of referenced objects
    uint32_t flags20;             // +0x20
    uint32_t flags24;             // +0x24
    uint32_t flags28;             // +0x28
    uint16_t approachTraffic[4];  // +0x2C
    uint16_t turnTraffic[4][4];   // +0x34
    CellRecord* next;             // +0x54
};
static_assert(sizeof(CellRecord) == 0x58);

// One approach of a signalled intersection (0x14 bytes).
struct Approach {
    int32_t kind;    // 0 none, 1 stop sign?, 2 signal, 3 other (0x004AE9E0)
    uint8_t green;   // +0x4
    uint8_t amber;   // +0x5
    uint8_t pad[2];
    float greenTime; // +0x8
    float redTime;   // +0xC
    float timer;     // +0x10
};
static_assert(sizeof(Approach) == 0x14);

// A signalled intersection (hash node, 0x70 bytes).
struct SignalNode {
    SignalNode* next;
    uint32_t key;            // the occupant (cISC4NetworkOccupant*)
    Approach approaches[4];  // +0x08
    uint8_t byte58;          // +0x58
    uint8_t pad59[3];
    GameVector objects;      // +0x5C referenced objects
    ListNode* waiting;       // +0x68 list of referenced automata
    uint8_t changed;         // +0x6C
    uint8_t pad6D[3];
};
static_assert(sizeof(SignalNode) == 0x70);

// One way of a lane record: a vector of uint32 and a round-robin index.
struct LaneWay {
    GameVector values;
    uint16_t index;
    uint16_t pad;
};

// The lanes of an occupant (hash node, 0x48 bytes).
struct LaneNode {
    LaneNode* next;
    uint32_t key;  // the occupant
    LaneWay ways[4];
};
static_assert(sizeof(LaneNode) == 0x48);

// The objects on a cell (hash node, 0x14 bytes).
struct CellObjectsNode {
    CellObjectsNode* next;
    uint32_t key;  // x << 16 | z
    GameVector objects;  // referenced object pointers
};
static_assert(sizeof(CellObjectsNode) == 0x14);

inline Hashtable& Table(void* self, std::size_t offset) { return At<Hashtable>(self, offset); }

// The record list of a cell, unchecked (slot 13, 0x004B3AC0).
inline CellRecord*& CellAt(void* self, int32_t x, int32_t z)
{
    auto** grid = At<CellRecord**>(self, field::kGrid);
    return grid[(z << At<uint8_t>(self, field::kShift)) + x];
}

// Our signal helpers (trafficmap_signals.cpp) that the other parts call too.
// 0x004AE850: pushes the approaches' states to the intersection's objects.
void ApplySignals(void* occupant, SignalNode* node, uint8_t flag);
// 0x004AFC20: sets up a new intersection's approaches.
void SetUpSignal(void* self, void* occupant, SignalNode* node);
// 0x004B2640: rebuilds an intersection's list of objects from the occupant manager.
void RefreshSignalObjects(void* self, void* occupant, SignalNode* node);

// Logs the first call of one of our functions ("trafficmap: first <name>").
void FirstCall(const char* name, bool& logged);

// One vtable slot to swap: its index, the function the game has there, ours.
struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_TM_SLOT(index, original, fn) ::opensc4::trafficmap::Slot{index, original, reinterpret_cast<void*>(&fn)}

// Swaps `count` slots of the vtable at `vtable` (which has `size` slots) after
// checking each holds `original`. Logs and returns false (nothing changed) if
// one does not.
bool PatchSlots(const char* group, std::uintptr_t vtable, int size, const Slot* slots, std::size_t count);

} // namespace opensc4::trafficmap
