// The "growify" cheat as players type it: parsing the command and choosing
// the buildings it applies to (game_growify.cpp).
#pragma once

#include <cstdint>
#include <string>

namespace opensc4::growify_command {

enum class Kind { Residential, Commercial, Agriculture, Industrial };

struct Command {
    Kind kind = Kind::Residential;
    int32_t zone = 0;       // the game's zone type (cISC4ZoneManager::ZoneType), 1 to 9
    bool historical = true; // also mark the lots historical
};

// "growify <type> <density> [historical]": the type R, C, A or I and the
// density L, M or H by their first letter, either case ("Res Med",
// "commercial high"); agriculture needs no density; historical is true,
// false, 1 or 0 (default true). Empty on success, else what to tell the
// player; `command` is changed only on success.
std::string Parse(const std::string& text, Command* command);

// Whether a building of exemplar Purpose `purpose` (property 0x27812833) is
// one `kind` converts: residence; services and office; agriculture;
// processing, manufacturing and high tech.
bool Matches(Kind kind, uint32_t purpose);

// "residential", "commercial", "agricultural", "industrial".
const char* Name(Kind kind);

} // namespace opensc4::growify_command
