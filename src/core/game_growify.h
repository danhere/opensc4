// The "growify" cheat (game_growify.cpp): plopped buildings to growable lots.
#pragma once

namespace opensc4::game_growify {

// Subscribes to the city messages and registers the "growify" cheat. The
// director calls this after the PostAppInit replacements.
void Start();

} // namespace opensc4::game_growify
