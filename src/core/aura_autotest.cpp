// The aura simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <cISC4City.h>
#include <cISC4SimGrid.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace opensc4::autotest {
namespace {

// Aura (cSC4AuraSimulator, build 641 offsets; docs/aura.md): the
// aura grid and the grids the monthly pass builds it from, the pass
// state, the mayor rating and its best and worst cells. The effect
// grid (+0x44) is left out while state 5 fills it: that state is cut
// into time slices, so a day can end at any point in it.
void Fingerprint(cISC4City* city, std::string& line)
{
    void* const aura = city->GetAuraSimulator();
    char auraText[256] = " | aura none";
    if (aura && game::IsBuild641()) {
        auto grid8 = [aura](std::size_t offset) {
            return GridChecksum(At<cISC4SimGrid<int8_t>*>(aura, offset));
        };
        auto grid16 = [aura](std::size_t offset) {
            return GridChecksum(At<cISC4SimGrid<int16_t>*>(aura, offset));
        };
        const uint32_t state = At<uint32_t>(aura, 0x70);
        std::snprintf(auraText, sizeof auraText,
                      " | aura %08X %08X %08X %08X %08X %08X %08X %08X %08X st %u rating %d %d max %d,%d=%d min "
                      "%d,%d=%d",
                      grid8(0x40), state == 5 ? 0u : grid8(0x44), grid8(0x48), grid8(0x4C), grid8(0x50),
                      grid16(0x54), grid16(0x58), grid16(0x68), grid16(0x6C), state, At<int32_t>(aura, 0x84),
                      At<int32_t>(aura, 0x88), At<int32_t>(aura, 0x148), At<int32_t>(aura, 0x14C),
                      At<int16_t>(aura, 0x150), At<int32_t>(aura, 0x154), At<int32_t>(aura, 0x158),
                      At<int16_t>(aura, 0x15C));
    }
    line += auraText;
}

// OPENSC4_TEST_AURA_PASS=<n>: on the n-th day, start the aura
// simulator's monthly pass as MonthPassed does when it is idle (state
// +0x70 of cSC4AuraSimulator set to 1, build 641; docs/aura.md), so a
// pass runs in every test window.
void Pass(const Day& day)
{
    if (OnRunDay(day, "OPENSC4_TEST_AURA_PASS") && game::IsBuild641()) {
        if (void* const aura = day.city->GetAuraSimulator()) {
            uint32_t& state = At<uint32_t>(aura, 0x70);
            if (state == 0)
                state = 1;
            log::Write("autotest: aura pass started on day %d (state %u)", day.day, state);
        }
    }
}

const FingerprintPart kFingerprint{"aura", 30, &Fingerprint};
const DayHook kPass{"aura.pass", 30, &Pass};

} // namespace
} // namespace opensc4::autotest
