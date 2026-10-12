// determinism.fixed_seed (fixed_seed.cpp): fixed seeds instead of clock seeds
// for the game's random number generators.
#pragma once

#include <cstdint>

namespace opensc4::fixed_seed {

// Ends the startup phase, in which every seed call gets the next number of a
// fixed sequence; after it only clock seeds are replaced. The director calls
// this after the PostAppInit replacements.
void EndStartup();

// For game.seed (game_seed.cpp), at Phase::Attach: installs the seed hooks if
// determinism.fixed_seed has not, so static generators are recorded.
bool InstallForGameSeed();

// Seeds every generator in the exe's data from `seed` and its address, and
// makes later clock seeds (0xFFFFFFFF) come from `seed` and their call site,
// counted afresh. Main thread, before a city loads.
void ReseedForCity(uint32_t seed);

// For our replacements of the sound code's rand() calls (the sites listed
// in fixed_seed.cpp): the next number of the sound code's own sequence, or
// the C runtime's rand() when that is not hooked.
int SoundRand();

// For replacements of code that constructs or seeds generators: the call
// site clock seeds are counted under is the first exe address up the stack,
// which from our code is not the original's. While a SiteOverride lives,
// seed calls on its thread count under `site` (the original's return
// address after the cRZRandom call).
class SiteOverride {
public:
    explicit SiteOverride(uint32_t site);
    ~SiteOverride();
    SiteOverride(const SiteOverride&) = delete;
    SiteOverride& operator=(const SiteOverride&) = delete;

private:
    uint32_t previous_;
};

} // namespace opensc4::fixed_seed
