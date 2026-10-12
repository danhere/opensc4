// The plop tool, cSC4ViewInputControlPlaceLot (vtable 0x00A90738, object
// 0x158 bytes; src/core/tool_plop_3.cpp holds its last part): what other code
// needs to know about our translation.
//
// game.quiet.plop (src/core/game_quiet.cpp) detours PlaceLot (0x004C1740) and
// clears its smoke argument when the call returns to 0x004C3190, the point
// after the tool's call at 0x004C318B inside OnMouseDownL (0x004C3120). Our
// OnMouseDownL calls PlaceLot from core.dll, so the return address the
// wrapper sees is ours. While our OnMouseDownL has that call in progress,
// PlaceLotCallSite() answers 0x004C318B (and 0 at every other time); the
// wrapper may take that in addition to its return address test.
//
// Our PlaceLot (src/core/tool_plop_2.cpp) sits at the game's entry
// (0x004C1740); game.quiet.plop wraps ours then (the same arrangement as
// src/core/demolition.h). Our group installs in PreAppInit, before the
// wrapper (PostAppInit), so the answers are settled by then.
#pragma once

#include <cstdint>

namespace opensc4::tool_plop {

// The call of PlaceLot in the game's OnMouseDownL, and where it returns to.
constexpr std::uintptr_t kPlaceLotCallSite = 0x004C318B;
constexpr std::uintptr_t kPlaceLotReturn = 0x004C3190;

// The game address of the PlaceLot call that our code is making for the
// plop tool right now, or 0.
std::uintptr_t PlaceLotCallSite();

// True while our OnMouseDownL is inside its PlaceLot call.
bool InToolPlaceLot();

// Our PlaceLot translation (`ret 0x10`: this, position, rotation, smoke
// flag, cell rectangle out); the address of the function in core.dll.
std::uintptr_t PlaceLotEntry();

// True once our detour sits at the game's entry (0x004C1740).
bool PlaceLotIsOurs();

} // namespace opensc4::tool_plop
