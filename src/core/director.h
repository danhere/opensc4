// core.dll's GZCOM director (director.cpp).
#pragma once

#include <cstdint>

class cIGZUnknown;

namespace opensc4::director {

// Registers a GZCOM class factory with core.dll's director. Only valid during
// registry::Phase::DirectorInit, which runs while the director is created.
bool AddClass(std::uint32_t clsid, cIGZUnknown* (*factory)());

} // namespace opensc4::director
