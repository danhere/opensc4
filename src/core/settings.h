// Plain settings from opensc4.ini next to core.dll (the file the replacement
// registry reads its [replacements] from), e.g.
//
//   [renderer]
//   vsync = on
//
// An environment variable OPENSC4_<SECTION>_<KEY>, upper case, overrides the
// file for one run (OPENSC4_RENDERER_VSYNC=off).
#pragma once

#include <string>

namespace opensc4::settings {

// opensc4.ini in core.dll's folder; empty if that folder is unknown.
std::wstring DefaultIniPath();

// The value of `key` in `section`, trimmed; `fallback` if neither the
// environment nor the file sets it. `iniPath` empty: DefaultIniPath().
std::string Get(const char* section, const char* key, const char* fallback, const std::wstring& iniPath = {});

// on/off, true/false, yes/no or 1/0; `fallback` for anything else.
bool GetBool(const char* section, const char* key, bool fallback, const std::wstring& iniPath = {});

} // namespace opensc4::settings
