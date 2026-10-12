// The zoning tool (view input control, vtable 0x00A92CA8, 0x98 bytes), see
// tool_zone.cpp.
#pragma once

#include <cstdint>

namespace opensc4::tool_zone {

// The two places in the game's zoning tool that call the zone manager's
// PlaceZone (slot 11): the preview in UpdatePreview (0x004DB240, place = 0)
// and the placing in Place (0x004DA7A0, place = 1). They are the return
// addresses game.dezone (src/core/game_dezone.cpp) tests for. Of its list
// (0x004DB312, 0x004DB40A, 0x004DB764, 0x004DBA3E, 0x004DA846) only
// 0x004DB312 and 0x004DA846 are PlaceZone calls; the other three call the
// sound service (slot 11 of its vtable has the same offset 0x2C).
constexpr std::uintptr_t kSiteUpdatePreview = 0x004DB312;
constexpr std::uintptr_t kSitePlace = 0x004DA846;

// While our zoning tool is inside a PlaceZone call: the address of the
// original call site it stands for (kSiteUpdatePreview or kSitePlace). 0
// otherwise. A detour on PlaceZone sees our code's address as the return
// address; this tells it which game call site that was.
std::uintptr_t PlaceZoneSite();

} // namespace opensc4::tool_zone
