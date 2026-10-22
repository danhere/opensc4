// The script variables' part of the autotest (src/core/autotest_hooks.h;
// docs/lua.md): the advisors publish some 295 values (`g_population` and
// the rest) into the `game` table of the advisor system's script context
// every time they run. The fingerprint reads each back through the context
// (cISCLua at 0x00B43CE8: GetGlobal, PushString, GetTable, Type, ToNumber or
// ToString, SetTop) and hashes them;
// OPENSC4_TEST_LUA_DUMP=1 also logs every value each day.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace opensc4::autotest {
namespace {

const char* const kNames[] = {
#include "core/lua_global_names.h"
};

struct Fnv {
    uint32_t h = 2166136261u;
    void Add(const void* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    }
};

// The hash of the last test script's result (0 before one ran).
uint32_t g_scriptResult = 0;

constexpr int kNil = 0;
constexpr int kNumber = 3;
constexpr int kString = 4;

void Fingerprint(cISC4City*, std::string& line)
{
    void* const script = game::IsBuild641() ? Global<void*>(addr::SC4_ScriptContext) : nullptr;
    if (!script || !Global<void*>(addr::cSC4AdvisorSystem_Instance))
        return;
    const bool dump = EnvInt("OPENSC4_TEST_LUA_DUMP", 0) != 0;
    const int32_t top = Virtual<int32_t>(script, 0x18);
    Fnv all;
    int numbers = 0;
    int strings = 0;
    int others = 0;
    for (std::size_t i = 0; i < sizeof kNames / sizeof kNames[0]; ++i) {
        // The game publishes these two from stack slots it never sets
        // (docs/lua.md): logged, not hashed.
        const bool garbage = !std::strcmp(kNames[i], "g_num_treated_patients") || !std::strcmp(kNames[i], "g_health_capacity");
        Virtual<void>(script, 0x94, "game");
        Virtual<void>(script, 0x84, kNames[i]);
        Virtual<void>(script, 0x98, int32_t{-2});
        const int type = Virtual<int32_t>(script, 0x30, int32_t{-1});
        all.Add(&type, sizeof type);
        if (type == kNumber) {
            const double value = Virtual<double>(script, 0x5C, int32_t{-1});
            if (!garbage)
                all.Add(&value, sizeof value);
            ++numbers;
            if (dump)
                log::Write("lua: %s = %.17g", kNames[i], value);
        } else if (type == kString) {
            const char* const text = Virtual<const char*>(script, 0x60, int32_t{-1});
            all.Add(text, std::strlen(text));
            ++strings;
            if (dump)
                log::Write("lua: %s = \"%s\"", kNames[i], text);
        } else if (type != kNil) {
            ++others;
            if (dump)
                log::Write("lua: %s is of type %d", kNames[i], type);
        }
        Virtual<void>(script, 0x1C, top);
        if (kNames[i][0] == 'l') {
            // A value with a place has the place under "<name>_subject":
            // only whether it is there goes into the hash (it is an object).
            char key[96];
            std::snprintf(key, sizeof key, "%s_subject", kNames[i]);
            Virtual<void>(script, 0x94, "game");
            Virtual<void>(script, 0x84, static_cast<const char*>(key));
            Virtual<void>(script, 0x98, int32_t{-2});
            const int subject = Virtual<int32_t>(script, 0x30, int32_t{-1});
            all.Add(&subject, sizeof subject);
            if (dump)
                log::Write("lua: %s is of type %d", key, subject);
            Virtual<void>(script, 0x1C, top);
        }
    }
    char text[96];
    std::snprintf(text, sizeof text, " | lua %d %d %d %08X script %08X", numbers, strings, others, all.h, g_scriptResult);
    line += text;
}

// The advice OPENSC4_TEST_LUA_ADVICE makes current: over the advisors in ID
// order and their advice records in order, the first advice that has a
// subject (+0xAC), else the first advice.
char* TestAdvice(void* system)
{
    char* found = nullptr;
    char* const header = At<char*>(system, 0x4C);
    for (char* node = At<char*>(header, 0x8); node != header;) {
        char* const advisor = At<char*>(node, 0x14) - 0x8; // the system holds the interface at +8
        char* const first = At<char*>(advisor, 0x84);
        char* const last = At<char*>(advisor, 0x88);
        const bool sane = first <= last && (last - first) % 0x10 == 0 && last - first < 0x10000;
        for (char* record = first; sane && record != last; record += 0x10) {
            char* const advice = At<char*>(record, 0x0);
            if (!advice)
                continue;
            if (At<void*>(advice, 0xAC))
                return advice;
            if (!found)
                found = advice;
        }
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
    return found;
}

// OPENSC4_TEST_LUA_SCRIPT=<file>: on run day OPENSC4_TEST_LUA_SCRIPT_DAY
// (default 1) the file is run as a chunk in the advisors' script context
// (DoString), and the string it leaves in the global `_test_result` is
// logged, in pieces. The chunk calls the game's native script functions,
// so their results can be compared between the original and ours. With
// OPENSC4_TEST_LUA_ADVICE=1 an advice is the advisor system's context advice
// while the chunk runs (PushContextAdvice, PopContextAdvice), as it is while
// the game runs an advice's trigger.
void RunScript(const Day& day)
{
    const char* const path = std::getenv("OPENSC4_TEST_LUA_SCRIPT");
    if (!path || !*path || !day.counted || day.day != day.first + EnvInt("OPENSC4_TEST_LUA_SCRIPT_DAY", 1) ||
        !game::IsBuild641())
        return;
    void* const script = Global<void*>(addr::SC4_ScriptContext);
    std::FILE* const file = std::fopen(path, "rb");
    if (!script || !file) {
        log::Write("autotest: lua script %s not run (context %p)", path, script);
        if (file)
            std::fclose(file);
        return;
    }
    std::string chunk;
    char buffer[4096];
    for (std::size_t got; (got = std::fread(buffer, 1, sizeof buffer, file)) > 0;)
        chunk.append(buffer, got);
    std::fclose(file);
    void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    char* const advice = system && EnvInt("OPENSC4_TEST_LUA_ADVICE", 0) ? TestAdvice(system) : nullptr;
    if (advice) {
        log::Write("autotest: lua script runs with advice %08X as the context advice (subject %s)",
                   At<uint32_t>(advice, 0x8C), At<void*>(advice, 0xAC) ? "yes" : "no");
        Virtual<void>(system, 0x48, advice);
    }
    const int32_t top = Virtual<int32_t>(script, 0x18);
    Virtual<void>(script, 0xD8, chunk.c_str());
    if (advice)
        Virtual<void*>(system, 0x4C);
    Virtual<void>(script, 0x1C, top);
    Virtual<void>(script, 0x94, "_test_result");
    if (Virtual<int32_t>(script, 0x30, int32_t{-1}) == kString) {
        const std::string result = Virtual<const char*>(script, 0x60, int32_t{-1});
        Fnv hash;
        hash.Add(result.data(), result.size());
        g_scriptResult = hash.h;
        log::Write("autotest: lua script result, %u bytes", static_cast<unsigned>(result.size()));
        for (std::size_t at = 0; at < result.size(); at += 600)
            log::Write("luaresult: %s", result.substr(at, 600).c_str());
    } else {
        log::Write("autotest: lua script left no _test_result");
    }
    Virtual<void>(script, 0x1C, top);
}

const FingerprintPart kFingerprint{"lua", 95, &Fingerprint};
const DayHook kRunScript{"lua.script", 96, &RunScript};

} // namespace
} // namespace opensc4::autotest
