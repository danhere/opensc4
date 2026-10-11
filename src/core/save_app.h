// The application's SaveCity and LoadCity, ours or the game's
// (docs/save-format.md, "Saving and loading a city"). Several features wrap
// those two functions with a detour of their own (the timelapse copies, the
// city seed, the load trace), and one address takes one detour. So a
// wrapper does not call the game's function directly: it hands the pointer
// it would have called to these, which run ours when `saves.app` is on.
// With no wrapper on an address, `saves.app` puts its own detour there.
#pragma once

#include <cstdint>

namespace opensc4::saves {

// `original` is the wrapper's way to the game's function (its trampoline);
// `self` is the cISC4App interface pointer, as the vtable slot gets it.
bool SaveCity(void* original, void* self, void* edx, const void* path, bool fast);
bool LoadCity(void* original, void* self, void* edx, void* path, void* city);

} // namespace opensc4::saves
