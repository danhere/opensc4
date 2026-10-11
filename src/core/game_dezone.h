// game.dezone (docs/dezone.md): de-zoning that keeps the roads.
#pragma once

#include <cstdint>

namespace opensc4::game_dezone {

// Called at the start of cSC4ZoneManager::PlaceZone, the game's or ours:
// when the replacement is on, the call de-zones for the player's zoning tool
// (`caller` is the return address) and Shift is held, the cells with a
// transport network are cleared from the region, so they are left as they
// are. Nothing otherwise.
void KeepRoads(int32_t* region, uint32_t type, uint32_t customZoning, const void* caller);

} // namespace opensc4::game_dezone
