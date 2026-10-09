// The fire and police simulators' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>
#include <cISC4SimGrid.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::autotest {
namespace {

// Fire and police (build 641 slots; docs/fire-police.md): the
// protection grids (slot 5), average and percent coverage (fire 19,
// 20; police 18, 19, float bits), station counts (fire 23, police
// 24), dispatch counts (25, 26), strikes (11, 10), and police jail
// population and capacity, criminals, arrests and jails (30, 29, 31,
// 34, 35: gzcom-dll's two GetJailCapacity overloads sit together at
// 28 and 29, which moves the declarations between them down one).
void Fingerprint(cISC4City* city, std::string& line)
{
    char serviceText[224] = "";
    if (game::IsBuild641()) {
        auto slotOf = [](void* object, int index) {
            using Fn = uint32_t(__thiscall*)(void*);
            return reinterpret_cast<Fn>((*static_cast<void***>(object))[index])(object);
        };
        auto floatOf = [](void* object, int index) {
            using Fn = float(__thiscall*)(void*);
            const float value = reinterpret_cast<Fn>((*static_cast<void***>(object))[index])(object);
            uint32_t bits;
            std::memcpy(&bits, &value, sizeof bits);
            return bits;
        };
        // Percent coverage (fire 20, police 19) returns a pointer to the
        // float (this + 0x9C), not the float gzcom-dll declares.
        auto percentOf = [&slotOf](void* object, int index) {
            return *reinterpret_cast<const uint32_t*>(static_cast<std::uintptr_t>(slotOf(object, index)));
        };
        using GridFn = cISC4SimGrid<int16_t>*(__thiscall*)(void*);
        void* const fire = city->GetFireProtectionSimulator();
        void* const police = city->GetPoliceSimulator();
        int at = 0;
        if (fire)
            at = std::snprintf(serviceText, sizeof serviceText, " | fire %08X %08X %08X st %u disp %u strike %u",
                               GridChecksum(reinterpret_cast<GridFn>((*static_cast<void***>(fire))[5])(fire)),
                               floatOf(fire, 19), percentOf(fire, 20), slotOf(fire, 23), slotOf(fire, 25),
                               slotOf(fire, 11) & 0xFF);
        if (police && at >= 0 && at < static_cast<int>(sizeof serviceText))
            std::snprintf(serviceText + at, sizeof serviceText - at,
                          " | police %08X %08X %08X st %u disp %u strike %u jail %u/%u crim %u arr %u jails %u",
                          GridChecksum(reinterpret_cast<GridFn>((*static_cast<void***>(police))[5])(police)),
                          floatOf(police, 18), percentOf(police, 19), slotOf(police, 24), slotOf(police, 26),
                          slotOf(police, 10) & 0xFF, slotOf(police, 30), slotOf(police, 29), slotOf(police, 31),
                          slotOf(police, 34), slotOf(police, 35));
    }
    line += serviceText;
}

// OPENSC4_TEST_PROTECTION: every fire and police station's funding set
// to 0 (fire slot 9, police slot 8), the month handler of both run (the
// base's slot 6, at +8) and the first jail broken out of (police slot
// 40), so strikes and jailbreaks happen in short runs
// (docs/fire-police.md). Build 641 layouts: stations list at +0x88,
// jails at +0xD4.
void DriveProtection(cISC4City* city)
{
    const struct {
        void* sim;
        std::size_t fundingSlot;
    } sims[] = {{city->GetFireProtectionSimulator(), 0x24}, {city->GetPoliceSimulator(), 0x20}};
    for (const auto& [sim, slot] : sims) {
        if (!sim)
            continue;
        static const float none = 0.0f; // passed by reference
        char* const head = *reinterpret_cast<char**>(static_cast<char*>(sim) + 0x88);
        for (char* node = *reinterpret_cast<char**>(head); node != head; node = *reinterpret_cast<char**>(node))
            Virtual<uint8_t>(sim, slot, *reinterpret_cast<void**>(node + 8), &none);
        Virtual<void>(static_cast<char*>(sim) + 8, 0x18);
    }
    if (void* const police = city->GetPoliceSimulator()) {
        char* const jails = *reinterpret_cast<char**>(static_cast<char*>(police) + 0xD4);
        if (char* const first = *reinterpret_cast<char**>(jails); first != jails)
            Virtual<uint8_t>(police, 0xA0, *reinterpret_cast<void**>(first + 8));
    }
    log::Write("autotest: fire and police driven (funding 0, month, jailbreak)");
}

void Drive(const Day& day)
{
    if (OnRunDay(day, "OPENSC4_TEST_PROTECTION") && game::IsBuild641())
        DriveProtection(day.city);
}

const FingerprintPart kFingerprint{"protection", 110, &Fingerprint};
const DayHook kDrive{"protection.drive", 110, &Drive};

} // namespace
} // namespace opensc4::autotest
