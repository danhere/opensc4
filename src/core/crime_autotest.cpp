// The crime simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>
#include <cISC4SimGrid.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace opensc4::autotest {
namespace {

// Crime (cSC4CrimeSimulator, build 641 slots): the crime grid (slot
// 5), average and monthly count (6, 7), the hotspot (8), the step's
// fields (+0x7C to +0xD8: count, hotspot, ordinance factor, state,
// the month's target and its parts) and the incidents (crime type
// and name key each) (docs/crime.md).
void Fingerprint(cISC4City* city, std::string& line)
{
    void* const crime = city->GetCrimeSimulator();
    char crimeText[224] = " | crime none";
    if (crime && game::IsBuild641()) {
        void** const vtable = *static_cast<void***>(crime);
        using GridFn = cISC4SimGrid<uint8_t>*(__thiscall*)(void*);
        using CountFn = uint32_t(__thiscall*)(void*);
        using HotspotFn = uint8_t(__thiscall*)(void*, uint8_t*, int32_t*, int32_t*);
        uint8_t hotValue = 0;
        int32_t hotX = 0, hotZ = 0;
        const uint8_t hot = reinterpret_cast<HotspotFn>(vtable[8])(crime, &hotValue, &hotX, &hotZ);
        char* const base = static_cast<char*>(crime);
        char* const* const incidents = *reinterpret_cast<char* const* const*>(base + 0x70);
        const uint32_t incidentCount =
            static_cast<uint32_t>(*reinterpret_cast<char* const* const*>(base + 0x74) - incidents);
        uint32_t incidentSum = 0;
        for (uint32_t i = 0; i < incidentCount; ++i)
            if (incidents[i])
                incidentSum = incidentSum * 31 + BytesChecksum(incidents[i] + 4, 4) * 7 +
                              BytesChecksum(incidents[i] + 0x1C, 12);
        const int written = std::snprintf(
            crimeText, sizeof crimeText, " | crime %08X avg %u cnt %u hot %u %u,%d,%d st %08X inc %u %08X",
            GridChecksum(reinterpret_cast<GridFn>(vtable[5])(crime)), reinterpret_cast<CountFn>(vtable[6])(crime),
            reinterpret_cast<CountFn>(vtable[7])(crime), hot, hotValue, hotX, hotZ,
            BytesChecksum(base + 0x7C, 0x5C), incidentCount, incidentSum);
        // OPENSC4_TEST_CRIME_QUERIES=1: the queries only the UI and the
        // automata make (slots 9-13): the incident events, the visuals
        // flag of the first incident (set, then read), and whether a
        // crime sim and a crime prop are within 2 km of the hotspot.
        if (EnvInt("OPENSC4_TEST_CRIME_QUERIES", 0) > 0 && written > 0) {
            struct {
                char* begin = nullptr;
                char* end = nullptr;
                char* capacity = nullptr;
            } events;
            using EventsFn = uint8_t(__thiscall*)(void*, void*);
            const uint8_t any = reinterpret_cast<EventsFn>(vtable[11])(crime, &events);
            const uint32_t eventSum =
                events.begin ? BytesChecksum(events.begin, static_cast<size_t>(events.end - events.begin)) : 0u;
            if (events.begin)
                reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(events.begin);
            using SetFn = uint8_t(__thiscall*)(void*, uint32_t, uint8_t);
            using GetFn = uint8_t(__thiscall*)(void*, uint32_t);
            const uint8_t set = reinterpret_cast<SetFn>(vtable[12])(crime, 0, 1);
            const uint8_t visuals = reinterpret_cast<GetFn>(vtable[13])(crime, 0);
            const float position[3] = {static_cast<float>(hotX * 16 + 8), 0.0f, static_cast<float>(hotZ * 16 + 8)};
            using NearestFn = void*(__thiscall*)(void*, const float*, float);
            const bool sim = reinterpret_cast<NearestFn>(vtable[9])(crime, position, 2000.0f) != nullptr;
            const bool prop = reinterpret_cast<NearestFn>(vtable[10])(crime, position, 2000.0f) != nullptr;
            std::snprintf(crimeText + written, sizeof crimeText - written, " q %u %08X %u%u %u%u", any, eventSum,
                          set, visuals, sim, prop);
        }
    }
    line += crimeText;
}

// OPENSC4_TEST_CRIME_MONTH=<n>: every n days of the run, restart the
// crime simulator's monthly pass as MonthPassed does (state +0x90 from
// 6, waiting, to 0; cSC4CrimeSimulator, build 641), so a week's run
// goes through it without a month's end (docs/crime.md).
void Month(const Day& day)
{
    if (EveryRunDays(day, "OPENSC4_TEST_CRIME_MONTH") && game::IsBuild641())
        if (void* const crime = day.city->GetCrimeSimulator(); crime && static_cast<uint8_t*>(crime)[0x90] == 6) {
            static_cast<uint8_t*>(crime)[0x90] = 0;
            log::Write("autotest: crime month restarted on day %d", day.day);
        }
}

const FingerprintPart kFingerprint{"crime", 40, &Fingerprint};
const DayHook kMonth{"crime.month", 40, &Month};

} // namespace
} // namespace opensc4::autotest
