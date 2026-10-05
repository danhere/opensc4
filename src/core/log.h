// Minimal logging for core.dll: one line per call, appended to opensc4.log
// next to the DLL. Win32 only, so it is safe under the loader lock.
#pragma once

namespace opensc4::log {

void Write(const char* format, ...);

} // namespace opensc4::log
