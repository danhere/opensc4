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

## The city at full size (option B, prototype, 2026-10-06)

B turned out much smaller than feared, because it can sit on top of A instead of beside it. With A, the game already lives in a smaller pixel space and the driver draws the city at the physical size; only the camera decides how much of the city a game pixel covers. That is one function, the city camera's projection setup (`cSC4CameraControl::UpdateProjection`, our name, 0x007CBE40):

- pixels per 16 m tile for the zoom level (this+0x108) come from a table at 0x00ABACE0 (8, 16, 32, 73, 146 for zoom levels 1 to 5, with camera pitches 30, 35, 40, 45, 45 degrees at 0x00ABACCC) times a fine zoom factor at this+0xF0 (1.0 normally);
- it stores metres per pixel (this+0x134) and pixels per metre (this+0x138), and sets the orthographic box to the view's size in pixels (this+0x12C, this+0x130) times metres per pixel (0x007FF2E0);
- the table is read nowhere else, and the function is called from the zoom setters (0x007CD6E0, 0x007CDC50).

`[renderer] scale_city = no` (with a scale above 1) wraps that function: it divides the fine zoom factor by the scale for the duration of the call and puts it back afterwards, so each game pixel covers scale times as much of the city. The city then comes out at the stock game's full-resolution size (a zoom-5 tile is 145 physical pixels wide; stock: 146), while the interface stays enlarged as with A. Everything that goes through what the camera stores follows on its own.

Checked unattended at 3440x1440, scale 1.5 (the game sees 2293x960):

- the whole city fits where A showed it 1.5 times as big; the interface is unchanged;
- picking: with the query tool, the highlighted tile is under the cursor at the top left, the centre and the bottom right of the screen (a projection mismatch would be off by about three tiles at the edges);
- the tutorial's camera moves centre the town; the wheel zooms; edge scrolling works and only starts on the last row of pixels; scrolling (which moves the saved picture by a pixel offset the game works out from the camera) leaves no gaps.

Not covered, or different:

- **The region view** has its own camera and is still drawn as with A (enlarged). Finding its projection is the next step if it should match.
- Text and interface images are still the scaled-up 1x art (see "Interface images" below); crisp text needs the fonts drawn at the larger size.
- Not tried yet: placing things (zoning, roads, lots), the other 3D views in panels, performance in a big city (the screen shows 2.25 times as much city at 1.5), and how scrolling speed feels.


## Render scale (option A, implemented 2026-10-06)

`opensc4.ini` `[renderer]` `scale = 1.5` (1 to 4, D3D11 driver only). The driver lists every mode at its physical size divided by the scale, and draws the game's frames at the physical size: viewports, scissors, clears and buffer-region copies are converted to physical pixels (edges rounded, so neighbouring rectangles stay neighbours); snapshots (`CopyColorBuffer`) read the physical pixels and return the game's size (nearest). The city's projections do not depend on pixel size, so it is drawn sharp; interface images and text are scaled up.

**The game sees a desktop scaled down by the same factor**, the way Windows virtualises coordinates for programs that are not DPI aware. Translating only our window's client coordinates was not enough: the window layer keeps its client area as a screen rectangle (0x0098D9B0) and takes the screen's size as its own in full screen (0x0098ADE0). So the game's imports of `ScreenToClient`, `ClientToScreen`, `MapWindowPoints`, `GetClientRect`, `GetWindowRect`, `GetWindowPlacement`, `MoveWindow`, `SetWindowPos`, `GetCursorPos`, `SetCursorPos`, `SystemParametersInfoA` (work area), `GetSystemMetrics` (screen size), `EnumDisplaySettingsA` and `GetDeviceCaps` (screen size) are wrapped in the exe's import table ([iat.h](../src/core/iat.h)), so other modules see Windows as it is; our window's messages (client coordinates, and the screen coordinates of `WM_MOVE` and the wheel) are divided in the window procedure. The game asks about the window while `CreateWindowEx` is still running (`WM_CREATE`, the first `WM_SIZE`), so the window procedure marks it as the scaled one on its first message; before that fix the game laid out a 3440x1440 interface inside its 2293x960 picture and clipped the tutorial panel. The wrappers go in when the driver registers (DirectorInit), before the game measures the screen.

With a scale, the ini decides the size: `resolution` in physical pixels (default: the desktop's), `window` (default full screen); the game's saved size would match no mode, which crashes it.

Checked unattended: 3440x1440 at scale 1.5 (game 2293x960) draws the city, the control panel and the tutorial panel at 1.5 times their size; 1920x1080 windowed at 1.5 (game 1280x720) runs; scale 1 still matches the stock driver in all 20 shots. Mouse input, checked unattended with synthesized input on 2026-10-06 (3440x1440, scale 1.5, full screen): clicks land on the button under the cursor (tutorial, Mayor and Budget panels, region view tiles and city card), tooltips appear by the cursor, the wheel zooms, edge scrolling works on the right and left edges, and right-dragging pans. Edge scrolling first showed the buffer-region bug described in [renderer.md](renderer.md) ("What the driver calls mean"), which was in the driver at every scale, not in the scaling. Still to try by hand: how it feels, and tools that place things (zoning, roads).

**Interface images.** Sampled as the game sets them (mostly nearest), they came out uneven at non-integer scales: some strokes one pixel wide, some two. Interface draws are recognised by their pixel-exact projection (one unit per pixel of the viewport, which the city's projection never is), and their nearest-filtered images are drawn with sharp bilinear filtering: each texel stays a flat square, with a band of about a pixel blended at its edges, the pixels-per-texel worked out from screen-space derivatives so stretched images work too. Strokes now have an even weight with soft edges. The glyphs themselves are thin, anti-aliased 1x images, so text stays light; crisp text means the game drawing its fonts (TrueType outlines, the `.mxf` files XOR-ed with 0x9D) at the larger size, which belongs with option B.
