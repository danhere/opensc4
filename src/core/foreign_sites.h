// Places where other DLLs patch the middle of game functions
// (docs/patch-census.md). Replacing such a function would silently drop the
// other DLL's change, so the hook layer leaves a function alone when one of
// its known sites has been patched: the game's code keeps running, with the
// other DLL's change in it (docs/architecture.md, "Hooking layer").
#pragma once

#include <cstdint>
#include <string>

namespace opensc4::foreign_sites {

// Empty if no known site inside the function starting at `function` has been
// patched; otherwise what patched it ("NAM.dll at 0x006D79BF: JMP to ...").
// A site counts as patched when it holds a JMP or CALL rel32 into another
// module or into allocated memory, which the game's own code never does.
std::string PatchedInside(std::uintptr_t function);

// Adds a site for the tests (the table is otherwise fixed).
void AddForTests(std::uintptr_t function, std::uintptr_t site);

} // namespace opensc4::foreign_sites
