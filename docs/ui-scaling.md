# UI scaling (P2)

The game draws its interface at its original pixel size, so it gets smaller as screens get bigger: at 3440x1440 the control panel and text are about a third of the size they were designed for on 1024x768. This document records how the game draws and positions its interface and the options for scaling it.

## How the game draws (survey, 2026-10-06)

A draw count by projection, viewport and calling function in the test city (D3D11 driver, 1024x768):

- **Interface panels** all draw through one function (the call returning to 0x00993242). Each window draws with the viewport set to its own screen rectangle (buttons 68x30, the tutorial dialog 768x600) and a pixel-exact orthographic projection for that size, y pointing down. So the driver can tell interface draws apart from the city.
- **The city** is drawn by other callers (returns to 0x007D29BB, 0x007D29E5, 0x007D2A09, 0x007D4B1C) and the terrain through the dynamic vertex buffers, with the full-screen viewport and the camera's orthographic projection.
- **Full-screen 2D** from 0x007AA0AF: a pixel projection over the whole screen (about 120,000 draws in 15 s), presumably overlays drawn over the city view.
- **A second 3D view** inside a panel (508x358, its own projection), presumably a preview or the map.

The window system (gzcom-dll's `cIGZWin`, `cIGZWinMgr`, `cISC4View3DWin`): windows have areas in screen pixels (`GetArea`, `SetArea`, `SetSize`) inside their parent; the window manager owns a canvas, finds windows under a point (`GetWindowFromPoint`), and takes mouse input through one entry point, `GZWinMsgPost_Mouse(message, x, z, flags)`, plus `GetCursorScreenPosition` and `SetCursorPosition`. The 3D view window picks terrain and occupants from screen coordinates (`PickTerrain`, `PickOccupant`). The game also calls `GetCursorPos`/`ScreenToClient` (0x0098AF00) and `SetCursorPos`/`ClientToScreen` (0x005C4480, 0x009DA9A4) itself.

## Options

**A. Render scale** (the roadmap's interim item). The game runs at the screen size divided by a scale factor (3440x1440 / 1.5 = 2293x960) and the D3D11 driver draws it at the full size. Everything gets bigger: interface, text and city. The city is drawn at full resolution (its projections do not depend on pixel size), so edges stay sharp; interface images and text are scaled up and a little soft. The visible part of the city is what a 2293x960 screen shows.

Work: the driver keeps a full-size back buffer and scales everything the game expresses in pixels (viewports, clears, buffer regions, front-buffer reads and snapshots, which go back to the game at its size); the mode list offers the scaled sizes; mouse messages are divided by the factor in the driver's window procedure, and the game's own `GetCursorPos`, `SetCursorPos`, `ClientToScreen` and `ScreenToClient` calls are translated (import hooks). Days, and every piece is plain driver or Win32 work.

**B. Interface-only scaling** (the roadmap's "proper" item). The city view keeps the full resolution and visible area; only the panels grow. The driver side is the same as A, applied only to interface draws (which it can recognise, see above). The game side needs the 3D view to render and pick at the full size while the window system lays panels out at the scaled size: the view window's size as the renderer and camera see it, its picking coordinates, and the mouse coordinates it receives would all be scaled. That means reverse engineering how `cISC4View3DWin` passes its size to the renderer and camera and how the full-screen 2D overlays relate to it. Weeks, with the size uncertain until that is understood.

A is a strict subset of B's work: the driver scaling and the input translation are needed by both. B adds the game-side hooks for the 3D view.

## Render scale (option A, implemented 2026-10-06)

`opensc4.ini` `[renderer]` `scale = 1.5` (1 to 4, D3D11 driver only). The driver lists every mode at its physical size divided by the scale, and draws the game's frames at the physical size: viewports, scissors, clears and buffer-region copies are converted to physical pixels (edges rounded, so neighbouring rectangles stay neighbours); snapshots (`CopyColorBuffer`) read the physical pixels and return the game's size (nearest). The city's projections do not depend on pixel size, so it is drawn sharp; interface images and text are scaled up.

**The game sees a desktop scaled down by the same factor**, the way Windows virtualises coordinates for programs that are not DPI aware. Translating only our window's client coordinates was not enough: the window layer keeps its client area as a screen rectangle (0x0098D9B0) and takes the screen's size as its own in full screen (0x0098ADE0). So the game's imports of `ScreenToClient`, `ClientToScreen`, `MapWindowPoints`, `GetClientRect`, `GetWindowRect`, `GetWindowPlacement`, `MoveWindow`, `SetWindowPos`, `GetCursorPos`, `SetCursorPos`, `SystemParametersInfoA` (work area), `GetSystemMetrics` (screen size), `EnumDisplaySettingsA` and `GetDeviceCaps` (screen size) are wrapped in the exe's import table ([iat.h](../src/core/iat.h)), so other modules see Windows as it is; our window's messages (client coordinates, and the screen coordinates of `WM_MOVE` and the wheel) are divided in the window procedure. The game asks about the window while `CreateWindowEx` is still running (`WM_CREATE`, the first `WM_SIZE`), so the window procedure marks it as the scaled one on its first message; before that fix the game laid out a 3440x1440 interface inside its 2293x960 picture and clipped the tutorial panel. The wrappers go in when the driver registers (DirectorInit), before the game measures the screen.

With a scale, the ini decides the size: `resolution` in physical pixels (default: the desktop's), `window` (default full screen); the game's saved size would match no mode, which crashes it.

Checked unattended: 3440x1440 at scale 1.5 (game 2293x960) draws the city, the control panel and the tutorial panel at 1.5 times their size; 1920x1080 windowed at 1.5 (game 1280x720) runs; scale 1 still matches the stock driver in all 20 shots. Not checked yet: mouse input (clicks, dragging, the wheel, cursor warps when scrolling) — needs a hands-on test.

Interface textures are sampled as the game sets them (mostly nearest), so at non-integer scales strokes come out uneven: text is readable but blocky. Next: a sharper filter for interface draws, which the driver can recognise by their caller.
