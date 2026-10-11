// The interface code's part of the autotest (src/core/autotest_hooks.h;
// docs/ui.md): switches that call window classes directly, as their window
// would, and put what they leave into the fingerprint.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <cstdint>
#include <cstdio>
#include <string>

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
};

char g_budgetToken[120];

// The small window's static table of seven departments: of each row the
// percentage (+0x10), the cost (+0x18) and whether it changed (+0x20).
void AddBudgetTable(Fnv& f)
{
    for (std::size_t row = 0; row < 7; ++row) {
        char* const entry = reinterpret_cast<char*>(std::uintptr_t{0x00B0D8B8}) + row * 0x28;
        f.Bytes(entry + 0x10, 4);
        f.Bytes(entry + 0x18, 8);
        f.Bytes(entry + 0x20, 1);
    }
}

uint8_t Send(char* object, void* holder, uint32_t type, uint32_t code, uint32_t id)
{
    uint32_t message[4] = {type, code, id, 0};
    return Virtual<uint8_t>(object + 0x8, 0xC, Virtual<void*>(holder, 0xC), &message[0]);
}

// OPENSC4_TEST_BUDGET_WINDOWS=<day>: on that run day the two budget windows
// are made as the budget simulator makes them and worked through their
// window procedures: the small one filled, two sliders moved, applied and
// closed; the large one filled, a spinner moved, closed. What they wrote to
// the simulator is in the budget's own part of the fingerprint.
void DriveBudgetWindows(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_BUDGET_WINDOWS") || !game::IsBuild641())
        return;
    using NewFn = void*(__cdecl*)(std::size_t);
    using ConstructFn = char*(__thiscall*)(void*);
    Fnv f;
    int made = 0;
    if (void* const memory = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x28)) {
        char* const small = reinterpret_cast<ConstructFn>(addr::BudgetWindow_Small)(memory);
        Virtual<uint32_t>(small, 0x4);
        if (void* const holder = At<void*>(small, 0x10)) {
            ++made;
            f.Add(Send(small, holder, 0xA2BF8AD5, 0, 0)); // filled
            AddBudgetTable(f);
            f.Bytes(small + 0x20, 8);
            f.Add(Send(small, holder, 3, 0x887113A3, 0x301)); // two sliders
            f.Add(Send(small, holder, 3, 0x887113A3, 0x304));
            AddBudgetTable(f);
            f.Add(Send(small, holder, 3, 0x287259F6, 0x111)); // applied and closed
            AddBudgetTable(f);
        }
        Virtual<uint32_t>(small, 0x8);
    }
    if (void* const memory = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x60)) {
        char* const large = reinterpret_cast<ConstructFn>(addr::BudgetWindow_Large)(memory);
        Virtual<uint32_t>(large, 0x4);
        if (void* const holder = At<void*>(large, 0x14)) {
            ++made;
            f.Add(Send(large, holder, 0xA2BF8AD5, 0, 0)); // filled
            f.Bytes(large + 0x20, 0x40);
            f.Add(Send(large, holder, 0x682876C9, 0x502, 0)); // a spinner
            f.Bytes(large + 0x20, 0x40);
            f.Add(Send(large, holder, 3, 0x287259F6, 0x107)); // closed
        }
        Virtual<uint32_t>(large, 0x8);
    }
    std::snprintf(g_budgetToken, sizeof g_budgetToken, " budwin %d %08X", made, f.h);
    log::Write("autotest: budget windows driven:%s", g_budgetToken);
}

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_budgetToken;
}

const FingerprintPart kFingerprint{"ui", 97, &Fingerprint};
const DayHook kBudgetWindows{"ui.budget_windows", 97, &DriveBudgetWindows};

} // namespace
} // namespace opensc4::autotest
