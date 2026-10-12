// Our cSC4ViewInputControlDemolish (src/core/tool_bulldoze.cpp), the bulldoze
// tool: where its four functions that game.bulldoze (game_bulldoze.cpp) wraps
// can be found, and how the wrapper can tell a demolition call of the tool.
// The wrapper detours the tool's Preview, OnMouseMove, EndDrag and
// IsOccupantShown at the game's entries and recognises the tool's
// DemolishRegion calls by their return address (inside 0x004B8830 to
// 0x004B9E20). With our translation on, a call made by us returns into
// core.dll, so InToolCall() / ToolCallSite() answer for it.
#pragma once

#include <cstdint>

namespace opensc4::tool_bulldoze {

// Our translations of the four wrapped functions (the addresses of the
// functions in core.dll); the group does not hook them itself.
//   Preview      0x004B93B0  thiscall, no arguments
//   OnMouseMove  0x004B9B70  thiscall (x, z, modifiers), ret 0xC
//   EndDrag      0x004B8ED0  thiscall, no arguments
//   IsOccupantShown 0x004B8880  (occupant), ret 4; `this` is not read
std::uintptr_t PreviewEntry();
std::uintptr_t MouseMoveEntry();
std::uintptr_t EndDragEntry();
std::uintptr_t ShownEntry();

// Whether our detour sits at the game's entry of each of the four (set by
// the group's install, in PreAppInit, before game.bulldoze wraps them in
// PostAppInit; false while the function is the game's). game.bulldoze then
// wraps our function in place of the game's entry.
enum class Wrapped { Preview, MouseMove, EndDrag, Shown };
bool WrappedIsOurs(Wrapped which);
bool WrappedAreOurs(); // all four
void SetWrappedIsOurs(Wrapped which, bool ours);

// True while one of our DemolishRegion calls for the tool is in progress;
// then ToolCallSite() is the address of the original call instruction
// (0x004B97F1 in Preview, 0x004B9D04 in OnMouseUpL), else 0.
bool InToolCall();
std::uintptr_t ToolCallSite();

} // namespace opensc4::tool_bulldoze
