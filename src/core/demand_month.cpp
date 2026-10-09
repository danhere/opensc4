// Our cSC4DemandSimulator (build 641), fourth part: the monthly passes
// (docs/demand.md, "The month"): the demand the city's civic buildings and
// connections create, and the jobs and workers exchanged with the
// neighbouring cities.
#include "core/demand_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <vector>

namespace opensc4::demand_sim {
namespace {

constexpr std::size_t kCivicDemand = 0xA8;  // float, saved
constexpr std::size_t kConnectionDemand = 0xAC; // float, saved
constexpr std::size_t kLogWeights = 0xBC;  // float[13], tuning 0x4A1F38C5
constexpr std::size_t kTuningC8 = 0xC8;    // tuning 0x4A1F38C6 (1)
constexpr std::size_t kTuningCC = 0xCC;    // tuning 0x4A1F38C7 (2)
constexpr std::size_t kTuningD0 = 0xD0;    // tuning 0x4A1F38C8 (20)
constexpr std::size_t kTuningD4 = 0xD4;    // tuning 0x4A1F38C9 (0.1)
constexpr std::size_t kTuningD8 = 0xD8;    // tuning 0x4A1F38CA (20)
constexpr std::size_t kConnections = 0x9C; // SC4List of the neighbour connections

using ListFn = void(__thiscall*)(void* list);
using SetInsertFn = void(__thiscall*)(void* set, void* result, const uint32_t* key);
using EraseFn = void(__thiscall*)(void* tree, TreeNode* root);
using FreeFn = void(__cdecl*)(void* p);
using AllocateFn = void*(__cdecl*)(std::size_t size);

// --- The game's lists and sets ---------------------------------------------------

// A list of the allocator at 0x00B207DC (or 0x00B207E0): a head node whose
// next and previous point to itself; elements are nodes (next, previous,
// object).
void* NewList(std::uintptr_t allocator)
{
    void** const head = Virtual<void**>(NodeAllocatorAt(allocator), 0xC, 0xCu);
    head[0] = head;
    head[1] = head;
    return head;
}

uint32_t Count(void* list)
{
    uint32_t count = 0;
    for (void** node = static_cast<void***>(list)[0]; node != list; node = static_cast<void**>(node[0]))
        ++count;
    return count;
}

// A std::set<uint32> (header, size), as the game's inline code builds one.
struct Set {
    TreeNode* header;
    uint32_t size;
};

Set NewSet()
{
    auto* const header = static_cast<TreeNode*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x14));
    header->color = 0;
    header->parent = nullptr;
    header->left = header;
    header->right = header;
    return {header, 0};
}

bool Contains(const Set& set, uint32_t key) { return Find(set.header, key) != set.header; }

void Insert(Set& set, uint32_t key)
{
    uint32_t result[2];
    reinterpret_cast<SetInsertFn>(addr::SC4Uint32Set_Insert2)(&set, result, &key);
}

void Destroy(Set& set)
{
    if (set.size) {
        reinterpret_cast<EraseFn>(addr::SC4Uint32Set_EraseTree)(&set, set.header->parent);
        set.header->left = set.header;
        set.header->parent = nullptr;
        set.header->right = set.header;
        set.size = 0;
    }
    if (set.header)
        reinterpret_cast<FreeFn>(addr::SC4_Free)(set.header);
}

// The IDs (slot 9) of the civic buildings of a kind (the service at +0x44,
// slot 17) into a list, and those that pass `keep` into the set.
template <typename Keep>
void Collect(void* self, uint32_t kind, void*& list, Set& set, Keep keep)
{
    list = NewList(addr::SC4List_NodeAllocator3);
    Virtual<uint32_t>(At<void*>(self, field::kService44), 0x44, kind, &list);
    for (void** node = static_cast<void***>(list)[0]; node != list; node = static_cast<void**>(node[0])) {
        const uint32_t id = Virtual<uint32_t>(node[2], 0x24);
        if (keep(id))
            Insert(set, id);
    }
}

int32_t AtLeastOne(uint32_t value) { return static_cast<int32_t>(value) < 1 ? 1 : static_cast<int32_t>(value); }

// The x87 sequence of 0x006A49A5 to 0x006A4A27: six counts through FYL2X
// (ln, at full extended precision) times their weights, summed, times
// 1/ln 2, plus `base`; stored as a float.
float LogTerms(const int32_t* counts, const float* weights, float base)
{
    static const uint32_t kInverseLn2 = 0x3FB8AA3B; // 0x00AB0E80
    float result;
    __asm {
        mov eax, weights
        mov edx, counts
        fild dword ptr [edx]
        fldln2
        fxch st(1)
        fyl2x
        fmul dword ptr [eax + 0x1c]
        fild dword ptr [edx + 4]
        fldln2
        fxch st(1)
        fyl2x
        fmul dword ptr [eax + 0x4]
        faddp st(1), st
        fild dword ptr [edx + 8]
        fldln2
        fxch st(1)
        fyl2x
        fmul dword ptr [eax + 0x2c]
        faddp st(1), st
        fild dword ptr [edx + 12]
        fldln2
        fxch st(1)
        fyl2x
        fmul dword ptr [eax + 0x18]
        faddp st(1), st
        fild dword ptr [edx + 16]
        fldln2
        fxch st(1)
        fyl2x
        fmul dword ptr [eax + 0x8]
        faddp st(1), st
        fild dword ptr [edx + 20]
        fldln2
        fxch st(1)
        fyl2x
        fmul dword ptr [eax]
        faddp st(1), st
        fmul dword ptr [kInverseLn2]
        fadd dword ptr [base]
        fstp dword ptr [result]
    }
    return result;
}

// --- 0x006A3ED0: the demand of civic buildings and connections --------------

// MonthPassed: +0xAC from the buildings of some kinds (the service at +0x44,
// slot 16; kinds 1, 2, 3, 8, 10, 12: their capacity through the service at
// 0x00B43DA8, slot 21, with 3) times +0xD0; +0xA8 from the counts of civic
// buildings by kind (unique IDs, see the sets below) through ln, weighted (+0xBC),
// times 1/ln 2; both plus two city counts (city slots 76, 77) times tuning.
// What changed goes to the supply of 0x4800 and 0x4900 (+0xAC) and 0x3800
// (+0xA8).
void __fastcall MonthEffects(void* self, void*)
{
    static bool logged = false;
    FirstCall("MonthEffects", logged);
    const float oldConnection = At<float>(self, kConnectionDemand);
    const float oldCivic = At<float>(self, kCivicDemand);
    float& connection = At<float>(self, kConnectionDemand);
    float& civic = At<float>(self, kCivicDemand);
    connection = 0.0f;
    civic = 0.0f;
    void* const buildings = At<void*>(self, field::kService44);

    void* all = NewList(addr::SC4List_NodeAllocator3);
    Virtual<uint32_t>(buildings, 0x40, &all);
    for (void** node = static_cast<void***>(all)[0]; node != all; node = static_cast<void**>(node[0])) {
        void* const building = node[2];
            const uint32_t kind = Virtual<uint32_t>(building, 0x14);
            if (kind == 1 || kind == 2 || kind == 3 || kind == 8 || kind == 10 || kind == 12) {
            void* const capacities = Global<void*>(addr::SC4_Service43DA8);
            // The 3 is pushed before the building's slot 3 call (which takes
            // nothing): it is the service call's second argument.
            const uint32_t capacity = Virtual<uint32_t>(capacities, 0x54, Virtual<uint32_t>(building, 0xC), 3u);
            connection = static_cast<float>(Unsigned(capacity) + connection);
        }
    }
    connection = static_cast<float>(static_cast<double>(At<float>(self, kTuningD0)) * connection);

    void* other = NewList(addr::SC4List_NodeAllocator4);
    Virtual<uint32_t>(buildings, 0x54, &other, 0u);

    void *list3 = nullptr, *list12 = nullptr, *list8 = nullptr, *list2 = nullptr, *list10 = nullptr,
         *list1 = nullptr, *list7 = nullptr, *list11 = nullptr, *list6 = nullptr;
    auto every = [](uint32_t) { return true; };
    Set set3 = NewSet();
    Collect(self, 3, list3, set3, every);
    Collect(self, 0xC, list12, set3, every);
    const int32_t count3 = AtLeastOne(set3.size + Count(list3));
    Set set8 = NewSet();
    Collect(self, 8, list8, set8, [&](uint32_t id) { return !Contains(set3, id); });
    const int32_t count8 = AtLeastOne(set8.size + Count(list8));
    Set set2 = NewSet();
    Collect(self, 2, list2, set2, [&](uint32_t id) { return !(Contains(set3, id) && Contains(set8, id)); });
    const int32_t count2 = AtLeastOne(set2.size + Count(list2));
    Set set10 = NewSet();
    Collect(self, 0xA, list10, set10,
            [&](uint32_t id) { return !(Contains(set3, id) && Contains(set8, id) && Contains(set2, id)); });
    const int32_t count10 = AtLeastOne(set10.size + Count(list10));
    Set set1 = NewSet();
    Collect(self, 1, list1, set1, every);
    const int32_t count1 = AtLeastOne(set1.size + Count(list1));
    Set set7 = NewSet();
    Collect(self, 7, list7, set7, every);
    Collect(self, 0xB, list11, set7, every);
    Collect(self, 6, list6, set7, every);
    const int32_t count7 = AtLeastOne(set7.size + Count(list6) + Count(list7));

    const int32_t counts[6] = {count7, count1, count10, count8, count3, count2};
    civic = LogTerms(counts, At<const float*>(self, kLogWeights), civic);

    void* const city = At<void*>(self, field::kCity);
    const double first = Unsigned(Virtual<uint32_t>(Virtual<void*>(city, 0x134), 0x34));
    civic = static_cast<float>(first * At<float>(self, kTuningC8) + civic);
    connection = static_cast<float>(first * At<float>(self, kTuningD4) + connection);
    const double second = Unsigned(Virtual<uint32_t>(Virtual<void*>(city, 0x130), 0x34));
    const double connectionRaw = second * At<float>(self, kTuningCC) + connection;
    connection = static_cast<float>(connectionRaw);
    civic = static_cast<float>(second * At<float>(self, kTuningD8) + civic);
    const float connectionChange = static_cast<float>(connectionRaw - oldConnection);
    Virtual<uint8_t>(DemandAt(self, 0x4800), 0x40, connectionChange);
    Virtual<uint8_t>(DemandAt(self, 0x4900), 0x40, connectionChange);
    const float civicChange = static_cast<float>(static_cast<double>(civic) - oldCivic);
    Virtual<uint8_t>(DemandAt(self, 0x3800), 0x40, civicChange);

    const auto destroy3 = reinterpret_cast<ListFn>(addr::SC4List_Destroy3);
    destroy3(&list6);
    destroy3(&list11);
    Destroy(set7);
    destroy3(&list7);
    Destroy(set1);
    destroy3(&list1);
    Destroy(set10);
    destroy3(&list10);
    Destroy(set2);
    destroy3(&list2);
    Destroy(set8);
    destroy3(&list8);
    destroy3(&list12);
    Destroy(set3);
    destroy3(&list3);
    reinterpret_cast<ListFn>(addr::SC4List_Destroy4)(&other);
    destroy3(&all);
}

// --- 0x006A8FF0: jobs and workers with the neighbours ----------------------------

constexpr std::size_t kSlotGet = 0x24;   // a neighbour's figure (slot 9)
constexpr std::size_t kSlotSet = 0x4C;   // a neighbour's figure set (slot 19)

// 0x006A68E0: for a residential group (0x1010, 0x1020, 0x1030), the workers
// a neighbour takes, by its own split (figures +0x1101 to +0x1104), added to
// the supply of the matching demands (+0x10000) and recorded at the
// neighbour.
void __fastcall ShareWorkers(void* self, void*, void* neighbour, uint32_t key, float amount)
{
    if (key != 0x1010 && key != 0x1020 && key != 0x1030)
        return;
    const int32_t base = Virtual<int32_t>(neighbour, kSlotGet, key);
    const int32_t part = Virtual<int32_t>(neighbour, kSlotGet, key + 1);
    const float ratio = base > 0 ? static_cast<float>(static_cast<double>(part) / base) : 0.8f;
    const float factor = *Virtual<const float*>(neighbour, 0x34);
    const float scaled = static_cast<float>(static_cast<double>(ratio) * factor * amount);
    int32_t values[4];
    for (uint32_t i = 0; i < 4; ++i)
        values[i] = Virtual<int32_t>(neighbour, kSlotGet, key + 0x1101 + i);
    int32_t total = static_cast<int32_t>(static_cast<uint32_t>(values[3]) + static_cast<uint32_t>(values[2]) +
                                         static_cast<uint32_t>(values[1]) + static_cast<uint32_t>(values[0]));
    if (total == 0) {
        values[0] += 1;
        total = 1;
    }
    float shares[4];
    for (int i = 0; i < 4; ++i)
        shares[i] = static_cast<float>(static_cast<double>(values[i]) * scaled / total);
    for (uint32_t i = 0; i < 4; ++i)
        Virtual<uint8_t>(DemandAt(self, key + 0x11101 + i), 0x40, shares[i]);
    for (uint32_t i = 0; i < 4; ++i)
        Virtual<uint32_t>(neighbour, kSlotSet, key + 0x1101 + i, shares[i]);
}

// 0x006A6AF0: for a job group (0x3110 ... 0x4400), the jobs a neighbour
// gives (by its own ratio), added to the supply of the group + 0x10001 and
// recorded at the neighbour; the workers they need by wealth (slot 8, jobs
// per unit) added to the demand of 0x12010, 0x12020, 0x12030.
void __fastcall ShareJobs(void* self, void*, void* neighbour, uint32_t key, float amount)
{
    switch (key) {
    case 0x3110:
    case 0x3120:
    case 0x3130:
    case 0x3320:
    case 0x3330:
    case 0x4100:
    case 0x4200:
    case 0x4300:
    case 0x4400:
        break;
    default:
        return;
    }
    const int32_t base = Virtual<int32_t>(neighbour, kSlotGet, key);
    const int32_t part = Virtual<int32_t>(neighbour, kSlotGet, key + 1);
    const double ratio = base > 0 ? static_cast<double>(part) / base : static_cast<double>(0.8f);
    const float jobs = static_cast<float>(ratio * amount);
    Virtual<uint8_t>(DemandAt(self, key + 0x10001), 0x40, jobs);
    Virtual<uint32_t>(neighbour, kSlotSet, key + 1, jobs);
    float perUnit[4];
    Virtual<void>(self, 0x20, perUnit, key + 1, 0u);
    const float workers[3] = {static_cast<float>(static_cast<double>(perUnit[1]) * jobs),
                              static_cast<float>(static_cast<double>(perUnit[2]) * jobs),
                              static_cast<float>(static_cast<double>(perUnit[3]) * jobs)};
    for (uint32_t i = 0; i < 3; ++i) {
        Virtual<uint8_t>(DemandAt(self, 0x12010 + 0x10 * i), 0x44, workers[i]);
        Virtual<uint32_t>(neighbour, kSlotSet, 0x2010 + 0x10 * i, workers[i]);
    }
}

using SortFn = void(__cdecl*)(void** begin, void** end, TreeNode* header, uint32_t size, uint32_t extra);
using CopyMapFn = void(__thiscall*)(void* destination, const void* source);

// MonthPassed: for each lot type (the service at +0x3C, slot 34) not
// updated in the last 0x41 days with at least 10 places open (its record's
// slots 9 less 8), the neighbour connections in order of their offer (slot
// 16 times 3, highest first) each take up to 1,000 of them (slot 18, slot
// 19 records it), as long as 10 are left: the type's supply (+0x10000),
// ShareWorkers and ShareJobs. Figures 0xAA772F1D, 0x6A774F64, 0xCA774F69:
// the places taken by lot kind (residential, commercial, industrial).
void __fastcall MonthJobs(void* self, void*)
{
    static bool logged = false;
    FirstCall("MonthJobs", logged);
    void* const city = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x90);
    if (city)
        Virtual<uint32_t>(city, 0x4);
    int32_t ignored[4] = {};
    Virtual<uint32_t>(city, 0x14, &ignored[1], &ignored[0]);
    Virtual<uint32_t>(city, 0x1C, &ignored[3], &ignored[2]);
    std::vector<void*> neighbours;
    auto** const connections = At<void**>(self, kConnections);
    for (auto** node = static_cast<void**>(connections[0]); node != connections; node = static_cast<void**>(node[0]))
        neighbours.push_back(node[2]);
    // The offers, by neighbour: a std::map<void*, float> of the game's.
    auto* const header = static_cast<TreeNode*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x18));
    header->color = 0;
    header->parent = nullptr;
    header->left = header;
    header->right = header;
    struct {
        TreeNode* header;
        uint32_t size;
    } offers = {header, 0};
    int32_t taken[3] = {};
    void* const service = At<void*>(self, field::kService3C);
    TreeNode* const types = *Virtual<TreeNode**>(service, 0x88);
    for (TreeNode* node = First(types); node != types; node = Next(node)) {
        void* const type = Value<void*>(node);
        const uint32_t key = Virtual<uint32_t>(type, 0x18);
        const uint32_t updated = Virtual<uint32_t>(service, 0xD0, key);
        const uint32_t today =
            Virtual<uint32_t>(Virtual<void*>(Global<void*>(addr::SC4_SimulatorService), 0x1C), 0x68);
        if (today <= updated || today - updated <= 0x41)
            continue;
        void* const record = Virtual<void*>(type, 0x48);
        const float wanted = Virtual<float>(record, 0x24);
        const double open = static_cast<double>(wanted) - Virtual<double>(record, 0x20);
        float left = static_cast<float>(open);
        if (!(open >= 10.0))
            continue;
        for (auto** entry = static_cast<void**>(connections[0]); entry != connections;
             entry = static_cast<void**>(entry[0])) {
            void* const neighbour = entry[2];
            int32_t ignoredPosition[4] = {};
            Virtual<uint32_t>(neighbour, 0x14, &ignoredPosition[1], &ignoredPosition[0]);
            Virtual<uint32_t>(neighbour, 0x1C, &ignoredPosition[3], &ignoredPosition[2]);
            const float offer = static_cast<float>(Virtual<double>(neighbour, 0x40, key) * 3.0);
            *FloatMapAt(&offers, reinterpret_cast<uint32_t>(neighbour)) = offer;
        }
        struct {
            TreeNode* header;
            uint32_t size;
            uint32_t extra;
        } copy = {};
        reinterpret_cast<CopyMapFn>(addr::SC4FloatMap_Copy)(&copy, &offers);
        reinterpret_cast<SortFn>(addr::cSC4DemandSimulator_SortByOffer)(
            neighbours.data(), neighbours.data() + neighbours.size(), copy.header, copy.size, copy.extra);
        for (void* const neighbour : neighbours) {
            int32_t count = Virtual<int32_t>(neighbour, 0x48, key);
            if (static_cast<double>(count) > 1000.0)
                count = 1000;
            if (static_cast<double>(count) > left)
                count = Ftol(left);
            const float amount = static_cast<float>(count);
            if (!(static_cast<double>(count) < 10.0)) {
                Virtual<uint32_t>(neighbour, 0x4C, key, amount);
                Virtual<uint8_t>(DemandAt(self, key + 0x10000), 0x40, amount);
                left = static_cast<float>(static_cast<double>(left) - amount);
                switch (Virtual<uint32_t>(type, 0x14)) {
                case 1:
                    taken[0] += count;
                    break;
                case 2:
                    taken[1] += count;
                    break;
                case 3:
                    taken[2] += count;
                    break;
                default:
                    break;
                }
                CallThis<void>(addr::cSC4DemandSimulator_ShareWorkers, self, neighbour, key, amount);
                CallThis<void>(addr::cSC4DemandSimulator_ShareJobs, self, neighbour, key, amount);
            }
            if (left < 10.0f)
                break;
        }
    }
    void* const figures = Global<void*>(addr::SC4_MonthlyFigures);
    Virtual<uint8_t>(figures, 0x30, 0xAA772F1Du, taken[0]);
    Virtual<uint8_t>(figures, 0x30, 0x6A774F64u, taken[1]);
    Virtual<uint8_t>(figures, 0x30, 0xCA774F69u, taken[2]);
    if (offers.size) {
        reinterpret_cast<EraseFn>(addr::SC4FloatMap_EraseTree)(&offers, offers.header->parent);
        offers.header->left = offers.header;
        offers.header->parent = nullptr;
        offers.header->right = offers.header;
        offers.size = 0;
    }
    reinterpret_cast<FreeFn>(addr::SC4_Free)(offers.header);
    Virtual<uint32_t>(city, 0x8);
}

// --- Installation -----------------------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

using SelfFn = void(__fastcall*)(void*, void*);
using ShareFn = void(__fastcall*)(void*, void*, void*, uint32_t, float);

SelfFn g_originalEffects = nullptr, g_originalJobs = nullptr;
ShareFn g_originalWorkers = nullptr, g_originalShareJobs = nullptr;

bool InstallMonth(registry::Mode)
{
    return game::IsBuild641() &&
           Install(addr::cSC4DemandSimulator_MonthEffects, &MonthEffects, &g_originalEffects) &&
           Install(addr::cSC4DemandSimulator_MonthJobs, &MonthJobs, &g_originalJobs) &&
           Install(addr::cSC4DemandSimulator_ShareWorkers, &ShareWorkers, &g_originalWorkers) &&
           Install(addr::cSC4DemandSimulator_ShareJobs, &ShareJobs, &g_originalShareJobs);
}

const bool g_registered = registry::Register({
    "simulators.demand.month",
    "simulators/demand",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DemandSimulator_MonthEffects,
    "Our demand month: civic buildings and connections, jobs and workers with the neighbours (docs/demand.md)",
    false,
    InstallMonth,
});

} // namespace
} // namespace opensc4::demand_sim
