// Minimal logging for core.dll: one line per call, appended to opensc4.log
// next to the DLL. Win32 only, so it is safe under the loader lock.
#pragma once

namespace opensc4::log {

void Write(const char* format, ...);

// Folder holding opensc4.log (the folder of the module containing this code),
// with a trailing backslash. Empty on failure.
bool Directory(wchar_t (&path)[260]);

} // namespace opensc4::log
