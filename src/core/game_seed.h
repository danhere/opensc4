// game.seed (game_seed.cpp): one seed per city for the game's random numbers.
#pragma once

namespace opensc4::game_seed {

// Subscribes to the city messages and registers the "seed" cheat. The
// director calls this after the PostAppInit replacements.
void Start();

} // namespace opensc4::game_seed
