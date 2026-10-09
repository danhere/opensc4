// The demand simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/demand_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::autotest {
namespace {

// Demand (cSC4DemandSimulator, build 641 offsets; docs/demand.md): the
// step's state, the four saved floats, each demand's fields (ID to
// region use, +0x10 to +0x48) under its key, the two maps of counted
// values (+0x1C, +0x28), the neutral tax rate (slot 10, float bits)
// and the jobs of every demand (slot 9). The tax rate is read only
// when the three demands it reads exist (the game's lookups insert).
void Fingerprint(cISC4City* city, std::string& line)
{
    void* const demandSim = city->GetDemandSimulator();
    char demandText[160] = " | demand none";
    if (demandSim && game::IsBuild641()) {
        using namespace demand_sim;
        TreeNode* const demands = Header(demandSim, field::kDemands);
        uint32_t demandSum = 0;
        for (TreeNode* node = First(demands); node != demands; node = Next(node)) {
            const char* const demand = static_cast<const char*>(Value<void*>(node));
            demandSum = demandSum * 31 + node->key;
            if (demand)
                demandSum = demandSum * 31 + BytesChecksum(demand + demand::kId, demand::kRegionUse + 4 - demand::kId);
        }
        auto mapSum = [demandSim](std::size_t offset) {
            TreeNode* const header = Header(demandSim, offset);
            uint32_t sum = 0;
            for (TreeNode* node = First(header); node != header; node = Next(node))
                sum = sum * 31 + BytesChecksum(&node->key, 8);
            return sum;
        };
        auto has = [demands](uint32_t id) { return Find(demands, id) != demands; };
        uint32_t taxBits = 0;
        if (has(0x1001) && has(0x3001) && has(0x4001) && At<void*>(demandSim, field::kTaxCurve)) {
            using TaxFn = double(__thiscall*)(void*);
            const float tax = static_cast<float>(reinterpret_cast<TaxFn>((*static_cast<void***>(demandSim))[10])(demandSim));
            std::memcpy(&taxBits, &tax, sizeof taxBits);
        }
        uint32_t jobsSum = 0;
        for (TreeNode* node = First(demands); node != demands; node = Next(node)) {
            using JobsFn = int32_t(__thiscall*)(void*, uint32_t);
            jobsSum = jobsSum * 31 + static_cast<uint32_t>(reinterpret_cast<JobsFn>(
                                         (*static_cast<void***>(demandSim))[9])(demandSim, node->key));
        }
        // The cell walk of state 1 fills +0x1C in time slices, so a day can
        // end anywhere in it: only its result (state 2 on) is compared.
        const uint32_t state = At<uint32_t>(demandSim, field::kState);
        std::snprintf(demandText, sizeof demandText, " | demand st %u %08X n %u %08X map %08X %08X tax %08X jobs %08X",
                      state, BytesChecksum(static_cast<char*>(demandSim) + field::kSaved, 16),
                      Size(demandSim, field::kDemands), demandSum, state == 1 ? 0u : mapSum(field::kPending),
                      mapSum(field::kCounted),
                      taxBits, jobsSum);
    }
    line += demandText;
}

// OPENSC4_TEST_DEMAND_MONTH=<n>: every n days of the run, the demand
// simulator's MonthPassed (0x006A9450, build 641): its monthly passes
// and a waiting step restarted, so a week's run goes through them
// without a month's end (docs/demand.md).
void Month(const Day& day)
{
    if (EveryRunDays(day, "OPENSC4_TEST_DEMAND_MONTH") && game::IsBuild641())
        if (void* const demand = day.city->GetDemandSimulator()) {
            reinterpret_cast<void(__thiscall*)(void*)>(addr::cSC4DemandSimulator_MonthPassed)(demand);
            log::Write("autotest: demand month passed on day %d", day.day);
        }
}

// OPENSC4_TEST_DEMAND_MESSAGES=<n>: on the n-th day, the messages the
// demand simulator gets rarely: SimulationBegin (0x66956810), the
// neighbours changed (0x8A0D15DA) and the passes (0x26C63343), to its
// DoMessage (docs/demand.md).
void Messages(const Day& day)
{
    if (OnRunDay(day, "OPENSC4_TEST_DEMAND_MESSAGES") && game::IsBuild641())
        if (void* const demand = day.city->GetDemandSimulator()) {
            for (const uint32_t type : {0x66956810u, 0x8A0D15DAu, 0x26C63343u}) {
                void* const message = demand_sim::NewMessage();
                Virtual<uint32_t>(message, 0x14, type);
                Virtual<uint8_t>(static_cast<char*>(demand) + 4, 0xC, message);
                Virtual<uint32_t>(message, 0x8);
            }
            log::Write("autotest: demand messages sent on day %d", day.day);
        }
}

const FingerprintPart kFingerprint{"demand", 50, &Fingerprint};
const DayHook kMonth{"demand.month", 80, &Month};
const DayHook kMessages{"demand.messages", 90, &Messages};

} // namespace
} // namespace opensc4::autotest
