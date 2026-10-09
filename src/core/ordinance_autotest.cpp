// The ordinance simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cIGZString.h>
#include <cISC4City.h>
#include <cISC4Ordinance.h>
#include <cISC4OrdinanceSimulator.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::autotest {
namespace {

// Ordinances (build 641): last month's expense and income, the
// enactment and repeal income pending (+0x28), how many are available
// and enacted, and a checksum of each one's ID, flags and adjusted
// income in ID order (docs/ordinance.md).
void Fingerprint(cISC4City* city, std::string& line)
{
    char ordinanceText[128] = "";
    if (cISC4OrdinanceSimulator* const ordinances = city->GetOrdinanceSimulator(); ordinances && game::IsBuild641()) {
        uint32_t ids[128];
        uint32_t count = 128;
        ordinances->GetOrdinanceIDArray(ids, count);
        uint32_t h = 2166136261u;
        for (uint32_t i = 0; i < count; ++i) {
            void* const ordinance = ordinances->GetOrdinanceByID(ids[i]);
            if (!ordinance)
                continue;
            struct {
                uint32_t id;
                uint8_t available, on, enabled, pad;
                int64_t adjusted;
            } state{ids[i], Virtual<uint8_t>(ordinance, 0x44), Virtual<uint8_t>(ordinance, 0x48),
                    Virtual<uint8_t>(ordinance, 0x4C), 0, Virtual<int64_t>(ordinance, 0x50)};
            const auto* b = reinterpret_cast<const uint8_t*>(&state);
            for (size_t k = 0; k < sizeof state; ++k)
                h = (h ^ b[k]) * 16777619u;
        }
        std::snprintf(ordinanceText, sizeof ordinanceText, " | ord %lld %lld %lld av %u on %u n %u %08X",
                      static_cast<long long>(ordinances->GetOrdinanceMonthlyExpense()),
                      static_cast<long long>(ordinances->GetOrdinanceMonthlyIncome()),
                      *reinterpret_cast<const long long*>(reinterpret_cast<const char*>(ordinances) + 0x28),
                      ordinances->GetOrdinanceAvailableCount(), ordinances->GetOrdinanceOnCount(), count, h);
    }
    line += ordinanceText;
}

void SetOrdinances(cISC4City* city, bool on)
{
    cISC4OrdinanceSimulator* const ordinances = city->GetOrdinanceSimulator();
    if (!ordinances)
        return;
    uint32_t ids[128];
    uint32_t count = 128;
    ordinances->GetOrdinanceIDArray(ids, count);
    for (uint32_t i = 0; i < count; ++i) {
        if (on) {
            ordinances->SetOrdinanceAvailable(ids[i], true);
            ordinances->SetOrdinanceOn(ids[i], true);
        } else {
            ordinances->SetOrdinanceOn(ids[i], false);
        }
    }
    log::Write("autotest: %u ordinances %s", count, on ? "made available and enacted" : "repealed");
}

void CycleOrdinances(cISC4City* city)
{
    cISC4OrdinanceSimulator* const ordinances = city->GetOrdinanceSimulator();
    if (!ordinances)
        return;
    using ThisFn = void(__thiscall*)(void*);
    reinterpret_cast<ThisFn>(addr::cSC4OrdinanceSimulator_CreateOrdinances)(ordinances);
    uint32_t ids[128];
    uint32_t count = 128;
    ordinances->GetOrdinanceIDArray(ids, count);
    uint32_t named = 0;
    for (uint32_t i = 0; i < count; ++i) {
        cISC4Ordinance* const ordinance = ordinances->GetOrdinanceByID(ids[i]);
        if (!ordinance)
            continue;
        if (ordinances->GetOrdinanceIDByName(*ordinance->GetName()) == ids[i])
            ++named;
        ordinance->AddRef();
        ordinances->RemoveOrdinance(*ordinance);
        ordinances->AddOrdinance(*ordinance);
        ordinance->Release();
    }
    using MessageFn = void*(__thiscall*)(void* memory);
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    void* const memory = reinterpret_cast<AllocateFn>(addr::RZ_operator_new)(0x2C);
    if (void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr) {
        Virtual<uint32_t>(message, 0x4);
        Virtual<uint32_t>(message, 0x14, 0x66956810u);
        Virtual<uint8_t>(reinterpret_cast<char*>(ordinances) + 0x4, 0xC, message); // DoMessage
        Virtual<uint32_t>(message, 0x8);
    }
    log::Write("autotest: ordinances created again, %u of %u found by name, removed and added back, recounted",
               named, count);
}

// OPENSC4_TEST_ORDINANCES=<n>: on the n-th day, make every ordinance
// available and enact it; OPENSC4_TEST_ORDINANCES_OFF=<n>: repeal
// every enacted one. The test cities enact none, so this reaches the
// ordinances' costs, effects and repeal paths (docs/ordinance.md).
void On(const Day& day)
{
    if (OnRunDay(day, "OPENSC4_TEST_ORDINANCES"))
        SetOrdinances(day.city, true);
}

void Off(const Day& day)
{
    if (OnRunDay(day, "OPENSC4_TEST_ORDINANCES_OFF"))
        SetOrdinances(day.city, false);
}

// OPENSC4_TEST_ORDINANCES_CYCLE=<n>: on the n-th day, the paths a
// saved city never runs: the new city's ordinance creation from the
// exemplars (they are already there, so each new one is released
// again), every ordinance looked up by name, removed and added back,
// and the recount message 0x66956810 (docs/ordinance.md).
void Cycle(const Day& day)
{
    if (OnRunDay(day, "OPENSC4_TEST_ORDINANCES_CYCLE") && game::IsBuild641())
        CycleOrdinances(day.city);
}

const FingerprintPart kFingerprint{"ordinance", 120, &Fingerprint};
const DayHook kOn{"ordinance.on", 130, &On};
const DayHook kOff{"ordinance.off", 140, &Off};
const DayHook kCycle{"ordinance.cycle", 150, &Cycle};

} // namespace
} // namespace opensc4::autotest
