// Seeds as players see and type them: 8 hex digits, "1F2E-3D4C"
// (game_seed.cpp).
#pragma once

#include <cstdint>
#include <string>

namespace opensc4::seed_code {

// "1F2E-3D4C".
std::string Format(std::uint32_t seed);

// 8 hex digits, either case, a dash after the fourth allowed; false for
// anything else.
bool Parse(const std::string& text, std::uint32_t* seed);

} // namespace opensc4::seed_code
