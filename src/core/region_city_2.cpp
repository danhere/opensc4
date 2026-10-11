// The regional city (cSC4RegionalCity, docs/region.md), part 2: 0x004A8960 to
// 0x004AA0E0. Replacement, off by default:
//
//   region.city.part2: the slot that gives a loaded city its names, difficulty,
//     save path and size, GetPointsOfInterest, GetAllConnections,
//     ChangeSymmetricConnection, the three collectors of connection records,
//     the deal filter with SetLocalDeals, UpdateLocalDeals and GetLocalDeals,
//     GetAllowableExtrapolation, ExtrapolateGrowth and the two loops over the
//     population map, SetPosition, the commodity getters and setters, and the
//     class's own container code: the commodity map (find or add, insert with
//     a hint), the points of interest map (insert, erase) and the sort of the
//     connection records (introsort, heap sort, insertion sort).
//
// The object as this part uses it (from the object's start): the position at
// +0x14 (x) and +0x18 (z), the size at +0x1C and +0x20 (in small city tiles),
// the difficulty level at +0x38, three maps from a population type to a
// number at +0x3C (the population), +0x48 (the extrapolated population) and
// +0x54 (the most the extrapolated population may reach), the list of
// connections at +0x90, a vector of 16-byte connection records at +0x94
// (type, x, z, a flag byte), a byte at +0xA0 set when the city is moved, the
// city name at +0xD0 and the mayor name at +0xF8 (strings), the commodity map
// at +0x180 (commodity -> five doubles: produced, demanded, exported,
// imported, cost per unit), the list of every deal at +0x18C, the list of the
// deals with the open city at +0x190 and the points of interest map at +0x194
// (type -> a vector of 12-byte items).
//
// Still the game's: the list assignment 0x004A9150 (called from the
// neighbours simulator too). 0x004A8B30 and 0x004A90A0 are not functions:
// they are inside 0x004A8A80 and 0x004A8EC0.
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

namespace object { // the regional city, from the object's start
constexpr std::size_t kX = 0x14;
constexpr std::size_t kZ = 0x18;
constexpr std::size_t kWidth = 0x1C;
constexpr std::size_t kHeight = 0x20;
constexpr std::size_t kDifficulty = 0x38;
constexpr std::size_t kPopulation = 0x3C;     // map type -> number
constexpr std::size_t kExtrapolated = 0x48;   // map type -> number
constexpr std::size_t kAllowable = 0x54;      // map type -> number
constexpr std::size_t kConnections = 0x90;    // list: pointer to the sentinel node
constexpr std::size_t kRecords = 0x94;        // vector of 16-byte records
constexpr std::size_t kRecordsEnd = 0x98;
constexpr std::size_t kRecordsCapacity = 0x9C;
constexpr std::size_t kMoved = 0xA0;
constexpr std::size_t kCityName = 0xD0;       // string
constexpr std::size_t kMayorName = 0xF8;      // string
constexpr std::size_t kCommodities = 0x180;   // map commodity -> five doubles
constexpr std::size_t kDeals = 0x18C;         // list
constexpr std::size_t kLocalDeals = 0x190;    // list
constexpr std::size_t kPointsOfInterest = 0x194; // map type -> vector
} // namespace object

namespace commodity { // indices into a commodity's five doubles
constexpr std::size_t kProduced = 0;
constexpr std::size_t kDemanded = 1;
constexpr std::size_t kExported = 2;
constexpr std::size_t kImported = 3;
constexpr std::size_t kCostPerUnit = 4;
} // namespace commodity

// A node of the game's lists.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    char* value;
};
static_assert(sizeof(ListNode) == 0xC);

// A node of the game's maps (red-black tree) keyed by a number; the value
// follows the key.
struct TreeNode {
    uint8_t colour;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    int32_t key;
    int32_t number; // the value of the population maps
};
static_assert(sizeof(TreeNode) == 0x18);

constexpr std::size_t kCommodityValues = 0x18; // from a commodity node: five doubles
constexpr std::size_t kPointsVector = 0x14;    // from a points of interest node: a vector
constexpr uint32_t kPointsNodeSize = 0x20;

// What the commodity map stores for a key.
struct CommodityEntry {
    int32_t key;
    uint32_t padding;
    double values[5];
};
static_assert(sizeof(CommodityEntry) == 0x30);

// What a tree's insert hands back: the node and whether it is new.
struct InsertResult {
    TreeNode* node;
    uint8_t added;
};
static_assert(sizeof(InsertResult) == 8);

// A connection record of the vector at +0x94.
struct Record {
    uint32_t type;
    int32_t x;
    int32_t z;
    uint32_t flag;
};
static_assert(sizeof(Record) == 0x10);

using Compare = uint8_t(__cdecl*)(const Record*, const Record*);

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

uint32_t Free(void* memory)
{
    return Cdecl<uint32_t>(addr::SC4_Free, memory);
}

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword.
int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The value's address in a map from a number to a number, added as 0 when
// missing.
int32_t* MapAt(char* map, const uint32_t* key)
{
    return This<int32_t*>(addr::SC4Uint32Map_At, map, key);
}

// The next node of a tree in order, as the original walks it inline.
TreeNode* TreeNext(TreeNode* node)
{
    if (node->right) {
        TreeNode* next = node->right;
        while (next->left)
            next = next->left;
        return next;
    }
    TreeNode* next = node;
    TreeNode* parent = node->parent;
    if (node == parent->right) {
        do {
            next = parent;
            parent = parent->parent;
        } while (next == parent->right);
    }
    if (next->right != parent)
        next = parent;
    return next;
}

// An empty list local as the original builds it inline: a sentinel node from
// the allocator cached at `allocator`, linked to itself.
ListNode* NewList(std::uintptr_t allocator)
{
    ListNode* const sentinel = Virtual<ListNode*>(NodeAllocatorAt(allocator), 0xC, uint32_t{0xC});
    sentinel->next = sentinel;
    sentinel->previous = sentinel;
    return sentinel;
}

int32_t ByteSpan(const Record* first, const Record* last)
{
    return static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(last) - reinterpret_cast<std::uintptr_t>(first));
}

// A record added at the end of the vector at +0x94, through the game's
// reallocating insert when the vector is full.
void PushRecord(char* self, const Record& record)
{
    char* const records = self + object::kRecords;
    Record* const end = At<Record*>(self, object::kRecordsEnd);
    if (end != At<Record*>(self, object::kRecordsCapacity)) {
        if (end)
            *end = record;
        At<Record*>(self, object::kRecordsEnd) = At<Record*>(self, object::kRecordsEnd) + 1;
        return;
    }
    uint8_t tag = 0;
    This<void>(addr::SC4Vector16_InsertOverflow, records, end, &record, &tag, uint32_t{1}, uint32_t{1});
}

// ---------------------------------------------------------------------------
// The game's 0x004A8960 (slot 26, ret 4): gives a city object what the region
// knows: the mayor name, the city name, the difficulty level, the path of the
// save file (made from the city name with serial 0) and the size in metres
// (1024 a tile). Returns what freeing the path's buffer left in EAX.
uint32_t __fastcall SetupCity(char* self, void*, char* city)
{
    Virtual<void>(city, 0x44, static_cast<void*>(self + object::kMayorName)); // SetMayorName
    char* const name = self + object::kCityName;
    Virtual<void>(city, 0x34, static_cast<void*>(name));                      // SetCityName
    Virtual<void>(city, 0x64, At<uint32_t>(self, object::kDifficulty));       // SetDifficultyLevel

    GameString path;
    path.begin = nullptr;
    path.end = nullptr;
    path.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    path.begin = buffer;
    path.end = buffer;
    path.capacity = buffer + 8;
    *buffer = 0;
    path.vtable = addr::SC4String_Vtable;
    path.zero = 0;
    Cdecl<void>(addr::SC4RegionalCity_MakeSavePath, static_cast<void*>(name), uint32_t{0}, &path);
    Virtual<void>(city, 0x2C, &path); // SetCitySaveFilePath

    const float depth = static_cast<float>(static_cast<int32_t>(At<uint32_t>(self, object::kHeight) << 10));
    const float width = static_cast<float>(static_cast<int32_t>(At<uint32_t>(self, object::kWidth) << 10));
    Virtual<uint8_t>(city, 0x160, width, depth); // SetSize

    char* const held = path.begin;
    path.vtable = addr::SC4String_Vtable;
    if (held)
        return Free(held);
    return 0;
}

// The game's 0x004A8A40 (slot 72, ret 8): GetPointsOfInterest. Copies the
// type's vector of the map at +0x194 into the caller's; nothing when the type
// has none.
void* __fastcall GetPointsOfInterest(char* self, void*, int32_t type, void* out)
{
    char* const map = self + object::kPointsOfInterest;
    TreeNode* found;
    This<void*>(addr::SC4Sint32Map_Find, map, &found, &type);
    if (found != At<TreeNode*>(map, 0)) {
        return This<void*>(addr::SC4TrafficRecords_Update, out,
                           static_cast<void*>(reinterpret_cast<char*>(found) + kPointsVector));
    }
    return found;
}

// The game's 0x004A8A80 (thiscall on the commodity map, ret 0xC): the map's
// insert with a hint. Puts the node of the entry's key into *out, adding the
// entry next to the hint when it belongs there and through the plain insert
// otherwise.
TreeNode** __fastcall CommodityInsertHint(char* tree, void*, TreeNode** out, TreeNode* hint, const int32_t* entry)
{
    const auto insert = [tree, out, entry](TreeNode* x, TreeNode* y, TreeNode* w) {
        This<void*>(addr::cSC4RegionalCity_CommodityInsertNode, tree, out, x, y, entry, w);
        return out;
    };
    InsertResult result;
    const auto insertUnique = [tree, out, entry, &result]() {
        *out = This<InsertResult*>(addr::cSC4RegionalCity_CommodityInsertUnique, tree, &result, entry)->node;
        return out;
    };

    TreeNode* const header = At<TreeNode*>(tree, 0);
    if (hint == header->left) {
        if (At<uint32_t>(tree, 4) == 0)
            return insertUnique();
        const int32_t key = *entry;
        if (hint->key > key)
            return insert(hint, hint, nullptr);
        if (!(hint->key < key)) {
            *out = hint;
            return out;
        }
        TreeNode* after = hint;
        This<void>(addr::SC4MapIterator_Next, &after);
        if (after == At<TreeNode*>(tree, 0))
            return insert(nullptr, hint, hint);
        if (key < after->key) {
            if (!hint->right)
                return insert(nullptr, hint, hint);
            return insert(after, after, nullptr);
        }
        return insertUnique();
    }
    if (hint == header) {
        TreeNode* const last = header->right;
        if (last->key < *entry)
            return insert(nullptr, last, hint);
        return insertUnique();
    }

    TreeNode* before = hint;
    This<void>(addr::SC4TreeIterator_Decrement, &before);
    const int32_t key = *entry;
    const uint8_t less = key < hint->key;
    if (less && before->key < key) {
        if (!before->right)
            return insert(nullptr, before, before);
        return insert(hint, hint, nullptr);
    }
    TreeNode* after = hint;
    This<void>(addr::SC4MapIterator_Next, &after);
    uint8_t greater = less == 0;
    if (!less) {
        greater = hint->key < key;
        if (!greater) {
            *out = hint;
            return out;
        }
        if (after == At<TreeNode*>(tree, 0) || key < after->key) {
            if (!hint->right)
                return insert(nullptr, hint, hint);
            return insert(after, after, nullptr);
        }
    }
    if (less == greater) {
        *out = hint;
        return out;
    }
    return insertUnique();
}

// The game's 0x004A8C90 (cdecl: first, last, compare): insertion sort of
// connection records; each record goes into the sorted run before it through
// 0x004A8830.
void __cdecl InsertionSort(Record* first, Record* last, Compare compare)
{
    if (first == last)
        return;
    for (Record* item = first + 1; item != last; ++item) {
        const Record value = *item;
        Cdecl<void>(addr::cSC4RegionalCity_SortLinearInsert, first, item, value.type, value.x, value.z, value.flag, compare);
    }
}

// The game's 0x004A8CF0 (cdecl: first, middle, last, an unused pointer,
// compare): heap sort. Makes a heap of [first, middle), swaps in every later
// record that sorts before the top, then sorts the heap.
void __cdecl PartialSort(Record* first, Record* middle, Record* last, void*, Compare compare)
{
    Cdecl<void>(addr::cSC4RegionalCity_SortMakeHeap, first, middle, compare, uint32_t{0}, uint32_t{0});
    for (Record* item = middle; item < last; ++item) {
        if (compare(item, first)) {
            const Record value = *item;
            *item = *first;
            Cdecl<void>(addr::cSC4RegionalCity_SortAdjustHeap, first, int32_t{0}, ByteSpan(first, middle) >> 4, value.type,
                        value.x, value.z, value.flag, compare);
        }
    }
    Cdecl<void>(addr::cSC4RegionalCity_SortHeap, first, middle, compare);
}

// The game's 0x004A9DE0 (cdecl: first, last, an unused pointer, depth,
// compare): the loop of the introsort. Runs of 16 records or fewer are left
// for the insertion sort; out of depth it falls back to the heap sort.
void __cdecl IntroSortLoop(Record* first, Record* last, void*, int32_t depth, Compare compare)
{
    if ((ByteSpan(first, last) & ~0xF) <= 0x100)
        return;
    for (;;) {
        if (depth == 0) {
            PartialSort(first, last, last, nullptr, compare);
            return;
        }
        --depth;
        Record* const middle = first + (ByteSpan(first, last) >> 4) / 2;
        Record* const back = last - 1;
        const Record* pivot;
        if (compare(first, middle)) {
            if (compare(middle, back))
                pivot = middle;
            else
                pivot = compare(first, back) ? back : first;
        } else {
            if (compare(first, back))
                pivot = first;
            else
                pivot = compare(middle, back) ? back : middle;
        }
        const Record value = *pivot;
        Record* const cut = Cdecl<Record*>(addr::cSC4RegionalCity_SortPartition, first, last, value.type, value.x, value.z,
                                           value.flag, compare);
        IntroSortLoop(cut, last, nullptr, depth, compare);
        last = cut;
        if ((ByteSpan(first, cut) & ~0xF) <= 0x100)
            return;
    }
}

// The game's 0x004A8DB0 (slot 21, ret 4): GetAllConnections. Empties the
// caller's list and appends every connection, referenced.
void* __fastcall GetAllConnections(char* self, void*, char* list)
{
    This<void>(addr::SC4List_ClearReleasing, list);
    ListNode* const sentinel = At<ListNode*>(self, object::kConnections);
    ListNode* const first = sentinel->next;
    ListNode* out;
    return This<void*>(addr::SC4List_InsertRange, list, At<ListNode*>(list, 0), first, sentinel, &out);
}

// The game's 0x004A8DF0 (slot 22, ret 8): ChangeSymmetricConnection. Adds a
// record for the cell across the city edge from the connection: its type, the
// cell on the other side and the flag.
void __fastcall ChangeSymmetricConnection(char* self, void*, char* connection, uint8_t on)
{
    int32_t x;
    int32_t z;
    Virtual<void>(connection, 0x18, &x, &z);
    int32_t tileX;
    int32_t tileZ;
    int32_t otherX;
    int32_t otherZ;
    void* city = nullptr;
    Cdecl<uint8_t>(addr::cSC4NeighborsSimulator_OtherSideCell, x, z, &tileX, &tileZ, &city, &otherX, &otherZ);
    Record record;
    record.type = Virtual<uint32_t>(connection, 0x14);
    record.x = otherX;
    record.z = otherZ;
    record.flag = on;
    PushRecord(self, record);
}

// The game's 0x004A8EC0 (thiscall on the city, ret 0xC): goes through the
// deals of `source`. A deal between this city and the open one (either end
// when this is the open city) is appended to `target`, referenced. A deal
// with an end that is no city of the region, and with `prune` a deal that was
// appended, is taken out of `source` afterwards, last first.
void __fastcall FilterDeals(char* self, void*, ListNode** source, ListNode** target, uint8_t prune)
{
    if (!Global<void*>(addr::SC4_City))
        return;
    if (!Global<void*>(addr::SC4_MonthEndService))
        return;
    void* const region = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x8C);
    char* const current = Virtual<char*>(Global<void*>(addr::SC4_Service43C94), 0x90);

    struct {
        ListNode** begin;
        ListNode** end;
        ListNode** capacity;
    } stale;
    stale.begin = nullptr;
    stale.end = nullptr;
    stale.capacity = nullptr;
    ListNode** end = nullptr;

    ListNode* node = (*source)->next;
    if (node == *source)
        return;
    const auto remember = [&stale, &end, &node]() {
        if (end == stale.capacity) {
            uint8_t tag = 0;
            This<void>(addr::SC4Uint32Vector_InsertN2, &stale, end, &node, &tag, uint32_t{1}, uint32_t{1});
            end = stale.end;
        } else {
            if (end)
                *end = node;
            ++end;
            stale.end = end;
        }
    };
    do {
        char* const deal = node->value;
        int32_t x;
        int32_t z;
        Virtual<void>(deal, 0x10, &x, &z);
        char** const first = Virtual<char**>(region, 0x2C, x, z); // GetCity
        Virtual<void>(deal, 0x18, &x, &z);
        char** const second = Virtual<char**>(region, 0x2C, x, z);
        if (!first || !second) {
            remember();
        } else {
            bool ours;
            if (self == current) {
                ours = *first == current || *second == current;
            } else {
                char* const city = *first;
                ours = (city == current && *second == self) || (city == self && *second == current);
            }
            if (ours) {
                Virtual<uint32_t>(deal, 0x4);
                ListNode* const sentinel = *target;
                ListNode* const fresh =
                    This<ListNode*>(addr::SC4List_AllocateNodes1, target, uint32_t{1}, uint32_t{0});
                fresh->value = deal;
                Virtual<uint32_t>(deal, 0x4);
                ListNode* const last = sentinel->previous;
                fresh->next = sentinel;
                fresh->previous = last;
                last->next = fresh;
                sentinel->previous = fresh;
                Virtual<uint32_t>(deal, 0x8);
                if (prune)
                    remember();
            }
        }
        node = node->next;
    } while (node != *source);

    ListNode** const begin = stale.begin;
    while (begin != end) {
        ListNode* out;
        This<void*>(addr::SC4List_EraseReleasing1, source, &out, end[-1]);
        --end;
    }
    if (begin)
        Free(begin);
}

// The game's 0x004A90D0 (slot 69, ret 4): SetLocalDeals. Empties the list at
// +0x190, fills it from the caller's list and updates imports and exports.
uint8_t __fastcall SetLocalDeals(char* self, void*, ListNode** deals)
{
    char* const local = self + object::kLocalDeals;
    This<void>(addr::SC4List_Clear, local);
    FilterDeals(self, nullptr, deals, reinterpret_cast<ListNode**>(local), 0);
    Virtual<uint8_t>(self, 0x11C); // UpdateImportExport
    return 1;
}

// The game's 0x004A9110 (slot 68): UpdateLocalDeals. Moves the deals with the
// open city from the list at +0x18C to the one at +0x190 (which is not
// emptied first) and calls slot 37 of every deal that stays with (0x80, 0).
uint8_t __fastcall UpdateLocalDeals(char* self, void*)
{
    ListNode** const deals = &At<ListNode*>(self, object::kDeals);
    FilterDeals(self, nullptr, deals, &At<ListNode*>(self, object::kLocalDeals), 1);
    ListNode* node = (*deals)->next;
    if (node != *deals) {
        do {
            Virtual<void>(node->value, 0x94, uint32_t{0x80}, uint32_t{0});
            node = node->next;
        } while (node != *deals);
    }
    return 1;
}

// The game's 0x004A9220 (thiscall on the city): for every city the
// neighbours simulator lists and every connection of it that leads to this
// city, adds a record with the connection's type, its cell seen from the open
// city and flag 1. The vector grows inline here, not through the game's
// reallocating insert.
void __fastcall CollectNeighbourConnections(char* self, void*)
{
    void* const neighbors = Global<void*>(addr::SC4_MonthEndService);
    ListNode* cities = nullptr;
    cities = NewList(addr::SC4List_NodeAllocator4);
    Virtual<void>(neighbors, 0x50, &cities, uint32_t{1});

    ListNode* cityNode = cities->next;
    if (cityNode != cities) {
        do {
            char* const other = cityNode->value;
            ListNode* connections = nullptr;
            connections = NewList(addr::SC4List_NodeAllocator3);
            Virtual<void>(other, 0x54, &connections); // GetAllConnections

            ListNode* node = connections->next;
            if (node != connections) {
                do {
                    char* const connection = node->value;
                    if (Virtual<char*>(connection, 0x24) == self) {
                        int32_t x;
                        int32_t z;
                        Virtual<void>(connection, 0x18, &x, &z);
                        int32_t acrossX;
                        int32_t acrossZ;
                        Cdecl<uint8_t>(addr::cSC4NeighborsSimulator_CellAcross, static_cast<void*>(other), x, z,
                                       &acrossX, &acrossZ);
                        Record record;
                        record.type = Virtual<uint32_t>(connection, 0x14);
                        record.x = acrossX;
                        record.z = acrossZ;
                        record.flag = 1;

                        Record* const end = At<Record*>(self, object::kRecordsEnd);
                        if (end != At<Record*>(self, object::kRecordsCapacity)) {
                            if (end)
                                *end = record;
                            At<Record*>(self, object::kRecordsEnd) += 1;
                        } else {
                            const uint32_t size =
                                static_cast<uint32_t>(ByteSpan(At<Record*>(self, object::kRecords), end) >> 4);
                            const uint32_t grown = (size < 1 ? 1 : size) + size;
                            Record* fresh = nullptr;
                            if (grown != 0)
                                fresh = Cdecl<Record*>(addr::SC4_stl_allocate, grown << 4);
                            Record* to = fresh;
                            for (Record* from = At<Record*>(self, object::kRecords); from != end; ++from, ++to) {
                                if (to)
                                    *to = *from;
                            }
                            if (to)
                                *to = record;
                            Record* const freshEnd = to + 1;
                            if (void* const old = At<void*>(self, object::kRecords))
                                Free(old);
                            At<Record*>(self, object::kRecords) = fresh;
                            At<Record*>(self, object::kRecordsCapacity) = fresh + grown;
                            At<Record*>(self, object::kRecordsEnd) = freshEnd;
                        }
                    }
                    node = node->next;
                } while (node != connections);
            }

            This<void>(addr::SC4List_ClearReleasing, &connections);
            Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, connections);
            cityNode = cityNode->next;
        } while (cityNode != cities);
    }

    This<void>(addr::SC4List_ClearReleasing4, &cities);
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, cities);
}

// The game's 0x004A9580 (thiscall on the city): for every connection of this
// city whose cell the neighbours simulator has an object for (its slot 12)
// that answers its slot 43 with true, adds a record with the connection's
// type, its cell and flag 0.
void __fastcall CollectOwnConnections(char* self, void*)
{
    void* const neighbors = Global<void*>(addr::SC4_MonthEndService);
    ListNode* connections = nullptr;
    connections = NewList(addr::SC4List_NodeAllocator3);
    Virtual<void>(self, 0x54, &connections); // GetAllConnections

    ListNode* node = connections->next;
    if (node != connections) {
        do {
            char* const connection = node->value;
            int32_t x;
            int32_t z;
            Virtual<void>(connection, 0x18, &x, &z);
            void* const there = Virtual<void*>(neighbors, 0x30, x, z);
            if (there && Virtual<uint8_t>(there, 0xAC)) {
                Record record;
                record.type = Virtual<uint32_t>(connection, 0x14);
                record.x = x;
                record.z = z;
                record.flag = 0;
                PushRecord(self, record);
            }
            node = node->next;
        } while (node != connections);
    }

    This<void>(addr::SC4List_ClearReleasing, &connections);
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, connections);
}

// The game's 0x004A9720 (thiscall on the city): hands this city to slot 10 of
// each of its connections. The list is emptied twice, as in the original.
void __fastcall NotifyConnections(char* self, void*)
{
    ListNode* connections = nullptr;
    connections = NewList(addr::SC4List_NodeAllocator3);
    Virtual<void>(self, 0x54, &connections); // GetAllConnections

    ListNode* node = connections->next;
    if (node != connections) {
        do {
            Virtual<void>(node->value, 0x28, static_cast<void*>(self));
            node = node->next;
        } while (node != connections);
    }

    This<void>(addr::SC4List_ClearReleasing, &connections);
    This<void>(addr::SC4List_ClearReleasing, &connections);
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, connections);
}

// The game's 0x004A9820 (slot 18, ret 4): GetAllowableExtrapolation. How far
// the type's extrapolated population is below its limit, 0 when it is not.
int32_t __fastcall GetAllowableExtrapolation(char* self, void*, uint32_t type)
{
    const int32_t* const limit = MapAt(self + object::kAllowable, &type);
    const int32_t* const now = MapAt(self + object::kExtrapolated, &type);
    const int32_t room = static_cast<int32_t>(static_cast<uint32_t>(*limit) - static_cast<uint32_t>(*now));
    return room <= 0 ? 0 : room;
}

// The game's 0x004A9860 (slot 19, ret 8): ExtrapolateGrowth. Adds to the
// type's extrapolated population, truncating; returns the new number.
int32_t __fastcall ExtrapolateGrowth(char* self, void*, uint32_t type, float added)
{
    int32_t* const now = MapAt(self + object::kExtrapolated, &type);
    const int32_t sum = Truncate(static_cast<double>(*now) + static_cast<double>(added));
    *now = sum;
    return sum;
}

// The game's 0x004A9890 (thiscall on the city, ret 4, the argument is not
// read): sets every population type's limit: 1.1 times the population but at
// least 1000, and 0 for a population of 0 or less.
void __fastcall SetAllowableGrowth(char* self, void*, uint32_t)
{
    TreeNode* node = At<TreeNode*>(self, object::kPopulation)->left;
    if (node == At<TreeNode*>(self, object::kPopulation))
        return;
    do {
        uint32_t type = static_cast<uint32_t>(node->key);
        const int32_t population = node->number;
        float limit;
        if (population > 0) {
            const double grown = static_cast<double>(population) * static_cast<double>(1.1f);
            const float rounded = static_cast<float>(grown);
            limit = grown < static_cast<double>(1000.0f) ? 1000.0f : rounded;
        } else {
            limit = 0.0f;
        }
        int32_t* const slot = MapAt(self + object::kAllowable, &type);
        *slot = Truncate(static_cast<double>(limit));
        node = TreeNext(node);
    } while (node != At<TreeNode*>(self, object::kPopulation));
}

// The game's 0x004A9970 (thiscall on the city, ret 4, the argument is not
// read): makes every type's extrapolated population its population.
void __fastcall ResetExtrapolation(char* self, void*, uint32_t)
{
    TreeNode* node = At<TreeNode*>(self, object::kPopulation)->left;
    if (node == At<TreeNode*>(self, object::kPopulation))
        return;
    do {
        uint32_t type = static_cast<uint32_t>(node->key);
        const int32_t population = node->number;
        *MapAt(self + object::kExtrapolated, &type) = population;
        node = TreeNext(node);
    } while (node != At<TreeNode*>(self, object::kPopulation));
}

// Both ends of every deal of a list moved by (dx, dz).
void MoveDeals(char* self, std::size_t list, int32_t dx, int32_t dz)
{
    ListNode* node = At<ListNode*>(self, list)->next;
    if (node == At<ListNode*>(self, list))
        return;
    do {
        char* const deal = node->value;
        node = node->next;
        int32_t x;
        int32_t z;
        Virtual<void>(deal, 0x10, &x, &z);
        Virtual<void>(deal, 0xC, static_cast<int32_t>(static_cast<uint32_t>(dx) + static_cast<uint32_t>(x)),
                      static_cast<int32_t>(static_cast<uint32_t>(dz) + static_cast<uint32_t>(z)));
        Virtual<void>(deal, 0x18, &x, &z);
        Virtual<void>(deal, 0x14, static_cast<int32_t>(static_cast<uint32_t>(dx) + static_cast<uint32_t>(x)),
                      static_cast<int32_t>(static_cast<uint32_t>(dz) + static_cast<uint32_t>(z)));
    } while (node != At<ListNode*>(self, list));
}

// The game's 0x004A99F0 (slot 6, ret 0xC): SetPosition. With `rearrange` the
// city is marked moved, its connections are told, and both ends of every deal
// of both lists move by as much as the city did.
void __fastcall SetPosition(char* self, void*, int32_t x, int32_t z, uint8_t rearrange)
{
    const uint32_t oldX = At<uint32_t>(self, object::kX);
    const uint32_t oldZ = At<uint32_t>(self, object::kZ);
    At<int32_t>(self, object::kZ) = z;
    At<int32_t>(self, object::kX) = x;
    if (!rearrange)
        return;
    At<uint8_t>(self, object::kMoved) = 1;
    NotifyConnections(self, nullptr);
    const int32_t dx = static_cast<int32_t>(At<uint32_t>(self, object::kX) - oldX);
    const int32_t dz = static_cast<int32_t>(At<uint32_t>(self, object::kZ) - oldZ);
    MoveDeals(self, object::kDeals, dx, dz);
    MoveDeals(self, object::kLocalDeals, dx, dz);
}

// The game's 0x004A9B40 (thiscall on the commodity map, ret 4): the five
// doubles of the commodity *key, added as zeros when the map has none.
double* __fastcall CommodityAt(char* map, void*, const int32_t* key)
{
    TreeNode* const header = At<TreeNode*>(map, 0);
    TreeNode* node = header->parent;
    TreeNode* found = header;
    if (node) {
        const int32_t wanted = *key;
        do {
            if (node->key < wanted) {
                node = node->right;
            } else {
                found = node;
                node = node->left;
            }
        } while (node);
    }
    if (found == header || *key < found->key) {
        CommodityEntry entry;
        entry.key = *key;
        entry.padding = 0;
        for (double& value : entry.values)
            value = 0.0;
        TreeNode* added;
        CommodityInsertHint(map, nullptr, &added, found, &entry.key);
        return reinterpret_cast<double*>(reinterpret_cast<char*>(added) + kCommodityValues);
    }
    return reinterpret_cast<double*>(reinterpret_cast<char*>(found) + kCommodityValues);
}

// The game's 0x004A9C00 (thiscall on the points of interest map, ret 4):
// frees a node and everything under it, each with its vector's buffer.
void __fastcall PointsEraseTree(char* map, void*, TreeNode* node)
{
    while (node) {
        PointsEraseTree(map, nullptr, node->right);
        TreeNode* const left = node->left;
        if (void* const buffer = At<void*>(node, kPointsVector))
            Free(buffer);
        Free(node);
        node = left;
    }
}

// The game's 0x004A9C50 (thiscall on the points of interest map, ret 0x14):
// links a new node holding a copy of the entry (a type and a vector) under
// `parent`, on the left when `parent` is the header, or `onLeft` is set, or
// the type sorts before the parent's, unless `onRight` is set; rebalances and
// counts it. Puts the node into *out.
TreeNode** __fastcall PointsInsertNode(char* map, void*, TreeNode** out, TreeNode* onLeft, TreeNode* parent,
                                       const int32_t* entry, TreeNode* onRight)
{
    const auto make = [entry]() {
        TreeNode* const node = Cdecl<TreeNode*>(addr::SC4_stl_allocate, kPointsNodeSize);
        node->key = entry[0];
        This<void*>(addr::SC4Vector12_CopyConstruct, reinterpret_cast<char*>(node) + kPointsVector,
                    static_cast<const void*>(entry + 1));
        return node;
    };
    TreeNode* node;
    if (parent != At<TreeNode*>(map, 0) && (onRight || (!onLeft && !(entry[0] < parent->key)))) {
        node = make();
        parent->right = node;
        TreeNode* const header = At<TreeNode*>(map, 0);
        if (parent == header->right)
            header->right = node;
    } else {
        node = make();
        parent->left = node;
        TreeNode* const header = At<TreeNode*>(map, 0);
        if (parent == header) {
            header->parent = node;
            At<TreeNode*>(map, 0)->right = node;
        } else if (parent == header->left) {
            header->left = node;
        }
    }
    node->parent = parent;
    node->left = nullptr;
    node->right = nullptr;
    Cdecl<void>(addr::SC4Tree_RebalanceForInsert, node, &At<TreeNode*>(map, 0)->parent);
    At<uint32_t>(map, 4) = At<uint32_t>(map, 4) + 1;
    *out = node;
    return out;
}

// The game's 0x004A9D20 (thiscall on the points of interest map, ret 8): adds
// the entry unless its type is there; the result is the type's node and
// whether it is new.
InsertResult* __fastcall PointsInsertUnique(char* map, void*, InsertResult* out, const int32_t* entry)
{
    TreeNode* const header = At<TreeNode*>(map, 0);
    TreeNode* node = header->parent;
    TreeNode* parent = header;
    uint8_t less = 1;
    if (node) {
        const int32_t key = *entry;
        do {
            less = key < node->key;
            parent = node;
            node = less ? node->left : node->right;
        } while (node);
    }
    TreeNode* before = parent;
    if (less) {
        if (parent == header->left) {
            TreeNode* added;
            out->node = *PointsInsertNode(map, nullptr, &added, node, parent, entry, nullptr);
            out->added = 1;
            return out;
        }
        This<void>(addr::SC4TreeIterator_Decrement, &before);
    }
    if (before->key < *entry) {
        TreeNode* added;
        TreeNode* const fresh = *PointsInsertNode(map, nullptr, &added, node, parent, entry, nullptr);
        out->added = 1;
        out->node = fresh;
        return out;
    }
    out->added = 0;
    out->node = before;
    return out;
}

// The commodity slots. A getter returns one of the commodity's doubles; a
// setter stores the value, 0 for a negative one.
double GetCommodity(char* self, int32_t* which, std::size_t index)
{
    return CommodityAt(self + object::kCommodities, nullptr, which)[index];
}

void SetCommodity(char* self, int32_t* which, double value, std::size_t index)
{
    if (value < 0.0)
        value = 0.0;
    CommodityAt(self + object::kCommodities, nullptr, which)[index] = value;
}

// The game's 0x004A9EE0 (slot 54, ret 4): GetExported.
double __fastcall GetExported(char* self, void*, int32_t which)
{
    return GetCommodity(self, &which, commodity::kExported);
}

// The game's 0x004A9F00 (slot 55, ret 0xC): SetExported.
void __fastcall SetExported(char* self, void*, int32_t which, double value)
{
    SetCommodity(self, &which, value, commodity::kExported);
}

// The game's 0x004A9F40 (slot 56, ret 4): GetImported.
double __fastcall GetImported(char* self, void*, int32_t which)
{
    return GetCommodity(self, &which, commodity::kImported);
}

// The game's 0x004A9F60 (slot 57, ret 0xC): SetImported.
void __fastcall SetImported(char* self, void*, int32_t which, double value)
{
    SetCommodity(self, &which, value, commodity::kImported);
}

// The game's 0x004A9FA0 (slot 58, ret 4): GetProduced.
double __fastcall GetProduced(char* self, void*, int32_t which)
{
    return GetCommodity(self, &which, commodity::kProduced);
}

// The game's 0x004A9FC0 (slot 59, ret 0xC): SetProduced.
void __fastcall SetProduced(char* self, void*, int32_t which, double value)
{
    SetCommodity(self, &which, value, commodity::kProduced);
}

// The game's 0x004AA000 (slot 60, ret 4): GetDemanded.
double __fastcall GetDemanded(char* self, void*, int32_t which)
{
    return GetCommodity(self, &which, commodity::kDemanded);
}

// The game's 0x004AA020 (slot 61, ret 0xC): SetDemanded.
void __fastcall SetDemanded(char* self, void*, int32_t which, double value)
{
    SetCommodity(self, &which, value, commodity::kDemanded);
}

// The game's 0x004AA060 (slot 62, ret 4): GetCostPerUnit.
double __fastcall GetCostPerUnit(char* self, void*, int32_t which)
{
    return GetCommodity(self, &which, commodity::kCostPerUnit);
}

// The game's 0x004AA080 (slot 63, ret 0xC): SetCostPerUnit.
void __fastcall SetCostPerUnit(char* self, void*, int32_t which, double value)
{
    SetCommodity(self, &which, value, commodity::kCostPerUnit);
}

// The game's 0x004AA0C0 (slot 70, ret 4): GetLocalDeals. Makes the caller's
// list a copy of the list at +0x190.
uint8_t __fastcall GetLocalDeals(char* self, void*, void* list)
{
    This<void*>(addr::SC4RefList_Assign, list, static_cast<void*>(self + object::kLocalDeals));
    return 1;
}

bool InstallCityPart2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4RegionalCity_SetupCity, reinterpret_cast<void*>(&SetupCity)},
        {addr::cSC4RegionalCity_GetPointsOfInterest, reinterpret_cast<void*>(&GetPointsOfInterest)},
        {addr::cSC4RegionalCity_CommodityInsertHint, reinterpret_cast<void*>(&CommodityInsertHint)},
        {addr::cSC4RegionalCity_SortInsertionRange, reinterpret_cast<void*>(&InsertionSort)},
        {addr::cSC4RegionalCity_SortPartial, reinterpret_cast<void*>(&PartialSort)},
        {addr::cSC4RegionalCity_GetAllConnections, reinterpret_cast<void*>(&GetAllConnections)},
        {addr::cSC4RegionalCity_ChangeSymmetricConnection, reinterpret_cast<void*>(&ChangeSymmetricConnection)},
        {addr::cSC4RegionalCity_FilterDeals, reinterpret_cast<void*>(&FilterDeals)},
        {addr::cSC4RegionalCity_SetLocalDeals, reinterpret_cast<void*>(&SetLocalDeals)},
        {addr::cSC4RegionalCity_UpdateLocalDeals, reinterpret_cast<void*>(&UpdateLocalDeals)},
        {addr::cSC4RegionalCity_CollectNeighbourConnections, reinterpret_cast<void*>(&CollectNeighbourConnections)},
        {addr::cSC4RegionalCity_CollectOwnConnections, reinterpret_cast<void*>(&CollectOwnConnections)},
        {addr::cSC4RegionalCity_NotifyConnections, reinterpret_cast<void*>(&NotifyConnections)},
        {addr::cSC4RegionalCity_GetAllowableExtrapolation, reinterpret_cast<void*>(&GetAllowableExtrapolation)},
        {addr::cSC4RegionalCity_ExtrapolateGrowth, reinterpret_cast<void*>(&ExtrapolateGrowth)},
        {addr::cSC4RegionalCity_SetAllowableGrowth, reinterpret_cast<void*>(&SetAllowableGrowth)},
        {addr::cSC4RegionalCity_ResetExtrapolation, reinterpret_cast<void*>(&ResetExtrapolation)},
        {addr::cSC4RegionalCity_SetPosition, reinterpret_cast<void*>(&SetPosition)},
        {addr::cSC4RegionalCity_CommodityAt, reinterpret_cast<void*>(&CommodityAt)},
        {addr::cSC4RegionalCity_PointsEraseTree, reinterpret_cast<void*>(&PointsEraseTree)},
        {addr::cSC4RegionalCity_PointsInsertNode, reinterpret_cast<void*>(&PointsInsertNode)},
        {addr::cSC4RegionalCity_PointsInsertUnique, reinterpret_cast<void*>(&PointsInsertUnique)},
        {addr::cSC4RegionalCity_SortIntroLoop, reinterpret_cast<void*>(&IntroSortLoop)},
        {addr::cSC4RegionalCity_GetExported, reinterpret_cast<void*>(&GetExported)},
        {addr::cSC4RegionalCity_SetExported, reinterpret_cast<void*>(&SetExported)},
        {addr::cSC4RegionalCity_GetImported, reinterpret_cast<void*>(&GetImported)},
        {addr::cSC4RegionalCity_SetImported, reinterpret_cast<void*>(&SetImported)},
        {addr::cSC4RegionalCity_GetProduced, reinterpret_cast<void*>(&GetProduced)},
        {addr::cSC4RegionalCity_SetProduced, reinterpret_cast<void*>(&SetProduced)},
        {addr::cSC4RegionalCity_GetDemanded, reinterpret_cast<void*>(&GetDemanded)},
        {addr::cSC4RegionalCity_SetDemanded, reinterpret_cast<void*>(&SetDemanded)},
        {addr::cSC4RegionalCity_GetCostPerUnit, reinterpret_cast<void*>(&GetCostPerUnit)},
        {addr::cSC4RegionalCity_SetCostPerUnit, reinterpret_cast<void*>(&SetCostPerUnit)},
        {addr::cSC4RegionalCity_GetLocalDeals, reinterpret_cast<void*>(&GetLocalDeals)},
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

const bool g_cityPart2Registered = registry::Register({
    "region.city.part2",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4RegionalCity_SetPosition,
    "Our regional city, part 2: connections, deals, extrapolation, SetPosition, commodities (docs/region.md)",
    false,
    InstallCityPart2,
});

} // namespace
} // namespace opensc4::region
