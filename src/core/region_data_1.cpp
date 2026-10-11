// The region (cSC4Region, docs/region.md), part 1: 0x004A44E0 to 0x004A53E0.
// Replacement, off by default:
//
//   region.data.part1: QueryInterface and Release, the two name setters, the
//     terrain and water getters, the bounding rectangle and the copy of the
//     layout tiles, the destructor, the map from a cell to the city there
//     (find, erase, grow, insert), GetCity, InsertCity, RemoveCity,
//     DeleteCity, ReloadCity, GetAllCities, the test whether a city's cells
//     are taken, and the loop that takes every city off the list and moves
//     its file away.
//
// The object as this part uses it: the main vtable at +0, the reference
// count at +8, the name at +0xC and the folder name at +0x20 (strings), the
// base terrain type at +0x34, two water settings at +0x38 and +0x3C, the
// list of cities at +0x40 (a pointer to the sentinel node), the vector of
// layout tiles at +0x44 (12 bytes each: x, y, a third value) and the cell
// map at +0x50.
//
// The cell map is a hash table: the bucket vector at +4 (begin, end,
// capacity) and the number of nodes at +0x10. A node is {next, x, y, holder};
// the hash of a cell is ((x & 0xF) << 4 | y & 0xF) modulo the bucket count.
// A holder is a 4-byte heap cell with a counted pointer to the city; every
// cell a city covers points at the same holder.
//
// Still the game's: the list splice 0x004A4530 and the bit vector setup
// 0x004A46F0 (both called from unrelated code), the shared set and list
// helpers 0x004A4660 and 0x004A4A50.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::region {
namespace {

namespace object { // the region, from the object's start
constexpr std::size_t kRefCount = 0x8;
constexpr std::size_t kName = 0xC;       // string
constexpr std::size_t kNameBuffer = 0x10;
constexpr std::size_t kFolder = 0x20;    // string
constexpr std::size_t kFolderBuffer = 0x24;
constexpr std::size_t kTerrainType = 0x34;
constexpr std::size_t kWaterA = 0x38;
constexpr std::size_t kWaterB = 0x3C;
constexpr std::size_t kCities = 0x40;    // list: pointer to the sentinel node
constexpr std::size_t kTiles = 0x44;     // vector of 12-byte tiles
constexpr std::size_t kTilesEnd = 0x48;
constexpr std::size_t kCells = 0x50;     // hash map cell -> holder
} // namespace object

namespace cells { // the cell map, from its start
constexpr std::size_t kBuckets = 0x4;
constexpr std::size_t kBucketsEnd = 0x8;
constexpr std::size_t kBucketsCapacity = 0xC;
constexpr std::size_t kCount = 0x10;
} // namespace cells

constexpr uint32_t kRegionIid = 0x89D2BE26;

// A node of the cell map.
struct CellNode {
    CellNode* next;
    int32_t x;
    int32_t y;
    char** holder;
};
static_assert(sizeof(CellNode) == 0x10);

// A node of the game's lists.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    char* value;
};
static_assert(sizeof(ListNode) == 0xC);

// A node of the game's sets (red-black tree) of pointers.
struct TreeNode {
    uint8_t colour;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    char* value;
};
static_assert(sizeof(TreeNode) == 0x14);

// The game's string.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

// The game's free; what it leaves in EAX is handed on where the original's
// own return value is that.
uint32_t Free(void* memory)
{
    return Cdecl<uint32_t>(addr::SC4_Free, memory);
}

// An empty string local as the original builds it inline: an 8-byte buffer
// holding the terminator.
void InitString(GameString& text)
{
    text.begin = nullptr;
    text.end = nullptr;
    text.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The end of a string local; returns what the original has in EAX after it.
uint32_t DropString(GameString& text)
{
    char* const buffer = text.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        return Free(buffer);
    return 0;
}

uint32_t CellHash(int32_t x, int32_t y)
{
    return ((static_cast<uint32_t>(x) & 0xF) << 4) | (static_cast<uint32_t>(y) & 0xF);
}

uint32_t BucketCount(char* table)
{
    return static_cast<uint32_t>(
        static_cast<int32_t>(At<char*>(table, cells::kBucketsEnd) - At<char*>(table, cells::kBuckets)) >> 2);
}

CellNode**& Buckets(char* table)
{
    return At<CellNode**>(table, cells::kBuckets);
}

// ---------------------------------------------------------------------------
// The game's 0x004A44E0 (slot 0, ret 8): QueryInterface.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != 1 && iid != kRegionIid)
        return 0;
    *out = self;
    Virtual<uint32_t>(self, 0x4); // AddRef
    return 1;
}

// The game's 0x004A4510 (slot 4, ret 4): SetName, a jump into the name
// string's Copy.
void* __fastcall SetName(char* self, void*, void* name)
{
    return Virtual<void*>(self + object::kName, 0x34, name);
}

// The game's 0x004A4520 (slot 6, ret 4): SetDirectoryName, the same for the
// folder name.
void* __fastcall SetDirectoryName(char* self, void*, void* name)
{
    return Virtual<void*>(self + object::kFolder, 0x34, name);
}

// The game's 0x004A4570 (slot 2): Release. The last reference shuts the
// region down and deletes it; the count itself stays at 1 then.
uint32_t __fastcall Release(char* self, void*)
{
    const int32_t count = At<int32_t>(self, object::kRefCount);
    if (count > 1) {
        At<int32_t>(self, object::kRefCount) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    Virtual<uint8_t>(self, 0x24);               // Shutdown
    Virtual<void*>(self, 0x64, uint32_t{1});    // the deleting destructor
    return 0;
}

// The game's 0x004A45A0 (slot 24, ret 4): GetBoundingRect. The smallest and
// largest tile x and y (the tiles' own corners, their sizes are not added)
// into rect[0..3] as min x, min y, max x, max y; all zero when either span is
// not positive.
int32_t __fastcall GetBoundingRect(char* self, void*, int32_t* rect)
{
    const int32_t* tile = At<int32_t*>(self, object::kTiles);
    const int32_t* const end = At<int32_t*>(self, object::kTilesEnd);
    rect[1] = INT32_MAX;
    rect[0] = INT32_MAX;
    rect[2] = INT32_MIN;
    rect[3] = INT32_MIN;
    for (; tile != end; tile += 3) {
        if (tile[0] < rect[0])
            rect[0] = tile[0];
        if (tile[1] < rect[1])
            rect[1] = tile[1];
        if (rect[2] < tile[0])
            rect[2] = tile[0];
        if (rect[3] < tile[1])
            rect[3] = tile[1];
    }
    int32_t span = static_cast<int32_t>(static_cast<uint32_t>(rect[2]) - static_cast<uint32_t>(rect[0]));
    if (span > 0) {
        span = static_cast<int32_t>(static_cast<uint32_t>(rect[3]) - static_cast<uint32_t>(rect[1]));
        if (span > 0)
            return span;
    }
    rect[0] = 0;
    rect[1] = 0;
    rect[2] = 0;
    rect[3] = 0;
    return span;
}

// The game's 0x004A46A0 (thiscall on the cell map, ret 4): the node of the
// cell key[0], key[1], or null.
CellNode* __fastcall CellsFind(char* table, void*, const int32_t* key)
{
    const int32_t x = key[0];
    const int32_t y = key[1];
    CellNode* node = Buckets(table)[CellHash(x, y) % BucketCount(table)];
    while (node) {
        if (node->x == x && node->y == y)
            break;
        node = node->next;
    }
    return node;
}

// The game's 0x004A4760 (thiscall on the cell map, ret 4): removes the nodes
// of the cell key[0], key[1]; returns how many. The first node of the bucket
// is looked at last.
uint32_t __fastcall CellsErase(char* table, void*, const int32_t* key)
{
    const uint32_t bucket = CellHash(key[0], key[1]) % BucketCount(table);
    uint32_t erased = 0;
    CellNode* const first = Buckets(table)[bucket];
    if (!first)
        return 0;

    CellNode* previous = first;
    CellNode* node = first->next;
    while (node) {
        if (node->x == key[0] && node->y == key[1]) {
            previous->next = node->next;
            Free(node);
            node = previous->next;
            ++erased;
            --At<uint32_t>(table, cells::kCount);
        } else {
            previous = node;
            node = node->next;
        }
    }
    if (first->x == key[0] && first->y == key[1]) {
        Buckets(table)[bucket] = first->next;
        Free(first);
        --At<uint32_t>(table, cells::kCount);
        return erased + 1;
    }
    return erased;
}

// The game's 0x004A4830 (thiscall on the cell map, ret 4): makes room for
// `wanted` nodes: the next prime bucket count, every node moved to its new
// bucket.
void __fastcall CellsResize(char* table, void*, uint32_t wanted)
{
    const uint32_t old = BucketCount(table);
    if (wanted <= old)
        return;
    const auto* const primes = reinterpret_cast<const uint32_t*>(addr::SC4HashTable_Primes);
    const auto* const primesEnd = reinterpret_cast<const uint32_t*>(addr::SC4HashTable_PrimesEnd);
    const uint32_t* const prime =
        Cdecl<const uint32_t*>(addr::SC4_LowerBoundUint32, primes, primesEnd, &wanted, wanted, uint32_t{0});
    uint32_t count = prime == primesEnd ? 0xFFFFFFFBu : *prime;
    if (count <= old)
        return;

    struct {
        CellNode** begin;
        CellNode** end;
        CellNode** capacity;
    } fresh;
    CellNode* empty = nullptr;
    This<void*>(addr::SC4PtrVector_Construct, &fresh, count, &empty, &count);
    CellNode** const moved = fresh.begin;
    for (uint32_t bucket = 0; bucket < old; ++bucket) {
        CellNode* node = Buckets(table)[bucket];
        if (!node)
            continue;
        CellNode** from = Buckets(table) + bucket;
        do {
            const uint32_t to = CellHash(node->x, node->y) % count;
            *from = node->next;
            node->next = moved[to];
            moved[to] = node;
            from = Buckets(table) + bucket;
            node = *from;
        } while (node);
    }
    CellNode** const before = Buckets(table);
    Buckets(table) = moved;
    At<CellNode**>(table, cells::kBucketsEnd) = fresh.end;
    At<CellNode**>(table, cells::kBucketsCapacity) = fresh.capacity;
    if (before)
        Free(before);
}

// The game's 0x004A4930 (slot 11, ret 8): GetCity. The holder of the city at
// the cell, or null.
char** __fastcall GetCity(char* self, void*, int32_t x, int32_t y)
{
    const int32_t key[2] = {x, y};
    CellNode* const node = CellsFind(self + object::kCells, nullptr, key);
    if (node)
        return node->holder;
    return nullptr;
}

// The game's 0x004A4970 (thiscall on the region, ret 4): whether any cell the
// city covers (its position and size) is in the cell map.
uint8_t __fastcall CityCellsTaken(char* self, void*, char* city)
{
    int32_t x;
    int32_t z;
    int32_t width;
    int32_t height;
    Virtual<void>(city, 0x14, &x, &z);          // GetPosition
    Virtual<void>(city, 0x1C, &width, &height); // GetCitySize
    int32_t cellX = x;
    for (int32_t i = 0; i < width; ++i, ++cellX) {
        for (int32_t j = 0; j < height; ++j) {
            const int32_t cellY = j + z;
            const uint32_t hash = (static_cast<uint32_t>(cellY) & 0xF) | ((static_cast<uint32_t>(cellX) & 0xF) << 4);
            char* const table = self + object::kCells;
            CellNode* node = Buckets(table)[hash % BucketCount(table)];
            while (node) {
                if (node->x == cellX && node->y == cellY)
                    return 1;
                node = node->next;
            }
        }
    }
    return 0;
}

// The game's 0x004A4AD0 (thiscall on the cell map, ret 4): adds a node for
// entry {x, y, holder} at the head of its bucket, growing the table first;
// no check for a node already there. Returns the node's copy of the entry.
int32_t* __fastcall CellsInsert(char* table, void*, const int32_t* entry)
{
    CellsResize(table, nullptr, At<uint32_t>(table, cells::kCount) + 1);
    const uint32_t bucket = CellHash(entry[0], entry[1]) % BucketCount(table);
    CellNode* const first = Buckets(table)[bucket];
    CellNode* const node = Cdecl<CellNode*>(addr::SC4_stl_allocate, uint32_t{0x10});
    node->next = nullptr;
    node->x = entry[0];
    node->y = entry[1];
    node->holder = *reinterpret_cast<char** const*>(entry + 2);
    node->next = first;
    Buckets(table)[bucket] = node;
    ++At<uint32_t>(table, cells::kCount);
    return &node->x;
}

// The game's 0x004A4B50 (slot 13, ret 4): RemoveCity. Takes the city's cells
// out of the cell map, shuts the city down and deletes the holder (the
// caller's pointer to it is left as it was). The list of cities is not
// touched. Returns what the last Release of the city returned.
uint32_t __fastcall RemoveCity(char* self, void*, char*** reference)
{
    char* const city = **reference;
    if (city)
        Virtual<uint32_t>(city, 0x4);
    int32_t x;
    int32_t z;
    int32_t width;
    int32_t height;
    Virtual<void>(city, 0x14, &x, &z);          // GetPosition
    Virtual<void>(city, 0x1C, &width, &height); // GetCitySize
    for (int32_t i = 0; i < width; ++i) {
        for (int32_t j = 0; j < height; ++j) {
            int32_t key[2];
            key[0] = i + x;
            key[1] = j + z;
            CellsErase(self + object::kCells, nullptr, key);
        }
    }
    Virtual<uint8_t>(city, 0x10); // Shutdown

    char** const holder = *reference;
    if (holder) {
        if (char* const held = *holder)
            Virtual<uint32_t>(held, 0x8);
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(holder));
    }
    return Virtual<uint32_t>(city, 0x8);
}

// The game's 0x004A4C20 (slot 23, ret 4): GetCityLocations. Copies the vector
// of layout tiles into the caller's; returns that vector.
void* __fastcall GetCityLocations(char* self, void*, void* out)
{
    return This<void*>(addr::SC4TrafficRecords_Update, out, static_cast<void*>(self + object::kTiles));
}

// The game's 0x004A4C30 (thiscall on the region): the destructor.
void __fastcall Destroy(char* self, void*)
{
    char* const table = self + object::kCells;
    At<std::uintptr_t>(self, 0) = addr::cSC4Region_vftable;
    This<void>(addr::SC4Uint32HashSet_Clear, table);
    if (void* const buckets = At<void*>(table, cells::kBuckets))
        Free(buckets);
    if (void* const tiles = At<void*>(self, object::kTiles))
        Free(tiles);
    This<void>(addr::SC4List_Destroy4, self + object::kCities);
    At<std::uintptr_t>(self, object::kFolder) = addr::SC4String_Vtable;
    if (void* const buffer = At<void*>(self, object::kFolderBuffer))
        Free(buffer);
    At<std::uintptr_t>(self, object::kName) = addr::SC4String_Vtable;
    if (void* const buffer = At<void*>(self, object::kNameBuffer))
        Free(buffer);
}

// The game's 0x004A4CA0 (slot 20): GetBaseTerrainHeight, from a table by the
// terrain type.
int32_t __fastcall GetBaseTerrainHeight(char* self, void*)
{
    return Global<int32_t>(addr::cSC4Region_TerrainHeights + At<uint32_t>(self, object::kTerrainType) * 4);
}

// The game's 0x004A4CB0 (slot 19, ret 4): SetBaseTerrainType. Returns the
// type.
uint32_t __fastcall SetBaseTerrainType(char* self, void*, uint32_t type)
{
    At<uint32_t>(self, object::kTerrainType) = type;
    return type;
}

// The game's 0x004A4CC0 (slot 21, ret 8): GetWaterPrefs. The low bytes of
// the two settings; the original leaves the second in AL.
uint8_t __fastcall GetWaterPrefs(char* self, void*, uint8_t* first, uint8_t* second)
{
    *first = At<uint8_t>(self, object::kWaterA);
    const uint8_t other = At<uint8_t>(self, object::kWaterB);
    *second = other;
    return other;
}

// The game's 0x004A4CE0 (slot 17, ret 4): GetAllCities. Empties the caller's
// list, collects the cities of every node of the cell map in a set (one
// entry per city however many cells it covers) and appends them to the list
// in the set's order (by address), each with a reference. Returns what
// freeing the set's header left in EAX.
uint32_t __fastcall GetAllCities(char* self, void*, char* out)
{
    This<void>(addr::SC4List_ClearReleasing4, out);

    struct {
        TreeNode* header;
        uint32_t count;
        uint32_t unused;
    } cities;
    cities.header = nullptr;
    cities.header = Cdecl<TreeNode*>(addr::SC4_stl_allocate, uint32_t{0x14});
    cities.count = 0;
    cities.header->colour = 0;
    cities.header->parent = nullptr;
    cities.header->left = cities.header;
    cities.header->right = cities.header;

    struct {
        CellNode* node;
        char* table;
    } first;
    This<void*>(addr::SC4HashTable_Begin, self + object::kCells, &first);
    CellNode* cell = first.node;
    char* const table = first.table;
    while (cell) {
        char* key = *cell->holder;
        uint32_t result[4];
        This<void*>(addr::SC4PtrSet_Insert, &cities, &result[0], &key);
        if (cell->next) {
            cell = cell->next;
            continue;
        }
        uint32_t bucket = CellHash(cell->x, cell->y) % BucketCount(table);
        const uint32_t count = BucketCount(table);
        cell = nullptr;
        while (++bucket < count) {
            cell = Buckets(table)[bucket];
            if (cell)
                break;
        }
    }

    TreeNode* node = cities.header->left;
    while (node != cities.header) {
        char* const city = node->value;
        if (city)
            Virtual<uint32_t>(city, 0x4);
        ListNode* const sentinel = At<ListNode*>(out, 0);
        void* const allocator = NodeAllocatorAt(addr::SC4List_NodeAllocator4);
        ListNode* const fresh = Virtual<ListNode*>(allocator, 0xC, uint32_t{0xC});
        fresh->value = city;
        if (city)
            Virtual<uint32_t>(city, 0x4);
        ListNode* const last = sentinel->previous;
        fresh->next = sentinel;
        fresh->previous = last;
        last->next = fresh;
        sentinel->previous = fresh;
        if (city)
            Virtual<uint32_t>(city, 0x8);

        // The next node of the tree in order.
        TreeNode* next = node;
        if (node->right) {
            next = node->right;
            while (next->left)
                next = next->left;
        } else {
            TreeNode* parent = node->parent;
            if (node == parent->right) {
                do {
                    next = parent;
                    parent = parent->parent;
                } while (next == parent->right);
            }
            if (next->right != parent)
                next = parent;
        }
        node = next;
    }

    if (cities.count != 0) {
        TreeNode* branch = cities.header->parent;
        while (branch) {
            This<void>(addr::SC4Uint32Set_EraseTree, &cities, static_cast<void*>(branch->right));
            TreeNode* const left = branch->left;
            Free(branch);
            branch = left;
        }
        cities.header->left = cities.header;
        cities.header->parent = nullptr;
        cities.header->right = cities.header;
        cities.count = 0;
    }
    if (cities.header)
        return Free(cities.header);
    return 0;
}

// The game's 0x004A4F00 (slot 12, ret 4): InsertCity. Makes a holder for the
// city and points every cell the city covers at it, adding nodes where there
// are none. A holder already in a cell is overwritten, not released. The
// list of cities is not touched.
void __fastcall InsertCity(char* self, void*, char* city)
{
    int32_t x;
    int32_t z;
    int32_t width;
    int32_t height;
    Virtual<void>(city, 0x14, &x, &z);          // GetPosition
    Virtual<void>(city, 0x1C, &width, &height); // GetCitySize

    char** const holder = Cdecl<char**>(addr::SC4_operator_new, uint32_t{4});
    if (holder) {
        *holder = city;
        Virtual<uint32_t>(city, 0x4);
    }

    for (int32_t i = 0; i < width; ++i) {
        for (int32_t j = 0; j < height; ++j) {
            char* const table = self + object::kCells;
            const int32_t cellX = x + i;
            const int32_t cellY = j + z;
            CellNode* node = Buckets(table)[CellHash(cellX, cellY) % BucketCount(table)];
            while (node) {
                if (node->x == cellX && node->y == cellY)
                    break;
                node = node->next;
            }
            char*** slot;
            if (node) {
                slot = &node->holder;
            } else {
                int32_t entry[3];
                entry[0] = cellX;
                entry[1] = cellY;
                entry[2] = 0;
                slot = reinterpret_cast<char***>(CellsInsert(table, nullptr, entry) + 2);
            }
            *slot = holder;
        }
    }
}

// The game's 0x004A5030 (slot 15, ret 4): ReloadCity. Removes the city, reads
// it again from its file, and when that worked gives the new city its path,
// inserts it and hands the caller the holder at its position. Returns what
// freeing the path's buffer left in EAX.
uint32_t __fastcall ReloadCity(char* self, void*, char*** reference)
{
    GameString path;
    InitString(path);
    Virtual<void*>(**reference, 0x7C, &path); // GetCitySaveFilePath
    Virtual<uint32_t>(self, 0x34, reference); // RemoveCity

    char* loaded = nullptr;
    if (Cdecl<uint8_t>(addr::SC4RegionalCity_LoadFromFile, &path, &loaded)) {
        Virtual<void*>(loaded, 0x80, &path); // SetCitySaveFilePath
        Virtual<void>(self, 0x30, loaded);   // InsertCity
        int32_t x;
        int32_t z;
        Virtual<void>(loaded, 0x14, &x, &z); // GetPosition
        *reference = Virtual<char**>(self, 0x2C, x, z); // GetCity
    }
    if (loaded)
        Virtual<uint32_t>(loaded, 0x8);
    return DropString(path);
}

// The game's 0x004A5120 (slot 14, ret 4): DeleteCity. Removes the city and
// deletes its file. Returns what the last Release of the city returned.
uint32_t __fastcall DeleteCity(char* self, void*, char*** reference)
{
    char* const city = **reference;
    if (city)
        Virtual<uint32_t>(city, 0x4);
    GameString path;
    InitString(path);
    Virtual<void*>(city, 0x7C, &path);        // GetCitySaveFilePath
    Virtual<uint32_t>(self, 0x34, reference); // RemoveCity
    Cdecl<uint8_t>(addr::RZFile_Remove, &path);
    DropString(path);
    return Virtual<uint32_t>(city, 0x8);
}

// The game's 0x004A51C0 (thiscall on the region): until the list of cities
// is empty, takes the first city off it, shuts the city down, and copies its
// file to a path made from the nation's download directory, the city's name
// and its serial number; the file is deleted when the copy worked.
void __fastcall MoveCitiesAway(char* self, void*)
{
    {
        ListNode* const sentinel = At<ListNode*>(self, object::kCities);
        if (sentinel->next == sentinel)
            return;
    }
    do {
        char* const city = At<ListNode*>(self, object::kCities)->next->value;
        if (city)
            Virtual<uint32_t>(city, 0x4);

        ListNode* const node = At<ListNode*>(self, object::kCities)->next;
        ListNode* const previous = node->previous;
        ListNode* const next = node->next;
        previous->next = next;
        next->previous = previous;
        if (char* const held = node->value)
            Virtual<uint32_t>(held, 0x8);
        Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, node);

        Virtual<uint8_t>(city, 0x10); // Shutdown

        GameString path;
        InitString(path);
        GameString target;
        InitString(target);
        // A third string of the original of which only the buffer is left.
        char* const spare = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
        *spare = 0;
        GameString name;
        InitString(name);

        Virtual<void*>(city, 0x7C, &path); // GetCitySaveFilePath
        Virtual<void*>(city, 0x84, &name); // GetCityName
        const uint32_t serial = Virtual<uint32_t>(city, 0x6C);
        void* const nation = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x88);
        void* const directory = Virtual<void*>(nation, 0x3C);
        Cdecl<void>(addr::SC4RegionalCity_MakeFilePath, directory, &name, serial, &target);
        if (Cdecl<uint8_t>(addr::RZFile_Copy, &path, &target, uint32_t{1}))
            Cdecl<uint8_t>(addr::RZFile_Remove, &path);

        DropString(name);
        Free(spare);
        DropString(target);
        DropString(path);
        Virtual<uint32_t>(city, 0x8);
    } while (At<ListNode*>(self, object::kCities)->next != At<ListNode*>(self, object::kCities));
}

bool InstallPart1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4Region_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4Region_SetName, reinterpret_cast<void*>(&SetName)},
        {addr::cSC4Region_SetDirectoryName, reinterpret_cast<void*>(&SetDirectoryName)},
        {addr::cSC4Region_Release, reinterpret_cast<void*>(&Release)},
        {addr::cSC4Region_GetBoundingRect, reinterpret_cast<void*>(&GetBoundingRect)},
        {addr::cSC4Region_CellMapFind, reinterpret_cast<void*>(&CellsFind)},
        {addr::cSC4Region_CellMapErase, reinterpret_cast<void*>(&CellsErase)},
        {addr::cSC4Region_CellMapResize, reinterpret_cast<void*>(&CellsResize)},
        {addr::cSC4Region_GetCity, reinterpret_cast<void*>(&GetCity)},
        {addr::cSC4Region_CityCellsTaken, reinterpret_cast<void*>(&CityCellsTaken)},
        {addr::cSC4Region_CellMapInsert, reinterpret_cast<void*>(&CellsInsert)},
        {addr::cSC4Region_RemoveCity, reinterpret_cast<void*>(&RemoveCity)},
        {addr::cSC4Region_GetCityLocations, reinterpret_cast<void*>(&GetCityLocations)},
        {addr::cSC4Region_Destruct, reinterpret_cast<void*>(&Destroy)},
        {addr::cSC4Region_GetBaseTerrainHeight, reinterpret_cast<void*>(&GetBaseTerrainHeight)},
        {addr::cSC4Region_SetBaseTerrainType, reinterpret_cast<void*>(&SetBaseTerrainType)},
        {addr::cSC4Region_GetWaterPrefs, reinterpret_cast<void*>(&GetWaterPrefs)},
        {addr::cSC4Region_GetAllCities, reinterpret_cast<void*>(&GetAllCities)},
        {addr::cSC4Region_InsertCity, reinterpret_cast<void*>(&InsertCity)},
        {addr::cSC4Region_ReloadCity, reinterpret_cast<void*>(&ReloadCity)},
        {addr::cSC4Region_DeleteCity, reinterpret_cast<void*>(&DeleteCity)},
        {addr::cSC4Region_MoveCitiesAway, reinterpret_cast<void*>(&MoveCitiesAway)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static void* originals[count] = {};
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_part1Registered = registry::Register({
    "region.data.part1",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Region_GetCity,
    "Our region, part 1: the small slots, the cell map, GetCity and the city slots (docs/region.md)",
    false,
    InstallPart1,
});

} // namespace
} // namespace opensc4::region
