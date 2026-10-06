#include "core/settings.h"

#include "core/log.h"

#include <windows.h>

#include <algorithm>
#include <cctype>

namespace opensc4::settings {
namespace {

std::string Trim(std::string s)
{
    const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), space));
    s.erase(std::find_if_not(s.rbegin(), s.rend(), space).base(), s.end());
    return s;
}

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

std::string StripComment(const std::string& value)
{
    for (size_t i = 0; i < value.size(); ++i)
        if (value[i] == ';' && (i == 0 || std::isspace(static_cast<unsigned char>(value[i - 1]))))
            return value.substr(0, i);
    return value;
}

std::wstring DefaultIniPath()
{
    wchar_t path[MAX_PATH];
    if (!log::Directory(path))
        return {};
    return std::wstring(path) + L"opensc4.ini";
}

std::string Get(const char* section, const char* key, const char* fallback, const std::wstring& iniPath)
{
    std::string variable = "OPENSC4_" + std::string(section) + "_" + key;
    std::transform(variable.begin(), variable.end(), variable.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    char buffer[512];
    DWORD length = GetEnvironmentVariableA(variable.c_str(), buffer, sizeof(buffer));
    if (length > 0 && length < sizeof(buffer))
        return Trim(buffer);

    const std::wstring ini = iniPath.empty() ? DefaultIniPath() : iniPath;
    if (ini.empty())
        return fallback;
    // ANSI is enough for keys and values; the path may not be.
    wchar_t wide[512];
    const std::wstring wsection(section, section + std::char_traits<char>::length(section));
    const std::wstring wkey(key, key + std::char_traits<char>::length(key));
    length = GetPrivateProfileStringW(wsection.c_str(), wkey.c_str(), L"\x01", wide, 512, ini.c_str());
    if (length == 1 && wide[0] == L'\x01')
        return fallback;
    std::string value;
    for (DWORD i = 0; i < length; ++i)
        value += static_cast<char>(wide[i] < 0x80 ? wide[i] : '?');
    return Trim(StripComment(value));
}

bool GetBool(const char* section, const char* key, bool fallback, const std::wstring& iniPath)
{
    const std::string value = Lower(Get(section, key, "", iniPath));
    if (value == "on" || value == "true" || value == "yes" || value == "1")
        return true;
    if (value == "off" || value == "false" || value == "no" || value == "0")
        return false;
    if (!value.empty())
        log::Write("settings: [%s] %s = \"%s\" is not on or off; using %s", section, key, value.c_str(),
                   fallback ? "on" : "off");
    return fallback;
}

} // namespace opensc4::settings
