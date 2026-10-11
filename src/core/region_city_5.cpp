// The regional city (cSC4RegionalCity, docs/region.md), part 5: 0x004AC5B0 to
// 0x004AD9C0. Replacement, off by default:
//
//   region.city.part5: the save record's Read (versions up to 13), the
//     refresh of the cached figures from a loaded city (main slot 24) and the
//     class factory.
//
// The object as this part uses it (0x1A0 bytes): the main vtable at +0, the
// message target's at +4, the serialisable part's at +8 (Read's `this`),
// position and size in the region at +0x14 to +0x20, population at +0x24,
// commercial and industrial jobs at +0x28 and +0x2C, the share of the
// population that works at +0x30 (float), the mayor rating at +0x34 (byte),
// the difficulty level at +0x38, three maps from a type to a number at +0x3C,
// +0x48 and +0x54, twelve tax rates (floats) at +0x60, the list of neighbour
// connections at +0x90, a vector of 16-byte connection records at +0x94, the
// serial number at +0xA4, the save file path at +0xBC, the city's name at
// +0xD0, the mayor's at +0xF8, more strings up to +0x154, "established" at
// +0x168 (byte), the tutorial ID at +0x16C, budget and income (doubles) at
// +0x170 and +0x178, the commodity map at +0x180 (five doubles each), the
// list of deals at +0x18C and the map from a kind (0 airports, 1 seaports)
// to a vector of 12-byte points at +0x194.
//
// Still the game's: everything from 0x004AD9C0 to 0x004ADC00, which is the
// toxic cloud occupant's (cSC4ToxicOccupant, class 0xABF98CB5, vtables
// 0x00A8FC88, 0x00A8FCA0, 0x00A8FD04, 0x00A8FD20 and 0x00A8FDA8), and the
// tree helpers this part calls (0x004A85A0, 0x004A9C00, 0x004ABF90).
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
constexpr std::size_t kSerializable = 0x8;
constexpr std::size_t kPosition = 0x14;       // x, z, then the size (x, z)
constexpr std::size_t kPopulation = 0x24;
constexpr std::size_t kCommercialJobs = 0x28;
constexpr std::size_t kIndustrialJobs = 0x2C;
constexpr std::size_t kWorkforce = 0x30;      // float
constexpr std::size_t kMayorRating = 0x34;    // byte
constexpr std::size_t kDifficulty = 0x38;
constexpr std::size_t kPopulationByType = 0x3C; // map type -> number
constexpr std::size_t kSecondMap = 0x48;      // map type -> number
constexpr std::size_t kThirdMap = 0x54;       // map type -> number
constexpr std::size_t kTaxRates = 0x60;       // twelve floats
constexpr std::size_t kConnections = 0x90;    // list: pointer to the sentinel node
constexpr std::size_t kRecords = 0x94;        // vector of 16-byte connection records
constexpr std::size_t kSavedA0 = 0xA0;        // byte, saved as a bool
constexpr std::size_t kSerialNumber = 0xA4;
constexpr std::size_t kSavedA8 = 0xA8;        // four dwords, saved signed
constexpr std::size_t kSavedB8 = 0xB8;
constexpr std::size_t kSavePath = 0xBC;       // string
constexpr std::size_t kCityName = 0xD0;       // string
constexpr std::size_t kCityNameBody = 0xD4;
constexpr std::size_t kStringE4 = 0xE4;
constexpr std::size_t kMayorName = 0xF8;      // string
constexpr std::size_t kMayorNameBody = 0xFC;
constexpr std::size_t kString10C = 0x10C;
constexpr std::size_t kString120 = 0x120;
constexpr std::size_t kString134 = 0x134;
constexpr std::size_t kSaved148 = 0x148;      // three floats
constexpr std::size_t kString154 = 0x154;
constexpr std::size_t kEstablished = 0x168;   // byte
constexpr std::size_t kTutorial = 0x16C;
constexpr std::size_t kBudget = 0x170;        // double
constexpr std::size_t kIncome = 0x178;        // double
constexpr std::size_t kCommodities = 0x180;   // map commodity -> five doubles
constexpr std::size_t kDeals = 0x18C;         // list
constexpr std::size_t kPoints = 0x194;        // map kind -> vector of 12-byte points
constexpr std::size_t kPointsCount = 0x198;
} // namespace object

namespace stream { // cIGZIStream, by vtable offset
constexpr std::size_t kGetSint8 = 0x10;
constexpr std::size_t kGetUint8 = 0x14;
constexpr std::size_t kGetUint16 = 0x1C;
constexpr std::size_t kGetSint32 = 0x20;
constexpr std::size_t kGetUint32 = 0x24;
constexpr std::size_t kGetFloat32 = 0x30;
constexpr std::size_t kGetFloat64 = 0x34;
constexpr std::size_t kGetString = 0x3C;
constexpr std::size_t kGetError = 0x48;
} // namespace stream

constexpr uint32_t kSerializableIid = 0xE4FDA3D4;
constexpr uint32_t kRecordIid = 0x4999A8F5; // asked of the stream: the save record it reads

// The text shown for a city that is not established yet, and the two texts
// a save file carries beside the record (group; the instance is a language).
constexpr uint32_t kNewCityTextGroup = 0x6A231EAA;
constexpr uint32_t kNewCityTextInstance = 0xAA738C4E;
constexpr uint32_t kCityNameTextGroup = 0x6AAEE931;
constexpr uint32_t kMayorNameTextGroup = 0x6AAEE932;

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// A node of the game's lists.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};
static_assert(sizeof(ListNode) == 0xC);

// The header node of one of the game's maps.
struct TreeNode {
    uint32_t colour;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
};

// A vector of items made of dwords (16-byte connection records, 12-byte
// points).
struct Words {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};
static_assert(sizeof(Words) == 0xC);

struct Pointers {
    void** begin;
    void** end;
    void** capacity;
};

// What the commodity map is given to insert (0x30 bytes) and what its insert
// answers (the node and whether it is new; the game writes five bytes).
struct CommodityEntry {
    int32_t commodity;
    uint32_t padding;
    double figures[5];
};
static_assert(sizeof(CommodityEntry) == 0x30);

struct InsertResult {
    char* node;
    uint8_t inserted;
};
static_assert(sizeof(InsertResult) == 0x8);

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

void* Allocate(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_stl_allocate, size);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

uint8_t Get(void* source, std::size_t slot, void* out)
{
    return Virtual<uint8_t>(source, slot, out);
}

uint32_t* Advance(uint32_t* at, uint32_t words)
{
    return reinterpret_cast<uint32_t*>(reinterpret_cast<std::uintptr_t>(at) + words * 4u);
}

// The vector's push_back as the game has it inlined, for an item of `words`
// dwords: in place when there is room, else into a buffer of twice the size
// (one item for an empty vector). Returns the new capacity in items when the
// vector was moved, else 0.
uint32_t Push(Words& vector, const uint32_t* item, uint32_t words)
{
    uint32_t* const end = vector.end;
    if (end != vector.capacity) {
        if (end) {
            for (uint32_t i = 0; i < words; ++i)
                end[i] = item[i];
        }
        vector.end = Advance(vector.end, words);
        return 0;
    }
    const int32_t bytes = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(end)
                                               - reinterpret_cast<std::uintptr_t>(vector.begin));
    const uint32_t size = static_cast<uint32_t>(bytes / static_cast<int32_t>(words * 4u));
    const uint32_t capacity = (size < 1u ? 1u : size) + size;
    uint32_t* const fresh = capacity != 0 ? static_cast<uint32_t*>(Allocate(capacity * words * 4u)) : nullptr;
    uint32_t* to = fresh;
    for (const uint32_t* from = vector.begin; from != end; from += words) {
        if (to) {
            for (uint32_t i = 0; i < words; ++i)
                to[i] = from[i];
        }
        to = Advance(to, words);
    }
    if (to) {
        for (uint32_t i = 0; i < words; ++i)
            to[i] = item[i];
    }
    uint32_t* const newEnd = Advance(to, words);
    if (vector.begin)
        Free(vector.begin);
    vector.begin = fresh;
    vector.end = newEnd;
    vector.capacity = Advance(fresh, capacity * words);
    return capacity;
}

// Links a new node holding `value` (counted) before the list's sentinel.
void Append(ListNode* sentinel, std::uintptr_t allocatorGlobal, void* value, bool checked)
{
    ListNode* const node = Virtual<ListNode*>(NodeAllocatorAt(allocatorGlobal), 0xC, int32_t{0xC});
    node->value = value;
    if (!checked || value)
        Virtual<uint32_t>(value, 0x4);
    ListNode* const previous = sentinel->previous;
    node->next = sentinel;
    node->previous = previous;
    previous->next = node;
    sentinel->previous = node;
}

// One run of saved deals: a count, then each deal's record. A deal that
// reads goes on the list at +0x18C; every deal gets flag 0x80.
void ReadDeals(char* self, void* source, uint32_t& left, uint32_t& scratch)
{
    Get(source, stream::kGetSint32, &left);
    while (left != 0) {
        --left;
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x80});
        char* const deal = memory ? This<char*>(addr::cSC4NeighborDeal_cSC4NeighborDeal, memory) : nullptr;
        if (deal)
            Virtual<uint32_t>(deal, 0x4);
        if (Virtual<uint8_t>(deal + 0x4, 0x10, source)) { // the deal's own Read
            Virtual<uint32_t>(deal, 0x4);
            ListNode* const sentinel = At<ListNode*>(self, object::kDeals);
            scratch = reinterpret_cast<std::uintptr_t>(sentinel);
            Append(sentinel, addr::SC4List_NodeAllocator, deal, false);
            Virtual<uint32_t>(deal, 0x8);
        }
        Virtual<void>(deal, 0x94, uint32_t{0x80}, uint32_t{1}); // SetFlags
        Virtual<uint32_t>(deal, 0x8);
    }
    --left;
}

// The game's 0x004AC5B0: slot 4 of the serialisable part (`part` = object
// + 8), Read. Versions up to 13; returns 1 when the stream has no error at
// the end, 0 for a newer version or a count out of range.
uint8_t __fastcall Read(char* part, void*, void* source)
{
    char* const self = part - object::kSerializable;
    // The original's stack slots that the stream fills more than once keep
    // their names here, so a value the stream fails to deliver is the stale
    // one as in the original (which starts with whatever the stack holds).
    uint32_t s10 = 0;
    uint32_t s14 = 0;
    uint8_t s1b = 0;
    uint32_t s1c = 0;
    uint32_t s20 = 0;
    uint32_t s24 = 0;
    uint16_t version = 0;
    uint32_t item[4] = {};
    int32_t commodities = 0;
    uint32_t taxes = 0;
    uint32_t connections = 0;
    uint32_t thirdCount = 0;
    uint32_t records = 0;
    uint32_t secondCount = 0;
    uint32_t firstCount = 0;

    Get(source, stream::kGetUint16, &version);
    if (version > 13)
        return 0;
    for (std::size_t i = 0; i < 7; ++i)
        Get(source, stream::kGetSint32, self + object::kPosition + i * 4);
    if (version >= 11) {
        Get(source, stream::kGetFloat32, &s10);
        At<uint32_t>(self, object::kWorkforce) = s10;
    } else if (version == 10) {
        Get(source, stream::kGetFloat32, &s10);
        At<float>(self, object::kWorkforce) =
            static_cast<float>(static_cast<double>(At<float>(&s10, 0)) * static_cast<double>(0.01f));
    } else {
        At<uint32_t>(self, object::kWorkforce) = 0x3F000000; // 0.5
    }
    Get(source, stream::kGetSint8, self + object::kMayorRating);
    s10 = 2;
    if (version >= 13) {
        Get(source, stream::kGetUint8, &s1b);
        s10 = s1b;
    }
    Cdecl<void*>(addr::SC4Stream_ReadBool, source, self + object::kSavedA0);
    Get(source, stream::kGetUint32, self + object::kSerialNumber);
    for (std::size_t i = 0; i < 4; ++i)
        Get(source, stream::kGetSint32, self + object::kSavedA8 + i * 4);
    Get(source, stream::kGetUint32, self + object::kSavedB8);
    Get(source, stream::kGetUint8, self + object::kEstablished);
    Get(source, stream::kGetString, self + object::kCityName);
    Get(source, stream::kGetString, self + object::kStringE4);
    Get(source, stream::kGetString, self + object::kMayorName);
    Get(source, stream::kGetString, self + object::kString10C);
    Get(source, stream::kGetString, self + object::kString154);
    Get(source, stream::kGetString, self + object::kString120);
    Get(source, stream::kGetString, self + object::kString134);
    for (std::size_t i = 0; i < 3; ++i)
        Get(source, stream::kGetFloat32, self + object::kSaved148 + i * 4);
    Get(source, stream::kGetSint32, self + object::kTutorial);
    if (At<uint8_t>(self, object::kEstablished) != 0)
        At<uint32_t>(self, object::kDifficulty) = s10;
    {
        // Old saves have 0 or 1 for the two tutorials the game shipped with.
        const uint32_t tutorial = At<uint32_t>(self, object::kTutorial);
        void* const registry = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0xC4);
        At<uint32_t>(self, object::kTutorial) =
            This<uint32_t>(addr::cSC4TutorialRegistry_UpgradeGuid, registry, tutorial);
    }

    // The three maps: a count, then pairs (type, number).
    const auto readMap = [&](uint32_t& count, std::size_t map) {
        Get(source, stream::kGetUint32, &count);
        if (count > 0x20000000)
            return false;
        for (uint32_t i = 0; i < count; ++i) {
            Get(source, stream::kGetUint32, &s10);
            Get(source, stream::kGetSint32, &s14);
            *This<uint32_t*>(addr::SC4Uint32Map_At, self + map, &s10) = s14;
        }
        return true;
    };
    if (!readMap(firstCount, object::kPopulationByType))
        return 0;
    if (!readMap(secondCount, object::kSecondMap))
        return 0;
    if (!readMap(thirdCount, object::kThirdMap))
        return 0;

    Get(source, stream::kGetUint32, &taxes);
    if (taxes > 12)
        return 0;
    for (uint32_t i = 0; i < taxes; ++i)
        Get(source, stream::kGetFloat32, self + object::kTaxRates + i * 4);

    // The neighbour connections: each is made, put on the list at +0x90 and
    // then reads itself.
    Get(source, stream::kGetUint32, &connections);
    if (connections > 0x100000)
        return 0;
    s20 = 0;
    if (connections != 0) {
        do {
            void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x44});
            void* const connection =
                memory ? This<void*>(addr::cSC4NeighborConnection_cSC4NeighborConnection, memory) : nullptr;
            if (connection)
                Virtual<uint32_t>(connection, 0x4);
            ListNode* const sentinel = At<ListNode*>(self, object::kConnections);
            s24 = reinterpret_cast<std::uintptr_t>(sentinel);
            Append(sentinel, addr::SC4List_NodeAllocator3, connection, true);
            if (connection)
                Virtual<uint32_t>(connection, 0x8);
            void* serializable = nullptr;
            if (Virtual<uint8_t>(connection, 0x0, kSerializableIid, &serializable))
                Virtual<uint8_t>(serializable, 0x10, source); // the connection's own Read
            if (serializable)
                Virtual<uint32_t>(serializable, 0x8);
            s10 = reinterpret_cast<std::uintptr_t>(serializable);
            ++s20;
        } while (s20 < connections);
    }

    // The connection records.
    Get(source, stream::kGetUint32, &records);
    if (records > 0x100000)
        return 0;
    s20 = 0;
    if (records != 0) {
        do {
            Get(source, stream::kGetUint32, &s24);
            item[0] = s24;
            Get(source, stream::kGetSint32, &item[1]);
            Get(source, stream::kGetSint32, &item[2]);
            Cdecl<void*>(addr::SC4Stream_ReadBool, source, &item[3]);
            Words& vector = At<Words>(self, object::kRecords);
            if (const uint32_t capacity = Push(vector, item, 4)) {
                s10 = reinterpret_cast<std::uintptr_t>(vector.begin);
                s14 = capacity;
            }
            ++s20;
        } while (s20 < records);
    }

    Get(source, stream::kGetFloat64, self + object::kBudget);
    Get(source, stream::kGetFloat64, self + object::kIncome);

    // The commodity figures: the entry is found or made, then its five
    // doubles are read in the order 0, 1, 3, 2, 4.
    Get(source, stream::kGetSint32, &commodities);
    if (static_cast<uint32_t>(commodities) > 0x100000)
        return 0;
    if (commodities != 0) {
        CommodityEntry entry;
        entry.padding = 0;
        for (double& figure : entry.figures)
            figure = 0.0;
        do {
            --commodities;
            Get(source, stream::kGetSint32, &s24);
            entry.commodity = static_cast<int32_t>(s24);
            InsertResult found = {nullptr, 0};
            This<void>(addr::cSC4RegionalCity_CommodityInsertUnique, self + object::kCommodities, &found, &entry);
            char* const figures = found.node + 0x18;
            Get(source, stream::kGetFloat64, figures);
            Get(source, stream::kGetFloat64, figures + 0x8);
            Get(source, stream::kGetFloat64, figures + 0x18);
            Get(source, stream::kGetFloat64, figures + 0x10);
            Get(source, stream::kGetFloat64, figures + 0x20);
        } while (commodities != 0);
    }

    // Two runs of deals, both onto the list at +0x18C.
    ReadDeals(self, source, s1c, s24);
    ReadDeals(self, source, s1c, s24);

    // From version 12: for each kind, the points of interest.
    if (version >= 12) {
        Get(source, stream::kGetUint32, &s14);
        while (s14 != 0) {
            --s14;
            Get(source, stream::kGetUint32, &s24);
            Get(source, stream::kGetUint32, &s20);
            s10 = s24;
            Words* const points = This<Words*>(addr::cSC4RegionalCity_PointsAt, self + object::kPoints, &s10);
            while (s20 != 0) {
                --s20;
                Get(source, stream::kGetFloat32, &item[0]);
                Get(source, stream::kGetFloat32, &item[1]);
                Get(source, stream::kGetFloat32, &item[2]);
                Push(*points, item, 3);
            }
            --s20;
        }
    }

    if (At<uint8_t>(self, object::kEstablished) == 0)
        Cdecl<void>(addr::SC4_LoadLocalizedText, self + object::kCityName, kNewCityTextGroup, kNewCityTextInstance,
                    uint32_t{0});

    // The names in the game's language, when the file holding the record has
    // them as texts of its own.
    const uint32_t language = Virtual<uint32_t>(Cdecl<void*>(addr::SC4_CurrentApp), 0x1C);
    void* record = nullptr;
    Virtual<uint8_t>(source, 0x0, kRecordIid, &record);
    void* file = nullptr;
    if (record) {
        Virtual<uint32_t>(record, 0x8);
        if (void* const owner = Virtual<void*>(record, 0x64))
            file = Virtual<void*>(owner, 0xC);
    }
    GameString text;
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = static_cast<char*>(Allocate(8));
    text.body.begin = buffer;
    text.body.end = buffer;
    text.body.capacity = buffer + 8;
    *buffer = 0;
    text.zero = 0;
    text.vtable = addr::SC4String_Vtable;
    if (Cdecl<uint8_t>(addr::SC4Resource_LoadText, file, kCityNameTextGroup, language, &text)) {
        StringBody* const target = &At<StringBody>(self, object::kCityNameBody);
        if (&text.body != target)
            This<void>(addr::SC4String_AssignRange, target, text.body.begin, text.body.end, &s1b);
    }
    if (Cdecl<uint8_t>(addr::SC4Resource_LoadText, file, kMayorNameTextGroup, language, &text)) {
        StringBody* const target = &At<StringBody>(self, object::kMayorNameBody);
        if (&text.body != target)
            This<void>(addr::SC4String_AssignRange, target, text.body.begin, text.body.end, &s1b);
    }
    const uint8_t result = Virtual<uint8_t>(source, stream::kGetError) == 0 ? 1 : 0;
    text.vtable = addr::SC4String_Vtable;
    if (text.body.begin)
        Free(text.body.begin);
    return result;
}

// The jobs of one demand type: the city's population of the type times the
// jobs a unit of the demand makes (the sum over the three wealths), cut to a
// whole number.
uint32_t Jobs(char* self, void* demand, float* perUnit, uint32_t type)
{
    Virtual<void>(demand, 0x20, perUnit, type, uint32_t{0}); // CalculateJobsPerUnitOfDemand
    const float sum = static_cast<float>(
        (static_cast<double>(perUnit[3]) + static_cast<double>(perUnit[2])) + static_cast<double>(perUnit[1]));
    const int32_t people = Virtual<int32_t>(self, 0x24, type);
    return static_cast<uint32_t>(
        static_cast<int32_t>(static_cast<int64_t>(static_cast<double>(people) * static_cast<double>(sum))));
}

// The places of the service's ports that answer yes at slot 0x94, added to
// the vector kept for `kind` in the map at `points`. Only those ports are
// released, as in the original.
void CollectPorts(char* points, uint32_t kind, std::uintptr_t serviceGlobal)
{
    uint32_t key = kind;
    Words* const places = This<Words*>(addr::cSC4RegionalCity_PointsAt, points, &key);
    Pointers ports = {nullptr, nullptr, nullptr};
    Virtual<void>(Global<void*>(serviceGlobal), 0x20, &ports);
    void** const end = ports.end;
    uint32_t place[3] = {};
    for (void** at = ports.begin; at != end; ++at) {
        void* const port = *at;
        if (Virtual<uint8_t>(port, 0x94)) {
            Virtual<void>(port, 0x3C, static_cast<uint32_t*>(place));
            Push(*places, place, 3);
            Virtual<uint32_t>(port, 0x8);
        }
    }
    if (ports.begin)
        Free(ports.begin);
}

// The game's 0x004AD290: main slot 24, the refresh of what the region keeps
// of a city from the loaded city itself: names, difficulty, save path,
// serial number, mayor rating, tax rates, connections, population, jobs and
// the places of the airports and seaports.
void __fastcall UpdateFromCity(char* self, void*, void* city)
{
    Virtual<void>(city, 0x40, self + object::kMayorName);
    Virtual<void>(city, 0x30, self + object::kCityName);
    At<uint32_t>(self, object::kDifficulty) = Virtual<uint32_t>(city, 0x60);
    Virtual<void>(city, 0x28, self + object::kSavePath);
    {
        const uint32_t serial = Virtual<uint32_t>(city, 0x14);
        Virtual<void>(self, 0x70, serial);
    }
    {
        const uint32_t established = Virtual<uint32_t>(city, 0x58);
        Virtual<void>(self, 0xB0, established);
    }
    At<uint8_t>(self, object::kMayorRating) = Virtual<uint8_t>(Virtual<void*>(city, 0xCC), 0x20);
    void* const budget = Virtual<void*>(city, 0xD0);
    for (int32_t i = 0; i < 12; ++i)
        At<float>(self, object::kTaxRates + static_cast<std::size_t>(i) * 4) =
            static_cast<float>(Virtual<double>(budget, 0x90, i));
    void* const neighbors = Virtual<void*>(city, 0xF8);
    Virtual<void>(neighbors, 0x40, self + object::kConnections);
    Virtual<void>(neighbors, 0x88, city);
    {
        void* const tutorial = Virtual<void*>(city, 0x148);
        if (tutorial && At<int32_t>(self, object::kTutorial) == -1)
            At<uint32_t>(self, object::kTutorial) = Virtual<uint32_t>(tutorial, 0x30);
    }
    if (void* const residential = Virtual<void*>(city, 0x110)) {
        At<uint32_t>(self, object::kPopulation) = Virtual<uint32_t>(residential, 0x40);
        uint32_t percent = 0;
        At<uint32_t>(self, object::kWorkforce) = *Virtual<const uint32_t*>(residential, 0x7C, &percent);
    }
    void* const demand = Virtual<void*>(city, 0xE0);
    Virtual<void>(demand, 0x2C, self + object::kPopulationByType);

    float perUnit[4] = {};
    {
        const uint32_t a = Jobs(self, demand, perUnit, 0x3111);
        const uint32_t b = Jobs(self, demand, perUnit, 0x3121);
        const uint32_t c = Jobs(self, demand, perUnit, 0x3131);
        const uint32_t d = Jobs(self, demand, perUnit, 0x3321);
        const uint32_t e = Jobs(self, demand, perUnit, 0x3331);
        At<uint32_t>(self, object::kCommercialJobs) = e + d + c + b + a;
    }
    {
        const uint32_t a = Jobs(self, demand, perUnit, 0x4101);
        const uint32_t b = Jobs(self, demand, perUnit, 0x4201);
        const uint32_t c = Jobs(self, demand, perUnit, 0x4301);
        const uint32_t d = Jobs(self, demand, perUnit, 0x4401);
        At<uint32_t>(self, object::kIndustrialJobs) = d + c + b + a;
    }

    char* const points = self + object::kPoints;
    if (At<uint32_t>(self, object::kPointsCount) != 0) {
        This<void>(addr::cSC4RegionalCity_PointsEraseTree, points, At<TreeNode*>(points, 0)->parent);
        At<TreeNode*>(points, 0)->left = At<TreeNode*>(points, 0);
        At<TreeNode*>(points, 0)->parent = nullptr;
        At<TreeNode*>(points, 0)->right = At<TreeNode*>(points, 0);
        At<uint32_t>(self, object::kPointsCount) = 0;
    }
    CollectPorts(points, 0, addr::SC4_Service43D2C);
    CollectPorts(points, 1, addr::SC4_Service43D30);
}

// The game's 0x004AD9A0: the class factory's maker, cdecl with no arguments.
void* __cdecl Create()
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1A0});
    if (!memory)
        return nullptr;
    return This<void*>(addr::cSC4RegionalCity_Construct, memory);
}

bool InstallRegionCity5(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4RegionalCity_Read, reinterpret_cast<void*>(&Read)},
        {addr::cSC4RegionalCity_UpdateCityCache, reinterpret_cast<void*>(&UpdateFromCity)},
        {addr::cSC4RegionalCity_Create, reinterpret_cast<void*>(&Create)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_regionCity5Registered = registry::Register({
    "region.city.part5",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4RegionalCity_Read,
    "Our regional city, part 5: the save record's Read, the refresh from a loaded city and the factory",
    false,
    InstallRegionCity5,
});

} // namespace
} // namespace opensc4::region
