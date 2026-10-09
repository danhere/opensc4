// The plumbing (water) simulator's part of the autotest
// (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <cISC4City.h>
#include <cISC4SimGrid.h>

#include <cstdint>
#include <cstdio>

namespace opensc4::autotest {
namespace {

// Water (cSC4PlumbingSimulator, build 641 slots): the watered grid
// (slot 7), water sources and polluted sources (12, 13), production
// capacity, consumption, import and export (14-17), pipes, distressed
// and burst pipes (18-20), watered and unwatered buildings (21, 22)
// and demand (23) (docs/water.md).
void Fingerprint(cISC4City* city, std::string& line)
{
    void* const water = city->GetPlumbingSimulator();
    char waterText[192] = " | water none";
    if (water && game::IsBuild641()) {
        auto slot = [water](int index) {
            using Fn = int32_t(__thiscall*)(void*);
            return reinterpret_cast<Fn>((*static_cast<void***>(water))[index])(water);
        };
        using GridFn = cISC4SimGrid<uint8_t>*(__thiscall*)(void*);
        auto* const watered = reinterpret_cast<GridFn>((*static_cast<void***>(water))[7])(water);
        std::snprintf(waterText, sizeof waterText,
                      " | water %08X src %d/%d cap %d con %d imp %d exp %d pipes %d/%d/%d bld %d/%d dem %d",
                      GridChecksum(watered), slot(12), slot(13), slot(14), slot(15), slot(16), slot(17), slot(18),
                      slot(19), slot(20), slot(21), slot(22), slot(23));
    }
    line += waterText;
}

// OPENSC4_TEST_WATER_WAKE=<n>: the same as OPENSC4_TEST_POWER_WAKE for the
// plumbing simulator (+0x14 of cSC4PlumbingSimulator, set by the same
// message; docs/water.md).
void Wake(const Day& day)
{
    if (OnRunDay(day, "OPENSC4_TEST_WATER_WAKE") && game::IsBuild641()) {
        if (void* const water = day.city->GetPlumbingSimulator()) {
            static_cast<uint8_t*>(water)[0x14] = 1;
            log::Write("autotest: plumbing simulator woken on day %d", day.day);
        }
    }
}

const FingerprintPart kFingerprint{"water", 20, &Fingerprint};
const DayHook kWake{"water.wake", 20, &Wake};

} // namespace
} // namespace opensc4::autotest
