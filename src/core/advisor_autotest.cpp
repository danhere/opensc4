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

void AddBytes(Fnv& f, const char* first, const char* last)
{
    if (first && first <= last && last - first < 0x10000)
        for (const char* at = first; at != last; ++at)
            f.Add(*at);
}

// OPENSC4_TEST_MISSIONS=1: the answers of a mission advice's getters (the
// city situation advice, docs/lua.md; `mission` is the object, the advice is
// at +4). No test starts a mission, so this is how the getters are compared.
// The text getters return a static string object (text from +4 to +8).
void AddMission(Fnv& f, char* mission, char* advice)
{
    for (std::size_t slot : {std::size_t{0x10}, std::size_t{0x64}, std::size_t{0x68}, std::size_t{0x6C},
                             std::size_t{0x70}, std::size_t{0x74}, std::size_t{0x8C}})
        f.Add(Virtual<uint32_t>(mission, slot));
    for (std::size_t slot : {std::size_t{0x18}, std::size_t{0x1C}, std::size_t{0x20}, std::size_t{0x24},
                             std::size_t{0x28}, std::size_t{0x2C}, std::size_t{0x50}, std::size_t{0x54},
                             std::size_t{0xD0}})
        f.Add(Virtual<double>(mission, slot));
    for (uint32_t flag = 1; flag <= 0x80; flag <<= 1)
        f.Add(static_cast<uint8_t>(Virtual<uint32_t>(mission, 0x3C, flag) != 0));
    f.Add(Virtual<uint8_t>(mission, 0x44));
    f.Add(Virtual<uint8_t>(mission, 0x4C));
    for (std::size_t slot : {std::size_t{0x30}, std::size_t{0x34}, std::size_t{0x40}}) {
        char* const vector = Virtual<char*>(mission, slot);
        AddBytes(f, At<char*>(vector, 0x0), At<char*>(vector, 0x4));
    }
    char* const rect = Virtual<char*>(mission, 0xA0);
    AddBytes(f, rect, rect + 8);
    uint32_t percent = 0;
    Virtual<uint32_t*>(mission, 0xB4, &percent);
    f.Add(percent);
    for (std::size_t slot : {std::size_t{0xB8}, std::size_t{0xC4}}) {
        uint32_t radius = 0;
        uint32_t magnitude = 0;
        f.Add(static_cast<uint8_t>(Virtual<uint32_t>(mission, slot, &radius, &magnitude) != 0));
        f.Add(radius);
        f.Add(magnitude);
    }
    for (std::size_t slot : {std::size_t{0xC0}, std::size_t{0xCC}}) {
        int32_t money[2] = {0, 0};
        f.Add(static_cast<uint8_t>(Virtual<uint32_t>(mission, slot, &money[0]) != 0));
        f.Add(money[0]);
        f.Add(money[1]);
    }
    for (std::size_t slot : {std::size_t{0x58}, std::size_t{0x5C}, std::size_t{0x84}, std::size_t{0x88}}) {
        char* const text = Virtual<char*>(mission, slot);
        AddBytes(f, At<char*>(text, 0x4), At<char*>(text, 0x8));
    }
    for (uint32_t index = 1; index <= 2; ++index) {
        char* const text = Virtual<char*>(mission, 0x60, index);
        AddBytes(f, At<char*>(text, 0x4), At<char*>(text, 0x8));
    }
    for (std::size_t slot : {std::size_t{0x48}, std::size_t{0x50}}) { // the advice's title and message
        char* const text = Virtual<char*>(advice, slot);
        AddBytes(f, At<char*>(text, 0x4), At<char*>(text, 0x8));
    }
}

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
    Fnv missions;
    int missionCount = 0;
    const bool withMissions = EnvInt("OPENSC4_TEST_MISSIONS", 0) != 0;
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
                if (withMissions && At<std::uintptr_t>(advice - 4, 0) == addr::SC4SituationAdvice_Vtable) {
                    Fnv mission;
                    AddMission(mission, advice - 4, advice);
                    if (EnvInt("OPENSC4_TEST_ADVISOR_DUMP", 0))
                        log::Write("mission: %08X hash %08X", At<uint32_t>(advice, 0x8C), mission.h);
                    missions.Add(mission.h);
                    ++missionCount;
                }
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
    if (withMissions) {
        std::snprintf(more, sizeof more, " missions %d %08X", missionCount, missions.h);
        line += more;
    }
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

// --- OPENSC4_TEST_ADVICE_WINDOW (docs/ui.md) ---------------------------------

// A stand-in for the advice window's list control while FillList runs: it
// hashes each row the controller hands over (three image strings, the
// message and the title as HTML, the flag byte, the advice).
struct WindowString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
};

Fnv g_rows;
int g_rowCount = 0;

void AddString(Fnv& f, const WindowString* text)
{
    if (text)
        AddBytes(f, text->begin, text->end);
    f.Add(uint8_t{0});
}

void __fastcall FakeAddRow(void*, void*, const WindowString* image1, const WindowString* image2,
                           const WindowString* message, const WindowString* title, uint32_t flag, char* advice,
                           const WindowString* image3, uint32_t zero, uint32_t style)
{
    for (const WindowString* text : {image1, image2, message, title, image3})
        AddString(g_rows, text);
    g_rows.Add(static_cast<uint8_t>(flag));
    g_rows.Add(advice ? At<uint32_t>(advice, 0x8C) : 0u);
    g_rows.Add(zero);
    g_rows.Add(style);
    ++g_rowCount;
}

void __fastcall FakeOneArgument(void*, void*, uint32_t value)
{
    g_rows.Add(value);
}

void __fastcall FakeClear(void*, void*)
{
    g_rows.Add(uint8_t{0xC1});
}

char g_windowToken[160];

void AddControllerState(Fnv& f, char* helper)
{
    f.Add(At<void*>(helper, 0xC) != nullptr);
    f.Add(At<int32_t>(helper, 0x10));
    f.Add(At<int32_t>(helper, 0x14));
    f.Add(At<uint8_t>(helper, 0x55));
    f.Add(At<uint8_t>(helper, 0x56));
    f.Add(At<int32_t>(helper, 0x58));
    f.Add(At<void*>(helper, 0x5C) != nullptr);
    for (std::size_t pane = 0; pane < 7; ++pane)
        f.Add(At<void*>(helper, 0x38 + 4 * pane) != nullptr);
    char* const end = At<char*>(helper, 0x60);
    int advices = 0;
    for (char* node = At<char*>(end, 0x0); node != end && advices < 10000; node = At<char*>(node, 0x0), ++advices)
        f.Add(At<uint32_t>(At<char*>(node, 0x8), 0x8C));
    f.Add(advices);
    f.Add(At<int32_t>(helper, 0x68));
    if (EnvInt("OPENSC4_TEST_ADVISOR_DUMP", 0)) {
        unsigned panes = 0;
        for (std::size_t pane = 0; pane < 7; ++pane)
            panes = panes * 2 + (At<void*>(helper, 0x38 + 4 * pane) != nullptr);
        log::Write("advwin: advisor %d type %d index %d news %u shown %u mode %d list %d panes %02X advices %d tree %d "
                   "hash %08X",
                   At<void*>(helper, 0xC) != nullptr, At<int32_t>(helper, 0x10), At<int32_t>(helper, 0x14),
                   At<uint8_t>(helper, 0x55), At<uint8_t>(helper, 0x56), At<int32_t>(helper, 0x58),
                   At<void*>(helper, 0x5C) != nullptr, panes, advices, At<int32_t>(helper, 0x68), f.h);
    }
}

// On that run day the advice window's code is called directly, as the
// window would call it: for each of the seven advisors the worst mood, the
// advice list and the list's rows (through the stand-in above); the
// advisor's head view made, given each mood, its models loaded, advanced
// and dropped, with no window; then the controller driven through its
// screens (an advisor's advice, its news, the overview) and hidden again.
// The result is one fingerprint token from then on.
void DriveAdviceWindow(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_ADVICE_WINDOW") || !game::IsBuild641())
        return;
    char* const system = Global<char*>(addr::cSC4AdvisorSystem_Instance);
    char* const helper = system ? At<char*>(system, 0x5C) : nullptr;
    if (!helper) {
        log::Write("autotest: advice window: no controller");
        return;
    }
    using ThisFn = uint32_t(__thiscall*)(void*);
    using ThisArgFn = uint32_t(__thiscall*)(void*, void*);
    using ThisTypeFn = uint32_t(__thiscall*)(void*, uint32_t);
    const bool window = At<void*>(helper, 0x18) != nullptr;
    static void* fakeVtable[8] = {nullptr,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  reinterpret_cast<void*>(&FakeAddRow),
                                  reinterpret_cast<void*>(&FakeOneArgument),
                                  reinterpret_cast<void*>(&FakeOneArgument),
                                  reinterpret_cast<void*>(&FakeClear)};
    static void* fakeList = fakeVtable;
    const uint32_t types[] = {0x19, 0x15, 0x16, 0x17, 0x18, 0x1A, 0x1B};

    Fnv lists;
    g_rows = Fnv{};
    g_rowCount = 0;
    for (const uint32_t type : types) {
        void* const advisor = Virtual<void*>(system, 0x1C, type);
        if (!advisor)
            continue;
        lists.Add(reinterpret_cast<ThisArgFn>(addr::SC4UI_F41DC20)(helper, advisor)); // the worst mood
        reinterpret_cast<ThisArgFn>(addr::SC4UI_F41E000)(helper, advisor);           // the advice list
        AddControllerState(lists, helper);
        if (window) {
            void* const list = At<void*>(helper, 0x5C);
            At<void*>(helper, 0x5C) = &fakeList;
            reinterpret_cast<ThisFn>(addr::SC4UI_F41E4B0)(helper); // the rows
            At<void*>(helper, 0x5C) = list;
        }
    }

    Fnv heads;
    for (const uint32_t type : types) {
        char* const memory = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0xF8));
        if (!memory)
            continue;
        char* const head = reinterpret_cast<char*(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F420B70)(memory, type);
        for (uint32_t mood = 0; mood <= 5; ++mood) {
            Virtual<uint32_t>(head, 0x24, mood);       // the mood
            Virtual<uint32_t>(head, 0x2C, uint32_t{1}); // the models
            heads.Add(At<int32_t>(head, 0x10));
            heads.Add(At<uint32_t>(head, 0x14));
            heads.Add(At<uint32_t>(head, 0x1C));
            for (std::size_t model : {std::size_t{0x18}, std::size_t{0x20}, std::size_t{0x24}})
                heads.Add(At<void*>(head, model) != nullptr);
            for (int step = 0; step < 4; ++step) {
                Virtual<uint32_t>(head, 0x34, uint32_t{700}); // advance
                heads.Add(At<uint32_t>(head, 0xC8));
                heads.Add(At<uint32_t>(head, 0xCC));
                heads.Add(At<uint32_t>(head, 0xD0));
                heads.Add(At<void*>(head, 0x24) == At<void*>(head, 0x18));
            }
            Virtual<void>(head, 0x28, 10.0f, 20.0f, 0.0f, 1.5f); // place
        }
        Virtual<uint32_t>(head, 0x2C, uint32_t{0});
        Virtual<void*>(head, 0x14, uint32_t{1}); // delete
    }

    Fnv screens;
    if (window) {
        // Show's result is whatever its last call left behind (a reference
        // count), so it is not compared.
        reinterpret_cast<ThisFn>(addr::SC4AdvisorHelper_Call1)(helper);
        AddControllerState(screens, helper);
        for (const uint32_t type : types) {
            reinterpret_cast<ThisTypeFn>(addr::SC4UI_F41F2D0)(helper, type); // an advisor's screen
            AddControllerState(screens, helper);
            reinterpret_cast<ThisFn>(addr::SC4UI_F41F610)(helper); // its news
            AddControllerState(screens, helper);
            reinterpret_cast<ThisFn>(addr::SC4UI_F41F720)(helper); // its advice
            AddControllerState(screens, helper);
        }
        reinterpret_cast<ThisFn>(addr::SC4UI_F41E1C0)(helper); // the overview
        AddControllerState(screens, helper);
        reinterpret_cast<ThisFn>(addr::SC4AdvisorHelper_Call2)(helper); // hide
        AddControllerState(screens, helper);
    }
    std::snprintf(g_windowToken, sizeof g_windowToken, " advwin %d %08X rows %d %08X heads %08X screens %08X",
                  window ? 1 : 0, lists.h, g_rowCount, g_rows.h, heads.h, screens.h);
    log::Write("autotest: advice window driven:%s", g_windowToken);
}

void WindowFingerprint(cISC4City*, std::string& line)
{
    line += g_windowToken;
}

const FingerprintPart kWindowFingerprint{"advisors.window", 87, &WindowFingerprint};
const DayHook kDriveWindow{"advisors.window", 88, &DriveAdviceWindow};
const FingerprintPart kFingerprint{"advisors", 86, &Fingerprint};
const DayHook kDrive{"advisors.drive", 86, &Drive};
const DayHook kDriveCheats{"advisors.cheats", 87, &DriveCheats};

} // namespace
} // namespace opensc4::autotest
