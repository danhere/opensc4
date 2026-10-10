// The advisor system's part of the autotest fingerprint (src/core/autotest_hooks.h;
// docs/advisors.md): its own fields, the advisor IDs, the events advisors
// subscribed to, the context advice stack and the data tags. Read only:
// fields, never methods (an advice's getters may run script).
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

struct Fnv {
    uint32_t h = 2166136261u;
    template <typename T>
    void Add(const T& value)
    {
        for (std::size_t i = 0; i < sizeof value; ++i)
            h = (h ^ reinterpret_cast<const uint8_t*>(&value)[i]) * 16777619u;
    }
};

// The keys of a std::map's nodes (key at +0x10), in order.
void MapKeys(Fnv& f, char* header)
{
    char* node = At<char*>(header, 0x8);
    while (node != header) {
        f.Add(At<int32_t>(node, 0x10));
        if (char* right = At<char*>(node, 0xC)) {
            while (At<char*>(right, 0x8))
                right = At<char*>(right, 0x8);
            node = right;
        } else {
            char* parent = At<char*>(node, 0x4);
            while (node == At<char*>(parent, 0xC)) {
                node = parent;
                parent = At<char*>(parent, 0x4);
            }
            if (At<char*>(node, 0xC) != parent)
                node = parent;
        }
    }
}

void Fingerprint(cISC4City*, std::string& line)
{
    char text[96] = " | adv none";
    char* const system = game::IsBuild641() ? Global<char*>(addr::cSC4AdvisorSystem_Instance) : nullptr;
    if (system) {
        Fnv own;
        for (std::size_t at = 0x1C; at <= 0x24; at += 4)
            own.Add(At<int32_t>(system, at)); // popup thresholds
        own.Add(At<uint8_t>(system, 0x28));
        own.Add(At<uint8_t>(system, 0x29));
        own.Add(At<uint32_t>(system, 0x34)); // generator state
        own.Add(At<uint32_t>(system, 0x38));
        own.Add(At<int32_t>(system, 0x48)); // last date
        own.Add(At<uint8_t>(system, 0x60)); // +0x64 is a service pointer
        Fnv ids;
        MapKeys(ids, At<char*>(system, 0x4C));
        Fnv events;
        MapKeys(events, At<char*>(system, 0x68));
        Fnv context;
        char** const first = At<char**>(system, 0x74);
        char** const last = At<char**>(system, 0x78);
        for (char** at = first; at != last; ++at) {
            context.Add(At<uint32_t>(*at, 0x8C)); // GUID
            context.Add(At<int32_t>(*at, 0xB0));  // session
            context.Add(At<int32_t>(*at, 0x70));  // flags
        }
        Fnv tags;
        for (int32_t* at = At<int32_t*>(system, 0x80); at != At<int32_t*>(system, 0x84); ++at)
            tags.Add(*at);
        std::snprintf(text, sizeof text, " | adv %u %u %08X %08X %08X %d %08X %08X", At<uint32_t>(system, 0x50),
                      At<uint32_t>(system, 0x6C), own.h, ids.h, events.h, static_cast<int32_t>(last - first),
                      context.h, tags.h);
    }
    line += text;
    if (!system)
        return;
    // Each advisor (docs/lua.md): its timers and counters, and of each of
    // its advice records (0x10 bytes: the advice, then three numbers) the
    // numbers. `OPENSC4_TEST_ADVISOR_DUMP=1` logs one line per advisor.
    Fnv advisors;
    int count = 0;
    int records = 0;
    char* const header = At<char*>(system, 0x4C);
    for (char* node = At<char*>(header, 0x8); node != header;) {
        char* const advisor = At<char*>(node, 0x14) - 0x8; // the system holds the interface at +8
        Fnv one;
        one.Add(At<int32_t>(node, 0x10));
        // +0x58 a stamp, +0x5C a time, +0x60 the script's id, +0x64, +0x68 the
        // advice type, +0x70 triggers, +0x7C active advices.
        for (std::size_t at : {std::size_t{0x58}, std::size_t{0x5C}, std::size_t{0x60}, std::size_t{0x64}, std::size_t{0x68},
                               std::size_t{0x70}, std::size_t{0x7C}})
            one.Add(At<int32_t>(advisor, at));
        one.Add(At<uint8_t>(advisor, 0xB4));
        int own = 0;
        char* const first = At<char*>(advisor, 0x84);
        char* const last = At<char*>(advisor, 0x88);
        const bool sane = first <= last && (last - first) % 0x10 == 0 && last - first < 0x10000;
        for (char* record = first; sane && record != last; record += 0x10) {
            if (char* const advice = At<char*>(record, 0x0)) {
                one.Add(At<uint32_t>(advice, 0x8C)); // GUID
                one.Add(At<int32_t>(advice, 0x70));  // flags
            }
            one.Add(At<int32_t>(record, 0x4));
            one.Add(At<int32_t>(record, 0x8));
            one.Add(At<int32_t>(record, 0xC));
            ++own;
        }
        if (EnvInt("OPENSC4_TEST_ADVISOR_DUMP", 0))
            log::Write("advisor: %08X stamp %d time %d cursor %d type %d triggers %d active %d records %d hash %08X",
                       At<uint32_t>(node, 0x10), At<int32_t>(advisor, 0x58), At<int32_t>(advisor, 0x5C),
                       At<int32_t>(advisor, 0x64), At<int32_t>(advisor, 0x68), At<int32_t>(advisor, 0x70),
                       At<int32_t>(advisor, 0x7C), own, one.h);
        advisors.Add(one.h);
        ++count;
        records += own;
        if (char* right = At<char*>(node, 0xC)) {
            while (At<char*>(right, 0x8))
                right = At<char*>(right, 0x8);
            node = right;
        } else {
            char* parent = At<char*>(node, 0x4);
            while (node == At<char*>(parent, 0xC)) {
                node = parent;
                parent = At<char*>(parent, 0x4);
            }
            if (At<char*>(node, 0xC) != parent)
                node = parent;
        }
    }
    char more[64];
    std::snprintf(more, sizeof more, " advisors %d %d %08X", count, records, advisors.h);
    line += more;
}

// OPENSC4_TEST_ADVISORS=<day>: on that run day every advisor is removed
// and created again from the script (the new-city path; a loaded city
// skips it), and the scripts are marked for a reload, which the next run
// message does (reload, then reconcile).
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_ADVISORS") || !game::IsBuild641())
        return;
    char* const system = Global<char*>(addr::cSC4AdvisorSystem_Instance);
    if (!system)
        return;
    using ThisFn = void(__thiscall*)(void*);
    reinterpret_cast<ThisFn>(addr::cSC4AdvisorSystem_RemoveAll)(system);
    reinterpret_cast<ThisFn>(addr::cSC4AdvisorSystem_CreateAll)(system);
    At<uint8_t>(system, 0x60) = 1;
    log::Write("autotest: advisors removed, created again, reload pending");
}


struct GameString { // SC4String
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

GameString* NewText(const char* text)
{
    static GameString strings[16];
    static int used = 0;
    GameString* const string = &strings[used++ % 16];
    *string = {};
    reinterpret_cast<void*(__thiscall*)(GameString*, const char*)>(addr::SC4String_FromText)(string, text);
    return string;
}

// A cheat message as the cheat manager sends it: data1 the cheat's ID,
// void2 its text; handed to the advisor system's DoMessage directly.
void SendCheat(char* system, uint32_t id, const char* text)
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::RZ_operator_new)(0x2C);
    void* const message = reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory);
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, uint32_t{0x230E27AC});
    Virtual<void>(message, 0x2C, id);
    Virtual<void>(message, 0x44, static_cast<void*>(NewText(text)));
    Virtual<uint8_t>(system + 0x4, 0xC, message);
    Virtual<uint32_t>(message, 0x8);
}

// OPENSC4_TEST_ADVISOR_CHEATS=<day>: on that run day the DollyLlama and Adv
// cheats (in the option spellings the tokenizer might take) and a script
// link (DoLink 0x1D) go to the advisor system.
void DriveCheats(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_ADVISOR_CHEATS") || !game::IsBuild641())
        return;
    char* const system = Global<char*>(addr::cSC4AdvisorSystem_Instance);
    if (!system)
        return;
    SendCheat(system, 0xAAB7F42A, "DollyLlama");
    SendCheat(system, 0xAAB7F42A, "DollyLlama");
    const char* const adv[] = {"Adv guids on",  "Adv -guids:on",  "Adv guids=on", "Adv -guids:off", "Adv -guids:maybe",
                               "Adv -trigger:3", "Adv trigger 3", "Adv -print:3",  "Adv -refresh",   "Adv refresh",
                               "Adv -reload",    "Adv reload"};
    for (const char* text : adv)
        SendCheat(system, 0xA919D25E, text);
    Virtual<uint8_t>(system, 0x2C, static_cast<void*>(NewText("0000001d00000000g_opensc4_link = 1")));
    log::Write("autotest: advisor cheats and a script link sent");
}

const FingerprintPart kFingerprint{"advisors", 86, &Fingerprint};
const DayHook kDrive{"advisors.drive", 86, &Drive};
const DayHook kDriveCheats{"advisors.cheats", 87, &DriveCheats};

} // namespace
} // namespace opensc4::autotest
