// Settings: ini values, environment overrides, booleans and fallbacks.
#include "core/settings.h"

#include <windows.h>

#include <cstdio>

using namespace opensc4;

namespace {

int Fail(const char* what)
{
    std::printf("settings test FAILED: %s\n", what);
    return 1;
}

} // namespace

int main()
{
    wchar_t ini[MAX_PATH];
    GetTempPathW(MAX_PATH, ini);
    lstrcatW(ini, L"opensc4-settings-test.ini");
    DeleteFileW(ini);
    WritePrivateProfileStringW(L"test", L"name", L"  value with spaces  ", ini);
    WritePrivateProfileStringW(L"test", L"flag", L"Off", ini);
    WritePrivateProfileStringW(L"test", L"odd", L"sometimes", ini);
    WritePrivateProfileStringW(L"test", L"empty", L"", ini);
    WritePrivateProfileStringW(L"test", L"commented", L"1.5   ; a note; with more", ini);
    WritePrivateProfileStringW(L"test", L"semicolon", L"a;b", ini);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, ini); // flush

    if (settings::Get("test", "name", "x", ini) != "value with spaces")
        return Fail("values are read and trimmed");
    if (settings::Get("test", "missing", "fallback", ini) != "fallback")
        return Fail("a missing key gives the fallback");
    if (settings::Get("other", "name", "fallback", ini) != "fallback")
        return Fail("keys belong to their section");
    if (settings::Get("test", "empty", "fallback", ini) != "")
        return Fail("an empty value is a value");
    if (settings::GetBool("test", "flag", true, ini))
        return Fail("Off is false");
    if (settings::Get("test", "commented", "x", ini) != "1.5")
        return Fail("a trailing ; comment is dropped");
    if (settings::Get("test", "semicolon", "x", ini) != "a;b")
        return Fail("a ; inside a value is kept");
    if (!settings::GetBool("test", "odd", true, ini) || settings::GetBool("test", "odd", false, ini))
        return Fail("an unknown boolean gives the fallback");

    SetEnvironmentVariableA("OPENSC4_TEST_FLAG", "yes");
    if (!settings::GetBool("test", "flag", false, ini))
        return Fail("the environment overrides the file");
    SetEnvironmentVariableA("OPENSC4_TEST_FLAG", nullptr);
    if (settings::GetBool("test", "flag", true, ini))
        return Fail("without the variable the file applies again");

    DeleteFileW(ini);
    std::printf("settings test OK\n");
    return 0;
}
