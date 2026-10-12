// The demolition service's part of the autotest (src/core/autotest_hooks.h;
// demolition_1.cpp, demolition_2.cpp): a switch that calls the game's
// cSC4Demolition functions by address and through the service's vtable, so
// that a run with simulators.demolition.* on and one with it off can be
// compared. Nothing here calls our own functions, and nothing changes the
// city: every call prices or reads (demolish and apply are always false).
//
//   OPENSC4_TEST_DEMOLITION=<day>    token "demolition": eight parts, each with
//     its own sub-hash on the log lines "autotest: demolition: ...".
//
//   1. the object: QueryInterface with the four IDs it takes and one it does
//      not (which part came back, as an offset from the object), AddRef and
//      Release as changes of the count, the documented fields;
//   2. the five tuning globals before and after the tuning is read again;
//   3. the demolition filter's Test (0x00466730) over a fixed sample of the
//      city's occupants, in eight configurations of its fields (rectangle,
//      removal test with both privilege modes, a caller's filter, a view
//      filter), and the small terrain filter's Test (0x00466530);
//   4. DemolishRegion (slot 6) with demolish false over three regions, with
//      the argument sets the game's callers use, the cost and the occupants
//      it collects (their types, sorted: the set is ordered by address);
//   5. DemolishOccupant (slot 8) with demolish false for occupants of each
//      type the region collected;
//   6. ModifyTerrainHeight (slot 9) with apply false;
//   7. DemolishLots (slot 7) with demolish false for lots in a fixed order.
//   8. after: the end check that the service's region pointer and the default
//      filter's fields are back as they were.
//
// What is left out, on purpose: anything with demolish or apply true,
// Init, Shutdown, the deleting destructor, DoMessage, DezoneLot, the effect
// slots (sound and camera), ModifyTerrainHeight's per-cell mode with a
// height that would change a cell (it tells the terrain at the end whether
// apply is set or not), and every pointer.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace opensc4::autotest {
namespace {

struct Fnv {
    uint32_t h = 2166136261u;
    void Bytes(const void* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    }
    template <typename T>
    void Add(const T& value)
    {
        Bytes(&value, sizeof value);
    }
    // A mark for something that is not there, so that a missing object
    // changes the hash instead of stopping the run.
    void Missing() { Add(uint8_t{0xEE}); }
};

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

char g_token[40];

constexpr std::uintptr_t kFilterConstructor = 0x00466700;
constexpr uint32_t kMostOccupants = 300;
constexpr uint32_t kMostSelected = 100;
constexpr uint32_t kMostLots = 10;

// --- Helpers --------------------------------------------------------------------------

// The game's cell region: the rectangle (x1, z1, x2, z2, inclusive), then the
// cell map at +0x10.
struct CellRegion {
    int32_t rect[4];
    uint32_t cells[5];
};
static_assert(sizeof(CellRegion) == 0x24);

// All cells set.
void MakeRegion(CellRegion& region, int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    region.rect[0] = x0;
    region.rect[1] = z0;
    region.rect[2] = x1;
    region.rect[3] = z1;
    const uint8_t fill = 1;
    This<void*>(addr::SC4CellRegion_ConstructCells, region.cells, static_cast<uint32_t>(x1 - x0 + 1),
                static_cast<uint32_t>(z1 - z0 + 1), &fill);
}

void DestroyRegion(CellRegion& region)
{
    region.cells[0] = static_cast<uint32_t>(addr::SC4CellRegion_CellsVtable);
    This<void>(addr::SC4CellRegion_DestroyCells, region.cells);
}

// A std::set header of referenced objects: the head node and the count.
struct TreeSet {
    char* head;
    uint32_t size;
};

void NewSet(TreeSet& set)
{
    set.head = nullptr;
    set.size = 0;
    This<void>(addr::SC4Uint32Set_Construct, &set);
}

void DestroySet(TreeSet& set)
{
    This<void>(addr::SC4LotPtrSet_Destroy, &set);
}

// The tree's next node after `node` (the leftmost is head + 8).
char* Next(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (char* left = At<char*>(right, 0x8))
            right = left;
        return right;
    }
    char* parent = At<char*>(node, 0x4);
    while (node == At<char*>(parent, 0xC)) {
        node = parent;
        parent = At<char*>(parent, 0x4);
    }
    if (At<char*>(node, 0xC) != parent)
        node = parent;
    return node;
}

// The occupants of a set, in the set's order (by address), at most a million.
std::vector<void*> SetMembers(const TreeSet& set)
{
    std::vector<void*> out;
    if (!set.head)
        return out;
    char* const end = set.head;
    for (char* node = At<char*>(end, 0x8); node != end && out.size() < 1000000; node = Next(node))
        out.push_back(At<void*>(node, 0x10));
    return out;
}

// A list of the game's (a head node, nodes next, previous, value at +8), made
// and freed with the node allocator as the game does.
char* NewList()
{
    char* const head = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
    At<char*>(head, 0x0) = head;
    At<char*>(head, 0x4) = head;
    return head;
}

void PushBack(char* head, void* value)
{
    char* const node = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
    char* const back = At<char*>(head, 0x4);
    At<void*>(node, 0x8) = value;
    At<char*>(node, 0x0) = head;
    At<char*>(node, 0x4) = back;
    At<char*>(back, 0x0) = node;
    At<char*>(head, 0x4) = node;
}

// Nodes and head freed; the values are not touched.
void FreeList(char* head)
{
    for (char* node = At<char*>(head, 0x0); node != head;) {
        char* const following = At<char*>(node, 0x0);
        Virtual<void>(ListNodeAllocator(), 0x10, static_cast<void*>(node));
        node = following;
    }
    Virtual<void>(ListNodeAllocator(), 0x10, static_cast<void*>(head));
}

bool ListIsEmpty(char* head)
{
    return At<char*>(head, 0x0) == head;
}

// A filter of the kind the bulldoze tool and the region's tools pass
// (SC4OccupantFilterByKind, 0x10 bytes; region_city_3.cpp builds it the same
// way): one reference.
char* MakeKindFilter(uint32_t kind)
{
    char* const filter = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x10}));
    if (!filter)
        return nullptr;
    This<void*>(addr::SC4_OccupantFilterBase, filter);
    At<std::uintptr_t>(filter, 0x0) = addr::SC4OccupantFilterByKind_vftable;
    At<std::uintptr_t>(filter, 0x4) = addr::SC4OccupantFilterByKind_vftable_4;
    At<uint32_t>(filter, 0xC) = kind;
    Virtual<uint32_t>(filter, 0x4);
    return filter;
}

// The city's occupants in a fixed order, as audio_autotest.cpp's Collect
// takes them: the occupant manager's first occupant of every fourth cell
// (slot 33, the call the game's cell test 0x004E3100 makes) and the building
// of the cell's lot (lot slot 48, the building's slot 3). Up to 300, spread
// evenly. The references the manager gave are kept in `held`.
struct Sample {
    std::vector<void*> chosen;
    std::vector<void*> held;
    std::vector<void*> lots; // distinct lots in walk order, at most 60
};

void Collect(Sample& out, int32_t sizeX, int32_t sizeZ)
{
    void* const manager = Global<void*>(addr::SC4_NetworkService);
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    if (!manager || !lotManager)
        return;
    std::vector<void*> all;
    const auto add = [&all](void* occupant) {
        if (!occupant)
            return;
        for (void* const known : all)
            if (known == occupant)
                return;
        all.push_back(occupant);
    };
    for (int32_t z = 0; z < sizeZ && all.size() < 3000; z += 4) {
        for (int32_t x = 0; x < sizeX && all.size() < 3000; x += 4) {
            void* found = nullptr;
            if (Virtual<uint8_t>(manager, 0x84, &found, x, z, uint32_t{0}) && found) {
                out.held.push_back(found);
                add(found);
            }
            if (void* const lot = Virtual<void*>(lotManager, 0x18, x, z, uint32_t{0})) {
                if (void* const building = Virtual<void*>(lot, 0xC0))
                    add(Virtual<void*>(building, 0xC));
            }
        }
    }
    const std::size_t count = all.size();
    const std::size_t take = count < kMostOccupants ? count : kMostOccupants;
    for (std::size_t i = 0; i < take; ++i)
        out.chosen.push_back(all[i * count / take]);
    // Lots, every cell in the same order.
    for (int32_t z = 0; z < sizeZ && out.lots.size() < 60; ++z) {
        for (int32_t x = 0; x < sizeX && out.lots.size() < 60; ++x) {
            void* const lot = Virtual<void*>(lotManager, 0x18, x, z, uint32_t{0});
            if (!lot || !Virtual<void*>(lot, 0xC0))
                continue;
            if (std::find(out.lots.begin(), out.lots.end(), lot) == out.lots.end())
                out.lots.push_back(lot);
        }
    }
}

// --- 1. The object ---------------------------------------------------------------------

void DriveObject(Fnv& f, void* d, void* city)
{
    if (!d) {
        f.Missing();
        return;
    }
    // QueryInterface: the four IDs it takes and one it does not.
    for (const uint32_t iid : {0x00000001u, 0x26DDE2EAu, 0x452294AAu, 0xE4FDA3D4u, 0x12345678u}) {
        void* out = nullptr;
        const uint32_t before = At<uint32_t>(d, 0xC);
        const uint8_t has = Virtual<uint8_t>(d, 0x0, iid, &out);
        f.Add(iid);
        f.Add(has);
        f.Add(out != nullptr);
        if (out)
            f.Add(static_cast<int32_t>(static_cast<char*>(out) - static_cast<char*>(d)));
        f.Add(At<uint32_t>(d, 0xC) - before);
        if (has)
            Virtual<uint32_t>(d, 0x8); // the reference the answer took
        f.Add(At<uint32_t>(d, 0xC) - before);
    }
    // AddRef and Release.
    const uint32_t before = At<uint32_t>(d, 0xC);
    Virtual<uint32_t>(d, 0x4);
    f.Add(At<uint32_t>(d, 0xC) - before);
    Virtual<uint32_t>(d, 0x8);
    f.Add(At<uint32_t>(d, 0xC) - before);
    // The fields.
    f.Add(At<uint8_t>(d, 0x10));
    f.Add(At<uint32_t>(d, 0x14));
    f.Add(At<uint32_t>(d, 0x18));
    if (city) {
        f.Add(At<uint32_t>(d, 0x14) == Virtual<uint32_t>(city, 0x174));
        f.Add(At<uint32_t>(d, 0x18) == Virtual<uint32_t>(city, 0x178));
    }
    for (const std::size_t at : {std::size_t{0x1C}, std::size_t{0x20}, std::size_t{0x24}, std::size_t{0x28},
                                 std::size_t{0x2C}, std::size_t{0x30}})
        f.Add(At<void*>(d, at) != nullptr);
    f.Add(At<void*>(d, 0x20) == city);
}

// --- 2. The tuning ---------------------------------------------------------------------

void AddTuning(Fnv& f, uint32_t* values)
{
    constexpr std::uintptr_t globals[5] = {addr::cSC4Demolition_OccupantEffectLow, addr::cSC4Demolition_OccupantEffectHigh,
                                           addr::cSC4Demolition_GroupEffectLow, addr::cSC4Demolition_GroupEffectHigh,
                                           addr::cSC4Demolition_LotCostScale};
    for (int i = 0; i < 5; ++i) {
        values[i] = Global<uint32_t>(globals[i]);
        f.Add(values[i]);
    }
}

void DriveTuning(Fnv& f, void* d, uint32_t* before, uint32_t* after)
{
    AddTuning(f, before);
    if (d)
        This<void>(addr::cSC4Demolition_LoadTuning, d);
    else
        f.Missing();
    AddTuning(f, after);
}

// --- 3. The filters --------------------------------------------------------------------

struct FilterCase {
    bool limited;
    bool active;
    uint32_t mode;
    uint32_t passedKind; // 0: none
    uint32_t viewKind;   // 0: none
};

// A demolition filter as DemolishRegion leaves the service's (0x24, 0x28,
// 0x2C, 0x30) and CollectOccupants leaves it for its query (0x10 and the
// rectangle in metres), every field the Test reads set here; the
// constructor leaves 0x14 to 0x28 as the allocator had them. The two
// filters of the case go to the filter, which gives them back when it goes.
char* MakeDemolitionFilter(const FilterCase& c, const float* low, const float* high)
{
    char* const filter = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x34}));
    if (!filter)
        return nullptr;
    This<void*>(kFilterConstructor, filter);
    At<uint8_t>(filter, 0x10) = c.limited ? 1 : 0;
    At<float>(filter, 0x14) = low[0];
    At<float>(filter, 0x18) = low[1];
    At<float>(filter, 0x1C) = high[0];
    At<float>(filter, 0x20) = high[1];
    At<uint8_t>(filter, 0x24) = c.active ? 1 : 0;
    At<uint32_t>(filter, 0x28) = c.mode;
    At<void*>(filter, 0x2C) = c.passedKind ? MakeKindFilter(c.passedKind) : nullptr;
    At<void*>(filter, 0x30) = c.viewKind ? MakeKindFilter(c.viewKind) : nullptr;
    Virtual<uint32_t>(filter, 0x4);
    return filter;
}

void DriveFilters(Fnv& f, const Sample& sample, void* city, int32_t sizeX, int32_t sizeZ, int& calls, int& yes,
                  uint32_t* subs)
{
    if (!city || sizeX <= 0 || sizeZ <= 0) {
        f.Missing();
        return;
    }
    // The quarter of the city in the middle, in metres (CollectOccupants's way).
    float low[2] = {0, 0};
    float high[2] = {0, 0};
    Virtual<void>(city, 0x180, sizeX / 4, sizeZ / 4, &low[0], &low[1]);
    Virtual<void>(city, 0x180, sizeX * 3 / 4, sizeZ * 3 / 4, &high[0], &high[1]);
    const FilterCase cases[] = {
        {false, false, 0, 0, 0}, {true, false, 0, 0, 0}, {false, true, 1, 0, 0}, {false, true, 2, 0, 0},
        {true, true, 1, 0, 0},   {false, false, 0, 1, 0}, {false, true, 2, 2, 3}, {true, true, 1, 0, 1},
    };
    int index = 0;
    for (const FilterCase& c : cases) {
        Fnv mine;
        char* const filter = MakeDemolitionFilter(c, low, high);
        if (!filter) {
            mine.Missing();
        } else {
            for (void* const occupant : sample.chosen) {
                const uint8_t answer = Virtual<uint8_t>(filter, 0xC, occupant);
                mine.Add(answer);
                ++calls;
                if (answer)
                    ++yes;
            }
            Virtual<uint32_t>(filter, 0x8); // the last one: freed with its two filters
        }
        subs[index++] = mine.h;
        f.Add(mine.h);
    }
    // The terrain filter ModifyTerrainHeight makes inline (0xC bytes).
    Fnv terrain;
    char* const made = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xC}));
    if (!made) {
        terrain.Missing();
    } else {
        This<void*>(addr::SC4_OccupantFilterBase, made);
        At<uint32_t>(made, 0x0) = 0x00A88050;
        At<uint32_t>(made, 0x4) = 0x00A88038;
        Virtual<uint32_t>(made, 0x4);
        for (void* const occupant : sample.chosen) {
            const uint8_t answer = Virtual<uint8_t>(made, 0xC, occupant);
            terrain.Add(answer);
            ++calls;
            if (answer)
                ++yes;
        }
        Virtual<uint32_t>(made, 0x8);
    }
    subs[index] = terrain.h;
    f.Add(terrain.h);
}

// --- 4. DemolishRegion, price only ----------------------------------------------------------

struct RegionCase {
    const char* name;
    int32_t rect[4];
};

struct ArgumentSet {
    const char* name;
    int32_t privilege;
    uint32_t flags;
    uint32_t clearZoned;
    uint32_t filterKind; // 0: no filter
};

struct RegionOutcome {
    uint8_t result;
    int64_t cost;
    std::vector<uint32_t> types; // sorted
    uint32_t ordered;            // hash of the types in the set's order (log only)
};

// One price-only call: a region of all cells set, a set for the occupants it
// collects when `withSet`, the filter if the argument set has a kind. The
// members of the set are handed to `keep` while it is alive.
RegionOutcome PriceRegion(void* d, const RegionCase& rc, const ArgumentSet& as, bool withSet,
                          std::vector<std::pair<void*, uint32_t>>* keep, TreeSet* keepSet)
{
    RegionOutcome out{};
    CellRegion region;
    MakeRegion(region, rc.rect[0], rc.rect[1], rc.rect[2], rc.rect[3]);
    char* const filter = as.filterKind ? MakeKindFilter(as.filterKind) : nullptr;
    TreeSet set{};
    if (withSet)
        NewSet(set);
    int64_t cost = 0;
    out.result = Virtual<uint8_t>(d, 0x18, uint32_t{0}, static_cast<void*>(&region), as.privilege, as.flags,
                                  as.clearZoned, static_cast<void*>(filter), &cost, withSet ? &set : nullptr,
                                  static_cast<void*>(nullptr), int32_t{0}, int32_t{0});
    out.cost = cost;
    Fnv ordered;
    if (withSet) {
        for (void* const occupant : SetMembers(set)) {
            const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
            out.types.push_back(type);
            ordered.Add(type);
            if (keep)
                keep->push_back({occupant, type});
        }
        std::sort(out.types.begin(), out.types.end());
        out.ordered = ordered.h;
    }
    if (filter)
        Virtual<uint32_t>(filter, 0x8);
    if (keepSet) {
        *keepSet = set;
    } else if (withSet) {
        DestroySet(set);
    }
    DestroyRegion(region);
    return out;
}

void DriveRegions(Fnv& f, void* d, const Sample& sample, int32_t sizeX, int32_t sizeZ, int& calls, int& occupants,
                  std::vector<std::pair<void*, uint32_t>>& keep, TreeSet& keepSet, bool& haveKept)
{
    if (!d || sizeX < 8 || sizeZ < 8) {
        f.Missing();
        return;
    }
    RegionCase cases[3] = {
        {"middle", {sizeX / 4, sizeZ / 4, sizeX * 3 / 4 - 1, sizeZ * 3 / 4 - 1}},
        {"strip", {0, 0, 3, sizeZ - 1}},
        {"lot", {sizeX / 2, sizeZ / 2, sizeX / 2, sizeZ / 2}},
    };
    if (!sample.lots.empty()) {
        int32_t rect[4] = {0, 0, 0, 0};
        if (Virtual<uint8_t>(sample.lots[0], 0x38, static_cast<int32_t*>(rect))) {
            for (int i = 0; i < 4; ++i)
                cases[2].rect[i] = rect[i];
        }
    }
    const ArgumentSet sets[] = {
        {"bulldoze", 1, 1u, 0u, 0},    // the tool's: privilege 1, flags 1 (game_bulldoze_autotest.cpp)
        {"dezone", 0, 0x2000u, 0u, 0}, // the lots' own occupants only (DemolishLots adds the bit)
        {"region", 0, 1u, 0u, 1},      // region_city_3.cpp: privilege 0, flags 1, a filter by kind
        {"kind3", 1, 1u, 1u, 3},       // the tool with a filter by kind and clearZoned
    };
    for (int r = 0; r < 3; ++r) {
        for (int s = 0; s < 4; ++s) {
            // The big region gets the first three only (time).
            if (r == 0 && s == 3)
                continue;
            const bool first = r == 0 && s == 0;
            RegionOutcome out = PriceRegion(d, cases[r], sets[s], true, first ? &keep : nullptr, first ? &keepSet : nullptr);
            if (first)
                haveKept = true;
            Fnv mine;
            mine.Add(out.result);
            mine.Add(out.cost);
            mine.Add(static_cast<uint32_t>(out.types.size()));
            for (const uint32_t type : out.types)
                mine.Add(type);
            ++calls;
            occupants += static_cast<int>(out.types.size());
            // Once more without a set: the same answer and cost.
            RegionOutcome bare{};
            if (s == 0 || s == 2) {
                bare = PriceRegion(d, cases[r], sets[s], false, nullptr, nullptr);
                mine.Add(bare.result);
                mine.Add(bare.cost);
                ++calls;
            }
            // The count of each type, for the log only.
            std::string kinds;
            for (std::size_t i = 0; i < out.types.size();) {
                std::size_t j = i;
                while (j < out.types.size() && out.types[j] == out.types[i])
                    ++j;
                char text[32];
                std::snprintf(text, sizeof text, " %08X:%u", out.types[i], static_cast<unsigned>(j - i));
                kinds += text;
                i = j;
            }
            log::Write("autotest: demolition: region %s/%s: %s, cost %lld, %u occupants, hash %08X, in order %08X, no set %u/%lld,%s",
                       cases[r].name, sets[s].name, out.result ? "true" : "false", static_cast<long long>(out.cost),
                       static_cast<unsigned>(out.types.size()), mine.h, out.ordered, static_cast<unsigned>(bare.result),
                       static_cast<long long>(bare.cost), kinds.c_str());
            f.Add(mine.h);
        }
    }
}

// --- 5. DemolishOccupant, price only ------------------------------------------------------

void DriveOccupants(Fnv& f, void* d, std::vector<std::pair<void*, uint32_t>>& kept, int& calls, int& types)
{
    if (!d || kept.empty()) {
        f.Missing();
        return;
    }
    // By type, then by position (the set is in address order, which is not
    // the same in two runs): a fixed order, the same few of each type.
    struct Entry {
        void* occupant;
        uint32_t type;
        float x;
        float z;
    };
    std::vector<Entry> all;
    for (const auto& member : kept) {
        float position[3] = {0, 0, 0};
        Virtual<void>(member.first, 0x20, position);
        all.push_back({member.first, member.second, position[0], position[2]});
    }
    std::sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) {
        if (a.type != b.type)
            return a.type < b.type;
        if (a.x != b.x)
            return a.x < b.x;
        return a.z < b.z;
    });
    std::size_t kinds = 0;
    for (std::size_t i = 0; i < all.size(); ++i)
        if (i == 0 || all[i].type != all[i - 1].type)
            ++kinds;
    // At most 14 of a type, and no more than the 100 share out evenly.
    std::size_t most = kinds ? kMostSelected / kinds : 14;
    if (most > 14)
        most = 14;
    if (most < 1)
        most = 1;
    std::vector<Entry> chosen;
    for (std::size_t begin = 0; begin < all.size();) {
        std::size_t end = begin;
        while (end < all.size() && all[end].type == all[begin].type)
            ++end;
        ++types;
        const std::size_t count = end - begin;
        const std::size_t take = count < most ? count : most;
        for (std::size_t i = 0; i < take; ++i)
            chosen.push_back(all[begin + i * count / take]);
        begin = end;
    }
    struct Call {
        int32_t privilege;
        uint32_t flags;
        uint32_t exclude;
    };
    const Call calls3[] = {{1, 1u, 1u}, {0, 0x2000u, 1u}, {1, 0x101u, 0u}};
    char* taken = NewList();
    Fnv mine;
    uint32_t yes = 0;
    for (const Entry& e : chosen) {
        mine.Add(e.type);
        for (const Call& c : calls3) {
            int64_t cost = 0x7777777777777777ll;
            const uint8_t answer = Virtual<uint8_t>(d, 0x20, uint32_t{0}, e.occupant, c.privilege, c.flags, &cost,
                                                    c.exclude, static_cast<void*>(&taken));
            mine.Add(answer);
            // A cost the call did not write stays the marker; that is an answer too.
            mine.Add(cost);
            ++calls;
            if (answer)
                ++yes;
        }
    }
    mine.Add(ListIsEmpty(taken));
    FreeList(taken);
    log::Write("autotest: demolition: occupants %08X (%u occupants of %d types, %d calls, %u true)", mine.h,
               static_cast<unsigned>(chosen.size()), types, calls, yes);
    f.Add(mine.h);
}

// --- 6. ModifyTerrainHeight, apply false ----------------------------------------------------

void DriveTerrain(Fnv& f, void* d, int32_t sizeX, int32_t sizeZ, int& calls)
{
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    if (!d || !terrain || sizeX < 16 || sizeZ < 16) {
        f.Missing();
        return;
    }
    struct Rect {
        int32_t x1, z1, x2, z2;
    };
    const Rect rects[] = {
        {sizeX / 2, sizeZ / 2, sizeX / 2 + 3, sizeZ / 2 + 3},
        {sizeX / 4, sizeZ / 4, sizeX * 3 / 4 - 1, sizeZ * 3 / 4 - 1},
        {5, 5, 5, 5},
        {0, 0, 0, sizeZ / 2},
    };
    uint32_t total = 0;
    // Not per cell: the height of the rectangle's first cell, two more, two less.
    for (const Rect& r : rects) {
        const float base = Virtual<float>(terrain, 0x38, r.x1, r.z1);
        for (const float height : {base, base + 2.0f, base - 2.0f}) {
            const int32_t changed = Virtual<int32_t>(d, 0x24, uint32_t{0}, uint32_t{0}, r.x1, r.z1, r.x2, r.z2, height,
                                                     static_cast<void*>(nullptr), uint32_t{0}, static_cast<int64_t*>(nullptr));
            f.Add(changed);
            total += static_cast<uint32_t>(changed);
            ++calls;
        }
    }
    // The rectangles above hold occupants the terrain filter takes, mostly:
    // the call then counts nothing. A fixed walk over the city finds the
    // first few small rectangles that it does count, and asks for those
    // with the three heights.
    int hits = 0;
    int asked = 0;
    for (int32_t z = 8; z + 8 < sizeZ && hits < 4; z += 8) {
        for (int32_t x = 8; x + 8 < sizeX && hits < 4 && asked < 800; x += 8) {
            const float base = Virtual<float>(terrain, 0x38, x, z);
            ++asked;
            const int32_t changed = Virtual<int32_t>(d, 0x24, uint32_t{0}, uint32_t{0}, x, z, x + 1, z + 1, base + 2.0f,
                                                     static_cast<void*>(nullptr), uint32_t{0}, static_cast<int64_t*>(nullptr));
            if (!changed)
                continue;
            ++hits;
            f.Add(x);
            f.Add(z);
            f.Add(changed);
            total += static_cast<uint32_t>(changed);
            for (const float height : {base, base - 2.0f, base + 0.01f}) {
                const int32_t again = Virtual<int32_t>(d, 0x24, uint32_t{0}, uint32_t{0}, x, z, x + 1, z + 1, height,
                                                       static_cast<void*>(nullptr), uint32_t{0}, static_cast<int64_t*>(nullptr));
                f.Add(again);
                total += static_cast<uint32_t>(again);
                ++calls;
            }
        }
    }
    f.Add(hits);
    calls += asked;
    // Per cell: only the height the first cell has, over one cell, so that no
    // cell is to change (the call tells the terrain of a change at its end
    // whether apply is set or not).
    for (const Rect& r : {Rect{5, 5, 5, 5}, Rect{sizeX / 2, sizeZ / 2, sizeX / 2, sizeZ / 2}}) {
        const float base = Virtual<float>(terrain, 0x38, r.x1, r.z1);
        const int32_t changed = Virtual<int32_t>(d, 0x24, uint32_t{0}, uint32_t{1}, r.x1, r.z1, r.x2, r.z2, base,
                                                 static_cast<void*>(nullptr), uint32_t{0}, static_cast<int64_t*>(nullptr));
        f.Add(changed);
        ++calls;
    }
    log::Write("autotest: demolition: terrain %08X (%d calls, %u cells counted)", f.h, calls, total);
}

// --- 7. DemolishLots, price only -------------------------------------------------------------

void DriveLots(Fnv& f, void* d, const Sample& sample, int& calls)
{
    if (!d || sample.lots.empty()) {
        f.Missing();
        return;
    }
    const std::size_t count = sample.lots.size();
    const std::size_t take = count < kMostLots ? count : kMostLots;
    std::vector<void*> chosen;
    for (std::size_t i = 0; i < take; ++i)
        chosen.push_back(sample.lots[i * count / take]);
    struct Call {
        std::size_t lots; // how many of the chosen, from the first
        int32_t privilege;
        uint32_t flags;
        uint32_t clearZoned;
    };
    const Call list[] = {{chosen.size(), 0, 0u, 0u}, {chosen.size(), 1, 1u, 1u}, {1, 0, 0u, 0u}, {3, 1, 1u, 0u}, {1, 1, 1u, 1u}};
    for (const Call& c : list) {
        char* head = NewList();
        for (std::size_t i = 0; i < c.lots; ++i)
            PushBack(head, chosen[i]);
        TreeSet set{};
        NewSet(set);
        int64_t cost = 0;
        const uint8_t result =
            Virtual<uint8_t>(d, 0x1C, uint32_t{0}, static_cast<void*>(&head), c.privilege, c.flags, c.clearZoned,
                             static_cast<void*>(nullptr), &cost, &set);
        ++calls;
        f.Add(result);
        f.Add(cost);
        f.Add(set.size);
        std::vector<uint32_t> types;
        for (void* const occupant : SetMembers(set))
            types.push_back(Virtual<uint32_t>(occupant, 0x1C));
        std::sort(types.begin(), types.end());
        for (const uint32_t type : types)
            f.Add(type);
        log::Write("autotest: demolition: lots %u: %s, cost %lld, %u taken", static_cast<unsigned>(c.lots),
                   result ? "true" : "false", static_cast<long long>(cost), static_cast<unsigned>(types.size()));
        DestroySet(set);
        FreeList(head);
    }
}

// --- The switch -------------------------------------------------------------------------------

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_DEMOLITION") || !game::IsBuild641())
        return;
    void* const d = day.city ? day.city->GetDemolitionUtility() : nullptr;
    void* const city = Global<void*>(addr::SC4_City);
    int32_t sizeX = 0, sizeZ = 0;
    if (city) {
        sizeX = Virtual<int32_t>(city, 0x174);
        sizeZ = Virtual<int32_t>(city, 0x178);
    }
    if (sizeX <= 0 || sizeZ <= 0 || sizeX > 4096 || sizeZ > 4096)
        sizeX = sizeZ = 0;
    Fnv object, tuning, filters, regions, occupants, terrain, lots, after;
    uint32_t before[5] = {}, later[5] = {};
    uint32_t filterSubs[9] = {};
    int filterCalls = 0, filterYes = 0, regionCalls = 0, regionOccupants = 0, occupantCalls = 0, occupantTypes = 0;
    int terrainCalls = 0, lotCalls = 0;
    Sample sample;
    Collect(sample, sizeX, sizeZ);

    DriveObject(object, d, city);
    DriveTuning(tuning, d, before, later);
    DriveFilters(filters, sample, city, sizeX, sizeZ, filterCalls, filterYes, filterSubs);
    std::vector<std::pair<void*, uint32_t>> kept;
    TreeSet keptSet{};
    bool haveKept = false;
    DriveRegions(regions, d, sample, sizeX, sizeZ, regionCalls, regionOccupants, kept, keptSet, haveKept);
    DriveOccupants(occupants, d, kept, occupantCalls, occupantTypes);
    if (haveKept)
        DestroySet(keptSet);
    DriveTerrain(terrain, d, sizeX, sizeZ, terrainCalls);
    DriveLots(lots, d, sample, lotCalls);
    // What the calls were to leave alone: the service's own fields and the
    // filter it keeps.
    if (d) {
        after.Add(At<void*>(d, 0x30) == nullptr);
        if (char* const filter = At<char*>(d, 0x1C)) {
            after.Add(At<uint8_t>(filter, 0x24));
            after.Add(At<uint32_t>(filter, 0x28));
            after.Add(At<void*>(filter, 0x2C) == nullptr);
            after.Add(At<void*>(filter, 0x30) == nullptr);
        } else {
            after.Missing();
        }
    } else {
        after.Missing();
    }
    for (void* const held : sample.held)
        Virtual<uint32_t>(held, 0x8);

    log::Write("autotest: demolition: object %08X tuning %08X (%08X %08X %08X %08X %08X, then %08X %08X %08X %08X %08X)",
               object.h, tuning.h, before[0], before[1], before[2], before[3], before[4], later[0], later[1], later[2],
               later[3], later[4]);
    log::Write("autotest: demolition: filters %08X (%u sampled, %d calls, %d true; cases %08X %08X %08X %08X %08X %08X %08X %08X, terrain %08X)",
               filters.h, static_cast<unsigned>(sample.chosen.size()), filterCalls, filterYes, filterSubs[0],
               filterSubs[1], filterSubs[2], filterSubs[3], filterSubs[4], filterSubs[5], filterSubs[6], filterSubs[7],
               filterSubs[8]);
    log::Write("autotest: demolition: regions %08X (%d calls, %d occupants) occupants %08X (%d calls) terrain %08X (%d calls) lots %08X (%d calls) after %08X",
               regions.h, regionCalls, regionOccupants, occupants.h, occupantCalls, terrain.h, terrainCalls, lots.h,
               lotCalls, after.h);
    // One short hash: the game cuts the daily fingerprint line at 4093 characters.
    Fnv all;
    for (const uint32_t h : {object.h, tuning.h, filters.h, regions.h, occupants.h, terrain.h, lots.h, after.h})
        all.Add(h);
    std::snprintf(g_token, sizeof g_token, " demolition %08X", all.h);
}

const DayHook kDemolition{"demolition", 56, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"demolition", 8, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
