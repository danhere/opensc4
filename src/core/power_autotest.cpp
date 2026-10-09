// The power simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cIGZMessage2Standard.h>
#include <cISC4City.h>
#include <cISC4SimGrid.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace opensc4::autotest {
namespace {

// Power (build 641 vtable slots, not gzcom-dll's declarations, whose
// overloads compile in another order): the powered grid (slot 35),
// the monthly requirement, production, import, export, demand and
// consumption (11-15, 17; 16 takes a cell), unpowered and powered consumers (21, 22), power
// plants (24) and poles (26), and overworked, aged and distressed
// plants (27-29).
void Fingerprint(cISC4City* city, std::string& line)
{
    void* const power = city->GetPowerSimulator();
    char powerText[192] = " | power none";
    if (power && game::IsBuild641()) {
        auto slot = [power](int index) {
            using Fn = int32_t(__thiscall*)(void*);
            return reinterpret_cast<Fn>((*static_cast<void***>(power))[index])(power);
        };
        using GridFn = cISC4SimGrid<uint8_t>*(__thiscall*)(void*);
        auto* const powered = reinterpret_cast<GridFn>((*static_cast<void***>(power))[35])(power);
        std::snprintf(powerText, sizeof powerText,
                      " | power %08X req %d prod %d imp %d exp %d dem %d con %d | consumers %d/%d plants %d poles "
                      "%d %d/%d/%d",
                      GridChecksum(powered), slot(11), slot(12), slot(13), slot(14), slot(15), slot(17), slot(21),
                      slot(22), slot(24), slot(26), slot(27), slot(28), slot(29));
    }
    line += powerText;
}

// OPENSC4_TEST_FIND_POWER_PLANTS=1: one cISC4PowerSimulator::
// FindPowerPlants call (slot 30; the advisors' query) after the load.
void FindPowerPlants(cISC4City* city)
{
    if (Env("OPENSC4_TEST_FIND_POWER_PLANTS") == "1" && game::IsBuild641()) {
        if (void* const power = city->GetPowerSimulator()) {
            using FindFn = void(__thiscall*)(void*, void**, uint32_t*, void**, int32_t*);
            void* funded = nullptr;
            void* oldest = nullptr;
            uint32_t funding = 0;
            int32_t age = 0;
            reinterpret_cast<FindFn>((*static_cast<void***>(power))[30])(power, &funded, &funding, &oldest, &age);
            log::Write("autotest: FindPowerPlants: lowest funded %s %08X, oldest %s %d%%", funded ? "found" : "none",
                       funding, oldest ? "found" : "none", age);
            for (void* plant : {funded, oldest}) {
                using ReleaseFn = uint32_t(__thiscall*)(void*);
                if (plant)
                    reinterpret_cast<ReleaseFn>((*static_cast<void***>(plant))[2])(plant);
            }
        }
    }
}

// OPENSC4_TEST_POWER_WAKE=<n>: on the n-th day of the run, wake the
// power simulator as message 0x6AC284F3 does (the network tools send
// it; +0x14 of cSC4PowerSimulator, build 641), so its recompute runs
// in a run without network edits (docs/power.md).
void Wake(const Day& day)
{
    if (OnRunDay(day, "OPENSC4_TEST_POWER_WAKE") && game::IsBuild641()) {
        if (void* const power = day.city->GetPowerSimulator()) {
            static_cast<uint8_t*>(power)[0x14] = 1;
            log::Write("autotest: power simulator woken on day %d", day.day);
        }
    }
}

// OPENSC4_TEST_POWER_MESSAGES=<n>: on the n-th day, hand the power
// simulator (its +0x4 cIGZMessageTarget2, build 641) the two network
// messages a network edit would bring, over the whole city: a network
// change (0x278E6003, corners packed in data 1 and 2) and a changed
// cell rectangle (0x07699455, the rectangle in void 2), so their
// handlers run in runs without edits (docs/power.md).
void SendPowerMessages(void* power, cISC4City* city)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = cIGZMessage2Standard*(__thiscall*)(void* memory);
    using DoMessageFn = bool(__thiscall*)(void* target, cIGZMessage2* message);
    static int32_t rect[4];
    const auto cellsX = Virtual<uint32_t>(city, 0x174); // the cell counts, as the power simulator reads them
    const auto cellsZ = Virtual<uint32_t>(city, 0x178);
    rect[0] = 0;
    rect[1] = 0;
    rect[2] = static_cast<int32_t>(cellsX) - 1;
    rect[3] = static_cast<int32_t>(cellsZ) - 1;
    void* const target = static_cast<char*>(power) + 0x4;
    const auto doMessage = reinterpret_cast<DoMessageFn>((*static_cast<void***>(target))[3]);
    for (const uint32_t type : {0x278E6003u, 0x07699455u}) {
        void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
        cIGZMessage2Standard* const message =
            memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
        if (!message)
            return;
        message->AddRef();
        message->SetType(type);
        if (type == 0x278E6003u) {
            message->SetData1(static_cast<intptr_t>((1u << 16) | (cellsX - 1)));
            message->SetData2(static_cast<intptr_t>((1u << 16) | (cellsZ - 1)));
            message->SetData4(0);
        } else {
            message->SetVoid2(rect);
            message->SetData3(0);
        }
        doMessage(target, message);
        message->Release();
    }
    log::Write("autotest: power messages 0x278E6003 and 0x07699455 delivered over %ux%u cells", cellsX, cellsZ);
}

void Messages(const Day& day)
{
    if (OnRunDay(day, "OPENSC4_TEST_POWER_MESSAGES") && game::IsBuild641()) {
        if (void* const power = day.city->GetPowerSimulator())
            SendPowerMessages(power, day.city);
    }
}

const FingerprintPart kFingerprint{"power", 10, &Fingerprint};
const LoadHook kFindPowerPlants{"power.find_plants", 10, &FindPowerPlants};
const DayHook kWake{"power.wake", 10, &Wake};
const DayHook kMessages{"power.messages", 160, &Messages};

} // namespace
} // namespace opensc4::autotest
