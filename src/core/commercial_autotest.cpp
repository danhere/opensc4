// The commercial and industrial simulators' part of the autotest
// (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <cISC4City.h>
#include <cISC4Lot.h>
#include <cISC4LotManager.h>

#include <cstdint>
#include <cstdio>

namespace opensc4::autotest {
namespace {

// A proximity map's cells (SC4SimGridBool8, as its 0x0045E0D0: +0x38 holds
// one 12-byte vector per x, +0x20/+0x24 the counts).
uint32_t Bits(char* map)
{
    if (!map || !At<char*>(map, 0x38))
        return 0;
    uint32_t h = 2166136261u;
    const uint32_t nx = At<uint32_t>(map, 0x20), nz = At<uint32_t>(map, 0x24);
    for (uint32_t x = 0; x < nx; ++x) {
        const uint8_t* const row = At<const uint8_t*>(At<char*>(map, 0x38), 12 * x);
        for (uint32_t z = 0; z < nz; ++z)
            h = (h ^ row[z]) * 16777619u;
    }
    return h;
}

// Commercial and industrial (the city's slots 54 and 60, build 641 offsets;
// docs/commercial-industrial.md): the commercial step, dirty flag and both
// proximity maps' cells; the industrial initialised flag.
void Fingerprint(cISC4City* city, std::string& line)
{
    char commercialText[96] = " | ci none";
    if (void* const commercial = Virtual<void*>(city, 0xD8); commercial && game::IsBuild641()) {
        void* const industrial = Virtual<void*>(city, 0xF0);
        std::snprintf(commercialText, sizeof commercialText, " | ci %u %u %08X %08X %u", At<uint32_t>(commercial, 0x24),
                      At<uint8_t>(commercial, 0x20), Bits(At<char*>(commercial, 0x34)), Bits(At<char*>(commercial, 0x38)),
                      industrial ? At<uint8_t>(industrial, 0xC) : 9u);
    }
    line += commercialText;
}

// OPENSC4_TEST_COMMERCIAL=<n>: on the n-th day, the commercial simulator
// handed "removed" then "added" (0x99EF1143, 0x99EF1142, kind 0x278128A0 in
// data 3) for each building on a lot, found cell by cell (at most 400), so
// that a commercial building starts its proximity-map cycle.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_COMMERCIAL") || !game::IsBuild641())
        return;
    cISC4City* const city = day.city;
    void* const commercial = Virtual<void*>(city, 0xD8);
    cISC4LotManager* const lots = city->GetLotManager();
    if (!commercial || !lots)
        return;
    const auto cellsX = Virtual<int32_t>(city, 0x174);
    const auto cellsZ = Virtual<int32_t>(city, 0x178);
    void* seen[400];
    uint32_t count = 0;
    for (int32_t z = 0; z < cellsZ && count < 400; ++z) {
        for (int32_t x = 0; x < cellsX && count < 400; ++x) {
            cISC4Lot* const lot = lots->GetLot(x, z, false);
            void* const building = lot ? static_cast<void*>(lot->GetBuilding()) : nullptr;
            if (!building)
                continue;
            bool known = false;
            for (uint32_t i = 0; i < count && !known; ++i)
                known = seen[i] == building;
            if (!known)
                seen[count++] = building;
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        Deliver(commercial, 0x99EF1143, seen[i], 0, 0x278128A0);
        Deliver(commercial, 0x99EF1142, seen[i], 0, 0x278128A0);
    }
    log::Write("autotest: %u buildings removed and added for the commercial simulator", count);
}

const FingerprintPart kFingerprint{"commercial", 87, &Fingerprint};
const DayHook kDrive{"commercial.drive", 75, &Drive};

} // namespace
} // namespace opensc4::autotest
