// Crash handling (docs/architecture.md, "ABI rules"):
//   - a first-chance (vectored) handler that logs fatal exceptions raised in
//     OpenSC4's own code and writes a minidump next to opensc4.log, before
//     SimCity 4's own __except handler writes its report and ends the process;
//   - a top-level exception filter doing the same for crashes nothing else
//     catches, chaining to any previous filter;
//   - a guard for hook entry points that names the hook in the log when an
//     exception passes through it, without catching it.
#pragma once

#include <windows.h>

namespace opensc4::crash {

// Installs the top-level filter, chaining to the current one. In the game,
// call it after the game has installed its own (PostAppInit).
void InstallHandler();

// Exception filter for hook guards: logs that the exception passed through
// `hook`, then lets it continue to the top-level filter. Use it as
//
//   __try { return Body(args...); }
//   __except (crash::HookFilter("cSC4App::LoadCity", GetExceptionInformation())) { return {}; }
//
// in a function that has no objects with destructors (an SEH requirement); the
// __except block never runs.
//
// SEH rule (clang-cl, 32-bit x86): __try reliably covers faults raised in
// functions called from the block, not faults raised by instructions directly
// inside it, even with /EHa. Keep anything that may fault in a separate,
// non-inlined function (tests/seh_test.cpp). Prefer not faulting at all, for
// example checking memory with VirtualQuery before reading it.
int HookFilter(const char* hook, EXCEPTION_POINTERS* exception);

} // namespace opensc4::crash
