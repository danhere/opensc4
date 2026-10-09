// The traffic simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"

#include <cISC4City.h>
#include <cISC4TrafficSimulator.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::autotest {
namespace {

// Traffic (build 641 slots): trip scale and the one shown (14, 15),
// last month's income (18) and the freight scaling (27), as float bits.
void Fingerprint(cISC4City* city, std::string& line)
{
    cISC4TrafficSimulator* traffic = city->GetTrafficSimulator();
    char trafficText[96] = "";
    if (traffic && game::IsBuild641()) {
        auto slot = [traffic](int index) {
            using Fn = float(__thiscall*)(void*);
            const float value = reinterpret_cast<Fn>((*reinterpret_cast<void***>(traffic))[index])(traffic);
            uint32_t bits;
            std::memcpy(&bits, &value, sizeof bits);
            return bits;
        };
        std::snprintf(trafficText, sizeof trafficText, " | traffic %08X %08X %08X %08X", slot(14), slot(15),
                      slot(18), slot(27));
    }
    line += trafficText;
}

const FingerprintPart kFingerprint{"traffic", 90, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
