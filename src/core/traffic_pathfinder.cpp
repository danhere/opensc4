// The traffic simulator's path finder (vtable 0x00AB2150, constructor
// 0x006D89C0; docs/traffic.md, "The path finder"): our Search
// (0x006D91B0) and its node pool, heap and estimate helpers. It works on
// the game's object; the goal checks and the route's finish are still the
// game's.
//
// Search holds a patch site of the Network Addon Mod (0x006D95B1); with
// this replacement on, that patch is not in effect.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::traffic_sim {
namespace {

// A search node (0x24 bytes, in pages of 0x24000 at +0x3148).
struct Node {
    uint32_t key;     // (parent side x 4 | side) << 24 | type << 16 | x << 8 | z, bit 31 outside the city
    Node* parent;
    float g;          // cost so far
    float f;          // g + estimate: the heap's key
    uint8_t open;     // in the heap
    uint8_t closed;
    uint8_t side;     // the side it was entered from
    uint8_t type;     // travel type
    int16_t x;
    int16_t z;
    Node* next;       // in its hash bucket
    uint32_t bucket;
    uint32_t heapIndex;
};
static_assert(sizeof(Node) == 0x24);

// std::vector<uint8>.
struct ByteVector {
    uint8_t* begin;
    uint8_t* end;
    uint8_t* capacity;
};

// The search's deadline: as the time-sliced phases' (SlicedDeadline).
struct Deadline {
    uint32_t end[2];
    uint32_t count;
    uint32_t step;
    uint8_t timedOut;
    uint8_t padding[3];
    uint32_t* stepGlobal;
};

// Fields of the path finder (offsets from the object's start).
namespace field {
constexpr std::size_t kSimulator = 0xC;
constexpr std::size_t kBest = 0x10;          // Node*: where the search ended
constexpr std::size_t kEstimateScale = 0x14; // float
constexpr std::size_t kSourceRect = 0x18;    // int32[4]
constexpr std::size_t kGoalRect = 0x28;      // int32[4]
constexpr std::size_t kGrownRect = 0x38;     // int32[4]: the source rectangle grown by 4
constexpr std::size_t kUnknown48 = 0x48;     // int32[4], -2 on a new search
constexpr std::size_t kFound = 0x58;         // int32 x, z: the goal cell found
constexpr std::size_t kCostOffset = 0x60;    // float, added to each type's cost limit; the route's cost after
constexpr std::size_t kTypeMask = 0x64;      // uint16: types a switch may go to
constexpr std::size_t kGoalTypes = 0x66;     // uint16
constexpr std::size_t kCount = 0x68;         // the trip's count
constexpr std::size_t kResult = 0x6C;        // trips the goal takes
constexpr std::size_t kFlags = 0x70;
constexpr std::size_t kRoute = 0x74;  // the route deque, when the caller wants the cells
constexpr std::size_t kMode = 0x80;
constexpr std::size_t kKinds = 0x84;         // uint8: trip kinds a mode 0 goal may take
constexpr std::size_t kAccepts = 0x88;       // mode 2: an object whose slot 3 accepts a network object
constexpr std::size_t kKind = 0x8C;          // the kind the goal took
constexpr std::size_t kCostLimits = 0x90;    // float* (9), or the simulator's at +0xC78
constexpr std::size_t kLimits = 0x94;        // float[9]
constexpr std::size_t kWidth = 0xB8;
constexpr std::size_t kDepth = 0xBC;
constexpr std::size_t kBudget = 0xC0;        // ms, 0 for no deadline
constexpr std::size_t kResume = 0xC4;        // uint8: the last search ran out of time
constexpr std::size_t kOverLimit = 0xC8;     // uint32[9]
constexpr std::size_t kBlockedTypes = 0xEC;  // uint8
constexpr std::size_t kExitX = 0xEE;         // int16
constexpr std::size_t kExitZ = 0xF0;         // int16
constexpr std::size_t kExitSide = 0xF2;      // uint8
constexpr std::size_t kColumnDistances = 0x10C; // int32*: from index -1
constexpr std::size_t kRowDistances = 0x110;    // int32*: from index -1
constexpr std::size_t kHeap = 0x114;
constexpr std::size_t kHeapSize = 0x120;
constexpr std::size_t kBucketsEnd = 0x128;   // uint32*: end of the buckets from +0x12C
constexpr std::size_t kPool = 0x128;       // the node hash and pool
constexpr std::size_t kBuckets = 0x12C;
} // namespace field

constexpr uint32_t kFlagAnyGoal = 0x2;
constexpr uint32_t kFlagKeep = 0x4;
constexpr uint32_t kFlagExit = 0x8;
constexpr uint32_t kFlagNoLinks = 0x10;

using SelfBoolFn = uint8_t(__thiscall*)(void* self);
using SelfFn = void(__thiscall*)(void* self);
using AddNodeFn = void(__thiscall*)(void* self, int32_t x, int32_t z, uint32_t side, uint32_t type, float cost,
                                    Node* parent, float limit, uint32_t outside);
using IsGoalFn = uint8_t(__thiscall*)(void* self, int32_t x, int32_t z, uint32_t type, uint32_t noTransit);
using PopFn = Node*(__thiscall*)(void* heap);
using PushFn = void(__thiscall*)(void* heap, Node* node);
using DistanceFn = double(__thiscall*)(void* self, int32_t x, int32_t z, int32_t x0, int32_t z0, int32_t x1,
                                       int32_t z1); // ST0
using IdCostFn = double(__thiscall*)(void* simulator, uint32_t id); // ST0
using EvaluateFn = double(__thiscall*)(void* curve, float x);       // ST0
using DeadlineStartFn = void(__thiscall*)(Deadline* deadline, uint32_t ms);
using DeadlineFn = uint8_t(__thiscall*)(Deadline* deadline);

void AddNode(void* self, int32_t x, int32_t z, uint32_t side, uint32_t type, float cost, Node* parent, float limit,
             uint32_t outside)
{
    reinterpret_cast<AddNodeFn>(addr::TrafficPathfinder_AddNode)(self, x, z, side, type, cost, parent, limit, outside);
}

// The edge of a network at a cell (cSC4TrafficSimulator::EdgeOfNetwork).
char* EdgeAt(void* simulator, uint32_t network, int32_t x, int32_t z)
{
    char* edge = reinterpret_cast<char**>(At<char**>(simulator, 0x7C)[3 * x])[z];
    for (; edge; edge = At<char*>(edge, 0x14))
        if (static_cast<uint8_t>(edge[6]) == network)
            return edge;
    return nullptr;
}

template <typename T>
T& Cell(void* grid, int32_t x, int32_t z)
{
    return reinterpret_cast<T*>(static_cast<char***>(grid)[0][3 * x])[z];
}

// The side of (x, z) a neighbour (nx, nz) is on, as Search computes it.
uint32_t SideTo(int32_t x, int32_t z, int32_t nx, int32_t nz)
{
    if (x < nx)
        return 0;
    if (x > nx)
        return 2;
    return z >= nz ? 3 : 1;
}

float Limit(void* self, uint32_t type)
{
    return At<float>(self, field::kLimits + 4 * type);
}

// The cost of an edge for a travel type: the link cost by the edge's
// kind, through the travel time curve at the cell's traffic when the type
// uses it, plus the edge's own cost.
float EdgeCost(void* const* grids, char* edge, uint32_t type, uint32_t network, int32_t x, int32_t z)
{
    const float cost = Global<float>(addr::SC4Traffic_LinkCostTables + 4 * (type * 13 + static_cast<uint8_t>(edge[5])));
    double total = cost;
    if (Global<uint8_t>(addr::SC4Traffic_TravelTypeEnabled + type)) {
        const auto load = static_cast<float>(static_cast<double>(Cell<float>(grids[network], x, z)) * At<float>(edge, 0));
        total = reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(
                    reinterpret_cast<void*>(addr::SC4Traffic_TravelTimeCurve), load) * cost;
    }
    return static_cast<float>(total + At<float>(edge, 8));
}

uint32_t Finish(Deadline& deadline, uint32_t result)
{
    reinterpret_cast<SelfFn>(addr::SC4Deadline_End)(&deadline);
    return result;
}

uint32_t __fastcall Search(void* self, void*)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("traffic: first path finder Search");
    }
    char* const object = static_cast<char*>(self);
    uint32_t* const step = &Global<uint32_t>(addr::TrafficPathfinder_SearchStep);
    if (!(step[2] & 1)) {
        step[2] |= 1;
        step[0] = 0x400;
        step[1] = 0;
    }
    Deadline deadline{};
    reinterpret_cast<DeadlineStartFn>(addr::SC4Deadline_Start)(&deadline, At<uint32_t>(self, field::kBudget));
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    const bool keep = (flags & kFlagKeep) != 0;
    const bool exit = (flags & kFlagExit) != 0;
    deadline.step = step[0];
    deadline.count = 0;
    deadline.timedOut = 0;
    deadline.stepGlobal = step;
    At<uint32_t>(self, field::kResult) = 0;
    if (!exit) {
        At<int16_t>(self, field::kExitX) = -2;
        At<int16_t>(self, field::kExitZ) = -2;
    }
    void* const simulator = At<void*>(self, field::kSimulator);
    const float* const costs = At<float*>(self, field::kCostLimits) ? At<float*>(self, field::kCostLimits)
                                                                    : &At<float>(simulator, 0xC78);
    for (int i = 0; i < 9; ++i)
        At<float>(self, field::kLimits + 4 * i) = costs[i] + At<float>(self, field::kCostOffset);
    if (!At<uint8_t>(self, field::kResume)) {
        if (!keep) {
            for (int i = 0; i < 4; ++i)
                At<int32_t>(self, field::kUnknown48 + 4 * i) = -2;
            Virtual<void>(self, 0x7C); // Reset
        }
        At<Node*>(self, field::kBest) = nullptr;
        for (int i = 0; i < 9; ++i)
            At<uint32_t>(self, field::kOverLimit + 4 * i) = 0;
        At<uint8_t>(self, field::kBlockedTypes) = 0;
    }
    void* const ids = Virtual<void*>(At<void*>(simulator, 0x704), 0x60);
    Virtual<void*>(At<void*>(simulator, 0x700), 0x60);
    uint32_t mask = At<uint16_t>(self, field::kTypeMask);
    const bool strike = At<uint32_t>(simulator, 0x7A4) == 1;
    if (strike)
        mask &= 0x33;
    const bool noLinks = (flags & kFlagNoLinks) != 0;
    if (!At<uint8_t>(self, field::kResume)) {
        for (int i = 0; i < 4; ++i)
            At<int32_t>(self, field::kGrownRect + 4 * i) = At<int32_t>(self, field::kSourceRect + 4 * i) + (i < 2 ? -4 : 4);
        const uint32_t mode = At<uint32_t>(self, field::kMode);
        if (mode == 0) {
            At<float>(self, field::kEstimateScale) = At<float>(simulator, 0xCF8);
            if (!reinterpret_cast<SelfBoolFn>(addr::TrafficPathfinder_SetUpGoals)(self))
                return Finish(deadline, 0);
            reinterpret_cast<SelfFn>(addr::TrafficPathfinder_BuildDistanceTables)(self);
            if (keep)
                reinterpret_cast<SelfFn>(addr::TrafficPathfinder_Rescore)(self);
        } else if (mode == 3) {
            reinterpret_cast<SelfFn>(addr::TrafficPathfinder_BuildDistanceTables)(self);
            At<float>(self, field::kEstimateScale) = At<float>(simulator, 0xCF8);
        } else {
            reinterpret_cast<SelfFn>(addr::TrafficPathfinder_BuildDistanceTables)(self);
            At<float>(self, field::kEstimateScale) = 1.0f;
        }
        if (At<uint16_t>(self, field::kGoalTypes) == 0)
            return 0;
        if (!keep && !reinterpret_cast<SelfBoolFn>(addr::TrafficPathfinder_AddStartNodes)(self))
            return Finish(deadline, 0);
    }
    void* grids[6];
    for (int i = 0; i < 6; ++i)
        grids[i] = Virtual<void*>(At<void**>(simulator, 0x164)[i], 0x60);
    At<uint8_t>(self, field::kResume) = 0;

    const auto* const dx = &Global<int32_t>(addr::TrafficPathfinder_SideDX);
    const auto* const dz = &Global<int32_t>(addr::TrafficPathfinder_SideDZ);
    const auto* const networkOf = &Global<uint32_t>(addr::TrafficPathfinder_NetworkOfType);
    const uint32_t width = At<uint32_t>(self, field::kWidth);
    const uint32_t depth = At<uint32_t>(self, field::kDepth);
    auto inMap = [width, depth](int32_t x, int32_t z) {
        return static_cast<uint32_t>(x) < width && static_cast<uint32_t>(z) < depth;
    };
    while (At<uint32_t>(self, field::kHeapSize)) {
        Node* const node = reinterpret_cast<PopFn>(addr::TrafficPathfinder_PopMin)(object + field::kHeap);
        const uint32_t type = node->type;
        const int32_t x = node->x;
        const int32_t z = node->z;
        const bool inside = inMap(x, z);
        uint8_t noTransit = At<uint8_t>(simulator, 0xC3D + type) == 0;
        if (!noTransit && inside) {
            if (char* const edge = EdgeAt(simulator, networkOf[type], x, z))
                noTransit = Global<uint8_t>(addr::TrafficPathfinder_TransitOfEdgeKind + static_cast<uint8_t>(edge[5])) == 0;
        }
        if (!(inside && Cell<uint32_t>(ids, x, z) != 0) &&
            reinterpret_cast<IsGoalFn>(addr::TrafficPathfinder_IsGoal)(self, x, z, type, noTransit)) {
            reinterpret_cast<PushFn>(addr::TrafficPathfinder_HeapPush)(object + field::kHeap, node);
            At<Node*>(self, field::kBest) = node;
            reinterpret_cast<SelfFn>(addr::TrafficPathfinder_FinishRoute)(self);
            return Finish(deadline, 1);
        }
        if (inside) {
            char* const record = At<char*>(simulator, 0x58) + ((x << 8) | z) * 0x2E;
            uint32_t ways = (At<uint16_t>(record, 2 * type) >> (4 * node->side)) & 0xF;
            uint32_t switches = At<uint16_t>(record, 0x12 + 2 * type);
            const bool roadParent = networkOf[type] == 1 && node->parent;
            if (Global<uint8_t>(addr::TrafficPathfinder_NoReverseOfType + type) && node->parent)
                ways &= ~((1u << node->parent->side) | (1u << node->side));
            for (uint32_t direction = 0; direction < 4; ++direction, ways >>= 1, switches >>= 4) {
                if (ways & 1) {
                    const int32_t nx = x + dx[direction];
                    const int32_t nz = z + dz[direction];
                    if (inMap(nx, nz)) {
                        const uint32_t side = SideTo(x, z, nx, nz);
                        const uint32_t network = networkOf[type];
                        if (char* const edge = EdgeAt(simulator, network, nx, nz)) {
                            if (!roadParent ||
                                (At<uint16_t>(record, 0x24 + 2 * node->side) & (1u << (node->parent->side + 4 * direction))))
                                AddNode(self, nx, nz, side, type, EdgeCost(grids, edge, type, network, nx, nz), node,
                                        Limit(self, type), 0);
                        } else if (const uint32_t id = Cell<uint32_t>(ids, nx, nz)) {
                            const auto cost = static_cast<float>(
                                reinterpret_cast<IdCostFn>(addr::cSC4TrafficSimulator_CostOfRouteId)(simulator, id));
                            AddNode(self, nx, nz, side, type, cost, node, Limit(self, type), 0);
                        }
                    } else {
                        AddNode(self, x, z, SideTo(nx, nz, x, z), type, node->f - node->g, node, Limit(self, type), 1);
                    }
                }
                const uint32_t to = switches & 0xF;
                if (to == 0xF || !(mask & (1u << to)))
                    continue;
                const int32_t nx = x + dx[direction];
                const int32_t nz = z + dz[direction];
                const uint32_t side = SideTo(x, z, nx, nz);
                if (const uint32_t id = Cell<uint32_t>(ids, nx, nz)) {
                    const auto cost = static_cast<float>(
                        reinterpret_cast<IdCostFn>(addr::cSC4TrafficSimulator_CostOfRouteId)(simulator, id));
                    AddNode(self, nx, nz, side, to, cost, node, Limit(self, to), 0);
                } else if (char* const edge = EdgeAt(simulator, networkOf[to], nx, nz)) {
                    AddNode(self, nx, nz, side, to, EdgeCost(grids, edge, to, networkOf[to], nx, nz), node,
                            Limit(self, to), 0);
                }
            }

            const uint8_t special = At<uint8_t>(record, 0x2C);
            if (special) {
                const auto key = static_cast<uint16_t>(((x & 0xFF) << 8) | z);
                auto find = [key](void* table) -> char* {
                    char** const buckets = At<char**>(table, 4);
                    const auto count = static_cast<uint32_t>(At<char**>(table, 8) - buckets);
                    for (char* entry = buckets[key % count]; entry; entry = At<char*>(entry, 0))
                        if (At<uint16_t>(entry, 4) == key)
                            return entry;
                    return nullptr;
                };
                if (special & 2) {
                    if (char* const entry = find(&At<char>(simulator, 0xC8))) {
                        if (At<uint16_t>(entry, 0xA) & (1u << type))
                            AddNode(self, static_cast<uint8_t>(entry[8]), static_cast<uint8_t>(entry[9]), node->side,
                                    type, At<float>(entry, 0xC + 4 * type), node, Limit(self, type), 0);
                    }
                }
                if (!strike) {
                    if (special & 1) {
                        const uint32_t other = type == 6 ? 0 : 6;
                        for (uint32_t side = 0; side < 4; ++side)
                            if (static_cast<uint16_t>(At<uint16_t>(record, 2 * type) >> (4 * node->side)))
                                AddNode(self, x, z, side, other, 0.0f, node, Limit(self, other), 0);
                    }
                    if (!noLinks && (special & 4)) {
                        if (char* const entry = find(&At<char>(simulator, 0xDC))) {
                            char* const links = At<char*>(entry, 8) + 8;
                            if (At<uint16_t>(links, 0xC) & (1u << type)) {
                                char* const first = At<char*>(links, 0x10);
                                for (char* item = At<char*>(first, 0); item != first; item = At<char*>(item, 0))
                                    AddNode(self, At<int16_t>(item, 0xA), At<int16_t>(item, 0xC), 0, type,
                                            At<float>(item, 0x10), node, Limit(self, type), 0);
                                char* const second = At<char*>(links, 0x14);
                                for (char* item = At<char*>(second, 0); item != second; item = At<char*>(item, 0)) {
                                    const uint32_t side = SideTo(At<int16_t>(item, 8), At<int16_t>(item, 0xA),
                                                                 At<int16_t>(item, 0xC), At<int16_t>(item, 0xE));
                                    AddNode(self, At<int16_t>(item, 0xC), At<int16_t>(item, 0xE), side, type,
                                            At<float>(item, 0x10), node, Limit(self, type), 1);
                                }
                            }
                        }
                    }
                }
            }
            if (exit && x == At<int16_t>(self, field::kExitX) && z == At<int16_t>(self, field::kExitZ)) {
                const uint8_t direction = At<uint8_t>(self, field::kExitSide);
                const int32_t nx = static_cast<int16_t>(x + dx[direction]);
                const int32_t nz = static_cast<int16_t>(z + dz[direction]);
                AddNode(self, nx, nz, SideTo(x, z, nx, nz), 1, 0.0f, node, Limit(self, type), !inMap(nx, nz));
            }
        }
        node->closed = 1;
        node->open = 0;
        if (At<uint32_t>(self, field::kBudget)) {
            deadline.count += deadline.step;
            if (!(deadline.count & 0x3FF) &&
                reinterpret_cast<DeadlineFn>(addr::SC4Deadline_Expired)(&deadline)) {
                At<uint8_t>(self, field::kResume) = 1;
                return Finish(deadline, 2);
            }
        }
    }

    // Out of nodes: with flag 2 in mode 3, the node nearest the goal.
    if ((flags & kFlagAnyGoal) && At<uint32_t>(self, field::kMode) == 3 && At<uint32_t>(self, field::kRoute)) {
        Node** const end = At<Node**>(self, field::kBucketsEnd);
        auto firstFrom = [end](Node** bucket) -> Node* {
            for (; bucket < end; ++bucket)
                if (*bucket)
                    return *bucket;
            return nullptr;
        };
        Node* best = firstFrom(&At<Node*>(self, field::kBuckets));
        if (best) {
            const auto* const rect = &At<int32_t>(self, field::kGoalRect);
            const auto distance = reinterpret_cast<DistanceFn>(addr::TrafficPathfinder_DistanceToRect);
            auto bestDistance = static_cast<float>(distance(self, best->x, best->z, rect[0], rect[1], rect[2], rect[3]));
            auto next = [&](Node* at) {
                return at->next ? at->next : firstFrom(&At<Node*>(self, field::kBuckets + 4 + 4 * at->bucket));
            };
            for (Node* at = next(best); at; at = next(at)) {
                const double d = distance(self, at->x, at->z, rect[0], rect[1], rect[2], rect[3]);
                if (d < bestDistance) {
                    bestDistance = static_cast<float>(d);
                    best = at;
                }
            }
            At<Node*>(self, field::kBest) = best;
            reinterpret_cast<SelfFn>(addr::TrafficPathfinder_FinishRoute)(self);
            return Finish(deadline, 1);
        }
    }
    return Finish(deadline, 0);
}

// --- Nodes and the heap ---------------------------------------------------------

constexpr uint32_t kBucketCount = 0xC07;
constexpr uint32_t kPageBytes = 0x24000;

// The node pool's hash (at +0x128): buckets from +4, pages of nodes
// (vector at +0x3020), the next free node and the page's end, the page
// index.
namespace pool {
constexpr std::size_t kBuckets = 0x4;
constexpr std::size_t kPages = 0x3020;
constexpr std::size_t kNext = 0x302C;
constexpr std::size_t kPageEnd = 0x3030;
constexpr std::size_t kPage = 0x3034;
} // namespace pool

// The binary heap at +0x114: a pointer vector used from index 1, the size
// and the last usable index.
struct Heap {
    Node** begin;
    Node** end;
    Node** capacity;
    uint32_t size;
    uint32_t last;
};

// 0x006D5DF0: a key's type and cell; an outside node is the cell beyond
// its side.
void __cdecl DecodeKey(uint32_t key, uint8_t* type, int16_t* x, int16_t* z)
{
    *type = static_cast<uint8_t>(key >> 16);
    *x = static_cast<int16_t>((key >> 8) & 0xFF);
    *z = static_cast<int16_t>(key & 0xFF);
    if (key & 0x80000000) {
        const uint32_t side = (key >> 24) & 3;
        *x = static_cast<int16_t>(*x + Global<int32_t>(addr::TrafficPathfinder_SideDX + 4 * side));
        *z = static_cast<int16_t>(*z + Global<int32_t>(addr::TrafficPathfinder_SideDZ + 4 * side));
    }
}

// 0x006D7730: the node of a key, made from the pool when new.
Node* __fastcall FindOrCreateNode(void* hash, void*, uint32_t key)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using PushBackFn = void(__thiscall*)(void* vector, void* const* value);
    const uint32_t bucket = (key * 0x9E3779B1u) % kBucketCount;
    Node** const head = &At<Node*>(hash, pool::kBuckets + 4 * bucket);
    for (Node* node = *head; node; node = node->next)
        if (node->key == key)
            return node;
    if (At<char*>(hash, pool::kNext) == At<char*>(hash, pool::kPageEnd)) {
        const uint32_t page = ++At<uint32_t>(hash, pool::kPage);
        char** const pages = At<char**>(hash, pool::kPages);
        if (page == static_cast<uint32_t>(At<char**>(hash, pool::kPages + 4) - pages)) {
            void* const fresh = reinterpret_cast<NewFn>(addr::SC4_operator_new)(kPageBytes);
            reinterpret_cast<PushBackFn>(addr::SC4PtrVector_PushBack)(&At<char>(hash, pool::kPages), &fresh);
        }
        char* const start = At<char**>(hash, pool::kPages)[page];
        At<char*>(hash, pool::kNext) = start;
        At<char*>(hash, pool::kPageEnd) = start + kPageBytes;
    }
    auto* const node = reinterpret_cast<Node*>(At<char*>(hash, pool::kNext));
    At<char*>(hash, pool::kNext) += sizeof(Node);
    node->next = *head;
    *head = node;
    node->bucket = bucket;
    node->key = key;
    node->open = 0;
    node->closed = 0;
    DecodeKey(key, &node->type, &node->x, &node->z);
    return node;
}

// 0x006D6380: removes and returns the node of lowest f.
Node* __fastcall PopMin(Heap* heap, void*)
{
    Node* const top = heap->begin[1];
    if (heap->size == 1) {
        heap->size = 0;
        return top;
    }
    heap->begin[1] = heap->begin[heap->size];
    heap->begin[1]->heapIndex = 1;
    if (--heap->size <= 1)
        return top;
    uint32_t at = 1;
    while (2 * at <= heap->size) {
        Node** const nodes = heap->begin;
        Node* const moving = nodes[at];
        uint32_t best = at;
        uint32_t child = 2 * at;
        if (nodes[child]->f < moving->f)
            best = child;
        if (++child <= heap->size && nodes[child]->f < nodes[best]->f)
            best = child;
        if (best == at)
            break;
        nodes[at] = nodes[best];
        nodes[at]->heapIndex = at;
        nodes[best] = moving;
        moving->heapIndex = best;
        at = best;
        if (at >= heap->size)
            break;
    }
    return top;
}

// Moves a node up from a heap position while its parent's f is greater.
void SiftUp(Heap* heap, Node* node, uint32_t at)
{
    for (uint32_t parent = at >> 1; parent > 0; parent = at >> 1) {
        Node* const above = heap->begin[parent];
        if (!(above->f > node->f)) // as the game: stops on equal or NaN
            break;
        heap->begin[at] = above;
        above->heapIndex = at;
        at = parent;
    }
    heap->begin[at] = node;
    node->heapIndex = at;
}

// 0x006D6460: a node's f went down.
void __fastcall DecreaseKey(Heap* heap, void*, Node* node)
{
    SiftUp(heap, node, node->heapIndex);
}

// 0x006D88B0: adds a node, growing the vector to twice the size when full.
void __fastcall HeapPush(Heap* heap, void*, Node* node)
{
    using FillFn = void(__thiscall*)(Heap* vector, uint32_t count, Node* const* value);
    if (++heap->size > heap->last) {
        Node* const none = nullptr;
        reinterpret_cast<FillFn>(addr::SC4PtrVector_Fill)(heap, 2 * heap->size, &none);
        heap->last = static_cast<uint32_t>(heap->end - heap->begin) - 1;
    }
    SiftUp(heap, node, heap->size);
}

// 0x006D6160: the estimate from a cell, 0 inside the rectangle at +0x38,
// else the distance tables' sum. In ST0.
double __fastcall Estimate(void* self, void*, int32_t x, int32_t z)
{
    if (x >= At<int32_t>(self, field::kGrownRect) && z >= At<int32_t>(self, field::kGrownRect + 4) &&
        x <= At<int32_t>(self, field::kGrownRect + 8) && z <= At<int32_t>(self, field::kGrownRect + 12))
        return 0.0;
    return At<int32_t*>(self, field::kColumnDistances)[x] + At<int32_t*>(self, field::kRowDistances)[z];
}

// 0x006D8FA0: opens a neighbour or lowers its cost.
void __fastcall AddNodeImpl(void* self, void*, int32_t x, int32_t z, uint32_t side, uint32_t type, float cost,
                            Node* parent, float limit, uint32_t outside)
{
    const uint32_t t = type & 0xFF;
    uint8_t& blocked = At<uint8_t>(self, field::kBlockedTypes);
    if (blocked & static_cast<uint8_t>(1u << (t & 31)))
        return;
    uint32_t key = ((static_cast<uint32_t>(parent->side) << 2 | (side & 0xFF)) << 24) | t << 16 |
                   (static_cast<uint32_t>(x) & 0xFF) << 8 | (static_cast<uint32_t>(z) & 0xFF);
    if (outside & 0xFF)
        key |= 0x80000000;
    if (parent->parent && parent->parent->key == key)
        return;
    const float g = cost + parent->g;
    if (!(At<uint32_t>(self, field::kFlags) & 1) && !(g <= limit)) {
        if (++At<uint32_t>(self, field::kOverLimit + 4 * t) > 0x40)
            blocked |= static_cast<uint8_t>(1u << (t & 31));
        return;
    }
    Node* const node = FindOrCreateNode(&At<char>(self, field::kPool), nullptr, key);
    const uint8_t wasOpen = node->open;
    if (wasOpen && !(g < node->g))
        return;
    if (node->closed && !(g < node->g))
        return;
    node->parent = parent;
    node->closed = 0;
    node->g = g;
    const double estimate = Estimate(self, nullptr, x, z);
    node->side = static_cast<uint8_t>(side);
    node->f = static_cast<float>(estimate * At<float>(self, field::kEstimateScale) + g);
    auto* const heap = &At<Heap>(self, field::kHeap);
    if (wasOpen) {
        DecreaseKey(heap, nullptr, node);
    } else {
        HeapPush(heap, nullptr, node);
        node->open = 1;
    }
}

// 0x006D6040: per column and row, the distance in cells to the goal
// rectangle (0 inside), from index -1 to the size.
void __fastcall BuildDistanceTables(void* self, void*)
{
    auto build = [](int32_t* table, int32_t first, int32_t last, int32_t size) {
        int32_t i = -1;
        if (first > -1)
            for (int32_t value = first + 1; i < first; ++i, --value)
                table[i] = value;
        for (; i <= last; ++i)
            table[i] = 0;
        for (int32_t value = i - last; i < size; ++i, ++value)
            table[i] = value;
    };
    build(At<int32_t*>(self, field::kColumnDistances), At<int32_t>(self, field::kGoalRect),
          At<int32_t>(self, field::kGoalRect + 8), At<int32_t>(self, field::kWidth) + 1);
    build(At<int32_t*>(self, field::kRowDistances), At<int32_t>(self, field::kGoalRect + 4),
          At<int32_t>(self, field::kGoalRect + 12), At<int32_t>(self, field::kDepth) + 1);
}

// 0x006D6780: every node's f again (after the goal moved); open nodes move
// up the heap.
void __fastcall Rescore(void* self, void*)
{
    char* const hash = &At<char>(self, field::kPool);
    const int32_t x0 = At<int32_t>(self, field::kGrownRect);
    const int32_t z0 = At<int32_t>(self, field::kGrownRect + 4);
    const auto spanX = static_cast<uint32_t>(At<int32_t>(self, field::kGrownRect + 8) - x0);
    const auto spanZ = static_cast<uint32_t>(At<int32_t>(self, field::kGrownRect + 12) - z0);
    char** const pages = At<char**>(hash, pool::kPages);
    char* at = pages[0];
    char* pageEnd = at + kPageBytes;
    uint32_t page = 0;
    for (;;) {
        char* const next = At<char*>(hash, pool::kNext);
        if (at == next)
            break;
        if (at == pageEnd) {
            at = At<char**>(hash, pool::kPages)[++page];
            pageEnd = at + kPageBytes;
            if (at == next)
                break;
        }
        auto* const node = reinterpret_cast<Node*>(at);
        at += sizeof(Node);
        if (static_cast<uint32_t>(node->x - x0) <= spanX && static_cast<uint32_t>(node->z - z0) <= spanZ) {
            node->f = node->g;
        } else {
            const int32_t distance =
                At<int32_t*>(self, field::kRowDistances)[node->z] + At<int32_t*>(self, field::kColumnDistances)[node->x];
            node->f = static_cast<float>(distance * static_cast<double>(At<float>(self, field::kEstimateScale)) + node->g);
        }
        if (node->open)
            DecreaseKey(&At<Heap>(self, field::kHeap), nullptr, node);
    }
}

// 0x006D5E50: (x, z) to the rectangle (x0, z0)-(x1, z1): 0 inside, the
// cells across beside it, else the straight distance to its nearest
// corner. In ST0.
double __fastcall DistanceToRect(void*, void*, int32_t x, int32_t z, int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    const bool inX = x <= x1 && x >= x0;
    const bool inZ = z <= z1 && z >= z0;
    if (inX)
        return inZ ? 0.0 : z < z0 ? z0 - z : z - z1;
    if (inZ)
        return x > x1 ? x - x1 : x0 - x;
    const float from[3] = {static_cast<float>(x), 0.0f, static_cast<float>(z)};
    const float to[3] = {static_cast<float>(x > x1 ? x1 : x0), 0.0f, static_cast<float>(z < z0 ? z0 : z1)};
    using DistanceFn = double(__cdecl*)(const float* a, const float* b); // ST0
    return reinterpret_cast<DistanceFn>(addr::cS3DVector3_Distance)(from, to);
}

// 0x006D8710: the search ended at the node at +0x10: the route's cost
// (+0x60), where it last left the road (+0xEE, +0xF0, side +0xF2), and its
// cells into the route deque at +0x74.
void __fastcall FinishRoute(void* self, void*)
{
    Node* const best = At<Node*>(self, field::kBest);
    float& cost = At<float>(self, field::kCostOffset);
    cost = best->g;
    Node* node = best;
    for (Node* parent = node->parent; parent; node = parent, parent = node->parent) {
        if (parent->type == 1 && node->type != 1) {
            At<int16_t>(self, field::kExitX) = node->x;
            At<int16_t>(self, field::kExitZ) = node->z;
            At<uint8_t>(self, field::kExitSide) = static_cast<uint8_t>(SideTo(parent->x, parent->z, node->x, node->z));
        }
    }
    cost -= node->g;
    if (cost < 0.0f)
        cost = 0.0f;
    const uint32_t mode = At<uint32_t>(self, field::kMode);
    if ((mode == 0 || mode == 3) && At<uint32_t>(self, field::kKind) != 3) {
        Node* const end = At<Node*>(self, field::kBest);
        const int32_t x = end->x;
        const int32_t z = end->z;
        if (static_cast<uint32_t>(x) >= At<uint32_t>(self, field::kWidth) ||
            static_cast<uint32_t>(z) >= At<uint32_t>(self, field::kDepth)) {
            using CellFn = char*(__thiscall*)(void* simulator, int32_t x, int32_t z);
            if (char* const cell = reinterpret_cast<CellFn>(addr::cSC4TrafficSimulator_NetworkCellAt)(
                    At<void*>(self, field::kSimulator), x, z))
                cost = At<float>(cell, 4) + cost;
        }
    }
    char* const route = At<char*>(self, field::kRoute);
    if (!route)
        return;
    reinterpret_cast<SelfFn>(addr::SC4CellDeque_Clear)(route);
    using ReserveFn = void(__thiscall*)(void* deque, uint32_t count, uint32_t atFront);
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    for (Node* at = At<Node*>(self, field::kBest); at; at = at->parent) {
        const uint8_t cell[3] = {static_cast<uint8_t>(at->x), static_cast<uint8_t>(at->z), at->type};
        char*& first = At<char*>(route, 0);
        if (first != At<char*>(route, 4)) {
            first -= 3;
            std::memcpy(first, cell, 3);
            continue;
        }
        if (static_cast<uint32_t>((At<char**>(route, 0xC) - At<char**>(route, 0x20))) < 1)
            reinterpret_cast<ReserveFn>(addr::SC4RouteDeque_ReserveMap)(route, 1, 1);
        void* const block = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x7E);
        At<char**>(route, 0xC)[-1] = static_cast<char*>(block);
        At<char**>(route, 0xC) -= 1;
        At<char*>(route, 4) = static_cast<char*>(block);
        At<char*>(route, 8) = static_cast<char*>(block) + 0x7E;
        first = static_cast<char*>(block) + 0x7E - 3;
        std::memcpy(first, cell, 3);
    }
}

// 0x006D5CF0
uint32_t __fastcall InMap(void* self, void*, int32_t x, int32_t z)
{
    return static_cast<uint32_t>(x) < At<uint32_t>(self, field::kWidth) &&
           static_cast<uint32_t>(z) < At<uint32_t>(self, field::kDepth);
}

// 0x006D5D80: whether a building has room for trips of a kind; the trips
// to send (+0x6C) are the room, at most the trip's count (+0x68).
uint8_t __fastcall TakesTrips(void* self, void*, void* building, uint32_t kind)
{
    using SentFn = uint32_t(__thiscall*)(void* simulator, void* building, uint32_t kind);
    const uint32_t k = kind & 0xFF;
    void* const simulator = At<void*>(self, field::kSimulator);
    const uint32_t capacity = Virtual<uint32_t>(simulator, 0x50, building, k);
    if (capacity == 0)
        return 0;
    const uint32_t sent = reinterpret_cast<SentFn>(addr::cSC4TrafficSimulator_GetTripsSent)(simulator, building, k);
    if (sent >= capacity)
        return 0;
    const uint32_t room = capacity - sent;
    At<uint32_t>(self, field::kResult) = room < At<uint32_t>(self, field::kCount) ? room : At<uint32_t>(self, field::kCount);
    At<uint32_t>(self, field::kKind) = k;
    return 1;
}

// A cell of the simulator's bordered byte grid at +0x4C (trip kinds by
// bit, from -1).
uint8_t& KindCell(void* simulator, int32_t x, int32_t z)
{
    return At<uint8_t*>(At<char*>(simulator, 0x4C), 4 * (3 * x + 3))[z + 1];
}

// 0x006D7960: whether the search ends at a cell, by mode: 0 a building
// with room for the trip's kinds beside the cell (or the city edge's
// connection outside it), 2 a network object in the cells around it that
// accepts the trip (+0x88), 3 the goal rectangle; 1 and 4 never.
uint8_t __fastcall IsGoal(void* self, void*, int32_t x, int32_t z, uint32_t type, uint32_t noTransit)
{
    using ContainsFn = uint32_t(__thiscall*)(const void* rect, int32_t x, int32_t z);
    using HolderFn = void*(__thiscall*)(void* simulator, uint32_t type, int32_t x, int32_t z);
    const auto contains = reinterpret_cast<ContainsFn>(addr::SC4Rect_Contains);
    void* const simulator = At<void*>(self, field::kSimulator);
    const uint8_t kinds = At<uint8_t>(self, field::kKinds);
    const auto* const dx = &Global<int32_t>(addr::TrafficPathfinder_SideDX);
    const auto* const dz = &Global<int32_t>(addr::TrafficPathfinder_SideDZ);
    auto found = [self](int32_t fx, int32_t fz) {
        At<int32_t>(self, field::kFound) = fx;
        At<int32_t>(self, field::kFound + 4) = fz;
        return static_cast<uint8_t>(1);
    };
    switch (At<uint32_t>(self, field::kMode)) {
    case 0: {
        if (!InMap(self, nullptr, x, z) && !contains(&At<char>(self, field::kSourceRect), x, z) &&
            !contains(&At<char>(self, field::kUnknown48), x, z)) {
            // Outside the city: its connection there.
            uint8_t& cell = KindCell(simulator, x, z);
            void* const holder = reinterpret_cast<HolderFn>(addr::cSC4TrafficSimulator_NeighborHolder)(simulator, type, x, z);
            if (!holder)
                return 0;
            for (uint32_t kind = 0; kind < 4; ++kind) {
                const auto bit = static_cast<uint8_t>(1u << kind);
                if (!(bit & (cell & At<uint8_t>(self, field::kKinds))))
                    continue;
                if (TakesTrips(self, nullptr, holder, kind))
                    return found(x, z);
                cell &= static_cast<uint8_t>(~bit);
            }
            return 0;
        }
        if (noTransit & 0xFF)
            return 0;
        const auto* const source = &At<int32_t>(self, field::kSourceRect);
        for (int direction = 0; direction < 4; ++direction) {
            const int32_t nx = x + dx[direction];
            const int32_t nz = z + dz[direction];
            if (!InMap(self, nullptr, nx, nz))
                continue;
            if (nx >= source[0] && nz >= source[1] && nx <= source[2] && nz <= source[3])
                continue;
            const uint8_t cell = KindCell(simulator, nx, nz);
            if (!(kinds & cell))
                continue;
            void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, nx, nz, 0u);
            if (!lot)
                continue;
            void* const building = Virtual<void*>(lot, 0xC);
            for (uint32_t kind = 0; kind < 4; ++kind) {
                const auto bit = static_cast<uint8_t>(1u << kind);
                if (!(cell & (At<uint8_t>(self, field::kKinds) & bit)))
                    continue;
                if (TakesTrips(self, nullptr, building, kind))
                    return found(nx, nz);
                // Full: the lot's cells lose the kind, and the count of
                // open destinations goes down.
                int32_t bounds[4];
                Virtual<void>(lot, 0x38, bounds);
                for (int32_t lx = bounds[0]; lx <= bounds[2]; ++lx)
                    for (int32_t lz = bounds[1]; lz <= bounds[3]; ++lz)
                        KindCell(simulator, lx, lz) &= static_cast<uint8_t>(~bit);
                uint32_t& open = At<uint32_t>(simulator, 0x1B0 + 4 * kind);
                if (open > 0)
                    --open;
            }
        }
        return 0;
    }
    case 2: {
        void* list = Virtual<void*>(ListNodeAllocator(), 0xC, 12u);
        At<void*>(list, 0) = list;
        At<void*>(list, 4) = list;
        const int32_t width = At<int32_t>(self, field::kWidth) - 1;
        const int32_t depth = At<int32_t>(self, field::kDepth) - 1;
        const int32_t rect[4] = {x - 1 < 0 ? 0 : x - 1, z - 1 < 0 ? 0 : z - 1, width < x + 1 ? width : x + 1,
                                 depth < z + 1 ? depth : z + 1};
        Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xA0, &list, &rect[0], &rect[2], 0u, 0xFFFFFFFFu);
        uint8_t result = 0;
        for (char* item = At<char*>(list, 0); item != list; item = At<char*>(item, 0)) {
            void* const object = At<void*>(item, 8);
            if (Virtual<uint8_t>(At<void*>(self, field::kAccepts), 0xC, object)) {
                At<uint32_t>(self, field::kResult) = At<uint32_t>(self, field::kCount);
                result = 1;
            }
            Virtual<uint32_t>(object, 0x8); // Release
        }
        reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4List_Destroy2)(&list);
        return result;
    }
    case 3: {
        const void* const goal = &At<char>(self, field::kGoalRect);
        auto accept = [&](int32_t gx, int32_t gz) {
            At<uint32_t>(self, field::kResult) = At<uint32_t>(self, field::kCount);
            return found(gx, gz);
        };
        if (contains(goal, x, z) && (!(noTransit & 0xFF) || !InMap(self, nullptr, x, z)))
            return accept(x, z);
        const auto* const rect = &At<int32_t>(self, field::kGoalRect);
        if (!InMap(self, nullptr, rect[0], rect[1]))
            return 0;
        for (int direction = 0; direction < 4; ++direction) {
            const int32_t nx = x + dx[direction];
            const int32_t nz = z + dz[direction];
            if (nx >= rect[0] && nz >= rect[1] && nx <= rect[2] && nz <= rect[3] &&
                (!(noTransit & 0xFF) || !InMap(self, nullptr, nx, nz)))
                return accept(nx, nz);
        }
        return 0;
    }
    default:
        return 0;
    }
}

// A start node: open, no parent, g the type's start cost.
void OpenStart(void* self, uint32_t key, double g, uint8_t side, int32_t x, int32_t z)
{
    Node* const node = FindOrCreateNode(&At<char>(self, field::kPool), nullptr, key);
    node->g = static_cast<float>(g);
    double estimate = 0.0;
    if (!(x >= At<int32_t>(self, field::kGrownRect) && z >= At<int32_t>(self, field::kGrownRect + 4) &&
          x <= At<int32_t>(self, field::kGrownRect + 8) && z <= At<int32_t>(self, field::kGrownRect + 12)))
        estimate = At<int32_t*>(self, field::kRowDistances)[z] + At<int32_t*>(self, field::kColumnDistances)[x];
    node->closed = 0;
    node->open = 1;
    node->parent = nullptr;
    node->side = side;
    node->f = static_cast<float>(estimate * At<float>(self, field::kEstimateScale) + g);
    HeapPush(&At<Heap>(self, field::kHeap), nullptr, node);
}

// 0x006D8A90: the start nodes. In rings around the source rectangle (as
// many as the simulator's +0xC50), the road cells in line with it, by each
// goal type of their network and each side the table allows; with none
// (or +0xC50 = 0) and a source outside the city, the border connections
// (list at simulator +0x104) at the source cell. False when none.
uint8_t __fastcall AddStartNodes(void* self, void*)
{
    struct RectIterator {
        uint8_t state;
        uint8_t padding[3];
        int32_t x, z, step, x0, z0, x1, z1;
    };
    using NextFn = uint8_t(__thiscall*)(RectIterator* iterator, int32_t* x, int32_t* z);
    void* const simulator = At<void*>(self, field::kSimulator);
    const float* const costs = At<float*>(self, field::kCostLimits) ? At<float*>(self, field::kCostLimits)
                                                                    : &At<float>(simulator, 0xC78);
    const auto* const source = &At<int32_t>(self, field::kSourceRect);
    const auto width = At<uint32_t>(self, field::kWidth);
    const auto depth = At<uint32_t>(self, field::kDepth);
    const uint16_t types = At<uint16_t>(self, field::kGoalTypes);
    uint8_t added = 0;
    bool onLot = false;
    std::uintptr_t sides;
    if (static_cast<uint32_t>(source[0]) < width && static_cast<uint32_t>(source[1]) < depth) {
        sides = addr::TrafficPathfinder_StartSides;
        if (Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, source[0], source[1], 0u))
            onLot = true;
    } else if (source[0] < 0) {
        sides = addr::TrafficPathfinder_StartSides + 4;
    } else if (source[0] >= static_cast<int32_t>(width)) {
        sides = addr::TrafficPathfinder_StartSides + 0xC;
    } else if (source[1] < 0) {
        sides = addr::TrafficPathfinder_StartSides + 8;
    } else {
        sides = addr::TrafficPathfinder_StartSides + 0x10;
    }

    const uint32_t rings = At<uint32_t>(simulator, 0xC50);
    if (rings >= 1) {
        int32_t x0 = source[0], z0 = source[1], x1 = source[2], z1 = source[3];
        for (uint32_t ring = 1; ring <= rings; ++ring) {
            x0 = x0 - 1 < 0 ? 0 : x0 - 1;
            z0 = z0 - 1 < 0 ? 0 : z0 - 1;
            x1 = x1 + 1 < static_cast<int32_t>(width) ? x1 + 1 : static_cast<int32_t>(width) - 1;
            z1 = z1 + 1 < static_cast<int32_t>(depth) ? z1 + 1 : static_cast<int32_t>(depth) - 1;
            RectIterator cells{};
            cells.step = 1;
            cells.x0 = x0;
            cells.z0 = z0;
            cells.x1 = x1 < x0 ? x0 : x1;
            cells.z1 = z1 < z0 ? z0 : z1;
            cells.x = x0;
            cells.z = z0;
            int32_t x = 0, z = 0;
            while (reinterpret_cast<NextFn>(addr::SC4RectIterator_Next)(&cells, &x, &z)) {
                if (!((x >= source[0] && x <= source[2]) || (z >= source[1] && z <= source[3])))
                    continue;
                for (uint32_t network = 0; network < 6; ++network) {
                    char* const edge = EdgeAt(simulator, network, x, z);
                    if (!edge)
                        continue;
                    if (onLot && !Global<uint8_t>(addr::TrafficPathfinder_StartOnLotOfEdgeKind + static_cast<uint8_t>(edge[5])))
                        continue;
                    char* const record = At<char*>(simulator, 0x58) + ((x << 8) | z) * 0x2E;
                    for (uint32_t type = 0; type < 9; ++type) {
                        if (!(types & (1u << type)) ||
                            Global<uint32_t>(addr::TrafficPathfinder_NetworkOfType + 4 * type) != network)
                            continue;
                        for (uint32_t side = 0; side < 4; ++side) {
                            if (!Global<uint8_t>(sides + side) || !(At<uint16_t>(record, 2 * type) & (0xFu << (4 * side))))
                                continue;
                            const uint32_t key = ((side | 0x10) << 24) | type << 16 |
                                                 (static_cast<uint32_t>(x) & 0xFF) << 8 | (static_cast<uint32_t>(z) & 0xFF);
                            OpenStart(self, key, costs[type], static_cast<uint8_t>(side), x, z);
                            added = 1;
                        }
                    }
                }
            }
        }
        if (added)
            return added;
    }

    if (static_cast<uint32_t>(source[0]) < width && static_cast<uint32_t>(source[1]) < depth)
        return added;
    char* const connections = At<char*>(simulator, 0x104);
    for (char* item = At<char*>(connections, 0); item != connections; item = At<char*>(item, 0)) {
        char* const ends = At<char*>(item, 0x1C);
        for (char* end = At<char*>(ends, 0); end != ends; end = At<char*>(end, 0)) {
            if (At<int16_t>(end, 8) != source[0] || At<int16_t>(end, 0xA) != source[1])
                continue;
            for (uint32_t type = 0; type < 9; ++type) {
                const uint32_t bit = 1u << type;
                if (!(types & bit) || !(At<uint16_t>(item, 0x14) & bit))
                    continue;
                const auto cx = static_cast<uint8_t>(item[0xA]);
                const auto cz = static_cast<uint8_t>(item[0xB]);
                const uint32_t key = (0x10u << 24) | type << 16 | static_cast<uint32_t>(cx) << 8 | cz;
                OpenStart(self, key, static_cast<double>(costs[type]) + At<float>(end, 0x10), 0, cx, cz);
                added = 1;
            }
        }
    }
    return added;
}

// std::vector of (int16 x, int16 z) cells.
struct CellVector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

void PushCell(CellVector& cells, int32_t x, int32_t z)
{
    using InsertFn = void(__thiscall*)(CellVector* vector, uint32_t* where, const uint32_t* value, void* allocator,
                                       uint32_t count, uint32_t atEnd);
    const uint32_t cell = (static_cast<uint32_t>(x) & 0xFFFF) | static_cast<uint32_t>(z) << 16;
    if (cells.end == cells.capacity) {
        uint8_t allocator = 0;
        reinterpret_cast<InsertFn>(addr::SC4CellPairVector_InsertN)(&cells, cells.end, &cell, &allocator, 1, 1);
        return;
    }
    *cells.end++ = cell;
}

int32_t CellX(uint32_t cell)
{
    return static_cast<int16_t>(cell & 0xFFFF);
}

int32_t CellZ(uint32_t cell)
{
    return static_cast<int16_t>(cell >> 16);
}

// 0x006D7E50: mode 0's goal. Rings grow from the source rectangle (within
// the city, or along the border the source is on, whose strip goes to
// +0x48) until one has cells whose trip-kind byte has a wanted kind; of
// those, the cells in the city if any, the one nearest the source; the
// goal is its lot (or the cell itself outside the city). False when no
// kind is wanted and open, when the rings run out, or when the cell has
// no lot.
uint8_t __fastcall SetUpGoals(void* self, void*)
{
    using FreeFn = void(__cdecl*)(void* p);
    const auto free = reinterpret_cast<FreeFn>(addr::SC4_Free);
    void* const simulator = At<void*>(self, field::kSimulator);
    const uint8_t kinds = At<uint8_t>(self, field::kKinds);
    bool any = false;
    for (uint32_t kind = 0; kind < 4 && !any; ++kind)
        any = (kinds & (1u << kind)) && At<uint32_t>(simulator, 0x1B0 + 4 * kind) > 0;
    if (!any)
        return 0;

    const auto* const source = &At<int32_t>(self, field::kSourceRect);
    auto* const border = &At<int32_t>(self, field::kUnknown48);
    const int32_t width = At<int32_t>(self, field::kWidth);
    const int32_t depth = At<int32_t>(self, field::kDepth);
    int32_t bounds[4] = {-1, -1, width, depth};
    if (source[0] < 0) {
        bounds[0] = 0;
        border[0] = -1;
        border[1] = -1;
        border[2] = -1;
        border[3] = depth;
    } else if (source[2] == width) {
        bounds[2] = width - 1;
        border[0] = width;
        border[1] = -1;
        border[2] = width;
        border[3] = depth;
    } else if (source[1] < 0) {
        bounds[1] = 0;
        border[0] = -1;
        border[1] = -1;
        border[2] = width;
        border[3] = -1;
    } else if (source[3] == depth) {
        bounds[3] = depth - 1;
        border[0] = -1;
        border[1] = depth;
        border[2] = width;
        border[3] = depth;
    }

    CellVector cells{};
    int32_t ring[4] = {source[0], source[1], source[2], source[3]};
    auto visit = [&](int32_t x, int32_t z) {
        if (!(At<uint8_t>(self, field::kKinds) & KindCell(simulator, x, z)))
            return;
        if (x >= border[0] && z >= border[1] && x <= border[2] && z <= border[3])
            return;
        PushCell(cells, x, z);
    };
    while (cells.begin == cells.end) {
        bool west = true, north = true, east = true, south = true;
        if (ring[0] > bounds[0])
            --ring[0];
        else
            west = false;
        if (ring[2] < bounds[2])
            ++ring[2];
        else
            east = false;
        if (ring[1] > bounds[1])
            --ring[1];
        else
            north = false;
        if (ring[3] < bounds[3])
            ++ring[3];
        else
            south = false;
        if (!west && !north && !east && !south) {
            if (cells.begin)
                free(cells.begin);
            return 0;
        }
        if (west)
            for (int32_t z = ring[1]; z <= ring[3]; ++z)
                visit(ring[0], z);
        if (east)
            for (int32_t z = ring[1]; z <= ring[3]; ++z)
                visit(ring[2], z);
        if (north)
            for (int32_t x = ring[0]; x <= ring[2]; ++x)
                visit(x, ring[1]);
        if (south)
            for (int32_t x = ring[0]; x <= ring[2]; ++x)
                visit(x, ring[3]);
    }

    // The cells in the city, if any.
    CellVector inside{};
    for (uint32_t* cell = cells.begin; cell != cells.end; ++cell)
        if (static_cast<uint32_t>(CellX(*cell)) < static_cast<uint32_t>(width) &&
            static_cast<uint32_t>(CellZ(*cell)) < static_cast<uint32_t>(depth))
            PushCell(inside, CellX(*cell), CellZ(*cell));
    if (inside.begin == inside.end) {
        const CellVector none = inside;
        inside = cells;
        cells = none;
    }
    auto distance = [source](uint32_t cell) {
        const int32_t x = CellX(cell);
        const int32_t z = CellZ(cell);
        const int32_t dx = x < source[0] ? source[0] - x : x > source[2] ? x - source[2] : 0;
        const int32_t dz = z < source[1] ? source[1] - z : z > source[3] ? z - source[3] : 0;
        return dx + dz;
    };
    uint32_t best = *inside.begin;
    int32_t bestDistance = distance(best);
    for (uint32_t* cell = inside.begin + 1; cell != inside.end; ++cell) {
        const int32_t d = distance(*cell);
        if (d < bestDistance) {
            best = *cell;
            bestDistance = d;
        }
    }
    const int32_t x = CellX(best);
    const int32_t z = CellZ(best);
    if (static_cast<uint32_t>(x) < static_cast<uint32_t>(width) && static_cast<uint32_t>(z) < static_cast<uint32_t>(depth)) {
        void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, 0u);
        if (!lot) {
            free(inside.begin);
            if (cells.begin)
                free(cells.begin);
            return 0;
        }
        Virtual<void>(lot, 0x38, &At<int32_t>(self, field::kGoalRect));
    } else {
        auto* const goal = &At<int32_t>(self, field::kGoalRect);
        goal[0] = x;
        goal[1] = z;
        goal[2] = x;
        goal[3] = z;
    }
    free(inside.begin);
    if (cells.begin)
        free(cells.begin);
    return 1;
}

// 0x006D61E0 (slot 30): whether the route still holds: each step between
// two cells in the city (not both ferry cells) leaves by a way the cell's
// record still has, or, where the type changes, a switch.
uint8_t __fastcall RouteStillOpen(void* self, void*)
{
    Node* node = At<Node*>(self, field::kBest);
    if (!node)
        return 1;
    void* const simulator = At<void*>(self, field::kSimulator);
    uint32_t nextType = node->type;
    int32_t nextSide = -1;
    for (Node* parent = node->parent; parent; node = parent, parent = node->parent) {
        const uint8_t type = node->type;
        const int32_t x = node->x, z = node->z, px = parent->x, pz = parent->z;
        bool skip = !InMap(self, nullptr, x, z) || !InMap(self, nullptr, px, pz) || (x == px && z == pz);
        if (!skip) {
            char* const records = At<char*>(simulator, 0x58);
            char* const record = records + ((x << 8) | z) * 0x2E;
            skip = (record[0x2C] & 2) && (records[((px << 8) | pz) * 0x2E + 0x2C] & 2);
            if (!skip) {
                if (nextSide != -1) {
                    if (nextType != type) {
                        if (((At<uint16_t>(record, 0x12 + 2 * type) >> (4 * nextSide)) & 0xF) == 0xF)
                            return 0;
                    } else if (parent->type == type) {
                        if (!(At<uint16_t>(record, 2 * type) & (1u << (node->side + 4 * nextSide))))
                            return 0;
                    }
                }
                nextSide = static_cast<int32_t>(SideTo(x, z, px, pz));
            }
        }
        if (skip)
            nextSide = -1;
        nextType = type;
    }
    return 1;
}

// 0x006D7070 (slot 32): the route as bytes (see the address database) and
// the travel types it uses.
void __fastcall EncodeRoute(void* self, void*, ByteVector* route, uint32_t* types)
{
    using InsertFn = void(__thiscall*)(ByteVector* vector, uint8_t* where, const uint8_t* value, void* allocator,
                                       uint32_t count, uint32_t atEnd);
    using RangeFn = void(__thiscall*)(ByteVector* vector, uint8_t* where, const uint8_t* first, const uint8_t* last,
                                      void* allocator);
    using ConstructFn = void(__thiscall*)(void* vector, uint32_t count, Node* const* value, void* allocator);
    using EdgeSideFn = int32_t(__thiscall*)(void* simulator, int32_t x, int32_t z, int32_t* edgeX, int32_t* edgeZ);
    uint8_t allocator = 0;
    auto push = [&](uint8_t value) {
        if (route->end == route->capacity) {
            reinterpret_cast<InsertFn>(addr::SC4ByteVector_Grow)(route, route->end, &value, &allocator, 1, 1);
            return;
        }
        *route->end++ = value;
    };
    route->end = route->begin;
    *types = 0;
    Node* const best = At<Node*>(self, field::kBest);
    if (!best)
        return;
    void* const simulator = At<void*>(self, field::kSimulator);
    uint32_t count = 0;
    for (Node* at = best; at; at = at->parent)
        ++count;
    struct {
        Node** begin;
        Node** end;
        Node** capacity;
    } nodes;
    Node* const none = nullptr;
    reinterpret_cast<ConstructFn>(addr::SC4PtrVector_Construct)(&nodes, count, &none, &allocator);
    {
        Node** slot = nodes.begin + count;
        for (Node* at = best; at; at = at->parent)
            *--slot = at;
    }
    auto pushCell = [&](const Node* at) {
        if (InMap(self, nullptr, at->x, at->z)) {
            push(static_cast<uint8_t>(at->x));
            push(static_cast<uint8_t>(at->z));
            return;
        }
        int32_t edgeX = 0, edgeZ = 0;
        reinterpret_cast<EdgeSideFn>(addr::cSC4TrafficSimulator_EdgeSide)(simulator, at->x, at->z, &edgeX, &edgeZ);
        push(static_cast<uint8_t>(edgeX));
        push(static_cast<uint8_t>(edgeZ));
    };
    const Node* const start = nodes.begin[0];
    push(start->type);
    *types |= 1u << start->type;
    pushCell(start);

    uint8_t steps[0x40] = {};
    uint32_t pending = 0; // steps in the buffer, at most 255
    uint8_t runType = start->type;
    auto flush = [&]() {
        push(static_cast<uint8_t>(runType | 0x40));
        push(static_cast<uint8_t>(pending));
        const uint8_t* const last = steps + (pending >> 2) + ((pending & 3) ? 1 : 0);
        reinterpret_cast<RangeFn>(addr::SC4ByteVector_InsertRange)(route, route->end, steps, last, &allocator);
        pending = 0;
    };
    const Node* previous = start;
    for (Node** slot = nodes.begin + 1; slot != nodes.end; ++slot) {
        const Node* const at = *slot;
        const auto dx = static_cast<int16_t>(at->x - previous->x);
        const auto dz = static_cast<int16_t>(at->z - previous->z);
        int32_t direction = -1;
        if (dx == 0 && dz == -1)
            direction = 1;
        else if (dx == 0 && dz == 1)
            direction = 3;
        else if (dz == 0 && dx == -1)
            direction = 0;
        else if (dz == 0 && dx == 1)
            direction = 2;
        if (direction >= 0) {
            if (pending == 0xFF || at->type != runType) {
                if (pending)
                    flush();
                runType = at->type;
                *types |= 1u << at->type;
            }
            const uint32_t shift = (pending & 3) * 2;
            if (shift == 0)
                steps[pending >> 2] = static_cast<uint8_t>(direction);
            else
                steps[pending >> 2] |= static_cast<uint8_t>(direction << shift);
            ++pending;
        } else {
            if (pending)
                flush();
            uint8_t jump = 0x80;
            if (InMap(self, nullptr, previous->x, previous->z) &&
                !(At<char*>(simulator, 0x58)[((previous->x << 8) | previous->z) * 0x2E + 0x2C] & 4))
                jump = 0;
            push(static_cast<uint8_t>(at->type | jump));
            pushCell(at);
            *types |= 1u << at->type;
            runType = at->type;
        }
        previous = at;
    }
    if (pending)
        flush();
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(nodes.begin);
}

// 0x006D6730 (slot 31): an empty pool (its pages kept) and heap.
void __fastcall Reset(void* self, void*)
{
    char* const hash = &At<char>(self, field::kPool);
    char* const first = At<char**>(hash, pool::kPages)[0];
    At<char*>(hash, pool::kNext) = first;
    At<char*>(hash, pool::kPageEnd) = first + kPageBytes;
    At<uint32_t>(hash, pool::kPage) = 0;
    std::memset(&At<char>(hash, pool::kBuckets), 0, 4 * kBucketCount);
    At<Node*>(self, field::kBest) = nullptr;
    if (At<uint32_t>(self, field::kHeapSize))
        At<uint32_t>(self, field::kHeapSize) = 0;
}

// 0x0070D860 (simulator slot 6): a new path finder for the simulator.
uint8_t __fastcall CreatePathFinder(void* simulator, void*, void** out)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using ConstructFn = void*(__thiscall*)(void* self);
    void* finder = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x3160);
    if (finder)
        finder = reinterpret_cast<ConstructFn>(addr::TrafficPathfinder_Construct)(finder);
    Virtual<uint32_t>(finder, 0x4); // AddRef
    Virtual<uint8_t>(finder, 0xC);  // Init
    At<void*>(finder, field::kSimulator) = simulator;
    *out = finder;
    return 1;
}

// 0x00711610 (simulator slot 7): a path finder set up for trips from an
// occupant's lot (see the address database).
uint8_t __fastcall SetupPathFinderForLot(void* simulator, void*, void* finder, void* occupant)
{
    if (!finder || !occupant)
        return 0;
    void* const lot = Virtual<void*>(occupant, 0x10);
    char** const trips = Virtual<char**>(lot, 0xC);
    if (*reinterpret_cast<char**>(*trips) == *trips) {
        Virtual<void>(lot, 0x10);
        if (*reinterpret_cast<char**>(*trips) == *trips)
            return 0;
    }
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    using FreeFn = void(__cdecl*)(void* p);
    auto* const counts = static_cast<uint32_t*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(12));
    counts[0] = counts[1] = counts[2] = 0;
    char* const head = *trips;
    for (char* entry = At<char*>(head, 0); entry != head; entry = At<char*>(entry, 0))
        counts[static_cast<uint8_t>(entry[0x25])] += At<uint32_t>(entry, 0x20);
    uint32_t most = counts[1] > counts[0] ? 1 : 0;
    if (counts[2] > counts[most])
        most = 2;
    const uint32_t kind = static_cast<uint8_t>(At<char*>(head, 0)[0x24]);
    int32_t rect[4];
    int32_t bounds[4];
    std::memcpy(bounds, Virtual<int32_t*>(lot, 0x14, rect), sizeof(bounds));
    Virtual<void>(finder, 0x4C, bounds[0], bounds[1], bounds[2], bounds[3]);
    Virtual<void>(finder, 0x1C);
    Virtual<void>(finder, 0x3C);
    Virtual<void>(finder, 0x40, static_cast<void*>(nullptr));
    Virtual<void>(finder, 0x44, At<uint32_t>(simulator, 0xC54));
    Virtual<void>(finder, 0x24, 0u);
    Virtual<void>(finder, 0x30);
    Virtual<void>(finder, 0x28, 1u << kind);
    uint8_t result = 0;
    if (kind <= 2) {
        Virtual<void>(finder, 0x34, 0x1CFu, 0u);
        Virtual<void>(finder, 0x34, 3u, 1u);
        static constexpr std::size_t kLimits[3] = {0xC9C, 0xCC0, 0xC78};
        Virtual<void>(finder, 0x40, &At<char>(simulator, kLimits[most]));
        result = 1;
    } else if (kind == 3) {
        Virtual<void>(finder, 0x14, 1u);
        Virtual<void>(finder, 0x34, 0x30u, 1u);
        result = 1;
    }
    reinterpret_cast<FreeFn>(addr::SC4_Free)(counts);
    return result;
}

// A float rounded to an integer the game's way: added to 1.5 x 2^23,
// stored as a float, and the bits read back.
uint32_t MagicRound(double value)
{
    const auto rounded = static_cast<float>(value + 12582912.0f);
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof(bits));
    return bits + 0xB4C00000;
}

// 0x006DA060 (slot 29): the route's trips recorded from its nodes.
void __fastcall RecordRoute(void* self, void*, uint32_t kind)
{
    using TableAtFn = char*(__thiscall*)(void* table, const uint32_t* key);
    Node* node = At<Node*>(self, field::kBest);
    const uint32_t trips = At<uint32_t>(self, field::kResult);
    if (!node || !trips)
        return;
    void* const simulator = At<void*>(self, field::kSimulator);
    void* const ids = Virtual<void*>(At<void*>(simulator, 0x704), 0x60);
    const auto count = static_cast<float>(static_cast<double>(trips));
    float weighted = count;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D90), 0x20, 0xA2BF1DDCu))
        weighted = static_cast<float>(std::ceil(static_cast<double>(count) * static_cast<double>(0.8f)));
    auto previousX = static_cast<uint8_t>(At<int32_t>(self, field::kFound));
    auto previousZ = static_cast<uint8_t>(At<int32_t>(self, field::kFound + 4));
    float time = 0.0f;
    for (; node; node = node->parent) {
        const int32_t x = node->x;
        const int32_t z = node->z;
        const float amount = node->type == 1 ? weighted : count;
        const uint32_t rounded = MagicRound(amount);
        if (InMap(self, nullptr, x, z)) {
            Node* const parent = node->parent;
            if (parent && parent->type != node->type) {
                uint32_t id = Cell<uint32_t>(ids, x, z);
                if (id) {
                    const float added = kind == 2 ? amount + amount : amount;
                    char* const record = reinterpret_cast<TableAtFn>(addr::SC4CostTable_At)(&At<char>(simulator, 0x70C), &id);
                    At<float>(record, 4) = added + At<float>(record, 4);
                }
            }
            const uint32_t network = Global<uint32_t>(addr::TrafficPathfinder_NetworkOfType + 4 * node->type);
            if (EdgeAt(simulator, network, x, z) && At<uint8_t>(simulator, 0xC34 + node->type)) {
                void* const grid = Virtual<void*>(At<void**>(simulator, 0x164)[network], 0x60);
                float& cell = Cell<float>(grid, x, z);
                const double sum = static_cast<double>(amount) + cell;
                cell = static_cast<float>(sum);
                if (kind == 2)
                    cell = static_cast<float>(sum + amount);
            }
            uint32_t sideIn;
            if (parent) {
                sideIn = SideTo(parent->x, parent->z, x, z);
            } else {
                const auto* const source = &At<int32_t>(self, field::kSourceRect);
                if (x < source[0])
                    sideIn = source[2] < x ? 0 : 2;
                else if (source[2] < x)
                    sideIn = 0;
                else
                    sideIn = source[3] >= z ? 3 : 1;
            }
            const uint32_t sideOut = SideTo(previousX, previousZ, x, z);
            auto add = [&](uint32_t key) {
                char* const density = reinterpret_cast<TableAtFn>(addr::SC4DensityTable_At)(&At<char>(simulator, 0x178), &key);
                uint16_t& word = At<uint16_t>(density, 0x22 + 2 * (sideOut + 4 * sideIn));
                const uint32_t value = word + rounded;
                word = static_cast<uint16_t>(value > 0xFFFF ? 0xFFFF : value);
            };
            const uint32_t cell = (static_cast<uint32_t>(x) & 0xFF) << 8 | (static_cast<uint32_t>(z) & 0xFF);
            add(static_cast<uint32_t>(kind == 1) << 24 | static_cast<uint32_t>(node->type) << 16 | cell);
            if (kind == 2)
                add((0x100u | node->type) << 16 | cell);
            time = static_cast<float>(static_cast<double>(trips) * At<float>(simulator, 0xBA4 + 4 * node->type) + time);
        }
        previousX = static_cast<uint8_t>(x);
        previousZ = static_cast<uint8_t>(z);
    }
    At<double>(simulator, 0x790) = static_cast<double>(time) + At<double>(simulator, 0x790);
}

// --- The object and its simple slots -------------------------------------------

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);

// 0x006D7690: the node pool with its first page.
void* __fastcall ConstructPool(void* hash, void*)
{
    using InsertFn = void(__thiscall*)(void* vector, void** where, void* const* value, void* allocator, uint32_t count,
                                       uint32_t atEnd);
    char* const pages = &At<char>(hash, pool::kPages);
    At<void*>(pages, 0) = nullptr;
    At<void*>(pages, 4) = nullptr;
    At<void*>(pages, 8) = nullptr;
    char* const page = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(kPageBytes));
    if (At<void**>(pages, 4) == At<void**>(pages, 8)) {
        uint8_t allocator = 0;
        void* const value = page;
        reinterpret_cast<InsertFn>(addr::SC4PtrVector_InsertN)(pages, At<void**>(pages, 4), &value, &allocator, 1, 1);
    } else {
        *At<void**>(pages, 4) = page;
        At<void**>(pages, 4) += 1;
    }
    At<char*>(hash, pool::kNext) = page;
    At<char*>(hash, pool::kPageEnd) = page + kPageBytes;
    At<uint32_t>(hash, pool::kPage) = 0;
    std::memset(&At<char>(hash, pool::kBuckets), 0, 4 * kBucketCount);
    At<void*>(hash, 0) = pages;
    return hash;
}

// 0x006D89C0
void* __fastcall Construct(void* self, void*)
{
    using FillFn = void(__thiscall*)(Heap* vector, uint32_t count, Node* const* value);
    char* const object = static_cast<char*>(self);
    At<std::uintptr_t>(self, 0) = addr::TrafficPathfinder_vftable;
    object[4] = 0;
    At<uint32_t>(self, 8) = 0;
    At<Node*>(self, field::kBest) = nullptr;
    At<float>(self, field::kEstimateScale) = 1.0f;
    At<uint32_t>(self, field::kCostOffset) = 0;
    At<uint32_t>(self, field::kCount) = 0;
    At<uint32_t>(self, field::kFlags) = 0;
    At<uint32_t>(self, field::kRoute) = 0;
    At<uint32_t>(self, 0x78) = 0;
    At<uint32_t>(self, field::kMode) = 3;
    At<uint8_t>(self, field::kKinds) = 0;
    At<uint32_t>(self, field::kAccepts) = 0;
    At<uint32_t>(self, field::kCostLimits) = 0;
    At<uint32_t>(self, field::kBudget) = 0;
    At<uint8_t>(self, field::kResume) = 0;
    At<uint16_t>(self, field::kTypeMask) = 2;
    At<uint16_t>(self, field::kGoalTypes) = 2;
    for (std::size_t at = 0xF4; at <= 0x108; at += 4)
        At<uint32_t>(self, at) = 0;
    auto* const heap = &At<Heap>(self, field::kHeap);
    heap->begin = heap->end = heap->capacity = nullptr;
    heap->size = 0;
    Node* const none = nullptr;
    reinterpret_cast<FillFn>(addr::SC4PtrVector_Fill)(heap, 100, &none);
    heap->last = static_cast<uint32_t>(heap->end - heap->begin) - 1;
    heap->begin[0] = nullptr;
    ConstructPool(&At<char>(self, field::kPool), nullptr);
    return self;
}

// 0x006D6BD0
void __fastcall DestroyPool(void* hash, void*)
{
    char* const pages = &At<char>(hash, pool::kPages);
    for (uint32_t i = 0; i < static_cast<uint32_t>(At<void**>(pages, 4) - At<void**>(pages, 0)); ++i)
        if (void* const page = At<void**>(pages, 0)[i])
            reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(page);
    if (void* const storage = At<void*>(pages, 0))
        reinterpret_cast<FreeFn>(addr::SC4_Free)(storage);
}

// 0x006D6CA0
void __fastcall Destruct(void* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::TrafficPathfinder_vftable;
    DestroyPool(&At<char>(self, field::kPool), nullptr);
    for (std::size_t at : {field::kHeap, std::size_t{0x100}, std::size_t{0xF4}})
        if (void* const storage = At<void*>(self, at))
            reinterpret_cast<FreeFn>(addr::SC4_Free)(storage);
}

// 0x006D7050 (slot 33)
void* __fastcall DeletingDestruct(void* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(self);
    return self;
}

// 0x006D5D20 (slot 0)
uint8_t __fastcall QueryInterface(void* self, void*, uint32_t iid, void** out)
{
    if (iid != 1 && iid != 0xAA0DB766)
        return 0;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// 0x004BDF00 (slot 1, shared)
uint32_t __fastcall AddRef(void* self, void*)
{
    return ++At<uint32_t>(self, 8);
}

// 0x006D61B0 (slot 2)
uint32_t __fastcall Release(void* self, void*)
{
    uint32_t& count = At<uint32_t>(self, 8);
    if (count > 1)
        return --count;
    if (At<uint8_t>(self, 4))
        Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x84, 1u);
    return 0;
}

// 0x006D78B0 (slot 3): the city's size and the distance tables.
uint8_t __fastcall Init(void* self, void*)
{
    using AssignFn = void(__thiscall*)(void* vector, uint32_t count, const uint32_t* value);
    if (At<uint8_t>(self, 4))
        return 0;
    At<uint8_t>(self, 4) = 1;
    At<uint32_t>(self, field::kWidth) = Virtual<uint32_t>(Global<void*>(addr::SC4_City), 0x174);
    At<uint32_t>(self, field::kDepth) = Virtual<uint32_t>(Global<void*>(addr::SC4_City), 0x178);
    const uint32_t zero = 0;
    reinterpret_cast<AssignFn>(addr::SC4Uint32Vector_Assign)(&At<char>(self, 0xF4), At<uint32_t>(self, field::kWidth) + 2, &zero);
    reinterpret_cast<AssignFn>(addr::SC4Uint32Vector_Assign)(&At<char>(self, 0x100), At<uint32_t>(self, field::kDepth) + 2, &zero);
    At<int32_t*>(self, field::kColumnDistances) = At<int32_t*>(self, 0xF4) + 1;
    At<int32_t*>(self, field::kRowDistances) = At<int32_t*>(self, 0x100) + 1;
    return 1;
}

// 0x006D5D50 (slot 4)
uint8_t __fastcall Shutdown(void* self, void*)
{
    if (!At<uint8_t>(self, 4))
        return 0;
    At<uint8_t>(self, 4) = 0;
    Virtual<void>(self, 0x7C);
    At<int32_t*>(self, field::kColumnDistances) = nullptr;
    At<int32_t*>(self, field::kRowDistances) = nullptr;
    return 1;
}

void __fastcall SetFlags(void* self, void*, uint32_t flags) { At<uint32_t>(self, field::kFlags) |= flags; }
void __fastcall ClearFlags(void* self, void*, uint32_t flags) { At<uint32_t>(self, field::kFlags) &= ~flags; }
void __fastcall ResetFlags(void* self, void*) { At<uint32_t>(self, field::kFlags) = 0; }
void __fastcall SetRouteDeque(void* self, void*, uint32_t route) { At<uint32_t>(self, field::kRoute) = route; }
void __fastcall SetMode(void* self, void*, uint32_t mode) { At<uint32_t>(self, field::kMode) = mode; }
void __fastcall AddKinds(void* self, void*, uint32_t kinds) { At<uint8_t>(self, field::kKinds) |= static_cast<uint8_t>(kinds); }
void __fastcall RemoveKinds(void* self, void*, uint32_t kinds) { At<uint8_t>(self, field::kKinds) &= static_cast<uint8_t>(~kinds); }
void __fastcall ResetKinds(void* self, void*) { At<uint8_t>(self, field::kKinds) = 0; }

void __fastcall AddTypes(void* self, void*, uint32_t types, uint32_t goals)
{
    At<uint16_t>(self, field::kTypeMask) |= static_cast<uint16_t>(types);
    if (goals & 0xFF)
        At<uint16_t>(self, field::kGoalTypes) |= static_cast<uint16_t>(types);
}

void __fastcall RemoveTypes(void* self, void*, uint32_t types)
{
    At<uint16_t>(self, field::kTypeMask) &= static_cast<uint16_t>(~types);
    At<uint16_t>(self, field::kGoalTypes) &= static_cast<uint16_t>(~types);
}

void __fastcall ResetTypes(void* self, void*)
{
    At<uint16_t>(self, field::kGoalTypes) = 0;
    At<uint16_t>(self, field::kTypeMask) = 0;
}

void __fastcall SetCostLimits(void* self, void*, uint32_t limits) { At<uint32_t>(self, field::kCostLimits) = limits; }
void __fastcall SetCostOffset(void* self, void*, uint32_t bits) { At<uint32_t>(self, field::kCostOffset) = bits; }
void __fastcall SetCount(void* self, void*, uint32_t count) { At<uint32_t>(self, field::kCount) = count; }

void __fastcall SetSourceRect(void* self, void*, int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    auto* const rect = &At<int32_t>(self, field::kSourceRect);
    rect[0] = x0;
    rect[1] = z0;
    rect[2] = x1;
    rect[3] = z1;
}

void __fastcall SetGoalRect(void* self, void*, int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    auto* const rect = &At<int32_t>(self, field::kGoalRect);
    rect[0] = x0;
    rect[1] = z0;
    rect[2] = x1;
    rect[3] = z1;
}

void __fastcall SetBudget(void* self, void*, uint32_t ms) { At<uint32_t>(self, field::kBudget) = ms; }
void __fastcall ClearResume(void* self, void*) { At<uint8_t>(self, field::kResume) = 0; }
uint32_t __fastcall GetResult(void* self, void*) { return At<uint32_t>(self, field::kResult); }
double __fastcall GetCost(void* self, void*) { return At<float>(self, field::kCostOffset); }

// Slot 26: the cost x the simulator's slot 15 (a float).
double __fastcall GetScaledCost(void* self, void*)
{
    return Virtual<double>(At<void*>(self, field::kSimulator), 0x3C) * At<float>(self, field::kCostOffset);
}

void __fastcall GetFound(void* self, void*, int32_t* x, int32_t* z)
{
    *x = At<int32_t>(self, field::kFound);
    *z = At<int32_t>(self, field::kFound + 4);
}

uint8_t __fastcall GetBestType(void* self, void*)
{
    Node* const best = At<Node*>(self, field::kBest);
    return best ? best->type : 0;
}

// Hooks to leave out, for bisecting: OPENSC4_TRAFFIC_SKIP lists function
// addresses (hex, e.g. 006D8FA0,006D6380).
bool Skipped(std::uintptr_t address)
{
    char list[512] = {};
    if (!GetEnvironmentVariableA("OPENSC4_TRAFFIC_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

// The path finder's simple slots, swapped into its vtable (several are
// too short to detour, three are shared with other classes); the slots
// detoured above keep the game's addresses.
bool InstallSlots()
{
    if (Skipped(addr::TrafficPathfinder_vftable))
        return true;
    const struct {
        int index;
        std::uintptr_t original;
        void* ours;
    } slots[] = {
        {0, 0x006D5D20, reinterpret_cast<void*>(&QueryInterface)},
        {1, 0x004BDF00, reinterpret_cast<void*>(&AddRef)},
        {2, 0x006D61B0, reinterpret_cast<void*>(&Release)},
        {3, 0x006D78B0, reinterpret_cast<void*>(&Init)},
        {4, 0x006D5D50, reinterpret_cast<void*>(&Shutdown)},
        {5, 0x006D6CF0, reinterpret_cast<void*>(&SetFlags)},
        {6, 0x006D6D00, reinterpret_cast<void*>(&ClearFlags)},
        {7, 0x006D6D20, reinterpret_cast<void*>(&ResetFlags)},
        {8, 0x00407AD0, reinterpret_cast<void*>(&SetRouteDeque)},
        {9, 0x006D6D30, reinterpret_cast<void*>(&SetMode)},
        {10, 0x006D6D40, reinterpret_cast<void*>(&AddKinds)},
        {11, 0x006D6D60, reinterpret_cast<void*>(&RemoveKinds)},
        {12, 0x006D6D80, reinterpret_cast<void*>(&ResetKinds)},
        {13, 0x006D6D90, reinterpret_cast<void*>(&AddTypes)},
        {14, 0x006D6DB0, reinterpret_cast<void*>(&RemoveTypes)},
        {15, 0x006D6DD0, reinterpret_cast<void*>(&ResetTypes)},
        {16, 0x006D6DE0, reinterpret_cast<void*>(&SetCostLimits)},
        {17, 0x006D6DF0, reinterpret_cast<void*>(&SetCostOffset)},
        {18, 0x006D6E00, reinterpret_cast<void*>(&SetCount)},
        {19, 0x006D6E10, reinterpret_cast<void*>(&SetSourceRect)},
        {20, 0x006D6E40, reinterpret_cast<void*>(&SetGoalRect)},
        {21, 0x006D6E70, reinterpret_cast<void*>(&SetBudget)},
        {22, 0x006D6E80, reinterpret_cast<void*>(&ClearResume)},
        {24, 0x00698CE0, reinterpret_cast<void*>(&GetResult)},
        {25, 0x006D6E90, reinterpret_cast<void*>(&GetCost)},
        {26, 0x006D6030, reinterpret_cast<void*>(&GetScaledCost)},
        {27, 0x006D6EA0, reinterpret_cast<void*>(&GetFound)},
        {28, 0x006D6EC0, reinterpret_cast<void*>(&GetBestType)},
        {33, 0x006D7050, reinterpret_cast<void*>(&DeletingDestruct)},
    };
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::TrafficPathfinder_vftable);
    for (const auto& slot : slots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("traffic path finder: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, 34 * sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    for (const auto& slot : slots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, 34 * sizeof(*vtable), old, &old);
    return true;
}

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[29] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::TrafficPathfinder_Search, reinterpret_cast<void*>(&Search)},
        {addr::TrafficPathfinder_AddNode, reinterpret_cast<void*>(&AddNodeImpl)},
        {addr::TrafficPathfinder_FindOrCreateNode, reinterpret_cast<void*>(&FindOrCreateNode)},
        {addr::TrafficPathfinder_DecodeKey, reinterpret_cast<void*>(&DecodeKey)},
        {addr::TrafficPathfinder_PopMin, reinterpret_cast<void*>(&PopMin)},
        {addr::TrafficPathfinder_HeapPush, reinterpret_cast<void*>(&HeapPush)},
        {addr::TrafficPathfinder_DecreaseKey, reinterpret_cast<void*>(&DecreaseKey)},
        {addr::TrafficPathfinder_Estimate, reinterpret_cast<void*>(&Estimate)},
        {addr::TrafficPathfinder_BuildDistanceTables, reinterpret_cast<void*>(&BuildDistanceTables)},
        {addr::TrafficPathfinder_Rescore, reinterpret_cast<void*>(&Rescore)},
        {addr::TrafficPathfinder_DistanceToRect, reinterpret_cast<void*>(&DistanceToRect)},
        {addr::TrafficPathfinder_FinishRoute, reinterpret_cast<void*>(&FinishRoute)},
        {addr::TrafficPathfinder_InMap, reinterpret_cast<void*>(&InMap)},
        {addr::TrafficPathfinder_TakesTrips, reinterpret_cast<void*>(&TakesTrips)},
        {addr::TrafficPathfinder_IsGoal, reinterpret_cast<void*>(&IsGoal)},
        {addr::TrafficPathfinder_AddStartNodes, reinterpret_cast<void*>(&AddStartNodes)},
        {addr::TrafficPathfinder_SetUpGoals, reinterpret_cast<void*>(&SetUpGoals)},
        {addr::TrafficPathfinder_RouteStillOpen, reinterpret_cast<void*>(&RouteStillOpen)},
        {addr::TrafficPathfinder_EncodeRoute, reinterpret_cast<void*>(&EncodeRoute)},
        {addr::TrafficPathfinder_Reset, reinterpret_cast<void*>(&Reset)},
        {addr::cSC4TrafficSimulator_CreatePathFinder, reinterpret_cast<void*>(&CreatePathFinder)},
        {addr::cSC4TrafficSimulator_SetupPathFinderForLot, reinterpret_cast<void*>(&SetupPathFinderForLot)},
        {addr::TrafficPathfinder_RecordRoute, reinterpret_cast<void*>(&RecordRoute)},
        {addr::TrafficPathfinder_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::TrafficPathfinder_ConstructPool, reinterpret_cast<void*>(&ConstructPool)},
        {addr::TrafficPathfinder_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::TrafficPathfinder_DestroyPool, reinterpret_cast<void*>(&DestroyPool)},
        {addr::TrafficPathfinder_Init, reinterpret_cast<void*>(&Init)},
        {addr::TrafficPathfinder_Shutdown, reinterpret_cast<void*>(&Shutdown)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) == sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        if (Skipped(hooks[i].address))
            continue;
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return InstallSlots();
}

const bool g_registered = registry::Register({
    "simulators.traffic.pathfinder",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::TrafficPathfinder_Search,
    "Our traffic path search, node pool and heap; NAM's patch inside the search is not in effect (docs/traffic.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::traffic_sim
