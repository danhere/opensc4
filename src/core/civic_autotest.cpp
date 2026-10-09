// The civic building simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cIGZMessage2Standard.h>
#include <cIGZUnknown.h>
#include <cISC4City.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::autotest {
namespace {

// Civic buildings (cSC4CivicBuildingSimulator, build 641 offsets;
// docs/civic.md): the excluded families (+0x18) and the conditional
// building statuses (+0x2C), each a count and a checksum in bucket
// order, the ape escape candidates and landmarks (list sizes), and
// whether the department (+0x14) is set.
void Fingerprint(cISC4City* city, std::string& line)
{
    char civicText[96] = "";
    if (game::IsBuild641()) {
        if (char* const civic = reinterpret_cast<char*>(city->GetCivicBuildingSimulator())) {
            auto tableSum = [](char* table, bool statuses) {
                char** const buckets = *reinterpret_cast<char***>(table + 4);
                const auto count = static_cast<uint32_t>(*reinterpret_cast<char***>(table + 8) - buckets);
                uint32_t sum = 0;
                for (uint32_t i = 0; i < count; ++i)
                    for (char* node = buckets[i]; node; node = *reinterpret_cast<char**>(node)) {
                        sum = sum * 31 + *reinterpret_cast<uint32_t*>(node + 4);
                        if (statuses) {
                            sum = sum * 31 + *reinterpret_cast<uint8_t*>(node + 8);
                            sum = sum * 31 + *reinterpret_cast<uint16_t*>(node + 0xA);
                            const char* const begin = *reinterpret_cast<char**>(node + 0x10);
                            const char* const end = *reinterpret_cast<char**>(node + 0x14);
                            sum = sum * 31 + (begin ? BytesChecksum(begin, static_cast<size_t>(end - begin)) : 0u);
                        }
                    }
                return sum;
            };
            auto listSize = [](char* list) {
                void** const head = *reinterpret_cast<void***>(list);
                uint32_t size = 0;
                for (void** node = static_cast<void**>(head[0]); node != head; node = static_cast<void**>(node[0]))
                    ++size;
                return size;
            };
            std::snprintf(civicText, sizeof civicText, " | civic %u %08X %u %08X z %u l %u d %u",
                          *reinterpret_cast<uint32_t*>(civic + 0x28), tableSum(civic + 0x18, false),
                          *reinterpret_cast<uint32_t*>(civic + 0x3C), tableSum(civic + 0x2C, true),
                          listSize(civic + 0x40), listSize(civic + 0x44),
                          *reinterpret_cast<void**>(civic + 0x14) != nullptr ? 1u : 0u);
        }
    }
    line += civicText;
}

// OPENSC4_TEST_CIVIC: the civic building simulator (docs/civic.md) is
// handed MonthPassed, then its first landmark's building leaves and
// comes back (occupant removed and inserted, then the lot's
// development message 0xE8BDA2E7 with state 3), and its slots 5 to 7
// are called: the excluded check for the first status's key, the first
// status set to itself and copied to a new key (0x0F0F0F0F).
void DriveCivic(cISC4City* city)
{
    char* const civic = reinterpret_cast<char*>(city->GetCivicBuildingSimulator());
    if (!civic)
        return;
    Deliver(civic, 0x66956816, nullptr, 0, 0);
    void** const landmarks = *reinterpret_cast<void***>(civic + 0x44);
    void* const occupant = landmarks[0] != landmarks ? static_cast<void**>(landmarks[0])[2] : nullptr;
    void* const lot = occupant ? Virtual<void*>(city->GetLotManager(), 0x38, occupant) : nullptr;
    if (occupant && lot) {
        Virtual<uint32_t>(occupant, 0x4);
        Virtual<uint32_t>(lot, 0x4);
        Deliver(civic, 0x99EF1143, occupant, 0, 0);
        Deliver(civic, 0x99EF1142, occupant, 0, 0);
        using NewFn = void*(__cdecl*)(std::size_t size);
        using MessageFn = cIGZMessage2Standard*(__thiscall*)(void* memory);
        using DoMessageFn = bool(__thiscall*)(void* target, cIGZMessage2* message);
        void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
        if (cIGZMessage2Standard* const message =
                memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr) {
            message->AddRef();
            message->SetType(0xE8BDA2E7);
            message->SetIGZUnknown(static_cast<cIGZUnknown*>(lot));
            message->SetData2(3);
            void* const target = civic + 0x4;
            reinterpret_cast<DoMessageFn>((*static_cast<void***>(target))[3])(target, message);
            message->Release();
        }
        Virtual<uint32_t>(lot, 0x8);
        Virtual<uint32_t>(occupant, 0x8);
    }
    uint8_t excluded = 0;
    char** const buckets = *reinterpret_cast<char***>(civic + 0x30);
    const auto count = static_cast<uint32_t>(*reinterpret_cast<char***>(civic + 0x34) - buckets);
    char* first = nullptr;
    for (uint32_t i = 0; i < count && !first; ++i)
        first = buckets[i];
    if (first) {
        const uint32_t key = *reinterpret_cast<uint32_t*>(first + 4);
        excluded = Virtual<uint8_t>(civic, 0x14, key);
        if (void* const status = Virtual<void*>(civic, 0x18, key)) {
            Virtual<uint32_t>(civic, 0x1C, key, status);
            Virtual<uint32_t>(civic, 0x1C, 0x0F0F0F0Fu, status);
        }
    }
    log::Write("autotest: civic driven (landmark %p, lot %p, excluded %u, status %p)", occupant, lot, excluded,
               static_cast<void*>(first));
}

void Drive(const Day& day)
{
    if (OnRunDay(day, "OPENSC4_TEST_CIVIC") && game::IsBuild641())
        DriveCivic(day.city);
}

const FingerprintPart kFingerprint{"civic", 130, &Fingerprint};
const DayHook kDrive{"civic.drive", 120, &Drive};

} // namespace
} // namespace opensc4::autotest
