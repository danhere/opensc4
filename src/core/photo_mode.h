// Photo mode (docs/photo-mode.md): the city at the game's own camera angles
// with the interface hidden, any fine zoom, and captures of the full-
// resolution frame. Needs our D3D11 driver, which owns the game's window.
#pragma once

#include <windows.h>

#include <string>

namespace opensc4::photo_mode {

// Keyboard messages to the game's window; true when photo mode took the
// message (the game never sees it). Ctrl+Shift+P toggles photo mode; in it
// PageUp/PageDown zoom, Home resets the zoom, Enter or P captures, Esc leaves.
bool OnKey(UINT message, WPARAM key);

bool Active();
void Enter();
void Exit();
// Multiplies the fine zoom by `step` (kept within 0.25 to 8).
void Zoom(float step);
// Saves the last shown frame as a PNG; returns its path ("" on failure).
std::wstring Capture();

} // namespace opensc4::photo_mode
