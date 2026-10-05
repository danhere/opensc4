// Facts about the host process: which SimCity 4 build core.dll is running in.
#pragma once

namespace opensc4::game {

// Byte at 0x6E5000: 0x0F in build 641, or -1 if that address is not readable
// (not running inside SimCity 4.exe). Ecosystem DLLs use the same sentinel
// (docs/patch-census.md, section 1).
int BuildSentinel();

// True only inside SimCity 4.exe 1.1.641. Anything that uses a fixed address
// in the game must check this first.
bool IsBuild641();

} // namespace opensc4::game
