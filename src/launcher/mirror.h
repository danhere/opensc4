// A large-address-aware mirror of the game install, so the game gets a 4 GB
// address space without touching the install or needing admin rights
// (docs/crashes.md, "Out of memory").
#pragma once

#include <string>

namespace opensc4::launcher {

// Makes or refreshes a mirror of the install that `gameExe` (...\Apps\SimCity
// 4.exe) belongs to under `mirrorRoot`: every top-level folder of the install
// but Apps as a directory junction, every top-level file as a hard link (or a
// copy where a link is refused), and an Apps folder of its own holding the
// other Apps files and a copy of the exe with the large-address-aware flag
// set. The install is only read. The mirror is rebuilt when the exe's size or
// time changes (`rebuilt` says whether it was). Returns the mirrored exe's
// path, or empty with `error` set.
std::wstring PrepareLargeAddressMirror(const std::wstring& gameExe, const std::wstring& mirrorRoot, bool* rebuilt,
                                       std::wstring& error);

} // namespace opensc4::launcher
