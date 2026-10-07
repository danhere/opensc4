// determinism.fixed_seed (fixed_seed.cpp): fixed seeds instead of clock seeds
// for the game's random number generators.
#pragma once

namespace opensc4::fixed_seed {

// Ends the startup phase, in which every seed call gets the next number of a
// fixed sequence; after it only clock seeds are replaced. The director calls
// this after the PostAppInit replacements.
void EndStartup();

} // namespace opensc4::fixed_seed
