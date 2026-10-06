// Replacement registry and compare-mode tests.
#include "core/compare.h"
#include "core/registry.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

using opensc4::registry::Kind;
using opensc4::registry::Mode;
using opensc4::registry::Phase;
namespace registry = opensc4::registry;

int g_appliedA = 0, g_appliedB = 0, g_appliedC = 0, g_appliedLate = 0;
Mode g_modeB = Mode::Original;

bool ApplyA(Mode) { ++g_appliedA; return true; }
bool ApplyB(Mode mode) { ++g_appliedB; g_modeB = mode; return true; }
bool ApplyC(Mode) { ++g_appliedC; return false; } // e.g. its target was already patched
bool ApplyLate(Mode) { ++g_appliedLate; return true; }

int Fail(const char* what)
{
    std::fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

int Original(int x) { return x * 2; }
int OursCorrect(int x) { return x + x; }
int OursWrong(int x) { return x * 2 + (x == 7 ? 1 : 0); }

} // namespace

int main()
{
    registry::Register({"test.a", "test", Kind::Detour, Phase::DirectorInit, 0x1000, "a", false, ApplyA});
    registry::Register({"test.b", "test", Kind::Detour, Phase::DirectorInit, 0x2000, "b", true, ApplyB});
    registry::Register({"test.c", "test", Kind::Gzcom, Phase::DirectorInit, 0x3000, "c", false, ApplyC});
    registry::Register({"test.d", "test", Kind::Detour, Phase::DirectorInit, 0x4000, "d", false, ApplyA});
    registry::Register({"test.late", "test", Kind::Detour, Phase::PostAppInit, 0x5000, "late", false, ApplyLate});
    if (registry::Count() != 5)
        return Fail("registration count");

    // a: ours from the ini; b: compare from the ini, overridden to ours by the
    // environment; c: ours but apply fails; d: unrecognised mode; e: unknown id.
    wchar_t ini[MAX_PATH];
    GetTempPathW(MAX_PATH, ini);
    lstrcatW(ini, L"opensc4-registry-test.ini");
    WritePrivateProfileStringW(L"replacements", nullptr, nullptr, ini); // clear
    WritePrivateProfileStringW(L"replacements", L"test.a", L"ours   ; with a comment", ini);
    WritePrivateProfileStringW(L"replacements", L"test.b", L"compare", ini);
    WritePrivateProfileStringW(L"replacements", L"test.c", L"ours", ini);
    WritePrivateProfileStringW(L"replacements", L"test.d", L"sometimes", ini);
    WritePrivateProfileStringW(L"replacements", L"test.e", L"ours", ini);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, ini); // flush

    SetEnvironmentVariableW(L"OPENSC4_REPLACE", L"test.b=ours, test.late = ours");
    registry::Apply(Phase::DirectorInit, ini);

    if (g_appliedA != 1 || registry::ModeOf("test.a") != Mode::Ours)
        return Fail("test.a should be applied as ours from the ini");
    if (g_appliedB != 1 || g_modeB != Mode::Ours || registry::ModeOf("test.b") != Mode::Ours)
        return Fail("test.b: the environment should override the ini");
    if (g_appliedC != 1 || registry::ModeOf("test.c") != Mode::Original)
        return Fail("test.c: a failed apply should fall back to original");
    if (registry::ModeOf("test.d") != Mode::Original || g_appliedA != 1)
        return Fail("test.d: an unrecognised mode should mean original, without applying");
    if (g_appliedLate != 0)
        return Fail("test.late belongs to a later phase");

    registry::Apply(Phase::PostAppInit, ini);
    if (g_appliedLate != 1 || registry::ModeOf("test.late") != Mode::Ours)
        return Fail("test.late should be applied in its own phase");

    // Compare is refused for replacements that do not support it.
    registry::ResetForTests();
    SetEnvironmentVariableW(L"OPENSC4_REPLACE", L"test.a=compare,test.b=compare");
    WritePrivateProfileStringW(L"replacements", nullptr, nullptr, ini);
    registry::Apply(Phase::DirectorInit, ini);
    if (registry::ModeOf("test.a") != Mode::Original || g_appliedA != 1)
        return Fail("test.a does not support compare and should stay original");
    if (registry::ModeOf("test.b") != Mode::Compare || g_modeB != Mode::Compare)
        return Fail("test.b supports compare");
    // Subsystem wildcards: "test.*" covers everything; an exact id and a
    // longer wildcard win over it.
    registry::Register({"test.group.x", "test/group", Kind::Detour, Phase::DirectorInit, 0x6000, "x", false, ApplyA});
    registry::Register({"test.group.y", "test/group", Kind::Detour, Phase::DirectorInit, 0x7000, "y", false, ApplyA});
    registry::ResetForTests();
    SetEnvironmentVariableW(L"OPENSC4_REPLACE", L"test.*=ours,test.group.*=original,test.group.y=ours,test.c=original");
    registry::Apply(Phase::DirectorInit, ini);
    if (registry::ModeOf("test.a") != Mode::Ours || registry::ModeOf("test.d") != Mode::Ours)
        return Fail("test.* should switch the whole subsystem to ours");
    if (registry::ModeOf("test.group.x") != Mode::Original)
        return Fail("the longer wildcard test.group.* should win over test.*");
    if (registry::ModeOf("test.group.y") != Mode::Ours)
        return Fail("an exact id should win over wildcards");
    if (registry::ModeOf("test.c") != Mode::Original)
        return Fail("an exact id set to original should win over test.*");
    DeleteFileW(ini);

    // Compare mode: the original's result is returned; divergences are counted.
    namespace compare = opensc4::compare;
    for (int x = 0; x < 10; ++x) {
        if (compare::Call("test.same", [&] { return Original(x); }, [&] { return OursCorrect(x); }) != Original(x))
            return Fail("compare should return the original's result");
        if (compare::Call("test.diverging", [&] { return Original(x); }, [&] { return OursWrong(x); }) != Original(x))
            return Fail("compare should return the original's result even when ours differs");
    }
    if (compare::StatsOf("test.same").calls != 10 || compare::StatsOf("test.same").divergences != 0)
        return Fail("matching implementations should have no divergences");
    if (compare::StatsOf("test.diverging").calls != 10 || compare::StatsOf("test.diverging").divergences != 1)
        return Fail("one input diverges");
    compare::LogStats();

    std::puts("registry test OK");
    return 0;
}
