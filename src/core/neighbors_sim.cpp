// Our cSC4NeighborsSimulator (build 641), first part: the connection
// queries of its interface and the region-cell helpers, swapped into the
// original object's vtable (docs/neighbors.md). Connections are kept in a
// list (+0x1C); a connection's cell is in city cells, possibly just outside
// the city, and the region is in 64-cell region tiles.
#include "core/neighbors_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::neighbors_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("neighbors: first %s", name);
}

namespace {

using ListFn = void(__thiscall*)(void* list);
using InsertRangeFn = void(__thiscall*)(void* list, ListNode* where, ListNode* first, ListNode* last, void* dummy);
using AssignFn = void*(__thiscall*)(void* list, const void* other);
using SetInsertFn = void*(__thiscall*)(void* set, void* result, void* const* key);
using SetEraseFn = void(__thiscall*)(void* set, void* node);
using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);

void* Region() { return Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x90); }
void* RegionCities() { return Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x8C); }

// The edge a city cell is past: 3 west (x < 0), 0 north (z < 0), 1 east, 2
// south, -1 inside.
int32_t Edge(int32_t x, int32_t z, int32_t width, int32_t height)
{
    if (x < 0)
        return 3;
    if (z < 0)
        return 0;
    if (x >= width)
        return 1;
    if (z >= height)
        return 2;
    return -1;
}

// --- Region cells ---------------------------------------------------------------

// 0x006CE820: the region tile on the other side of a city cell past the edge
// (the city's tile, slot 5 of `region` or else the current region view,
// moved by the edge; along the edge by the cell / 64).
void __cdecl OtherSideTile(int32_t x, int32_t z, int32_t* tileX, int32_t* tileZ, void* region)
{
    static bool logged = false;
    FirstCall("OtherSideTile", logged);
    if (!region)
        region = Region();
    int32_t width = 0, height = 0;
    Virtual<void>(region, 0x1C, &width, &height);
    const int32_t edge = Edge(x, z, width << 6, height << 6);
    Virtual<void>(region, 0x14, tileX, tileZ);
    const int32_t alongX = x / 64;
    const int32_t alongZ = z / 64;
    switch (edge) {
    case 3:
        --*tileX;
        *tileZ += alongZ;
        break;
    case 0:
        *tileX += alongX;
        --*tileZ;
        break;
    case 1:
        *tileX += width;
        *tileZ += alongZ;
        break;
    case 2:
        *tileX += alongX;
        *tileZ += height;
        break;
    default:
        break;
    }
}

// 0x006CE920: the city across the edge from a city cell past it, and the
// cell on its side (just outside it, across from the cell). False without a
// region or a city there.
uint8_t __cdecl OtherSideCell(int32_t x, int32_t z, int32_t* tileX, int32_t* tileZ, void** city, int32_t* otherX,
                              int32_t* otherZ)
{
    static bool logged = false;
    FirstCall("OtherSideCell", logged);
    *city = nullptr;
    *otherZ = 0;
    *otherX = 0;
    void* const ours = Global<void*>(addr::SC4_City);
    const int32_t cellsX = Virtual<int32_t>(ours, 0x174);
    const int32_t cellsZ = Virtual<int32_t>(ours, 0x178);
    const int32_t edge = Edge(x, z, cellsX, cellsZ);
    void* const region = Region();
    if (!region)
        return 0;
    Virtual<void>(region, 0x14, tileX, tileZ);
    int32_t width = 0, height = 0;
    Virtual<void>(region, 0x1C, &width, &height);
    const int32_t alongX = x / 64;
    const int32_t alongZ = z / 64;
    const int32_t restX = x % 64;
    const int32_t restZ = z % 64;
    switch (edge) {
    case 3:
        --*tileX;
        *tileZ += alongZ;
        break;
    case 0:
        *tileX += alongX;
        --*tileZ;
        break;
    case 1:
        *tileX += width;
        *tileZ += alongZ;
        break;
    case 2:
        *tileX += alongX;
        *tileZ += height;
        break;
    default:
        break;
    }
    void* const cities = RegionCities();
    if (!cities)
        return 0;
    void** const slot = Virtual<void**>(cities, 0x2C, *tileX, *tileZ);
    if (!slot || !*slot)
        return 0;
    void* const other = *slot;
    *city = other;
    int32_t cornerX = 0, cornerZ = 0;
    Virtual<void>(other, 0x14, &cornerX, &cornerZ);
    int32_t otherWidth = 0, otherHeight = 0;
    Virtual<void>(other, 0x1C, &otherWidth, &otherHeight);
    const int32_t dx = *tileX - cornerX;
    const int32_t dz = *tileZ - cornerZ;
    switch (edge) {
    case 1:
        *otherX = -1;
        *otherZ = (dz << 6) + restZ;
        break;
    case 2:
        *otherX = (dx << 6) + restX;
        *otherZ = -1;
        break;
    case 3:
        *otherX = otherWidth << 6;
        *otherZ = (dz << 6) + restZ;
        break;
    case 0:
        *otherX = (dx << 6) + restX;
        *otherZ = otherHeight << 6;
        break;
    default:
        break;
    }
    return 1;
}

// --- Connection queries -----------------------------------------------------------

// 10 (0x006CDF80): FindConnection (slot 11) found one; the whole word.
uint32_t __fastcall HasConnection(void* self, void*, uint32_t type, int32_t x, int32_t z)
{
    return Virtual<void*>(self, 0x2C, type, x, z) ? 1 : 0;
}

// 11 (0x006CF6B0): the connection of a type at a cell.
void* __fastcall FindConnection(void* self, void*, uint32_t type, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("FindConnection", logged);
    ListNode* const head = Head(self, field::kConnections);
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const connection = node->value;
        int32_t cellX = 0, cellZ = 0;
        Virtual<void>(connection, 0x18, &cellX, &cellZ);
        if (Virtual<uint32_t>(connection, 0x14) == type && cellX == x && cellZ == z)
            return connection;
    }
    return nullptr;
}

// 12 (0x006CF030): the city on the region tile across from a cell.
void* __fastcall FindCityOnOtherSideOfConnection(void*, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("FindCityOnOtherSideOfConnection", logged);
    int32_t tileX = x, tileZ = z;
    reinterpret_cast<decltype(&OtherSideTile)>(addr::cSC4NeighborsSimulator_OtherSideTile)(x, z, &tileX, &tileZ,
                                                                                          nullptr);
    void* const cities = RegionCities();
    if (!cities)
        return nullptr;
    void** const slot = Virtual<void**>(cities, 0x2C, tileX, tileZ);
    return slot ? *slot : nullptr;
}

// 13 (0x006CDFA0): the connection across from another (slot 14 with its
// type and cell).
void* __fastcall FindConnectionOnOtherSideOf(void* self, void*, void* connection)
{
    const uint32_t type = Virtual<uint32_t>(connection, 0x14);
    int32_t x = 0, z = 0;
    Virtual<void>(connection, 0x18, &x, &z);
    return Virtual<void*>(self, 0x38, type, x, z);
}

// 14 (0x006CEDE0): the connection of a type in the city across the edge
// from a cell (that city's slot 20 at the cell on its side).
void* __fastcall FindConnectionOnOtherSide(void*, void*, uint32_t type, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("FindConnectionOnOtherSide", logged);
    void* city = nullptr;
    int32_t tileX = 0, tileZ = 0;
    if (!reinterpret_cast<decltype(&OtherSideCell)>(addr::cSC4NeighborsSimulator_OtherSideCell)(
            x, z, &tileX, &tileZ, &city, &x, &z))
        return nullptr;
    return Virtual<void*>(city, 0x50, type, x, z);
}

// 15 (0x006CF720): a connection of a type to a city not visited this cycle.
void* __fastcall FindUnvisitedConnection(void* self, void*, uint32_t type, const void* city)
{
    ListNode* const head = Head(self, field::kConnections);
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const connection = node->value;
        if (Virtual<uint32_t>(connection, 0x14) == type && !Virtual<uint8_t>(connection, 0x2C) &&
            Virtual<const void*>(connection, 0x24) == city)
            return connection;
    }
    return nullptr;
}

// 16 (0x006CFF00): every connection, referenced, into the list.
void __fastcall GetAllConnections(void* self, void*, void* list)
{
    reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing)(list);
    ListNode* const head = Head(self, field::kConnections);
    char dummy;
    reinterpret_cast<InsertRangeFn>(addr::SC4List_InsertRange)(list, *static_cast<ListNode**>(list), head->next, head,
                                                              &dummy);
}

// The connections that pass `match`, referenced, into the list (0x00B207DC
// nodes), as slots 17-19 do.
template <typename Match>
void Collect(void* self, void* list, Match match)
{
    reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing)(list);
    ListNode* const head = Head(self, field::kConnections);
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const connection = node->value;
        if (!match(connection))
            continue;
        Virtual<uint32_t>(connection, 0x4);
        PushBackReferenced(*static_cast<ListNode**>(list), connection, addr::SC4List_NodeAllocator3);
        Virtual<uint32_t>(connection, 0x8);
    }
}

// 17 (0x006CFF30).
void __fastcall GetConnectionsOfType(void* self, void*, uint32_t type, void* list)
{
    Collect(self, list, [type](void* connection) { return Virtual<uint32_t>(connection, 0x14) == type; });
}

// 18 (0x006D0010).
void __fastcall GetConnectionsOfTypeToCity(void* self, void*, uint32_t type, const void* city, void* list)
{
    Collect(self, list, [type, city](void* connection) {
        return Virtual<uint32_t>(connection, 0x14) == type && Virtual<const void*>(connection, 0x24) == city;
    });
}

// 19 (0x006D0100).
void __fastcall GetConnectionsToCity(void* self, void*, const void* city, void* list)
{
    Collect(self, list, [city](void* connection) { return Virtual<const void*>(connection, 0x24) == city; });
}

// 20 (0x006D01E0): the cities on the region tiles around ours (north and
// south rows, then west and east columns), established ones only (slot 43)
// unless asked; in address order, referenced, into the list.
void __fastcall GetAdjacentCities(void*, void*, void* list, uint8_t unestablished)
{
    static bool logged = false;
    FirstCall("GetAdjacentCities", logged);
    reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing4)(list);
    struct {
        char* head;
        uint32_t size;
    } set{};
    set.head = static_cast<char*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x14));
    set.size = 0;
    set.head[0] = 0;
    At<char*>(set.head, 0x4) = nullptr;
    At<char*>(set.head, 0x8) = set.head;
    At<char*>(set.head, 0xC) = set.head;
    void* const cities = RegionCities();
    void* const region = Region();
    int32_t tileX = 0, tileZ = 0, width = 0, height = 0;
    Virtual<void>(region, 0x14, &tileX, &tileZ);
    Virtual<void>(region, 0x1C, &width, &height);
    const auto consider = [&](int32_t x, int32_t z) {
        void** const slot = Virtual<void**>(cities, 0x2C, x, z);
        if (!slot || !*slot)
            return;
        if (!unestablished && !Virtual<uint8_t>(*slot, 0xAC))
            return;
        void* city = *slot;
        char result[8];
        reinterpret_cast<SetInsertFn>(addr::SC4PtrSet_Insert)(&set, result, &city);
    };
    for (int32_t i = 0; i < width; ++i) {
        consider(i + tileX, tileZ - 1);
        consider(i + tileX, height + tileZ);
    }
    for (int32_t i = 0; i < height; ++i) {
        consider(tileX - 1, i + tileZ);
        consider(width + tileX, i + tileZ);
    }
    for (char* node = At<char*>(set.head, 0x8); node != set.head;) {
        void* const city = At<void*>(node, 0x10);
        if (city)
            Virtual<uint32_t>(city, 0x4);
        PushBackReferenced(*static_cast<ListNode**>(list), city, addr::SC4List_NodeAllocator4);
        if (city)
            Virtual<uint32_t>(city, 0x8);
        if (char* right = At<char*>(node, 0xC)) {
            while (At<char*>(right, 0x8))
                right = At<char*>(right, 0x8);
            node = right;
        } else {
            char* parent = At<char*>(node, 0x4);
            while (node == At<char*>(parent, 0xC)) {
                node = parent;
                parent = At<char*>(parent, 0x4);
            }
            if (At<char*>(node, 0xC) != parent)
                node = parent;
        }
    }
    if (set.size) {
        for (char* node = At<char*>(set.head, 0x4); node;) {
            reinterpret_cast<SetEraseFn>(addr::SC4PtrSet_EraseTree)(&set, At<char*>(node, 0xC));
            char* const left = At<char*>(node, 0x8);
            reinterpret_cast<FreeFn>(addr::SC4_Free)(node);
            node = left;
        }
        At<char*>(set.head, 0x8) = set.head;
        At<char*>(set.head, 0x4) = nullptr;
        At<char*>(set.head, 0xC) = set.head;
        set.size = 0;
    }
    if (set.head)
        reinterpret_cast<FreeFn>(addr::SC4_Free)(set.head);
}

// Transport connections (not power, 4, or water, 5) of types 1 to 12.
bool Transport(uint32_t type) { return type >= 1 && type <= 12 && type != 4 && type != 5; }

// A list of the 0x00B207DC allocator, empty.
void* NewList()
{
    auto* const sentinel = static_cast<ListNode*>(Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0xC, 0xCu));
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    return sentinel;
}

void DropList(void*& list)
{
    reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing)(&list);
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, list);
}

bool Contains(ListNode* head, const void* value)
{
    ListNode* node = head->next;
    while (node != head && node->value != value)
        node = node->next;
    return node != head;
}

// 21 (0x006D1E00): the cities across our transport connections, once each,
// established ones only unless asked.
void __fastcall GetConnectedCities(void* self, void*, void* list, uint8_t unestablished)
{
    static bool logged = false;
    FirstCall("GetConnectedCities", logged);
    using AllocateNodesFn = ListNode*(__thiscall*)(void* list, uint32_t count, uint32_t zero);
    reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing4)(list);
    void* connections = NewList();
    Virtual<void>(self, 0x40, &connections);
    auto* const all = static_cast<ListNode*>(connections);
    for (ListNode* node = all->next; node != all; node = node->next) {
        void* const connection = node->value;
        if (!Transport(Virtual<uint32_t>(connection, 0x14)))
            continue;
        void* const city = Virtual<void*>(connection, 0x24);
        if (!city || (!unestablished && !Virtual<uint8_t>(city, 0xAC)))
            continue;
        Virtual<uint32_t>(city, 0x4);
        const bool known = Contains(*static_cast<ListNode**>(list), city);
        Virtual<uint32_t>(city, 0x8);
        if (known)
            continue;
        Virtual<uint32_t>(city, 0x4);
        ListNode* const head = *static_cast<ListNode**>(list);
        ListNode* const added = reinterpret_cast<AllocateNodesFn>(addr::SC4List_AllocateNodes4)(list, 1, 0);
        added->value = city;
        Virtual<uint32_t>(city, 0x4);
        ListNode* const last = head->prev;
        added->next = head;
        added->prev = last;
        last->next = added;
        head->prev = added;
        Virtual<uint32_t>(city, 0x8);
    }
    DropList(connections);
}

// 22 (0x006D1AB0): every city reachable over transport connections from
// ours (ours first, breadth first through each city's connections, slot 21
// of a regional city), then ours taken off; established ones only unless
// asked.
void __fastcall GetAllConnectedCities(void* self, void*, void* list, uint8_t unestablished)
{
    static bool logged = false;
    FirstCall("GetAllConnectedCities", logged);
    using AllocateNodesFn = ListNode*(__thiscall*)(void* list, uint32_t count, uint32_t zero);
    using EraseFn = void*(__thiscall*)(void* list, ListNode** out, ListNode* node);
    reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing4)(list);
    void* const ours = Region();
    if (ours)
        Virtual<uint32_t>(ours, 0x4);
    {
        ListNode* const head = *static_cast<ListNode**>(list);
        ListNode* const added = reinterpret_cast<AllocateNodesFn>(addr::SC4List_AllocateNodes4)(list, 1, 0);
        added->value = ours;
        if (ours)
            Virtual<uint32_t>(ours, 0x4);
        ListNode* const last = head->prev;
        added->next = head;
        added->prev = last;
        last->next = added;
        head->prev = added;
        if (ours)
            Virtual<uint32_t>(ours, 0x8);
    }
    for (ListNode* current = (*static_cast<ListNode**>(list))->next; current != *static_cast<ListNode**>(list);) {
        void* const city = current->value;
        void* connections = NewList();
        if (city == ours)
            Virtual<void>(self, 0x40, &connections);
        else
            Virtual<void>(city, 0x54, &connections);
        auto* const all = static_cast<ListNode*>(connections);
        for (ListNode* node = all->next; node != all; node = node->next) {
            void* const connection = node->value;
            if (!Transport(Virtual<uint32_t>(connection, 0x14)))
                continue;
            void* const other = Virtual<void*>(connection, 0x24);
            if (!other)
                continue;
            Virtual<uint32_t>(other, 0x4);
            const bool known = Contains(*static_cast<ListNode**>(list), other);
            Virtual<uint32_t>(other, 0x8);
            if (known)
                continue;
            Virtual<uint32_t>(other, 0x4);
            PushBackReferenced(*static_cast<ListNode**>(list), other, addr::SC4List_NodeAllocator4);
            Virtual<uint32_t>(other, 0x8);
        }
        current = current->next;
        DropList(connections);
    }
    ListNode* out = nullptr;
    reinterpret_cast<EraseFn>(addr::SC4List_EraseReleasing4)(list, &out, (*static_cast<ListNode**>(list))->next);
    if (unestablished)
        return;
    ListNode* const head = *static_cast<ListNode**>(list);
    for (ListNode* node = head->next; node != *static_cast<ListNode**>(list);) {
        if (Virtual<uint8_t>(node->value, 0xAC)) {
            node = node->next;
            continue;
        }
        ListNode* const next = node->next;
        node->prev->next = next;
        next->prev = node->prev;
        if (node->value)
            Virtual<uint32_t>(node->value, 0x8);
        Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, node);
        node = next;
    }
}

// 23 (0x006D1FC0): a connection to a city a deal of this type can use:
// power (0x4773FFE0/E1), water (E2/E3) or transport (E4/E5; highways,
// ground highways, avenues, roads, then dirt roads, the first kind there
// is). For the city's side (E1, E3, E5) our connection must be active; for
// ours (E0, E2) the one across from it; E4 takes the first.
void* __fastcall FindConnectionSuitableForDeal(void* self, void*, uint32_t dealType, const void* city)
{
    static bool logged = false;
    FirstCall("FindConnectionSuitableForDeal", logged);
    using EraseFn = void*(__thiscall*)(void* list, ListNode** out, ListNode* node);
    void* list = NewList();
    bool ours = true;
    bool check = true;
    const auto collect = [&](uint32_t type) {
        Virtual<void>(self, 0x48, type, city, &list);
        auto* const head = static_cast<ListNode*>(list);
        return head->next != head;
    };
    switch (dealType - 0x4773FFE0u) {
    case 0:
        ours = false;
        [[fallthrough]];
    case 1:
        collect(4);
        break;
    case 2:
        ours = false;
        [[fallthrough]];
    case 3:
        collect(5);
        break;
    case 4:
        ours = false;
        check = false;
        [[fallthrough]];
    case 5:
        if (!collect(3) && !collect(12) && !collect(8) && !collect(2))
            collect(10);
        break;
    default:
        break;
    }
    void* found = nullptr;
    auto* const head = static_cast<ListNode*>(list);
    if (head->next != head) {
        if (!check) {
            found = head->next->value;
        } else {
            while (head->next != head) {
                void* const connection = head->next->value;
                void* const other = Virtual<void*>(self, 0x34, connection);
                bool active;
                if (ours)
                    active = Virtual<uint8_t>(connection, 0x34) != 0;
                else
                    active = other && Virtual<uint8_t>(other, 0x34) != 0;
                if (active) {
                    found = connection;
                    break;
                }
                ListNode* out = nullptr;
                reinterpret_cast<EraseFn>(addr::SC4List_EraseReleasing3)(&list, &out, head->next);
            }
        }
    }
    reinterpret_cast<ListFn>(addr::SC4List_Destroy3)(&list);
    return found;
}

// --- Deal queries -----------------------------------------------------------------

// 29 (0x006D0840): the deals of a type with a city, referenced, into the list.
void __fastcall FindDealsWithCity(void* self, void*, uint32_t type, const void* city, void* list)
{
    reinterpret_cast<ListFn>(addr::SC4List_Clear)(list);
    ListNode* const head = Head(self, field::kDeals);
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const deal = node->value;
        if (Virtual<uint32_t>(deal, 0x20) != type || Virtual<const void*>(deal, 0x1C) != city)
            continue;
        Virtual<uint32_t>(deal, 0x4);
        PushBackReferenced(*static_cast<ListNode**>(list), deal, addr::SC4List_NodeAllocator);
        Virtual<uint32_t>(deal, 0x8);
    }
}

// 30 (0x006D0760): the deals of a type.
void __fastcall FindDeals(void* self, void*, uint32_t type, void* list)
{
    reinterpret_cast<ListFn>(addr::SC4List_Clear)(list);
    ListNode* const head = Head(self, field::kDeals);
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const deal = node->value;
        if (Virtual<uint32_t>(deal, 0x20) != type)
            continue;
        Virtual<uint32_t>(deal, 0x4);
        PushBackReferenced(*static_cast<ListNode**>(list), deal, addr::SC4List_NodeAllocator);
        Virtual<uint32_t>(deal, 0x8);
    }
}

// The month's cost of the deals of three types not flagged 0x3D (slot 39).
int64_t DealTotal(void* self, uint32_t first)
{
    int64_t total = 0;
    ListNode* const head = Head(self, field::kDeals);
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const deal = node->value;
        if (Virtual<uint8_t>(deal, 0x9C, 0x3Du))
            continue;
        const uint32_t type = Virtual<uint32_t>(deal, 0x20);
        if (type == first || type == first + 2 || type == first + 4)
            total += Virtual<int64_t>(deal, 0x58);
    }
    return total;
}

// 32 (0x006CF860): deals we sell (0x4773FFE1, E3, E5).
int64_t __fastcall GetTotalMonthlyNeighborDealIncome(void* self, void*) { return DealTotal(self, 0x4773FFE1); }

// 33 (0x006CF8D0): deals we buy (0x4773FFE0, E2, E4).
int64_t __fastcall GetTotalMonthlyNeighborDealExpense(void* self, void*) { return DealTotal(self, 0x4773FFE0); }

// --- Deal flags -------------------------------------------------------------------

// 27 (0x006CDFE0).
void __fastcall SetUIDisableFlag(void* self, void*, uint8_t flag) { At<uint8_t>(self, field::kUIDisabled) = flag; }

// 28 (0x006D4670): every deal, into the list (assigned).
void __fastcall FindDealsAll(void* self, void*, void* list)
{
    reinterpret_cast<AssignFn>(addr::SC4RefList_Assign)(list, static_cast<char*>(self) + field::kDeals);
}

// 31 (0x006CFD70).
void __fastcall MarkDealsDirty(void* self, void*) { At<uint8_t>(self, field::kDealsDirty) = 1; }

// --- Installation -----------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(10, 0x006CDF80, HasConnection),
    OPENSC4_SLOT(11, 0x006CF6B0, FindConnection),
    OPENSC4_SLOT(12, 0x006CF030, FindCityOnOtherSideOfConnection),
    OPENSC4_SLOT(13, 0x006CDFA0, FindConnectionOnOtherSideOf),
    OPENSC4_SLOT(14, 0x006CEDE0, FindConnectionOnOtherSide),
    OPENSC4_SLOT(15, 0x006CF720, FindUnvisitedConnection),
    OPENSC4_SLOT(16, 0x006CFF00, GetAllConnections),
    OPENSC4_SLOT(17, 0x006CFF30, GetConnectionsOfType),
    OPENSC4_SLOT(18, 0x006D0010, GetConnectionsOfTypeToCity),
    OPENSC4_SLOT(19, 0x006D0100, GetConnectionsToCity),
    OPENSC4_SLOT(20, 0x006D01E0, GetAdjacentCities),
    OPENSC4_SLOT(21, 0x006D1E00, GetConnectedCities),
    OPENSC4_SLOT(22, 0x006D1AB0, GetAllConnectedCities),
    OPENSC4_SLOT(23, 0x006D1FC0, FindConnectionSuitableForDeal),
    OPENSC4_SLOT(27, 0x006CDFE0, SetUIDisableFlag),
    OPENSC4_SLOT(28, 0x006D4670, FindDealsAll),
    OPENSC4_SLOT(29, 0x006D0840, FindDealsWithCity),
    OPENSC4_SLOT(30, 0x006D0760, FindDeals),
    OPENSC4_SLOT(31, 0x006CFD70, MarkDealsDirty),
    OPENSC4_SLOT(32, 0x006CF860, GetTotalMonthlyNeighborDealIncome),
    OPENSC4_SLOT(33, 0x006CF8D0, GetTotalMonthlyNeighborDealExpense),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 39;

using TileFn = void(__cdecl*)(int32_t, int32_t, int32_t*, int32_t*, void*);
using CellFn = uint8_t(__cdecl*)(int32_t, int32_t, int32_t*, int32_t*, void**, int32_t*, int32_t*);
TileFn g_originalTile = nullptr;
CellFn g_originalCell = nullptr;

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4NeighborsSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("neighbors interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    const addr::Entry* tile = addr_check::Find(addr::cSC4NeighborsSimulator_OtherSideTile);
    const addr::Entry* cell = addr_check::Find(addr::cSC4NeighborsSimulator_OtherSideCell);
    if (!tile || !cell || !hook::Install(*tile, &OtherSideTile, &g_originalTile) ||
        !hook::Install(*cell, &OtherSideCell, &g_originalCell))
        return false;
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("neighbors interface: %u of %d cISC4NeighborsSimulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])), kVtableSlots);
    return true;
}

const bool g_registered = registry::Register({
    "simulators.neighbors.interface",
    "simulators/neighbors",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4NeighborsSimulator_vftable,
    "Our cISC4NeighborsSimulator connection queries and the region-cell helpers (docs/neighbors.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::neighbors_sim
