// What the two parts of our overlay manager share (src/core/overlay_manage_1.cpp,
// overlay_manage_2.cpp; docs/renderer.md, "Bookkeeping").
#pragma once

namespace opensc4::overlay_manage {

// Inside a compared call of either part: whose code the hooked functions
// run. Each part's detours read it, so a call one part compares reaches the
// other part's functions on the same side.
enum class Side { None, Game, Ours };
extern Side g_side;

} // namespace opensc4::overlay_manage
