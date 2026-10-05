// Coverage meter (docs/architecture.md, "Replacement registry"): which of the
// game's functions run at all. The measure of progress towards exe
// independence, where it must reach zero.
//
// Method: a one-shot INT3 at every function entry from a list exported from
// Ghidra (tools/ghidra/ExportFunctions.java). The first time a function runs,
// a first-chance exception handler records the hit, restores the original
// byte and resumes at the entry, so each function costs one exception once.
// Entries whose first byte differs from the list (patched by another DLL or
// by our own hooks) are skipped.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace opensc4::coverage {

struct Function {
    std::uintptr_t address;
    unsigned char firstByte;
};

// Reads "address,size,first_byte,name" rows (ExportFunctions.java's CSV).
std::vector<Function> LoadFunctionList(const std::wstring& path);

// Plants breakpoints at every listed entry whose first byte matches, and
// starts recording. Returns the number planted. Call once.
std::size_t Start(const std::vector<Function>& functions);

struct Summary {
    std::size_t listed = 0;   // in the function list
    std::size_t planted = 0;  // breakpoints placed (first byte matched)
    std::size_t hit = 0;      // functions that have run since Start
};
Summary GetSummary();

// Writes "address,first_hit_ms" for every function that has run, ordered by
// address. Returns false if the file cannot be written.
bool WriteHits(const std::wstring& path);

// Restores every byte that is still a breakpoint and stops recording.
void Stop();

} // namespace opensc4::coverage
