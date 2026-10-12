// Our cSC4Demolition (src/core/demolition_1.cpp, demolition_2.cpp): where its
// two functions that other code wraps can be found. game.bulldoze wraps
// DemolishRegion and game.quiet.demolish replaces DoDemolishOccupantEffect;
// with our demolition on, each of them is put on top of our translation (the
// game's entry jumps to ours, the wrapper sits on ours), so both work as they
// do on the game's code. Our groups install in PreAppInit, before the
// wrappers (PostAppInit), so the answers below are settled by then.
#pragma once

#include <cstdint>

namespace opensc4::demolition {

// Our DemolishRegion translation (ret 0x2C) and DoDemolishOccupantEffect
// translation (ret 0x18); the addresses of the functions in core.dll.
std::uintptr_t RegionEntry();
std::uintptr_t EffectEntry();

// True once our detour sits at the game's entry (0x00468410, 0x004673B0).
bool RegionIsOurs();
bool EffectIsOurs();

} // namespace opensc4::demolition
