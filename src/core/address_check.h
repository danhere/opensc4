// Checks the address database against the running game: the startup manifest
// of docs/architecture.md ("which third-party DLLs patched which bytes").
#pragma once

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace opensc4::addr_check {

// The database entry at exactly `address`, or null.
const addr::Entry* Find(std::uintptr_t address);

// Empty if the bytes at `address` match `expect`; otherwise a description of
// what is there instead, naming the module a JMP/CALL patch leads into.
std::string DescribeMismatch(std::uintptr_t address, const unsigned char* expect, std::size_t size);

// Checks every entry with expected bytes and logs one line per mismatch plus a
// summary. Returns the number of mismatches. Call it after all plugins have
// loaded (their patches are applied by then).
int LogManifest();

// Logs every loaded module outside the Windows folder: the game, its own DLLs,
// plugin DLLs and injectors (ReShade, ...). For bug reports.
void LogModules();

} // namespace opensc4::addr_check
