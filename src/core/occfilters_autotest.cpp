// The occupant filters' part of the autotest (src/core/autotest_hooks.h;
// occfilters_1.cpp): a switch that makes one of each small filter class the
// way the game does (the factories by address, the group filter and the
// lot's filter by their constructors), sets their parameter fields, and
// calls their tests and interface slots over a fixed sample of the city's
// occupants, so that a run with simulators.occfilters.* on and one with it
// off can be compared. Nothing here calls our own functions, and the city is
// not changed.
//
//   OPENSC4_TEST_OCCFILTERS=<day>    token "occfilters": three parts, with
//     their sub-hashes on the log lines "autotest: occfilters: ...".
//
//   1. the tests: for every configuration of every class (type filter with
//      the three most frequent occupant types of the sample; types filter
//      empty and with two types; network filter with three masks; view
//      level filter with the kinds 1, 2 and 3; realistic filter with its
//      flags clear and each set; group filter with three sets of groups;
//      the base and default filters; the lot's filter for three lots), the
//      three tests IsOccupantIncluded (slot 3), IsOccupantTypeIncluded
//      (slot 4) and IsPropertyHolderIncluded (slot 5) over the sample: the
//      low byte of every answer is hashed, and the number of true answers
//      per test is on the log line;
//   2. QueryInterface (slot 0) with the IDs 1, 0x46A80026, 0xE4FDA3D4 and one
//      the filters do not take: the answer, which part came back (as an
//      offset), the change of the count, released again;
//   3. GetGZCLSID of the serializable part (+4, slot 5) of every class.
//
// Each filter is made with count 0, given one reference (AddRef, slot 1) and
// released at the end (slot 2), so that its own deleting destructor frees it
// once. The hashed types filter (0x005EB970, not ours) only takes part in
// parts 2 and 3. Write and Read are left out: there is no stand-in stream in
// the autotests to give them. Every pointer is left out of the hashes.
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

constexpr uint32_t kMostOccupants = 300;
constexpr std::uintptr_t kHashedFactory = 0x005EB970;

// The filter's slots and fields.
namespace slot {
constexpr std::size_t kQueryInterface = 0x00;
constexpr std::size_t kAddRef = 0x04;
constexpr std::size_t kRelease = 0x08;
constexpr std::size_t kIsOccupantIncluded = 0x0C;
constexpr std::size_t kIsOccupantTypeIncluded = 0x10;
constexpr std::size_t kIsPropertyHolderIncluded = 0x14;
constexpr std::size_t kAddGroup = 0x1C;
} // namespace slot

constexpr std::size_t kValue = 0x0C;
constexpr std::size_t kGetGZCLSIDOffset = 0x14; // slot 5 of the serializable part (+4)

// The city's occupants in a fixed order, as demolition_autotest.cpp takes
// them (the occupant manager's first occupant of eight types in every eighth
// cell and the building of the cell's lot), up to 300; the references are kept in `held`.
struct Sample {
    std::vector<void*> chosen;
    std::vector<void*> held;
    std::vector<void*> lots;
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
    // The first occupant of each of these types in the cell (slot 33): props,
    // flora, lot textures, network pieces, foundations, construction, walls
    // and fire (0 as the demolition test asks: none, the buildings come from
    // the lots).
    constexpr uint32_t kTypes[] = {0xA823821E, 0x74758926, 0x497F6D9D, 0xC772BF98, 0x68FD0C69, 0x096E763C,
                                   0x895D1169, 0x69D25C5F};
    for (int32_t z = 0; z < sizeZ && all.size() < 6000; z += 8) {
        for (int32_t x = 0; x < sizeX && all.size() < 6000; x += 8) {
            for (const uint32_t type : kTypes) {
                void* found = nullptr;
                if (Virtual<uint8_t>(manager, 0x84, &found, x, z, type) && found) {
                    out.held.push_back(found);
                    add(found);
                }
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

// A filter of the test: its name for the log, the object (one reference
// held by the test) and its class's own parts of the hash.
struct Made {
    std::string name;
    char* object;
    bool lotFilter;
};

char* Reference(char* object)
{
    if (object)
        Virtual<uint32_t>(object, slot::kAddRef);
    return object;
}

// The game's vector of uint32 at +0xC: a value added as the group filter's
// AddGroup does (the game's insert when the end is the capacity).
void AddToVector(char* object, uint32_t value)
{
    char* const vector = object + kValue;
    uint32_t*& end = At<uint32_t*>(object, 0x10);
    if (end == At<uint32_t*>(object, 0x14)) {
        This<void>(addr::SC4PtrVector_InsertN, vector, end, &value, &value, uint32_t{1}, uint32_t{1});
        return;
    }
    if (end)
        *end = value;
    end = end + 1;
}

void Add(std::vector<Made>& list, const char* name, char* object, bool lotFilter = false)
{
    list.push_back({name, Reference(object), lotFilter});
}

std::vector<uint32_t> FrequentTypes(const Sample& sample, std::size_t most)
{
    std::vector<std::pair<uint32_t, uint32_t>> counts; // type, count
    for (void* const occupant : sample.chosen) {
        const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
        auto at = std::find_if(counts.begin(), counts.end(), [type](const auto& c) { return c.first == type; });
        if (at == counts.end())
            counts.push_back({type, 1});
        else
            ++at->second;
    }
    std::sort(counts.begin(), counts.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    std::vector<uint32_t> out;
    for (std::size_t i = 0; i < counts.size() && i < most; ++i)
        out.push_back(counts[i].first);
    return out;
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_OCCFILTERS") || !game::IsBuild641())
        return;
    void* const city = Global<void*>(addr::SC4_City);
    int32_t sizeX = 0, sizeZ = 0;
    if (city) {
        sizeX = Virtual<int32_t>(city, 0x174);
        sizeZ = Virtual<int32_t>(city, 0x178);
    }
    if (sizeX <= 0 || sizeZ <= 0 || sizeX > 4096 || sizeZ > 4096)
        sizeX = sizeZ = 0;
    Sample sample;
    Collect(sample, sizeX, sizeZ);
    const std::vector<uint32_t> allTypes = FrequentTypes(sample, 1000);
    const std::vector<uint32_t> types(allTypes.begin(), allTypes.begin() + (allTypes.size() < 3 ? allTypes.size() : 3));

    // --- the filters --------------------------------------------------------------------
    std::vector<Made> made;
    Add(made, "base", Cdecl<char*>(addr::SC4OccupantFilterBase_Factory));
    Add(made, "default", Cdecl<char*>(addr::SC4OccupantFilterAll_Factory));
    for (std::size_t i = 0; i < 3; ++i) {
        char* const filter = Cdecl<char*>(addr::SC4OccupantTypeFilter_Factory);
        if (filter)
            At<uint32_t>(filter, kValue) = i < types.size() ? types[i] : 0x12345678u + static_cast<uint32_t>(i);
        char name[16];
        std::snprintf(name, sizeof name, "type%u", static_cast<unsigned>(i));
        Add(made, name, filter);
    }
    Add(made, "types-empty", Cdecl<char*>(addr::SC4OccupantTypesFilter_Factory));
    {
        char* const filter = Cdecl<char*>(addr::SC4OccupantTypesFilter_Factory);
        if (filter) {
            AddToVector(filter, types.size() > 0 ? types[0] : 0x497F6D9Du);
            AddToVector(filter, types.size() > 1 ? types[1] : 0xA823821Eu);
        }
        Add(made, "types-two", filter);
    }
    for (const uint32_t mask : {0x1F4Fu, 0x1FFFu, 0x0003u}) {
        char* const filter = Cdecl<char*>(addr::SC4NetworkOccupantFilter_Factory);
        if (filter)
            At<uint32_t>(filter, kValue) = mask;
        char name[24];
        std::snprintf(name, sizeof name, "network%04X", mask);
        Add(made, name, filter);
    }
    for (const uint32_t kind : {1u, 2u, 3u}) {
        char* const filter = Cdecl<char*>(addr::SC4OccupantFilterByKind_Factory);
        if (filter)
            At<uint32_t>(filter, kValue) = kind;
        char name[16];
        std::snprintf(name, sizeof name, "kind%u", kind);
        Add(made, name, filter);
    }
    for (uint32_t flags = 0; flags < 5; ++flags) {
        // clear, then lot, group, prop, all three
        char* const filter = Cdecl<char*>(addr::SC4RealisticOccupantFilter_Factory);
        if (filter) {
            At<uint8_t>(filter, 0xC) = (flags == 1 || flags == 4) ? 1 : 0;
            At<uint8_t>(filter, 0xD) = (flags == 2 || flags == 4) ? 1 : 0;
            At<uint8_t>(filter, 0xE) = (flags == 3 || flags == 4) ? 1 : 0;
        }
        char name[24];
        std::snprintf(name, sizeof name, "realistic%u", flags);
        Add(made, name, filter);
    }
    // Groups that occur in the sample: the first three of 0x4200 to 0x4240 and
    // 0x5000 to 0x5010 some occupant belongs to (slot 0x58), then the ones
    // the filters' own tests ask for.
    std::vector<uint32_t> present;
    for (uint32_t group = 0x4200; group < 0x5010 && present.size() < 3; group = group == 0x423F ? 0x5000 : group + 1) {
        for (void* const occupant : sample.chosen) {
            if (Virtual<uint8_t>(occupant, 0x58, group)) {
                present.push_back(group);
                break;
            }
        }
    }
    std::vector<std::vector<uint32_t>> groupSets = {{0x5001}, {0x4217, 0x4208, 0x421D}, {0x4217, 0x5001, 0x421D, 0x4208}};
    if (!present.empty()) {
        groupSets.push_back({present[0]});
        groupSets.push_back(present);
    }
    for (std::size_t g = 0; g < groupSets.size(); ++g) {
        char* const filter = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x10}));
        if (filter) {
            This<void*>(addr::SC4OccupantGroupFilter_SC4OccupantGroupFilter, filter, groupSets[g][0]);
            for (std::size_t i = 1; i < groupSets[g].size(); ++i)
                Virtual<void>(filter, slot::kAddGroup, groupSets[g][i]);
        }
        char name[16];
        std::snprintf(name, sizeof name, "group%u", static_cast<unsigned>(g));
        Add(made, name, filter);
    }
    const std::size_t lotCount = sample.lots.size() < 3 ? sample.lots.size() : 3;
    for (std::size_t l = 0; l < lotCount; ++l) {
        char* filter = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x38}));
        if (filter) {
            filter = This<char*>(addr::cSC4LotOccupantFilter_Construct, filter);
            This<void>(addr::cSC4LotOccupantFilter_SetLot, filter, sample.lots[l]);
        }
        char name[16];
        std::snprintf(name, sizeof name, "lot%u", static_cast<unsigned>(l));
        Add(made, name, filter, true);
    }
    // The hashed types filter: parts 2 and 3 only.
    char* const hashed = Reference(Cdecl<char*>(kHashedFactory));

    // --- 1. the tests -------------------------------------------------------------------
    Fnv tests;
    unsigned configs = 0;
    for (const Made& m : made) {
        Fnv mine;
        unsigned yes[3] = {0, 0, 0};
        if (!m.object) {
            mine.Missing();
        } else {
            for (void* const occupant : sample.chosen) {
                const uint8_t included = Virtual<uint8_t>(m.object, slot::kIsOccupantIncluded, occupant);
                const uint8_t typeIncluded =
                    Virtual<uint8_t>(m.object, slot::kIsOccupantTypeIncluded, Virtual<uint32_t>(occupant, 0x1C));
                void* const holder = Virtual<void*>(occupant, 0x18);
                uint8_t holderIncluded = 0xEE;
                if (holder)
                    holderIncluded = Virtual<uint8_t>(m.object, slot::kIsPropertyHolderIncluded, holder);
                mine.Add(included);
                mine.Add(typeIncluded);
                mine.Add(holderIncluded);
                yes[0] += included ? 1 : 0;
                yes[1] += typeIncluded ? 1 : 0;
                yes[2] += (holderIncluded != 0xEE && holderIncluded) ? 1 : 0;
            }
        }
        log::Write("autotest: occfilters: tests %s %08X (%u occupants, true: occupant %u, type %u, holder %u)",
                   m.name.c_str(), mine.h, static_cast<unsigned>(sample.chosen.size()), yes[0], yes[1], yes[2]);
        tests.Add(mine.h);
        ++configs;
    }

    // --- 2. QueryInterface and 3. GetGZCLSID ---------------------------------------------
    Fnv queries, ids;
    std::vector<char*> everything;
    for (const Made& m : made)
        everything.push_back(m.object);
    everything.push_back(hashed);
    for (char* const filter : everything) {
        if (!filter) {
            queries.Missing();
            ids.Missing();
            continue;
        }
        for (const uint32_t iid : {0x00000001u, 0x46A80026u, 0xE4FDA3D4u, 0x12345678u}) {
            void* out = nullptr;
            const uint32_t before = At<uint32_t>(filter, 0x8);
            const uint8_t has = Virtual<uint8_t>(filter, slot::kQueryInterface, iid, &out);
            queries.Add(iid);
            queries.Add(has);
            queries.Add(out != nullptr);
            if (out)
                queries.Add(static_cast<int32_t>(static_cast<char*>(out) - filter));
            queries.Add(At<uint32_t>(filter, 0x8) - before);
            if (has && out)
                Virtual<uint32_t>(filter, slot::kRelease);
            queries.Add(At<uint32_t>(filter, 0x8) - before);
        }
        ids.Add(Virtual<uint32_t>(filter + 4, kGetGZCLSIDOffset));
    }
    log::Write("autotest: occfilters: queries %08X ids %08X (%u filters)", queries.h, ids.h,
               static_cast<unsigned>(everything.size()));

    // --- the end: every filter freed once ----------------------------------------------
    for (char* const filter : everything)
        if (filter)
            Virtual<uint32_t>(filter, slot::kRelease);
    for (void* const held : sample.held)
        Virtual<uint32_t>(held, 0x8);

    log::Write("autotest: occfilters: all %08X (%u configurations), types %08X %08X %08X, %u lots, %u distinct types, %u groups present",
               tests.h, configs, types.size() > 0 ? types[0] : 0u, types.size() > 1 ? types[1] : 0u,
               types.size() > 2 ? types[2] : 0u, static_cast<unsigned>(lotCount),
               static_cast<unsigned>(allTypes.size()), static_cast<unsigned>(present.size()));
    Fnv all;
    for (const uint32_t h : {tests.h, queries.h, ids.h})
        all.Add(h);
    std::snprintf(g_token, sizeof g_token, " occfilters %08X", all.h);
}

const DayHook kOccFilters{"occfilters", 58, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"occfilters", 11, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
