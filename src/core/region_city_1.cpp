// The regional city (cSC4RegionalCity, docs/region.md), part 1: 0x004A74E0 to
// 0x004A8960. Replacement, off by default:
//
//   region.city.part1: QueryInterface and Release, Init and Shutdown, the
//     budget and income getters and setters, the tax rate by type, the
//     commodity balance, IsTutorial, SetupPreferences, FindConnection, the
//     population by type and its extrapolated twin, the ordering of the
//     connection records and the save record's Write.
//
// The object as this part uses it: the main vtable at +0, the message
// target's at +4, the serialisable part's at +8, a byte "started" at +0xC,
// the reference count at +0x10, the saved figures from +0x14 on, three maps
// from a type to a number at +0x3C, +0x48 and +0x54 (header pointer, count),
// twelve tax rates (floats) at +0x60, the list of neighbour connections at
// +0x90, a vector of 16-byte connection records at +0x94, the strings at
// +0xD0 to +0x15C, the tutorial ID at +0x16C, budget and income (doubles) at
// +0x170 and +0x178, the commodity map at +0x180, two lists of deals at
// +0x18C and +0x190 and a map from a type to a vector of 12-byte points at
// +0x194.
//
// Still the game's: the stream helper 0x004A7560, the sort helpers of the
// connection records (0x004A7700, 0x004A7770, 0x004A7A20, 0x004A7A70,
// 0x004A7BB0, 0x004A7C70, 0x004A8830, 0x004A88C0), the tree helpers
// (0x004A79A0, 0x004A79E0, 0x004A83A0, 0x004A85A0), the vector clear
// 0x004A8570 and the list helpers (0x004A8660, 0x004A86F0, 0x004A8790).
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
constexpr std::size_t kMessageTarget = 0x4;
constexpr std::size_t kSerializable = 0x8;
constexpr std::size_t kStarted = 0xC;        // byte
constexpr std::size_t kRefCount = 0x10;
constexpr std::size_t kSaved14 = 0x14;       // seven dwords, saved signed
constexpr std::size_t kSaved30 = 0x30;       // float
constexpr std::size_t kSaved34 = 0x34;       // byte
constexpr std::size_t kSaved38 = 0x38;       // byte when saved, dword for the preferences
constexpr std::size_t kPopulation = 0x3C;    // map type -> number
constexpr std::size_t kPopulationCount = 0x40;
constexpr std::size_t kExtrapolated = 0x48;  // map type -> number
constexpr std::size_t kExtrapolatedCount = 0x4C;
constexpr std::size_t kThirdMap = 0x54;      // map type -> number
constexpr std::size_t kThirdMapCount = 0x58;
constexpr std::size_t kTaxRates = 0x60;      // twelve floats
constexpr std::size_t kConnections = 0x90;   // list: pointer to the sentinel node
constexpr std::size_t kRecords = 0x94;       // vector of 16-byte connection records
constexpr std::size_t kRecordsEnd = 0x98;
constexpr std::size_t kSavedA0 = 0xA0;       // byte, saved as a bool
constexpr std::size_t kSavedA4 = 0xA4;
constexpr std::size_t kSavedA8 = 0xA8;       // four dwords, saved signed
constexpr std::size_t kSavedB8 = 0xB8;
constexpr std::size_t kCityName = 0xD0;      // string
constexpr std::size_t kCityNameBuffer = 0xD4;
constexpr std::size_t kStringE4 = 0xE4;
constexpr std::size_t kMayorName = 0xF8;     // string
constexpr std::size_t kMayorNameBuffer = 0xFC;
constexpr std::size_t kString10C = 0x10C;
constexpr std::size_t kString120 = 0x120;
constexpr std::size_t kString134 = 0x134;
constexpr std::size_t kSaved148 = 0x148;     // three floats
constexpr std::size_t kString154 = 0x154;
constexpr std::size_t kSaved168 = 0x168;     // byte
constexpr std::size_t kTutorial = 0x16C;
constexpr std::size_t kBudget = 0x170;       // double
constexpr std::size_t kIncome = 0x178;       // double
constexpr std::size_t kCommodities = 0x180;  // map commodity -> five doubles
constexpr std::size_t kCommoditiesCount = 0x184;
constexpr std::size_t kDealsA = 0x18C;       // list
constexpr std::size_t kDealsB = 0x190;       // list
constexpr std::size_t kPoints = 0x194;       // map type -> vector of 12-byte points
constexpr std::size_t kPointsCount = 0x198;
} // namespace object

constexpr uint32_t kRegionalCityIid = 0x69D2BE1D;
constexpr uint32_t kMessageTargetIid = 0x452294AA;
constexpr uint32_t kSerializableIid = 0xE4FDA3D4;

constexpr uint32_t kMessages[5] = {0x26C63342, 0x26C63343, 0x26D31EC1, 0x26D31EC3, 0xCA63D517};

// A node of the game's lists.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    char* value;
};
static_assert(sizeof(ListNode) == 0xC);

// The start of a node of the game's maps (red-black tree); the key is at
// +0x10 and the value follows it.
struct TreeNode {
    uint32_t colour;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    uint32_t key;
};
static_assert(sizeof(TreeNode) == 0x14);

// One 16-byte connection record of the vector at +0x94.
struct Record {
    uint32_t id;
    int32_t x;
    int32_t z;
    uint8_t flag;
};
static_assert(sizeof(Record) == 0x10);

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

// The next node of a map in key order, as the original has it inline.
TreeNode* TreeNext(TreeNode* node)
{
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
    return next;
}

// The number of nodes of a list, counted as the original does.
uint32_t ListLength(ListNode* sentinel)
{
    uint32_t count = 0;
    for (ListNode* node = sentinel->next; node != sentinel; node = node->next)
        ++count;
    return count;
}

// ---------------------------------------------------------------------------
// The game's 0x004A74E0 (slot 0, ret 8): QueryInterface. The object itself
// for the unknown and the regional city IDs, the message target at +4 and
// the serialisable part at +8.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kRegionalCityIid || iid == 1) {
        *out = self;
    } else if (iid == kMessageTargetIid) {
        *out = self ? self + object::kMessageTarget : nullptr;
    } else if (iid == kSerializableIid) {
        *out = self ? self + object::kSerializable : nullptr;
    } else {
        return 0;
    }
    Virtual<uint32_t>(self, 0x4); // AddRef
    return 1;
}

// The game's 0x004A7580 (slot 16, ret 4): the tax rate of a population or
// job type; 7 for a type that has none.
double __fastcall GetTaxRate(char* self, void*, uint32_t type)
{
    std::size_t index;
    switch (type) {
    case 0x1010: index = 0; break;
    case 0x1020: index = 1; break;
    case 0x1030: index = 2; break;
    case 0x3110: index = 3; break;
    case 0x3120: index = 4; break;
    case 0x3130: index = 5; break;
    case 0x3320: index = 6; break;
    case 0x3330: index = 7; break;
    case 0x4100: index = 8; break;
    case 0x4200: index = 9; break;
    case 0x4300: index = 10; break;
    case 0x4400: index = 11; break;
    default:
        return static_cast<double>(7.0f); // the float at 0x00A85FB8
    }
    return static_cast<double>(At<float>(self, object::kTaxRates + index * 4));
}

// The game's 0x004A7640 (slot 50): GetBudget.
double __fastcall GetBudget(char* self, void*)
{
    return At<double>(self, object::kBudget);
}

// The game's 0x004A7650 (slot 51, ret 8): SetBudget.
void __fastcall SetBudget(char* self, void*, double budget)
{
    At<double>(self, object::kBudget) = budget;
}

// The game's 0x004A7660 (slot 52): GetIncome.
double __fastcall GetIncome(char* self, void*)
{
    return At<double>(self, object::kIncome);
}

// The game's 0x004A7670 (slot 53, ret 8): SetIncome.
void __fastcall SetIncome(char* self, void*, double income)
{
    At<double>(self, object::kIncome) = income;
}

// The game's 0x004A7680 (slot 64, ret 4): GetCommodityBalance: what is
// imported and produced less what is exported and demanded.
double __fastcall GetCommodityBalance(char* self, void*, int32_t commodity)
{
    const double produced = Virtual<double>(self, 0xE8, commodity);
    const double supply = Virtual<double>(self, 0xE0, commodity) + produced;
    const double demanded = Virtual<double>(self, 0xF0, commodity);
    const double use = Virtual<double>(self, 0xD8, commodity) + demanded;
    return supply - use;
}

// The game's 0x004A76E0 (slot 67): IsTutorial: the tutorial ID is not -1.
uint8_t __fastcall IsTutorial(char* self, void*)
{
    return Virtual<uint32_t>(self, 0x104) != 0xFFFFFFFF;
}

// The game's 0x004A7800 (slot 2): Release. The last reference shuts the city
// down and deletes it; the count itself stays at 1 then.
uint32_t __fastcall Release(char* self, void*)
{
    const int32_t count = At<int32_t>(self, object::kRefCount);
    if (count > 1) {
        At<int32_t>(self, object::kRefCount) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    Virtual<uint8_t>(self, 0x10);             // Shutdown
    Virtual<void*>(self, 0x124, uint32_t{1}); // the deleting destructor
    return 0;
}

// The game's 0x004A7830 (slot 3): Init. On the first call asks the message
// server for five messages to the message target.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, object::kStarted) == 0) {
        At<uint8_t>(self, object::kStarted) = 1;
        if (Global<uint8_t>(addr::cSC4RegionalCity_ClassStarted) == 0)
            Global<uint8_t>(addr::cSC4RegionalCity_ClassStarted) = 1;
        void* const server = Global<void*>(addr::SC4_MessageServer);
        char* const target = self + object::kMessageTarget;
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x14, target, message);
    }
    return 1;
}

// The game's 0x004A78A0 (slot 23, ret 4): SetupPreferences. The city's name
// to the start of the preferences, the mayor's name to +0x200 and the dword
// at +0x38 to +0x400. Returns that dword.
uint32_t __fastcall SetupPreferences(char* self, void*, char* preferences)
{
    const char* from = At<const char*>(self, object::kCityNameBuffer);
    char* to = preferences;
    char byte;
    do {
        byte = *from++;
        *to++ = byte;
    } while (byte != 0);

    from = At<const char*>(self, object::kMayorNameBuffer);
    to = preferences + 0x200;
    do {
        byte = *from++;
        *to++ = byte;
    } while (byte != 0);

    const uint32_t value = At<uint32_t>(self, object::kSaved38);
    At<uint32_t>(preferences, 0x400) = value;
    return value;
}

// The game's 0x004A78E0 (cdecl, two records): whether connection record `a`
// sorts before `b`. Records with a negative x come first, then those with a
// negative z, then those whose x is not below what the open city's slot
// 0x174 answers, then the rest; within a group by x, then by z.
uint8_t __cdecl RecordBefore(const Record* a, const Record* b)
{
    void* const city = Global<void*>(addr::SC4_City);
    int32_t groupA;
    if (a->x < 0)
        groupA = 0;
    else if (a->z < 0)
        groupA = 1;
    else
        groupA = (a->x < Virtual<int32_t>(city, 0x174) ? 1 : 0) + 2;

    int32_t bx = b->x;
    int32_t groupB;
    if (bx < 0) {
        groupB = 0;
    } else if (b->z < 0) {
        groupB = 1;
    } else {
        bx = b->x;
        groupB = (bx < Virtual<int32_t>(city, 0x174) ? 1 : 0) + 2;
    }

    if (groupA != groupB)
        return groupA < groupB;
    if (a->x != bx)
        return a->x < bx;
    return a->z < b->z;
}

// The game's 0x004A7B40 (slot 20, ret 0xC): FindConnection. The first
// connection of the list of that type whose cell is (x, z), or null.
char* __fastcall FindConnection(char* self, void*, int32_t type, int32_t x, int32_t z)
{
    ListNode* node = At<ListNode*>(self, object::kConnections)->next;
    if (node == At<ListNode*>(self, object::kConnections))
        return nullptr;
    do {
        char* const connection = node->value;
        int32_t cellX;
        int32_t cellZ;
        Virtual<void>(connection, 0x18, &cellX, &cellZ); // GetConnectionCell
        if (Virtual<int32_t>(connection, 0x14) == type && cellX == x && cellZ == z)
            return connection;
        node = node->next;
    } while (node != At<ListNode*>(self, object::kConnections));
    return nullptr;
}

// Writes the nodes of a map from a type to a number: the key unsigned, the
// value signed.
void WriteNumberMap(char* city, std::size_t map, void* stream)
{
    TreeNode* node = At<TreeNode*>(city, map)->left;
    if (node == At<TreeNode*>(city, map))
        return;
    do {
        const int32_t value = At<int32_t>(node, 0x14);
        Virtual<uint8_t>(stream, 0x24, node->key);
        Virtual<uint8_t>(stream, 0x20, value);
        node = TreeNext(node);
    } while (node != At<TreeNode*>(city, map));
}

// Writes a list of deals: each one's serialisable part writes itself. The
// result of the QueryInterface is not looked at.
void WriteDeals(char* city, std::size_t list, void* stream, ListNode* node)
{
    while (node != At<ListNode*>(city, list)) {
        void* part = nullptr;
        Virtual<uint8_t>(node->value, 0x0, kSerializableIid, &part);
        Virtual<uint8_t>(part, 0xC, stream);
        node = node->next;
        if (part)
            Virtual<uint32_t>(part, 0x8);
    }
}

// The game's 0x004A7D10 (serialisable slot 3, ret 4; `this` is the object
// +8): Write, version 13 of the record. Returns 1 when the stream has no
// error after it.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    char* const city = part - object::kSerializable;

    Virtual<uint8_t>(stream, 0x1C, uint32_t{0xD});
    for (std::size_t i = 0; i < 7; ++i)
        Virtual<uint8_t>(stream, 0x20, At<int32_t>(city, object::kSaved14 + i * 4));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(city, object::kSaved30)); // a float, pushed as its bits
    Virtual<uint8_t>(stream, 0x10, uint32_t{At<uint8_t>(city, object::kSaved34)});
    Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(city, object::kSaved38)});
    Cdecl<void*>(addr::SC4Stream_WriteBool, stream, uint32_t{At<uint8_t>(city, object::kSavedA0)});
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(city, object::kSavedA4));
    for (std::size_t i = 0; i < 4; ++i)
        Virtual<uint8_t>(stream, 0x20, At<int32_t>(city, object::kSavedA8 + i * 4));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(city, object::kSavedB8));
    Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(city, object::kSaved168)});
    Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(city + object::kCityName));
    Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(city + object::kStringE4));
    Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(city + object::kMayorName));
    Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(city + object::kString10C));
    Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(city + object::kString154));
    Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(city + object::kString120));
    Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(city + object::kString134));
    for (std::size_t i = 0; i < 3; ++i)
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(city, object::kSaved148 + i * 4));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(city, object::kTutorial));

    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(city, object::kPopulationCount));
    WriteNumberMap(city, object::kPopulation, stream);
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(city, object::kExtrapolatedCount));
    WriteNumberMap(city, object::kExtrapolated, stream);
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(city, object::kThirdMapCount));
    WriteNumberMap(city, object::kThirdMap, stream);

    Virtual<uint8_t>(stream, 0x24, uint32_t{0xC});
    {
        const uint32_t* rate = &At<uint32_t>(city, object::kTaxRates);
        for (int32_t left = 0xC; left != 0; --left, ++rate)
            Virtual<uint8_t>(stream, 0x30, *rate);
    }

    // The connections: those that have a serialisable part write themselves.
    {
        ListNode* const sentinel = At<ListNode*>(city, object::kConnections);
        ListNode* node = sentinel->next;
        Virtual<uint8_t>(stream, 0x24, ListLength(sentinel));
        while (node != sentinel) {
            void* written = nullptr;
            if (Virtual<uint8_t>(node->value, 0x0, kSerializableIid, &written))
                Virtual<uint8_t>(written, 0xC, stream);
            if (written)
                Virtual<uint32_t>(written, 0x8);
            node = node->next;
        }
    }

    // The connection records.
    {
        const int32_t bytes = static_cast<int32_t>(
            At<char*>(city, object::kRecordsEnd) - At<char*>(city, object::kRecords));
        Virtual<uint8_t>(stream, 0x24, static_cast<uint32_t>(bytes >> 4));
        for (Record* record = At<Record*>(city, object::kRecords); record != At<Record*>(city, object::kRecordsEnd);
             ++record) {
            Virtual<uint8_t>(stream, 0x24, record->id);
            Virtual<uint8_t>(stream, 0x20, record->x);
            Virtual<uint8_t>(stream, 0x20, record->z);
            Cdecl<void*>(addr::SC4Stream_WriteBool, stream, uint32_t{record->flag});
        }
    }

    Virtual<uint8_t>(stream, 0x34, At<double>(city, object::kBudget));
    Virtual<uint8_t>(stream, 0x34, At<double>(city, object::kIncome));

    // The commodities: the key and five doubles, the fourth before the third.
    {
        TreeNode* node = At<TreeNode*>(city, object::kCommodities)->left;
        Virtual<uint8_t>(stream, 0x20, At<int32_t>(city, object::kCommoditiesCount));
        while (node != At<TreeNode*>(city, object::kCommodities)) {
            Virtual<uint8_t>(stream, 0x20, static_cast<int32_t>(node->key));
            Virtual<uint8_t>(stream, 0x34, At<double>(node, 0x18));
            Virtual<uint8_t>(stream, 0x34, At<double>(node, 0x20));
            Virtual<uint8_t>(stream, 0x34, At<double>(node, 0x30));
            Virtual<uint8_t>(stream, 0x34, At<double>(node, 0x28));
            Virtual<uint8_t>(stream, 0x34, At<double>(node, 0x38));
            node = TreeNext(node);
        }
    }

    // The two lists of deals.
    {
        ListNode* const sentinel = At<ListNode*>(city, object::kDealsA);
        ListNode* const first = sentinel->next;
        Virtual<uint8_t>(stream, 0x20, static_cast<int32_t>(ListLength(sentinel)));
        WriteDeals(city, object::kDealsA, stream, first);
    }
    {
        Virtual<uint8_t>(stream, 0x20, static_cast<int32_t>(ListLength(At<ListNode*>(city, object::kDealsB))));
        WriteDeals(city, object::kDealsB, stream, At<ListNode*>(city, object::kDealsB)->next);
    }

    // The points: per type a vector of three floats each.
    {
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(city, object::kPointsCount));
        TreeNode* const header = At<TreeNode*>(city, object::kPoints);
        TreeNode* node = header->left;
        while (node != header) {
            Virtual<uint8_t>(stream, 0x24, node->key);
            const int32_t bytes = static_cast<int32_t>(At<char*>(node, 0x18) - At<char*>(node, 0x14));
            Virtual<uint8_t>(stream, 0x24, static_cast<uint32_t>(bytes / 12));
            const uint32_t* const end = At<const uint32_t*>(node, 0x18);
            for (const uint32_t* point = At<const uint32_t*>(node, 0x14); point != end; point += 3) {
                Virtual<uint8_t>(stream, 0x30, point[0]);
                Virtual<uint8_t>(stream, 0x30, point[1]);
                Virtual<uint8_t>(stream, 0x30, point[2]);
            }
            node = TreeNext(node);
        }
    }

    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0; // GetError
}

// The game's 0x004A8470 (slot 4): Shutdown. Takes the five messages back and
// empties the list of connections and the two lists of deals.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (At<uint8_t>(self, object::kStarted) != 0) {
        At<uint8_t>(self, object::kStarted) = 0;
        void* const server = Global<void*>(addr::SC4_MessageServer);
        char* const target = self + object::kMessageTarget;
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x18, target, message);
        This<void>(addr::SC4List_ClearReleasing, self + object::kConnections);
        This<void>(addr::SC4List_Clear, self + object::kDealsA);
        This<void>(addr::SC4List_Clear, self + object::kDealsB);
    }
    return 1;
}

// The game's 0x004A8500 (slot 9, ret 4): the population of a type from the
// map at +0x3C, 0 for a type that is not there.
int32_t __fastcall GetPopulationOfType(char* self, void*, uint32_t type)
{
    char* const map = self + object::kPopulation;
    TreeNode* found;
    This<void>(addr::SC4Uint32Map_Find, map, &found, &type);
    if (found == At<TreeNode*>(map, 0))
        return 0;
    return At<int32_t>(found, 0x14);
}

// The game's 0x004A8530 (slot 17, ret 4): GetExtrapolatedPopulation: the
// figure of the map at +0x48, or the population of the type (slot 9) when
// the map has none.
int32_t __fastcall GetExtrapolatedPopulation(char* self, void*, uint32_t type)
{
    char* const map = self + object::kExtrapolated;
    TreeNode* found;
    This<void>(addr::SC4Uint32Map_Find, map, &found, &type);
    if (found != At<TreeNode*>(map, 0))
        return At<int32_t>(found, 0x14);
    return Virtual<int32_t>(self, 0x24, type);
}

bool InstallCityPart1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4RegionalCity_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4RegionalCity_GetTaxRate, reinterpret_cast<void*>(&GetTaxRate)},
        {addr::cSC4RegionalCity_GetBudget, reinterpret_cast<void*>(&GetBudget)},
        {addr::cSC4RegionalCity_SetBudget, reinterpret_cast<void*>(&SetBudget)},
        {addr::cSC4RegionalCity_GetIncome, reinterpret_cast<void*>(&GetIncome)},
        {addr::cSC4RegionalCity_SetIncome, reinterpret_cast<void*>(&SetIncome)},
        {addr::cSC4RegionalCity_GetCommodityBalance, reinterpret_cast<void*>(&GetCommodityBalance)},
        {addr::cSC4RegionalCity_IsTutorial, reinterpret_cast<void*>(&IsTutorial)},
        {addr::cSC4RegionalCity_Release, reinterpret_cast<void*>(&Release)},
        {addr::cSC4RegionalCity_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4RegionalCity_SetupPreferences, reinterpret_cast<void*>(&SetupPreferences)},
        {addr::cSC4RegionalCity_ConnectionRecordBefore, reinterpret_cast<void*>(&RecordBefore)},
        {addr::cSC4RegionalCity_FindConnection, reinterpret_cast<void*>(&FindConnection)},
        {addr::cSC4RegionalCity_Write, reinterpret_cast<void*>(&Write)},
        {addr::cSC4RegionalCity_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4RegionalCity_GetPopulationByType, reinterpret_cast<void*>(&GetPopulationOfType)},
        {addr::cSC4RegionalCity_GetExtrapolatedPopulation, reinterpret_cast<void*>(&GetExtrapolatedPopulation)},
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

const bool g_cityPart1Registered = registry::Register({
    "region.city.part1",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4RegionalCity_QueryInterface,
    "Our regional city, part 1: the small slots, Init and Shutdown, FindConnection and the save record's Write",
    false,
    InstallCityPart1,
});

} // namespace
} // namespace opensc4::region
