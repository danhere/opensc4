// The budget simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <cISC4City.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace opensc4::autotest {
namespace {

uint32_t Mix(uint32_t h, const void* data, std::size_t size)
{
    const auto* b = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i)
        h = (h ^ b[i]) * 16777619u;
    return h;
}

// In-order successor in a std::map.
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

// Budget (cSC4BudgetSimulator, build 641 offsets; docs/budget.md): the
// funds and their copy, this year's expense and income so far, the tax
// income, the stored totals (+0x830 to +0x848), this month's total expense
// and income (slots 14, 16), a checksum of the 12 tax groups' rates and
// assessed values, the bonds (count, payments) and the departments (count,
// a checksum of each one's ID, expenses, income, spending and funding,
// slots 15, 16, 30, 10, and of its line items' IDs, full and current
// expenses and income).
void Fingerprint(cISC4City* city, std::string& line)
{
    void* const budget = city->GetBudgetSimulator();
    char text[320] = " | budget none";
    if (budget && game::IsBuild641()) {
        auto i64 = [budget](std::size_t offset) { return static_cast<long long>(At<int64_t>(budget, offset)); };
        uint32_t groups = 2166136261u;
        for (std::size_t g = 0; g < 12; ++g) {
            groups = Mix(groups, static_cast<char*>(budget) + 0x70 + g * 0x80, 4);
            groups = Mix(groups, static_cast<char*>(budget) + 0xE0 + g * 0x80, 8);
        }
        int bonds = 0;
        char* const head = At<char*>(budget, 0x780);
        for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0))
            ++bonds;
        uint32_t departments = 2166136261u;
        int count = 0;
        char* const header = At<char*>(budget, 0x54);
        for (char* node = At<char*>(header, 0x8); node != header;) {
            void* const department = At<void*>(node, 0x14);
            const int64_t figures[3] = {Virtual<int64_t>(department, 0x3C), Virtual<int64_t>(department, 0x40),
                                        Virtual<int64_t>(department, 0x78)};
            departments = Mix(departments, node + 0x10, 4);
            departments = Mix(departments, figures, sizeof figures);
            departments = Mix(departments, Virtual<const float*>(department, 0x28), 4);
            char* const lines = At<char*>(department, 0x4C);
            for (char* at = At<char*>(lines, 0x8); at != lines; at = Next(at)) {
                void* const item = At<void*>(at, 0x14);
                const int64_t amounts[3] = {Virtual<int64_t>(item, 0x28), Virtual<int64_t>(item, 0x2C),
                                            Virtual<int64_t>(item, 0x38)};
                departments = Mix(departments, at + 0x10, 4);
                departments = Mix(departments, amounts, sizeof amounts);
            }
            ++count;
            node = Next(node);
        }
        std::snprintf(text, sizeof text,
                      " | budget %lld %lld %lld ytd %lld %lld tax %lld tot %lld %lld %lld %lld mon %lld %lld groups "
                      "%08X bonds %d %lld depts %d %08X",
                      i64(0x28), i64(0x30), i64(0x38), i64(0x40), i64(0x48), i64(0x670), i64(0x830), i64(0x838),
                      i64(0x840), i64(0x848), static_cast<long long>(Virtual<int64_t>(budget, 0x38)),
                      static_cast<long long>(Virtual<int64_t>(budget, 0x40)), groups, bonds,
                      static_cast<long long>(Virtual<int64_t>(budget, 0xB4)), count, departments);
    }
    line += text;
}

// OPENSC4_TEST_BUDGET_BOND=<n>: on the n-th day, issue a bond of 25,000 and
// read back the loans, the groups and each group's departments (build 641
// slots; docs/budget.md). The test cities issue no bonds.
void Bond(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_BUDGET_BOND") || !game::IsBuild641())
        return;
    void* const budget = day.city->GetBudgetSimulator();
    if (!budget)
        return;
    const uint8_t issued = Virtual<uint8_t>(budget, 0xA0, 25000u);
    struct Vector {
        char* begin = nullptr;
        char* end = nullptr;
        char* capacity = nullptr;
    } loans, groups, departments;
    Virtual<uint32_t>(budget, 0xB8, &loans);
    Virtual<uint32_t>(budget, 0x74, &groups);
    int members = 0;
    for (char* group = groups.begin; group && group < groups.end; group += 0x28) {
        departments.end = departments.begin;
        Virtual<uint32_t>(budget, 0x7C, *reinterpret_cast<uint32_t*>(group), &departments);
        for (char* at = departments.begin; at && at < departments.end; at += 4, ++members)
            Virtual<uint32_t>(*reinterpret_cast<void**>(at), 0x8);
    }
    log::Write("autotest: bond %s, %d loans, %d groups, %d departments in groups", issued ? "issued" : "refused",
               static_cast<int>((loans.end - loans.begin) / 0x20), static_cast<int>((groups.end - groups.begin) / 0x28),
               members);
}

// OPENSC4_TEST_BUDGET_FUNDING=<n>: on the n-th day, each department's
// funding to 80%, and for its costliest locally funded object the local
// funding to 50%, the cost up by 100, then out of the department and in
// again; each department's line items and local figures read back (build
// 641 slots; docs/budget.md). The test cities change no funding.
void Funding(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_BUDGET_FUNDING") || !game::IsBuild641())
        return;
    void* const budget = day.city->GetBudgetSimulator();
    if (!budget)
        return;
    int departments = 0, local = 0, lines = 0;
    char* const header = At<char*>(budget, 0x54);
    for (char* node = At<char*>(header, 0x8); node != header; node = Next(node), ++departments) {
        void* const department = At<void*>(node, 0x14);
        const float most = 0.8f;
        Virtual<uint8_t>(department, 0x2C, &most, 0u);
        struct Vector {
            void** begin = nullptr;
            void** end = nullptr;
            void** capacity = nullptr;
        } items;
        Virtual<uint8_t>(department, 0x54, &items);
        for (void** at = items.begin; at && at < items.end; ++at, ++lines) {
            if (Virtual<void*>(department, 0x50, Virtual<uint32_t>(*at, 0xC)) != *at)
                log::Write("autotest: line item %08X not found again", Virtual<uint32_t>(*at, 0xC));
            Virtual<uint32_t>(*at, 0x8);
        }
        // The object of the highest cost (the set is ordered by address,
        // which can differ between runs); none on a tie.
        char* const set = At<char*>(department, 0x58);
        void* holder = nullptr;
        uint32_t purpose = 0;
        int64_t highest = -1;
        bool tie = false;
        for (char* at = At<char*>(set, 0x8); at != set; at = Next(at)) {
            const int64_t cost = Virtual<int64_t>(department, 0x6C, At<void*>(at, 0x10), At<uint32_t>(at, 0x14));
            tie = cost == highest || (tie && cost < highest);
            if (cost > highest) {
                highest = cost;
                holder = At<void*>(at, 0x10);
                purpose = At<uint32_t>(at, 0x14);
            }
        }
        if (!holder || tie)
            continue;
        const float half = 0.5f;
        Virtual<uint8_t>(department, 0x60, holder, &half, purpose);
        const int64_t cost = Virtual<int64_t>(department, 0x6C, holder, purpose);
        Virtual<uint8_t>(department, 0x64, holder, cost + 100, purpose);
        local += Virtual<uint8_t>(department, 0x84, purpose);
        Virtual<uint32_t>(holder, 0x4);
        Virtual<uint8_t>(department, 0x5C, holder, purpose);
        Virtual<uint8_t>(department, 0x58, holder, purpose);
        Virtual<uint32_t>(holder, 0x8);
        log::Write("autotest: local funding %.4f, cost %lld -> %lld", static_cast<double>(*Virtual<const float*>(
                       department, 0x68, holder, purpose)), static_cast<long long>(cost),
                   static_cast<long long>(Virtual<int64_t>(department, 0x6C, holder, purpose)));
    }
    log::Write("autotest: funding set for %d departments, %d line items, %d with local objects", departments, lines,
               local);
}

const FingerprintPart kFingerprint{"budget", 70, &Fingerprint};
const DayHook kBond{"budget.bond", 70, &Bond};
const DayHook kFunding{"budget.funding", 71, &Funding};

} // namespace
} // namespace opensc4::autotest
