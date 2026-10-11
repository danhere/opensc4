// game.bulldoze (docs/bulldoze.md): the label by the cursor that says what
// the held keys make the bulldozer do. For the unattended test.
#pragma once

namespace opensc4::game_bulldoze {

// Sets the city view's cursor text for a mode (0 all, 1 flora, 2 networks,
// 3 all but networks) and the diagonal strip; no text for mode 0 without
// the strip. `view` is the cISC4View3DWin.
void SetLabel(void* view, int mode, bool diagonal);
void ClearLabel(void* view);

} // namespace opensc4::game_bulldoze
