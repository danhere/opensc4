# Photo mode

Photo mode shows the city at the game's own camera angles (4 rotations, 5 zoom levels) with the interface hidden, lets the zoom go between and past the game's levels, and saves the picture at the window's full resolution. It needs OpenSC4's D3D11 driver (`renderer.d3d11`), which owns the game's window and holds the frame. The camera control (`renderer.camera.control`, [renderer.md](renderer.md), "The camera control") is not required: photo mode only calls its entry points, which run whichever code is installed.

## Keys

| Key | |
|---|---|
| Ctrl+Shift+P | photo mode on or off |
| PageUp, PageDown | zoom in or out by 1/8 (fine zoom 0.25 to 8, on top of the game's zoom level) |
| Home | fine zoom back to 1 |
| Enter or P | save the picture |
| Esc | leave photo mode |

The game's own keys keep working in photo mode (arrow keys scroll, the zoom and rotation keys change the view); other typed characters are held back. Leaving photo mode shows the interface again and puts the fine zoom back to 1.

Pictures are PNG files named `photo-YYYYMMDD-HHMMSS.png`, at the window's physical size (with `[renderer] scale = 1.5` on a 3440x1440 screen, 3440x1440 even though the game runs at 2293x960), in `Pictures\OpenSC4` under the user's profile, or in the folder set by `[photo] folder` in `opensc4.ini` (or `OPENSC4_PHOTO_FOLDER`). They are written with the Windows Imaging Component, loaded at run time.

## Why only the game's angles

A camera turned off the game's angles shows the terrain correctly, but every building, tree and animal disappears and only their shadows stay: the game's models are pictures made for its 4 rotations and 5 zooms. A free camera needs models drawn by our own renderer; Dan decided (2026-10-09) to keep photo mode to the game's angles for now.

## What it does

- On: every visible interface window except the app frame and the city view is hidden (the same windows as the shots harness's `OPENSC4_TEST_SHOT_HIDE_UI`), and remembered so that leaving shows them again.
- With every interface window hidden the game stops calling the driver's `Flush`: its window manager (0x0098E5C0) presents only when it has something to update, and the city view alone does not give it any. The unmodified game with the stock DirectX driver keeps presenting in the same state (call counts through `renderer.passthrough`); why ours does not is still open. Photo mode therefore has our driver show the finished back buffer on a 15 ms timer while it is on (window messages run between the game's frames, so the back buffer then holds a whole frame).
- The zoom keys call `cSC4CameraControl::SetZoomFactor` (0x007CD6E0), which rebuilds the camera's box and posts its change message, so the city redraws at the new size. Buildings and trees are pictures drawn for the zoom level, so they get blurrier as the fine zoom grows past 1.

## Checks

`OPENSC4_TEST_PHOTO=1` in the shots harness turns photo mode on before the first shot, steps the fine zoom (x1, x1.5, x0.75 in turn), saves a picture after each shot and leaves photo mode at the end. 2026-10-09: 20 pictures, all different, with the interface hidden and the zoom applied; the 7 views at fine zoom 1 match the stock DirectX driver's shots with the interface hidden (`--tolerance 32 --max-differing 1.2`).

Finding this also turned up a driver bug: our buffer regions took the game's surface 0 for the front buffer and 1 for the back buffer, where the stock driver's are the render target and a depth surface ([renderer.md](renderer.md), "D3D11 driver"). With the fix, our driver still matches the stock one in 20 of 20 shots, and in 20 of 20 with the camera workout (`OPENSC4_TEST_CAMERA_EXERCISE=1`).

The keys were not pressed in an unattended run (that needs the game in the foreground on Dan's screen); the harness calls the same functions the keys do.

Not done yet: pictures larger than the window (rendering at a multiple of the window's size), depth of field and other effects, pausing the simulation while in photo mode.
