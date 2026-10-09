// The flora simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <cstdint>
#include <cstdio>

namespace opensc4::autotest {
namespace {

// Flora: every flora occupant (occupant manager slot 39, type
// 0x74758926): count, and a checksum of each one's type, birth and
// last seeding dates, state, orientation (cISC4FloraOccupant slots
// 4, 6, 8, 10, 12) and position (docs/flora.md).
void Fingerprint(cISC4City* city, std::string& line)
{
    char floraText[64] = " | flora none";
    if (void* const occupants = Virtual<void*>(city, 0x88); occupants && game::IsBuild641()) {
        void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
        sentinel[0] = sentinel;
        sentinel[1] = sentinel;
        void* head = sentinel;
        Virtual<uint8_t>(occupants, 0x9C, &head, 0u, 0u, 0x74758926u, -1);
        uint32_t count = 0, sum = 0;
        for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
            void* const occupant = node[2];
            void* flora = nullptr;
            if (!Virtual<uint8_t>(occupant, 0x0, 0xC83DD057u, &flora) || !flora)
                continue;
            float position[3] = {};
            Virtual<uint32_t>(occupant, 0x20, position);
            const uint32_t values[5] = {Virtual<uint32_t>(flora, 0x10), Virtual<uint32_t>(flora, 0x18),
                                        Virtual<uint32_t>(flora, 0x20), Virtual<uint32_t>(flora, 0x28),
                                        Virtual<uint32_t>(flora, 0x30)};
            sum = sum * 31 + BytesChecksum(values, sizeof values) * 7 + BytesChecksum(position, sizeof position);
            ++count;
            Virtual<uint32_t>(flora, 0x8);
        }
        Virtual<uint8_t>(occupants, 0xC0, &head);
        reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4List_Destroy2)(&head);
        std::snprintf(floraText, sizeof floraText, " | flora %u %08X", count, sum);
    }
    line += floraText;
}

// OPENSC4_TEST_FLORA_MONTH=<n>: every n days of the run, the flora
// simulator's monthly pass, as MonthPassed does (the date into +0x40,
// then 0x006B2CB0; cSC4FloraSimulator, build 641) (docs/flora.md).
void Month(const Day& day)
{
    if (EveryRunDays(day, "OPENSC4_TEST_FLORA_MONTH") && game::IsBuild641())
        if (void* const flora = day.city->GetFloraSimulator()) {
            static_cast<int32_t*>(flora)[0x40 / 4] = day.day;
            reinterpret_cast<void(__thiscall*)(void*)>(addr::cSC4FloraSimulator_OnMonth)(flora);
            log::Write("autotest: flora month on day %d", day.day);
        }
}

const FingerprintPart kFingerprint{"flora", 70, &Fingerprint};
const DayHook kMonth{"flora.month", 60, &Month};

} // namespace
} // namespace opensc4::autotest
