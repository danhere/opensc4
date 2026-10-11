// Which region a city file belongs to, for game.date_sync
// (game_date_sync.cpp).
#pragma once

#include <string>

namespace opensc4::date_sync_region {

// The folder of a city file ("...\Regions\London\City - X.sc4" ->
// "...\regions\london"), lowercased with forward slashes made backslashes so
// two spellings of one folder compare equal; empty if the path has no
// folder.
std::string Of(const std::string& cityPath);

} // namespace opensc4::date_sync_region
