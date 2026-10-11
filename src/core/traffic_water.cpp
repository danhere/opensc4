// The traffic simulator's water route finder (vtable 0x00AB34FC, at the
// simulator's +0x108; docs/traffic.md, "The water route finder"): its
// object, its message handling, the byte grid of water depths it keeps up
// to date and its route search (0x00725170; no test run reaches it).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/traffic_cells.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::traffic_sim {
namespace {

namespace field {
constexpr std::size_t kInited = 0x4;
constexpr std::size_t kCount = 0x8;
constexpr std::size_t kPool = 0xC;     // node pool: buckets, then pages
constexpr std::size_t kNodes = 0x3040; // nodes in use
constexpr std::size_t kDepths = 0x3044; // byte grid (0x44-byte object)
constexpr std::size_t kDirty = 0x3048; // int32 x0, z0, x1, z1
constexpr std::size_t kIsDirty = 0x3058;
} // namespace field

// The node pool (from +0xC): 0xC07 buckets, the page vector, the next free
// node, the page's end and the page index.
namespace pool {
constexpr std::size_t kPages = 0x301C;
constexpr std::size_t kNext = 0x3028;
constexpr std::size_t kPageEnd = 0x302C;
constexpr std::size_t kPage = 0x3030;
constexpr uint32_t kPageBytes = 0x1C000;
constexpr uint32_t kBuckets = 0xC07;
} // namespace pool

constexpr uint32_t kMessages[] = {0x26D31EC1, 0x278E6003, 0x678E6E3A, 0x6A3B0AFD, 0x99EF1142, 0x99EF1143};

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);

// std::vector<uint8>.
struct ByteVector {
    uint8_t* begin;
    uint8_t* end;
    uint8_t* capacity;
};

// 0x00724F70: the node pool with its first page.
void* __fastcall ConstructPool(void* hash, void*)
{
    using InsertFn = void(__thiscall*)(void* vector, void** where, void* const* value, void* allocator, uint32_t count,
                                       uint32_t atEnd);
    char* const pages = &At<char>(hash, pool::kPages);
    At<void*>(pages, 0) = nullptr;
    At<void*>(pages, 4) = nullptr;
    At<void*>(pages, 8) = nullptr;
    char* const page = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(pool::kPageBytes));
    if (At<void**>(pages, 4) == At<void**>(pages, 8)) {
        uint8_t allocator = 0;
        void* const value = page;
        reinterpret_cast<InsertFn>(addr::SC4PtrVector_InsertN)(pages, At<void**>(pages, 4), &value, &allocator, 1, 1);
    } else {
        *At<void**>(pages, 4) = page;
        At<void**>(pages, 4) += 1;
    }
    At<char*>(hash, pool::kNext) = page;
    At<char*>(hash, pool::kPageEnd) = page + pool::kPageBytes;
    At<uint32_t>(hash, pool::kPage) = 0;
    std::memset(hash, 0, 4 * pool::kBuckets);
    return hash;
}

// 0x00724BF0
void __fastcall DestroyPool(void* hash, void*)
{
    char* const pages = &At<char>(hash, pool::kPages);
    for (uint32_t i = 0; i < static_cast<uint32_t>(At<void**>(pages, 4) - At<void**>(pages, 0)); ++i)
        if (void* const page = At<void**>(pages, 0)[i])
            reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(page);
    if (void* const storage = At<void*>(pages, 0))
        reinterpret_cast<FreeFn>(addr::SC4_Free)(storage);
}

// 0x007250F0
void* __fastcall Construct(void* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4WaterRouteFinder_vftable;
    At<uint8_t>(self, field::kInited) = 0;
    At<uint32_t>(self, field::kCount) = 0;
    ConstructPool(&At<char>(self, field::kPool), nullptr);
    At<uint32_t>(self, field::kNodes) = 0;
    At<void*>(self, field::kDepths) = nullptr;
    At<uint8_t>(self, field::kIsDirty) = 0;
    return self;
}

// 0x00725130 (slot 2)
uint32_t __fastcall Release(void* self, void*)
{
    uint32_t& count = At<uint32_t>(self, field::kCount);
    if (count > 1)
        return --count;
    At<std::uintptr_t>(self, 0) = addr::SC4WaterRouteFinder_vftable;
    if (void* const depths = At<void*>(self, field::kDepths))
        Virtual<uint32_t>(depths, 0x8);
    DestroyPool(&At<char>(self, field::kPool), nullptr);
    reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(self);
    return 0;
}

// 0x00725490: the depth grid (a 0x44-byte uint8 sim grid, built inline) and
// the six messages.
uint8_t __fastcall Init(void* self, void*)
{
    if (At<uint8_t>(self, field::kInited))
        return 0;
    char* grid = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x44));
    if (grid) {
        At<std::uintptr_t>(grid, 4) = addr::SC4SimGrid_VtableBase;
        At<std::uintptr_t>(grid, 0) = addr::SC4SimGridUint8_Vtable;
        At<std::uintptr_t>(grid, 4) = addr::SC4SimGridUint8_Vtable2;
        grid[8] = 0;
        At<uint32_t>(grid, 0xC) = 0;
        At<uint32_t>(grid, 0x10) = 0x49B9E602;
        At<uint32_t>(grid, 0x18) = 0;
        At<uint32_t>(grid, 0x38) = 0;
        At<uint32_t>(grid, 0x3C) = 0;
        At<uint32_t>(grid, 0x40) = 0;
    }
    void* const old = At<void*>(self, field::kDepths);
    if (grid != old) {
        if (grid)
            Virtual<uint32_t>(grid, 0x4);
        At<void*>(self, field::kDepths) = grid;
        if (old)
            Virtual<uint32_t>(old, 0x8);
    }
    void* const depths = At<void*>(self, field::kDepths);
    Virtual<uint8_t>(depths, 0xC);
    Virtual<void>(depths, 0x24, 1u);
    Virtual<void>(depths, 0x18, 0xCBDF6BF8u);
    for (uint32_t message : kMessages)
        Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x14, self, message);
    At<uint8_t>(self, field::kInited) = 1;
    return 1;
}

// 0x00724760
uint8_t __fastcall Shutdown(void* self, void*)
{
    if (!At<uint8_t>(self, field::kInited))
        return 0;
    At<uint32_t>(self, field::kNodes) = 0;
    char* const hash = &At<char>(self, field::kPool);
    char* const first = At<char**>(hash, pool::kPages)[0];
    At<char*>(hash, pool::kNext) = first;
    At<char*>(hash, pool::kPageEnd) = first + pool::kPageBytes;
    At<uint32_t>(hash, pool::kPage) = 0;
    std::memset(hash, 0, 4 * pool::kBuckets);
    for (uint32_t message : kMessages)
        Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x18, self, message);
    if (void* const depths = At<void*>(self, field::kDepths)) {
        At<void*>(self, field::kDepths) = nullptr;
        Virtual<uint32_t>(depths, 0x8);
    }
    At<uint8_t>(self, field::kInited) = 0;
    return 1;
}

// A float rounded to an integer the game's way: added to 1.5 x 2^23,
// stored as a float, and the bits read back.
int32_t MagicRound(double value)
{
    const auto rounded = static_cast<float>(value + 12582912.0f);
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof(bits));
    return static_cast<int32_t>(bits + 0xB4C00000);
}

// 0x00724840: the dirty rectangle's depth bytes again: 0 for dry land,
// else the water depth (at most 40) x 5 rounded up.
void __fastcall RebuildDepths(void* self, void*)
{
    void* const terrain = Global<void*>(addr::SC4_Service43CF0);
    void* const grid = Virtual<void*>(At<void*>(self, field::kDepths), 0x60);
    Virtual<uint32_t>(Global<void*>(addr::SC4_City), 0x174);
    const auto* const dirty = &At<int32_t>(self, field::kDirty);
    const int32_t x0 = dirty[0];
    const int32_t z0 = dirty[1];
    const int32_t x1 = dirty[2] < x0 ? x0 : dirty[2];
    const int32_t z1 = dirty[3] < z0 ? z0 : dirty[3];
    for (int32_t z = z0; z <= z1; ++z) {
        for (int32_t x = x0; x <= x1; ++x) {
            uint8_t& cell = reinterpret_cast<uint8_t*>(static_cast<char***>(grid)[0][3 * x])[z];
            if (!Virtual<uint8_t>(terrain, 0x20, x, z, 0u, 0u)) {
                cell = 0;
                continue;
            }
            const double depth = Virtual<double>(terrain, 0x18, x, z); // ST0
            const auto stored = static_cast<float>(depth);
            if (!(depth > 0.0)) {
                cell = 0;
                continue;
            }
            const float capped = 40.0f < stored ? 40.0f : stored;
            const double scaled = static_cast<double>(capped) * 5.0f;
            int32_t rounded = MagicRound(0.5f + scaled);
            if (scaled == static_cast<double>(rounded - 1))
                --rounded;
            cell = static_cast<uint8_t>(rounded);
        }
    }
    At<uint8_t>(self, field::kIsDirty) = 0;
}

// The dirty rectangle grown by a rectangle (Rect::Union, 0x004665C0).
void MarkDirty(void* self, const int32_t* rect)
{
    auto* const dirty = &At<int32_t>(self, field::kDirty);
    if (At<uint8_t>(self, field::kIsDirty)) {
        if (dirty[0] >= rect[0])
            dirty[0] = rect[0];
        if (dirty[1] >= rect[1])
            dirty[1] = rect[1];
        if (dirty[2] <= rect[2])
            dirty[2] = rect[2];
        if (dirty[3] <= rect[3])
            dirty[3] = rect[3];
        return;
    }
    std::memcpy(dirty, rect, 16);
    At<uint8_t>(self, field::kIsDirty) = 1;
}

// 0x007249D0 (slot 3): terrain changes and water buildings mark cells
// dirty; CityInited rebuilds everything, 0x6A3B0AFD what is dirty.
uint8_t __fastcall DoMessage(void* self, void*, void* message)
{
    void* const city = Global<void*>(addr::SC4_City);
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == 0x278E6003 || type == 0x678E6E3A) {
        const auto width = static_cast<int32_t>(Virtual<uint32_t>(city, 0x174));
        const auto depth = static_cast<int32_t>(Virtual<uint32_t>(city, 0x178));
        const uint32_t a = Virtual<uint32_t>(message, 0x24);
        const uint32_t b = Virtual<uint32_t>(message, 0x38);
        int32_t rect[4] = {static_cast<int32_t>(a >> 16) - 1, static_cast<int32_t>(b >> 16) - 1,
                           static_cast<int32_t>(a & 0xFFFF), static_cast<int32_t>(b & 0xFFFF)};
        if (rect[0] < 0)
            rect[0] = 0;
        if (rect[2] >= width)
            rect[2] = width - 1;
        if (rect[1] < 0)
            rect[1] = 0;
        if (rect[3] >= depth)
            rect[3] = depth - 1;
        MarkDirty(self, rect);
        return 1;
    }
    if (type == 0x26D31EC1) {
        const auto depth = static_cast<int32_t>(Virtual<uint32_t>(city, 0x178));
        const auto width = static_cast<int32_t>(Virtual<uint32_t>(city, 0x174));
        auto* const dirty = &At<int32_t>(self, field::kDirty);
        dirty[0] = 0;
        dirty[1] = 0;
        dirty[2] = width - 1;
        dirty[3] = depth - 1;
        At<uint8_t>(self, field::kIsDirty) = 1;
        RebuildDepths(self, nullptr);
        return 1;
    }
    if (type == 0x6A3B0AFD) {
        if (At<uint8_t>(self, field::kIsDirty))
            RebuildDepths(self, nullptr);
        return 1;
    }
    if (type == 0x99EF1142 || type == 0x99EF1143) {
        if (Virtual<uint32_t>(message, 0x4C) != 0xC772BF98)
            return 1;
        void* const occupant = Virtual<void*>(message, 0x28);
        int32_t rect[4];
        Virtual<void>(occupant, 0x2C, rect);
        if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Terrain), 0x88, rect[0], rect[1]))
            return 1;
        MarkDirty(self, rect);
        return 1;
    }
    return 1;
}

// --- The search ---------------------------------------------------------------------

// A search node (0x1C bytes): its cell (x << 8 | z, a uint16 followed by
// padding in the game's; CellKey, which needs the whole word for a big
// city), the parent, g, f, open and closed flags, the next node of the open
// list and of its bucket.
struct WaterNode {
    uint32_t cell;
    WaterNode* parent;
    float g;
    float f;
    uint8_t open;
    uint8_t closed;
    uint8_t padding2[2];
    WaterNode* next;
    WaterNode* chain;
};
static_assert(sizeof(WaterNode) == 0x1C);

// 0x00725010: the node of a cell, made from the pool when new.
WaterNode* __fastcall FindOrCreateNode(void* hash, void*, uint32_t key)
{
    using PushBackFn = void(__thiscall*)(void* vector, void* const* value);
    const uint32_t cell = g_wideCells ? key : key & 0xFFFF;
    const uint32_t bucket = (cell * 0x9E3779B1u) % pool::kBuckets;
    WaterNode** const head = &At<WaterNode*>(hash, 4 * bucket);
    for (WaterNode* node = *head; node; node = node->chain)
        if (node->cell == cell)
            return node;
    if (At<char*>(hash, pool::kNext) == At<char*>(hash, pool::kPageEnd)) {
        const uint32_t page = ++At<uint32_t>(hash, pool::kPage);
        char** const pages = At<char**>(hash, pool::kPages);
        if (page == static_cast<uint32_t>(At<char**>(hash, pool::kPages + 4) - pages)) {
            void* const fresh = reinterpret_cast<NewFn>(addr::SC4_operator_new)(pool::kPageBytes);
            reinterpret_cast<PushBackFn>(addr::SC4PtrVector_PushBack)(&At<char>(hash, pool::kPages), &fresh);
        }
        char* const start = At<char**>(hash, pool::kPages)[page];
        At<char*>(hash, pool::kNext) = start;
        At<char*>(hash, pool::kPageEnd) = start + pool::kPageBytes;
    }
    auto* const node = reinterpret_cast<WaterNode*>(At<char*>(hash, pool::kNext));
    At<char*>(hash, pool::kNext) += sizeof(WaterNode);
    node->chain = *head;
    *head = node;
    node->cell = cell;
    node->open = 0;
    node->closed = 0;
    return node;
}

// 0x00724670: into the open list (+0x3040), sorted by f, after equals.
void __fastcall InsertOpen(WaterNode** list, void*, WaterNode* node)
{
    WaterNode* at = *list;
    if (!at) {
        *list = node;
        node->next = nullptr;
        return;
    }
    if (node->f < at->f) {
        node->next = at;
        *list = node;
        return;
    }
    for (WaterNode* next = at->next; next; at = next, next = at->next) {
        if (node->f < next->f) {
            node->next = next;
            at->next = node;
            return;
        }
    }
    at->next = node;
    node->next = nullptr;
}

// 0x007246E0: a node whose f went down, unlinked and inserted again.
void __fastcall ReinsertOpen(WaterNode** list, void*, WaterNode* node)
{
    WaterNode* at = *list;
    if (at == node) {
        *list = node->next;
        InsertOpen(list, nullptr, node);
        return;
    }
    if (at->next) {
        while (at->next != node) {
            at = at->next;
            if (!at->next)
                break;
        }
        if (at->next)
            at->next = node->next;
    }
    InsertOpen(list, nullptr, node);
}

// 0x00724730: the straight distance between two cells, in ST0.
double __cdecl Distance(int32_t x, int32_t z, int32_t x1, int32_t z1)
{
    const int32_t dx = x - x1;
    const int32_t dz = z - z1;
    return std::sqrt(static_cast<double>(dz * dz + dx * dx));
}

// 0x00724C50: the route from the start to a node as route bytes (type 0,
// the start cell, runs of 2-bit steps of at most 255; reversed when asked),
// and its cell count; a jump between cells ends it where it is.
void __fastcall WriteRoute(void* /*self*/, void*, WaterNode* end, ByteVector* route, uint32_t* count, uint32_t reverse)
{
    using GrowFn = void(__thiscall*)(ByteVector* vector, uint8_t* where, const uint8_t* value, void* allocator,
                                     uint32_t count, uint32_t atEnd);
    using RangeFn = void(__thiscall*)(ByteVector* vector, uint8_t* where, const uint8_t* first, const uint8_t* last,
                                      void* allocator);
    using ConstructFn = void(__thiscall*)(void* vector, uint32_t count, WaterNode* const* value, void* allocator);
    uint8_t allocator = 0;
    auto push = [&](uint8_t value) {
        if (route->end == route->capacity) {
            reinterpret_cast<GrowFn>(addr::SC4ByteVector_Grow)(route, route->end, &value, &allocator, 1, 1);
            return;
        }
        *route->end++ = value;
    };
    route->end = route->begin;
    *count = 0;
    if (!end)
        return;
    uint32_t total = 0;
    for (WaterNode* at = end; at; at = at->parent)
        ++total;
    *count = total;
    struct {
        WaterNode** begin;
        WaterNode** end;
        WaterNode** capacity;
    } nodes;
    WaterNode* const none = nullptr;
    reinterpret_cast<ConstructFn>(addr::SC4PtrVector_Construct)(&nodes, total, &none, &allocator);
    if (reverse & 0xFF) {
        WaterNode** slot = nodes.begin;
        for (WaterNode* at = end; at; at = at->parent)
            *slot++ = at;
    } else {
        WaterNode** slot = nodes.begin + total;
        for (WaterNode* at = end; at; at = at->parent)
            *--slot = at;
    }
    push(0);
    auto x = static_cast<uint32_t>(KeyX(nodes.begin[0]->cell));
    auto z = static_cast<uint32_t>(KeyZ(nodes.begin[0]->cell));
    PushRouteCell(push, static_cast<int32_t>(x), static_cast<int32_t>(z));
    uint8_t steps[0x40] = {};
    uint32_t pending = 0;
    auto insertSteps = [&](const uint8_t* last) {
        reinterpret_cast<RangeFn>(addr::SC4ByteVector_InsertRange)(route, route->end, steps, last, &allocator);
    };
    for (WaterNode** slot = nodes.begin + 1; slot != nodes.end; ++slot) {
        const auto nx = static_cast<uint32_t>(KeyX((*slot)->cell));
        const auto nz = static_cast<uint32_t>(KeyZ((*slot)->cell));
        const auto dx = static_cast<int16_t>(nx - x);
        const auto dz = static_cast<int16_t>(nz - z);
        x = nx;
        z = nz;
        uint32_t direction;
        if (dx == 0 && dz == -1)
            direction = 1;
        else if (dx == 0 && dz == 1)
            direction = 3;
        else if (dz == 0 && dx == -1)
            direction = 0;
        else if (dz == 0 && dx == 1)
            direction = 2;
        else {
            reinterpret_cast<FreeFn>(addr::SC4_Free)(nodes.begin);
            return;
        }
        if (pending == 0xFF) {
            push(0x40);
            push(0xFF);
            insertSteps(steps + 0x40);
            pending = 0;
        }
        const uint32_t shift = (pending & 3) * 2;
        if (shift == 0)
            steps[pending >> 2] = static_cast<uint8_t>(direction);
        else
            steps[pending >> 2] |= static_cast<uint8_t>(direction << shift);
        ++pending;
    }
    if (pending) {
        push(0x40);
        push(static_cast<uint8_t>(pending));
        insertSteps(steps + (pending >> 2) + ((pending & 3) ? 1 : 0));
    }
    reinterpret_cast<FreeFn>(addr::SC4_Free)(nodes.begin);
}

// 0x00725170: a water route between two cells of the same body of water
// (0x00B43CF0 slot 9's map, slot 7): A* over the depth grid, a step
// costing (40 - depth x 0.2) x 0.1 + 1, the estimate the straight
// distance; the routes there and back into the two vectors.
uint8_t __fastcall Find(void* self, void*, int32_t x0, int32_t z0, int32_t x1, int32_t z1, uint32_t* count,
                        ByteVector* there, ByteVector* back)
{
    if (At<uint8_t>(self, field::kIsDirty))
        RebuildDepths(self, nullptr);
    void* const water = Global<void*>(addr::SC4_Service43CF0);
    const uint32_t from = Virtual<uint32_t>(Virtual<void*>(water, 0x24), 0x1C, x0, z0);
    const uint32_t to = Virtual<uint32_t>(Virtual<void*>(water, 0x24), 0x1C, x1, z1);
    if (static_cast<uint16_t>(from) == 0 || static_cast<uint16_t>(from) != static_cast<uint16_t>(to))
        return 0;
    void* const city = Global<void*>(addr::SC4_City);
    const int32_t lastX = static_cast<int32_t>(Virtual<uint32_t>(city, 0x174)) - 1;
    const int32_t lastZ = static_cast<int32_t>(Virtual<uint32_t>(city, 0x178)) - 1;
    void* const grid = Virtual<void*>(At<void*>(self, field::kDepths), 0x60);
    auto** const open = &At<WaterNode*>(self, field::kNodes);
    *open = nullptr;
    char* const hash = &At<char>(self, field::kPool);
    char* const first = At<char**>(hash, pool::kPages)[0];
    At<char*>(hash, pool::kNext) = first;
    At<char*>(hash, pool::kPageEnd) = first + pool::kPageBytes;
    At<uint32_t>(hash, pool::kPage) = 0;
    std::memset(hash, 0, 4 * pool::kBuckets);
    WaterNode* const start = FindOrCreateNode(hash, nullptr, CellKey(x0, z0));
    if (!start)
        return 0;
    const int32_t sx = x0 - x1;
    const int32_t sz = z0 - z1;
    start->g = 0.0f;
    start->parent = nullptr;
    start->f = static_cast<float>(std::sqrt(static_cast<double>(sz * sz + sx * sx)));
    InsertOpen(open, nullptr, start);
    while (*open) {
        WaterNode* const node = *open;
        *open = node->next;
        const int32_t x = KeyX(node->cell);
        const int32_t z = KeyZ(node->cell);
        if (x == x1 && z == z1) {
            if (there)
                WriteRoute(self, nullptr, node, there, count, 0);
            if (back)
                WriteRoute(self, nullptr, node, back, count, 1);
            return 1;
        }
        for (int direction = 0; direction < 4; ++direction) {
            const int32_t nx = x + Global<int32_t>(addr::SC4WaterRouteFinder_SideDX + 4 * direction);
            if (nx < 0 || nx > lastX)
                continue;
            const int32_t nz = z + Global<int32_t>(addr::SC4WaterRouteFinder_SideDZ + 4 * direction);
            if (nz < 0 || nz > lastZ)
                continue;
            const uint8_t depth = reinterpret_cast<uint8_t*>(static_cast<char***>(grid)[0][3 * nx])[nz];
            if (depth == 0 && !(nx == x1 && nz == z1))
                continue;
            const uint32_t cell = CellKey(nx, nz);
            if (node->parent && node->parent->cell == cell)
                continue;
            const auto g = static_cast<float>(((40.0 - depth * static_cast<double>(0.2f)) * static_cast<double>(0.1f) +
                                               node->g) + 1.0);
            WaterNode* const next = FindOrCreateNode(hash, nullptr, cell);
            if (next->open && !(g < next->g))
                continue;
            if (next->closed && !(g < next->g))
                continue;
            next->closed = 0;
            next->g = g;
            next->f = static_cast<float>(Distance(nx, nz, x1, z1) + g);
            next->parent = node;
            if (next->open) {
                ReinsertOpen(open, nullptr, next);
            } else {
                InsertOpen(open, nullptr, next);
                next->open = 1;
            }
        }
        node->open = 0;
        node->closed = 1;
    }
    return 0;
}

// Hooks to leave out, for bisecting: OPENSC4_TRAFFIC_SKIP lists function
// addresses (hex).
bool Skipped(std::uintptr_t address)
{
    char list[512] = {};
    if (!GetEnvironmentVariableA("OPENSC4_TRAFFIC_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[14] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4WaterRouteFinder_ConstructPool, reinterpret_cast<void*>(&ConstructPool)},
        {addr::SC4WaterRouteFinder_DestroyPool, reinterpret_cast<void*>(&DestroyPool)},
        {addr::SC4WaterRouteFinder_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4WaterRouteFinder_Release, reinterpret_cast<void*>(&Release)},
        {addr::SC4WaterRouteFinder_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4WaterRouteFinder_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4WaterRouteFinder_RebuildDepths, reinterpret_cast<void*>(&RebuildDepths)},
        {addr::SC4WaterRouteFinder_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::SC4WaterRouteFinder_Find, reinterpret_cast<void*>(&Find)},
        {addr::SC4WaterRouteFinder_FindOrCreateNode, reinterpret_cast<void*>(&FindOrCreateNode)},
        {addr::SC4WaterRouteFinder_InsertOpen, reinterpret_cast<void*>(&InsertOpen)},
        {addr::SC4WaterRouteFinder_ReinsertOpen, reinterpret_cast<void*>(&ReinsertOpen)},
        {addr::SC4WaterRouteFinder_Distance, reinterpret_cast<void*>(&Distance)},
        {addr::SC4WaterRouteFinder_WriteRoute, reinterpret_cast<void*>(&WriteRoute)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) == sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        if (Skipped(hooks[i].address))
            continue;
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "simulators.traffic.waterroutes",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4WaterRouteFinder_DoMessage,
    "Our water route finder: object, messages, depth grid and search (docs/traffic.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::traffic_sim
