# Renderer (P1)

Notes for replacing the game's graphics driver. Build 641, Steam, 2026-10-05.

## How the game gets its driver

- The game talks to graphics hardware only through `cIGZGDriver`, an 80-method interface (83 vtable slots with `cIGZUnknown`; gzcom-dll's [cIGZGDriver.h](../third_party/gzcom-dll/gzcom-dll/include/cIGZGDriver.h) matches the DirectX driver's vtable at 0x00AC4660 slot for slot).
- The graphics system (`cIGZGraphicSystem2`, service 0xC416025C) creates exactly the three stock drivers between core.dll's director `OnStart` and `PreAppInit`: Software 0x7ACA35C6, DirectX 0xBADB6906 and OpenGL 0xC4554841. It makes one current (DirectX here).
- `cSC4App` keeps the requested driver's class ID at offset 0x65C from the object start (`cISC4App` − 0xC, `cIGZApp` − 0x2C), set from the `-d:` option by 0x0044EED0. 0x0044CE40 passes it to `cIGZGraphicSystem2::SetDefaultDriverClassID`.
- **A new class ID is never instantiated.** With the field and `SetDefaultDriverClassID` set to an unknown ID from `OnStart`, the graphics system still created only the three stock drivers and chose DirectX, and never asked core.dll's director for the class. GZCOM sends a director requests only for classes it registered, so nothing else would reach it either. This is why scvk and SCGL take over the OpenGL ID instead.

## Installing our driver: `SetGDriver`

`cIGZGraphicSystem2::SetGDriver(cIGZGDriver*)` sets the current driver object directly. At `PreAppInit` the drivers exist but the game has not set its video mode or loaded anything; a driver made current there receives `SetVideoMode` and everything after it. No class ID is taken (so no collision with scvk, SCGL or render-services' need for the DirectX driver) and nothing is patched.

`renderer.passthrough` ([gdriver_proxy.cpp](../src/core/gdriver_proxy.cpp), registry kind `api`, phase `PreAppInit`) wraps the current driver this way and forwards every call. With it on, a 120 s autotest on a clean install runs the city at the same pace as without it (2015-03-03 and 2015-03-04 at the end) and quits cleanly.

All measurements here are from a clean Steam install (2026-10-05). The first ones were taken on an install whose game folder still had dxwrapper (DirectX 7 to 9), sc4-graphics-options and ReShade; those changed the call volumes several times over (for example about 980,000 `Clear` calls in 120 s instead of 24,515), though not which methods were called.

Not seen through the proxy: calls made before `PreAppInit` (the driver's `Init`, anything the graphics system does while creating drivers). Code that looks a driver up with `GetGDriverByID` gets the original.

## Other driver interfaces

The DirectX driver's `QueryInterface` (0x00882A00) also serves four other interfaces. The OpenGL driver's (0x0087D670) serves the first three and not 0x09CD86F9. The game runs without 0x09CD86F9 but then draws no terrain and no textured models: it is the dynamic vertex buffer interface they draw through ("What the driver calls mean"). So a replacement driver needs the buffer region, snapshot and vertex buffer interfaces; lighting is never queried. Names come from the Mac build's debug symbols (sc4-ghidra-symbols types, where the DirectX driver is `nSGLDX7::cGDriver`), matched by method count, `this` offset and what the Windows code does. The proxy wraps all four with [interface_counter](../src/core/interface_counter.h), which counts calls by vtable slot without needing the signatures.

| IID | Interface | Where | Methods | 120 s run |
|---|---|---|---|---|
| 0x669565FE | `cIGZGBufferRegionExtension` | the driver object (vtable 0x00AC47EC) | 8 (then 7 more of the class's own) | queried once (0x007C3810, kept in 0x00B4E1F0); NewBufferRegion 2, ReadBufferRegion 2,770, DrawBufferRegion 16,600 |
| 0xE69BFE2A | `cIGZGSnapshotExtension` | object + 4 (0x00AC47DC) | 1: `CopyColorBuffer` | queried 2, CopyColorBuffer 1 |
| 0x87E2B87D | `cIGZGDriverLightingExtension` | object + 8 (0x00AC47AC) | 9 | never queried |
| 0x09CD86F9 | dynamic vertex buffers (no Mac name) | separate object held at + 0x1C (vtable 0x00AC4F10) | 9 | queried 225,137 times (0x00593E10, 0x007545C0); slot 3 once per query; slots 6 and 8 407,570 each, 10 404,965, 5 176,675, 7 59,164, 4 15,942 |

- **Buffer regions** (slot: method, Windows DirectX implementation): 3 `BufferRegionEnabled` (true), 4 `NewBufferRegion(buffer)` (0 front, 1 back, 2 depth: creates an off-screen DirectDraw surface, returns a handle), 5 `DeleteBufferRegion`, 6 `ReadBufferRegion` (7 arguments), 7 `DrawBufferRegion`, 8 `IsBufferRegion`, 9 `CanDoPartialRegionWrites` and 10 `CanDoOffsetReads` (both true). Saving and restoring screen areas, probably for the UI.
- **Lighting**: `EnableLighting`, `EnableLight`, `LightModelAmbient`, `LightColor` (two overloads), `LightPosition`, `LightDirection`, `MaterialColor` (two overloads). The Mac order is GCC's; MSVC may order the overloads differently.
- **0x09CD86F9**: dynamic vertex buffers, see "What the driver calls mean". The Mac symbols' interfaces do not match it.

## render-services

[sc4-render-services](https://github.com/caspervg/sc4-render-services) 0.7.1 runs normally in the autotest, and crashes the game as soon as `renderer.passthrough` is on. Its ImGui service gets the current driver (`GetGDriver`), checks `GetGZCLSID() == 0xBADB6906`, then reads a pointer at offset 0x24C of the `cIGZGDriver` pointer: the DirectX driver's internal Direct3D helper (`cISGLDX7D3DX`, which gives it the DirectDraw and Direct3D 7 device), whose `EndScene` it hooks to draw ImGui. The proxy forwards `GetGZCLSID`, so the check passes, and offset 0x24C of the proxy is not that field ([DX7InterfaceHook.cpp](https://github.com/caspervg/sc4-render-services/blob/v0.7.1/src/DX7InterfaceHook.cpp), `CaptureInterface`). Its other services (draw passes, camera, terrain decals) do not touch the driver.

Fix: the proxy copies the DirectX driver's pointer at 0x24C to the same offset in itself, at install and on every `GetGZCLSID` call (render-services calls it right before reading, on every tick). With that, a 60 s run with render-services and the proxy runs normally and quits cleanly (tested on the earlier, modded install). Not verified yet: that its ImGui overlay actually draws (its log, which would say so, goes to the real `Documents\SimCity 4` or to the debugger). If mirroring causes trouble, the fallback is to report a class ID other than DirectX's, which makes render-services skip its overlay.

Testing it: its installer puts `imgui.dll` in the game's `Apps` folder and `SC4RenderServices.dll` in Plugins. Instead, the DLLs were unpacked from the installer with 7-Zip, `SC4RenderServices.dll` went into the test `Plugins` (with `LogToFile=false`: its log otherwise goes to the real `Documents\SimCity 4` whatever `-UserDir` says, and `ShowDemoPanel=true` so ImGui draws every frame), and `imgui.dll` was loaded with `OPENSC4_TEST_PRELOAD`.

## What the game calls

Making Money Tutorial, 120 s at speed 2, autotest window (no menus, no UI interaction), counted from `PreAppInit` on. 42 of the 80 methods were called:

| Method | Calls | Method | Calls |
|---|---|---|---|
| TexParameter | 7,157,942 | IsDeviceReady | 563,816 |
| Disable | 5,251,831 | GetViewport | 563,812 |
| TexStage | 3,968,650 | PolygonOffset | 563,810 |
| Enable | 2,611,018 | SetViewport (rect) | 373,043 |
| SetTexture | 2,444,960 | BlendFunc | 372,342 |
| MatrixMode | 2,051,869 | TexEnv (float) | 202,772 |
| InterleavedArrays | 1,770,830 | DepthFunc | 72,714 |
| ColorMultiplier | 1,657,029 | SetCombiner | 64,308 |
| LoadMatrix | 1,575,046 | Flush | 32,820 |
| DrawArrays | 1,282,759 | Clear | 24,515 |
| DrawElements | 1,278,266 | ClearColor | 24,419 |
| AlphaMultiplier | 1,190,821 | LoadTextureLevel | 3,701 |
| TexStageMatrix | 1,162,049 | CreateTexture | 140 |
| TexEnv (int) | 1,046,028 | DeleteTextures | 62 |
| AlphaFunc | 951,155 | GetVideoModeInfo | 10 by index, 10 current |
| LoadIdentity | 918,924 | GetError | 8 |
| TexStageCoord | 797,587 | GetDriverInfo | 3 |
| EnableVertexColors | 654,790 | CountVideoModes | 2 |
| DepthMask | 604,742 | ClearDepth | 2 |
| ColorMask | 599,214 | SetVideoMode | 1 |
| SetViewport (full) | 565,115 | | |

Never called in this run: the blits (`BitBlt`, `StretchBlt` and their alpha variants), the GL-style texture calls (`GenTextures`, `BindTexture`, `TexImage2D`), stencil, fog, `ShadeModel`, `PixelStore`, the `Get*` queries, the vertex format helpers, `GetTexture`, `Punt`. UI screens and menus may use some of them; the call log of a run with UI interaction will tell.

Observations to follow up: `TexParameter` runs about three times per draw call, and `IsDeviceReady`, `GetViewport` and `PolygonOffset` run in lockstep (about 564,000 each), probably once per rendered object.

## Screenshot harness

[scripts/run-shots.sh](../scripts/run-shots.sh) loads the test city with the simulation paused, saves the frame at each of the 5 zooms and 4 rotations, and quits; [tools/shotdiff.py](../tools/shotdiff.py) compares two output folders and writes diff images (differing pixels in magenta). Building block for step 3: every D3D11 change is checked against the stock DirectX driver this way.

```bash
scripts/run-shots.sh "$B" "$LOCALAPPDATA/opensc4/shots/base"
scripts/run-shots.sh "$B" "$LOCALAPPDATA/opensc4/shots/ours" OPENSC4_REPLACE=renderer.passthrough=ours
python3 tools/shotdiff.py "$LOCALAPPDATA/opensc4/shots/base" "$LOCALAPPDATA/opensc4/shots/ours" \
    --tolerance 32 --max-differing 1.2 --diff-dir "$LOCALAPPDATA/opensc4/shots/diff"
```

How it works ([autotest.cpp](../src/core/autotest.cpp), `OPENSC4_TEST_SHOTS`):

- **View:** `cISC4View3DWin::SetZoomAndRotation` on the city view (main window → 0x6104489A → 0x9A47B417). The camera starts where the save left it, and the autotest never saves, so every run looks at the same spot. Zooms are 1 (farthest) to 5, rotations 0 to 3. After each change the harness waits 3 s, calls `cISC43DRender::ForceFullRedraw`, waits 1 s more and captures.
- **Capture:** the game's own `cISC43DRender::TakeSnapshot(cIGZString&, bool)` (0x007C2570), which reads the frame through the driver's `cIGZGSnapshotExtension::CopyColorBuffer` and writes a 1024×768 RGBA PNG of the whole window, UI included. A replacement driver must implement that extension for the harness to see its output. Called by vtable slot, see below.
- **Render properties** (`OPENSC4_TEST_RENDER`, by name from `cISC4App::GetRenderProperties`): `RenderAutomata`, `RenderCloudsAndFog` and `RenderWaterEffects` are turned off, because they move in real time. They have to be set at PostAppInit: set just before the city loads, `RenderCloudsAndFog=0` had no effect.
- **Random seeds** (`determinism.fixed_seed`, below).

### Why runs differed, and what is left

| Cause | Effect between two runs | Fix |
|---|---|---|
| Random seeds from the clock | terrain texture variation, flora and wildlife placement: 20-50% of pixels at most zooms | `determinism.fixed_seed` |
| Automata | cars and pedestrians in other places | `RenderAutomata=0` |
| Clouds, water effects | drifting cloud blobs at zoom 1, shimmering coast | `RenderCloudsAndFog=0`, `RenderWaterEffects=0` at PostAppInit |
| Wildlife animation | deer at the same spots in different poses, up to 1.1% of pixels (deltas up to 235) at zooms 3-5 | none: animation runs on real time even while paused |
| Cloud shadows on terrain | a faint drifting patch at zoom 1 (up to 5.7% of pixels, deltas up to 30) | none found; `--tolerance 32` absorbs it |

With all of the above, two runs of the stock driver match each other with `--tolerance 32 --max-differing 1.2`, and so does `renderer.passthrough` against either. As a check that this still catches real changes, a run with `RenderShadows=0` fails 18 of 20 shots (1.1-5.9% of pixels); the two that pass are far zooms where shadows cover about 1% of the frame. So a pass is not proof: look at the diff images when a change is near the limit. A fixed animation clock (P4, fixed stepping) would make the frames exact.

### Fixed random seeds

The game has two random number generators, named from the Mac build's types (`cRZRandom`, `cRZRandomSuper`) by what they do:

- `cRZRandom::Seed` (0x00913667): a 32-bit LCG (`x * 0x41C64E6D + 0x3039`, 0x00913696). Seed 0xFFFFFFFF means `time() ^ clock()`. Its constructor (0x00913A5C) is called from about 80 places, many of them static initialisers that seed from the local time XOR a constant.
- `cRZRandomSuper::Seed` (0x00913A72): a Mersenne Twister; seed 0xFFFFFFFF mixes `time()` and `clock()` (0x00913977).
- The CRT's `rand` is seeded only by Lua's `math.randomseed` (0x008C0B20).

`determinism.fixed_seed` ([fixed_seed.cpp](../src/core/fixed_seed.cpp)) detours both seed functions and gives the n-th call `Mix(base + n)` whatever the game asked for (`OPENSC4_SEED` sets the base). It needs the new registry phase `Attach`, applied from `DllMain`: the launcher injects core.dll while the game's main thread is still suspended, so the hooks are in place before the exe's static initialisers run. Steam's stub does not mind the patched code. The first seeds the game asks for are visibly clock-derived (0x6F615500, 0x2F5DEEBE, 0xCF615537, ...). This is the first piece of P4's "capture RNG"; whether the simulation becomes deterministic with it is still open.

### gzcom-dll: overloaded virtuals are in the wrong order (cISC43DRender only)

MSVC, and clang-cl with it, puts overloaded virtual functions in the vtable in reverse declaration order. gzcom-dll's `cISC43DRender.h` declares `TakeSnapshot(cIGZString&, bool)` before `TakeSnapshot(cIGZBuffer**, bool)`, the order of the exe's slots 22 and 23, so the compiled call to the string overload goes to slot 23, the buffer overload: it would pass a string where a `cIGZBuffer**` is expected. Checked with clang-cl's assembly (`+0x5C` for the string call) against the exe's vtable at 0x00ABAB60 (slot 22 is 0x007C2570, which itself calls slot 23 with a buffer pointer). The autotest calls slot 22 directly. Other overloads in gzcom-dll headers (`StartScreenShake`, `SetBackgroundPicture`, `PickTerrain`, `Pick` here; others elsewhere) may have the same problem; worth reporting upstream once checked. `cIGZGDriver.h` is right: every overload group there (`MakeVertexFormat`, `TexEnv`, `Fog`, `TexStageCombine`, `GetVideoModeInfo`, `SetViewport`) is declared in reverse slot order, as MSVC needs, which the OpenGL and DirectX vtables confirm (for example slot 74 is the rectangle `SetViewport`, `glViewport` plus `glScissor`, and the header declares `SetViewport()` first). So the call counts in "What the game calls" are labelled correctly.

## What the driver calls mean

Reference for writing a driver, from decompiling both stock drivers (vtables: DirectX 0x00AC4660, OpenGL 0x00AC36D0, `cIGZGDriver` at object + 0xC in both). The OpenGL driver is the plainest spec, since most methods are one GL call after a table lookup; the DirectX driver is what the game was tuned against and what the screenshot harness compares with (the OpenGL driver renders without terrain and with black boxes around trees on a current NVIDIA card, so it is no visual reference).

**The model is OpenGL 1.3 fixed function with `ARB_texture_env_combine`.** GD enums are indices into translation tables (OpenGL driver 0x00B12010-0x00B12238, DirectX driver 0x00AC44CC-0x00AC45F8); [gd.h](../src/core/gd/gd.h) lists them.

- **Window.** `SetVideoMode(mode, wndproc, ?, ?)` creates the driver's own ANSI window ("GDriverClass (DirectX)": `WS_OVERLAPPEDWINDOW | WS_VISIBLE` sized to the mode's client area for windowed modes, a popup otherwise), stores the driver in `GWLP_USERDATA` and sends every message to `wndproc`. Mode -1 destroys it. `SetGDriver` calls `SetVideoMode` itself when a mode is already set. Modes are `sGDMode` records of 0x34 bytes (index, width, height, bits per pixel, A/R/G/B masks, a full-screen flag at 0x20, then capabilities the game configures itself from; see "What the game asks the driver"); the DirectX driver lists 120 here, every desktop resolution twice (full screen and windowed).
- **Frames.** `Flush` presents: `glFlush` + `SwapBuffers`, or the DirectX driver's blit to the primary surface. The back buffer keeps its contents between frames, which the game relies on: it redraws only what changed. `Clear(mask)` takes 0x4000 colour, 0x1000 depth, 0x2000 stencil and clears the current viewport (a Direct3D 7 clear), with the values from `ClearColor`/`ClearDepth`/`ClearStencil`. `ColorMask` is a no-op in the DirectX driver.
- **Viewport.** GL coordinates, origin bottom left: the DirectX driver clamps to the surface and converts with `top = height - y - h`; `GetViewport` returns `{x, y, x + w, y + h}` in the same coordinates. Direct3D 7 viewports clip, so a viewport also acts as a scissor rectangle (the OpenGL driver sets `glScissor` with it).
- **Matrices.** `MatrixMode` 0 model-view, 1 projection; GL layout. The DirectX driver loads the model-view as the world matrix (view = identity) and rewrites the projection for Direct3D (depth range and half-pixel offset, which is why it reloads it on every viewport change).
- **Vertices.** `InterleavedArrays(format, stride, data)` only records the pointer (stride 0: the format's); `DrawArrays(prim, first, count)` and `DrawElements(prim, count, type, indices)` read from it, with unsigned byte, short or int indices. Formats are bit fields of per-element counts (position, normal, up to 3 colours, 1D-4D texture coordinates), with legacy IDs for common layouts ([vertex_format.h](../src/core/gd/vertex_format.h)). **Colours are D3DCOLOR (BGRA bytes)**: the OpenGL driver swaps red and blue in the caller's array before each draw and back after. Primitives: triangles, strips, fans, points, lines, line strips, quads, quad strips; the DirectX driver turns strips, fans and quads into indexed lists.
- **Colour.** `ColorMultiplier(r, g, b)` and `AlphaMultiplier(a)` switch on lighting with only an ambient light, and leave it on. In the DirectX driver (0x00883240, 0x00883310, 0x008833E0) the lit colour is the ambient material times the multiplier, where `EnableVertexColors(diffuse, specular)` picks the ambient material (vertex colour if `diffuse`, else white); the lit alpha is the diffuse material's, which `EnableVertexColors` sets to the vertex colour if `specular` is set and to the material otherwise, `AlphaMultiplier(a < 1)` sets to the material (alpha `a`), and `AlphaMultiplier(1)` sets back to the vertex colour if the last `specular` was set. (Ghidra's decompile of 0x008833E0 shows a constant for that argument; the disassembly reads `[esp+0x10]`, the second argument.) Cloud shadows depend on it: they redraw the terrain with `ColorMultiplier(0, 0, 0.2)`, `AlphaMultiplier(0)` and an alpha test of 0.22, so with the material's alpha of 0 the stock driver draws almost nothing there. Vertices without a colour use the material.
- **Textures.** `CreateTexture(format, w, h, levels, hints)` returns a handle (formats RGB5, RGB8, RGBA4, RGB5_A1, RGBA8, DXT1 with alpha, DXT3, DXT5); `LoadTextureLevel(texture, level, x, y, w, h, format, type, rowLength, pixels)` uploads (source formats RGB, RGBA, BGR, BGRA, alpha, luminance, luminance-alpha, DXT1/1A/3/5). `SetTexture(texture, stage)`, `GetTexture(stage)`; the GL-style `GenTextures`/`BindTexture`/`TexImage2D` exist but were not called in our runs. `TexParameter` 0-3 are min filter, mag filter, wrap S, wrap T, per stage.
- **Texture stages.** `TexStage(n)` selects the stage that `Enable(Texture2D)`, `TexEnv`, `TexParameter`, `TexStageCoord`, `TexStageMatrix` and `TexStageCombine` apply to. `TexEnv(int)` sets the GL texture env mode (replace, modulate, decal, blend, combine); `TexEnv(float*)` sets the constant colour (Direct3D's texture factor). `SetCombiner(combiner, stage)` takes a 10-byte `cGDCombiner`: RGB op, three RGB args, alpha op, three alpha args, RGB and alpha scale; an arg's low nibble is the source (texture, previous, constant, primary colour), the high nibble the operand (colour, one minus colour, alpha, one minus alpha); RGB op 0xFF leaves the stage alone. `TexStageCoord(n)`: the low 3 bits pick the coordinate set, 0x10/0x20/0x30 generate coordinates from the camera-space position, normal or reflection vector.
- **State.** `Enable`/`Disable`: 0 alpha test, 1 depth test, 2 stencil test, 3 culling (clockwise faces), 4 blending, 5 texturing on the current stage, 6 fog, 7 nothing. Depth, alpha and stencil functions, stencil ops, blend factors and shade model are GL's lists in GL's order. `PolygonOffset(n)` is a depth bias: the DirectX driver (0x00883710) sets Direct3D 7's `D3DRENDERSTATE_ZBIAS` (0-16, higher draws in front) to `n`. The game calls it about once per object, always with 0 in the test city.
- **Fog.** The DirectX driver supports only vertex fog, and only when the device reports it: `Enable(6)` sets `D3DRENDERSTATE_FOGENABLE`, `Fog(mode, value)` sets `D3DRENDERSTATE_FOGVERTEXMODE` (exp, exp2, linear), `Fog(param, float*)` sets the colour, density, start and end (0x00883100, 0x00883180). Neither driver received a fog call in the test city, with `RenderCloudsAndFog` on or off: the clouds are drawn as textured sprites and terrain passes, not with fog.
- **Errors.** Methods set an error code that `GetError` returns and clears (DirectX driver: 1 bad enum, 2 bad value, 3 unsupported, 6 failed mode set).
- **Not implemented by either driver:** the six blits (`BitBlt`, `StretchBlt` and their alpha variants) only set an error. `GetBoolean` and `GetFloat` are empty in the DirectX driver; `GetInteger` answers only the unpack row length.
- **Buffer regions** (`cIGZGBufferRegionExtension`): `NewBufferRegion(surface)`, with 0 front, 1 back, 2 depth, returns a handle (from 1) to a surface-sized copy; `ReadBufferRegion(h, x, y, w, h, rx, ry)` copies a rectangle of the surface to (rx, ry) in the region, and `DrawBufferRegion(h, x, y, w, h, rx, ry)` copies (rx, ry, w, h) of the region back to (x, y) of the same surface. Top-left origin. The game uses them to restore parts of the frame instead of redrawing them (about 16,600 draws per 120 s).
- **Snapshots** (`cIGZGSnapshotExtension`): `CopyColorBuffer(x, y, w, h, buffer)` copies part of the front buffer into a 32-bit `cIGZBuffer`, creating one (class 0xC470D325) if `buffer` is null, and returns it.
- **Dynamic vertex buffers** (0x09CD86F9, a separate object; DirectX driver vtable 0x00AC4F10 over Direct3D 7 vertex buffers): slot 3 `GetBuffer(vertexFormat)` returns a handle (always 0 in the DirectX driver), 4 the buffer's format, 5 its capacity in vertices (64 KB worth: 2,048 of the 32-byte format 0xB), 6 `Lock(buffer, count)` reserves `count` vertices after the previous batch, starting over at 0 when they do not fit, starts a new batch and returns a pointer to write them to, 7 `Append(buffer, count)` extends the locked batch, 8 `Unlock`, 9 `Draw(buffer, primitive)` and 10 `DrawIndexed(buffer, primitive, count, uint16 indices)` draw the batch with the current state (indices relative to its first vertex), 11 resets every buffer. The terrain (0x007545C0) and the scene renderer (0x00593E10) draw through it, about 180,000 queries per 90 s shots run; the stock OpenGL driver does not offer it, which is one reason it draws no terrain.
- **Texture transforms.** `TexStageMatrix(m, inputs, outputs, flags)`: the terrain shadow projection uses camera-space coordinates (`TexStageCoord(0x10)`) with (4 inputs, 2 outputs, flag 1). The DirectX driver negates the matrix's third row in that case (and for 3 inputs without flag 1); in our driver the matrix as given matches the stock output better, so Direct3D 7's camera space evidently has the opposite z. The OpenGL driver instead generates only s and t from the eye position.

## D3D11 driver

`renderer.d3d11` ([src/core/gd](../src/core/gd)) is OpenSC4's own driver. The driver ([driver.cpp](../src/core/gd/driver.cpp)) keeps the game's state the way the DirectX driver does and calls a backend interface ([backend.h](../src/core/gd/backend.h)); the D3D11 backend ([d3d11_backend.cpp](../src/core/gd/d3d11_backend.cpp), shaders in [d3d11_shaders.h](../src/core/gd/d3d11_shaders.h)) is the only one so far, and a Vulkan one can sit beside it.

**It takes over the OpenGL driver's class ID (0xC4554841), like scvk and SCGL, and is selected whenever it is on** (Dan's choice for now, 2026-10-06; later a menu option): a detour on the driver choice (`cSC4App::SelectGraphicsDriver`, 0x0044CE40) requests it when no `-d:` option names a driver, then restores the saved choice the game would otherwise overwrite, so turning the driver off leaves the user's settings as they were. `-d:DirectX` still picks the stock driver. The first version was made current at PreAppInit with `SetGDriver`, like the pass-through driver, and drew the UI correctly but no terrain or model textures: the game creates 3D textures through a driver pointer it cached before PreAppInit, so they went to the stock DirectX driver and the handles it then bound on ours were unknown. Created by the graphics system at startup, the driver gets everything. It lists its video modes as the DirectX driver does (see "Resolutions"). Full-screen modes change the display mode as the OpenGL driver does (0x0087FC90: width and height only, `CDS_FULLSCREEN`, the desktop's settings back when the mode goes) and use a popup window; a mode the size of the desktop leaves the display alone, which makes it borderless full screen. Switching away from the game gives the desktop its resolution back until the game is active again. Showing a smaller mode scaled on an unchanged desktop is left for later: the game calls `GetCursorPos`/`ScreenToClient` and `SetCursorPos`/`ClientToScreen` itself, so mouse coordinates would need translating in those places too. Tested unattended: 1024x768 full screen on a 1920x1080 desktop matches the windowed run in all 20 shots, and 1920x1080 full screen renders (the game needs `-CustomResolution:enabled` for that size, with either driver; `OPENSC4_GAME_WINDOW="-f -CustomResolution:enabled -r1920x1080x32"` replaces the scripts' default `-w`).

- The back buffer is a texture of our own that keeps its contents; `Flush` copies it to a front-buffer texture, which the window shows as described under "Frame pacing". Buffer regions and snapshots work on these textures; depth regions and clears go through a small full-screen shader, because Direct3D 11 cannot copy part of a depth buffer.
- Drawing: vertices and indices go through ring buffers (the game's arrays are only valid during the call); fans, quads and quad strips become indexed lists; elements a vertex format lacks come from a stride-0 buffer of defaults. One vertex shader does the transforms, the ambient-only lighting, texture coordinate generation and texture matrices, and per-vertex fog; one pixel shader (and a flat-shaded twin) runs up to 8 texture stages of GL texture environment modes or combiners, the alpha test against the game's byte reference, and fog. Input layouts, samplers, blend, depth-stencil and rasterizer states are cached. Textures are BGRA8, or BC1-3 for DXT data that fills whole blocks; uploads are converted on the CPU ([pixels.cpp](../src/core/gd/pixels.cpp)).
- The dynamic vertex buffers keep CPU copies and draw through the same path.
- `d3d11.dll` and `d3dcompiler_47.dll` are loaded only when the driver starts; `debug_layer = on` in `opensc4.ini`'s `[renderer]` section (or `OPENSC4_RENDERER_DEBUG_LAYER=on`) turns on the debug layer. The log records the first occurrence of each primitive, vertex format, texture format, upload format, sampler and stage setup (up to 64 of each).

Texture stages follow the DirectX driver's Direct3D setup (0x00884320, 0x00884500) rather than GL's: the first stage that is off, lacks a texture or uses the blend env mode (which that driver does not implement) ends the cascade; filters map as it maps them, including its `LinearMipmapNearest`, which a missing `break` turns into point-filtered with linear mipmaps; textures created as RGB5 or RGB8 sample an alpha of 1 whatever the uploads hold; modulate on a texture without alpha keeps the current alpha.

**Status: renders the city like the stock driver.** Against the stock DirectX driver in the screenshot harness (2026-10-06, Making Money Tutorial), all 20 shots pass `--tolerance 32 --max-differing 1.2`, at most 0.6% of pixels off by more than 32, all of them deer, which move in real time. With `RenderCloudsAndFog=1` (which the harness turns off because clouds drift in real time), 18 of 20 pass and the two others (zoom 1) differ where the clouds have moved; before the diffuse-source fix above, our driver drew cloud shadows as opaque dark-blue blobs. The tree shadows long had a dark halo that no rendering change fixed: the game drew them differently for our driver, because the driver reported weaker capabilities than the stock one (see "What the game asks the driver" below). `PolygonOffset` maps a z-bias of `n` to a depth bias of `-n` units plus a slope-scaled bias of `-n`, as Wine's OpenGL backend maps `ZBIAS` (`glPolygonOffset(-n, -n)`); unverified, because the test city never sets one. Fog follows Direct3D 7 vertex fog (camera-space depth) and is logged on first use; also unverified, for the same reason. Not done yet: `TexImage2D` (not called). Menus, the region view and UI interaction have not been tried on it yet ([run-game.sh](../scripts/run-game.sh) plays the game in the test user folder).

### Resolutions

The game picks a mode from its video preferences (saved in `SimCity 4.cfg`) or `-r<width>x<height>x<depth>`, `-w` and `-f`; with the command line, sizes outside its own list need `-CustomResolution:enabled` (with either driver, or it quits at startup), which is how players get widescreen sizes.

**Mode list.** The DirectX driver lists the desktop's resolution first, then every other resolution the display offers, each as a full-screen and a windowed mode, all at 32 bits and then all again at 16 bits (144 modes on the AW3423DW; seen with `diagnostics.d3d7_probe`). The D3D11 driver lists them the same way (96 here), so the game picks the same mode indices (11 for 1024x768 windowed, 58 for 1024x768 16-bit full screen). It must: when no mode matches the preferences the game crashes before setting one (0x0098E9A6, an object at + 0xE4 missing). The first version listed only 32-bit modes and, like the OpenGL driver, windowed modes smaller than the desktop, and crashed with a saved 16-bit full-screen preference, which the graphics rules recommend on cards they do not recognise. The driver renders in 32 bits whatever the mode says; in full screen it changes only the display's size (Windows offers no 16-bit desktop modes).

**Settings.** `opensc4.ini` `[renderer]` `resolution` (`WxH` or `desktop`) and `window` (`windowed`, `fullscreen` or `borderless`) override the game's video preferences in memory just before it picks its driver and mode (in the `cSC4App::SelectGraphicsDriver` detour), as sc4-graphics-options does to the saved preferences; colour depth becomes 32 bits. `borderless` is full screen at the desktop's size, which needs no display mode change. No `-CustomResolution` is needed this way, and the game does not write the overridden values to `SimCity 4.cfg` (checked: the next start sees its own preferences again). Unset, the game's own options apply.

**The >2048 px crash is in the stock DirectX driver, not the game.** At 3440x1440 full screen (desktop size, 2026-10-06, Alienware AW3423DW) the stock driver crashes at 0x00884FE1 in its `Disable`: the per-stage state array at driver + 0x11C is null, so its device setup failed silently and the first state change writes through a null pointer. The D3D11 driver runs the same city at 3440x1440 with no problem (20 screenshots and a full test run), so with it the cap is gone.

A windowed size larger than the desktop crashes in game code instead (0x0098E9A6, an object at + 0xE4 missing; tried 2000x1000 and 2560x1440 on a 1920x1080 desktop), which matters only for testing: the window cannot be bigger than the screen anyway.

### Frame pacing

`Flush` is not a frame boundary. The framework's main loop (0x008795BD) never waits while the game runs: it handles window messages, then calls the framework's idle step (0x0087A812), which ticks every tick service (the simulation among them) and every fourth pass the on-idle services, among them the window manager's update (0x0098E5C0), which draws the software cursor and calls `Flush` when the view changed. How often that happens depends on what the loop is doing (2026-10-06, D3D11 driver, logged every 10 s):

| Phase | Flushes per second | Frames shown per second (175 Hz display) |
|---|---|---|
| Start-up and loading screens | about 20,000 | 172-174 |
| City running at speed 2 | about 15 | about 20-30 (front-buffer updates count too) |

So while the simulation runs, the game itself redraws the city about 15 times a second; the stock driver's total flush count over the same run is similar. Changing the on-idle interval (`cIGZFrameWork::SetOnIdleInterval`, default 4) to 1 or 16 made no difference to either rate or to the simulation.

Waiting for vertical sync in `Flush` starves that loop: in a 60 s run at speed 2, a blocking `Present(1)` showed 7.5 frames per second and the city reached 2010-01-25, where the stock driver reached 2011-12-09. So the driver paces what the window shows, not the game: a flip-model swap chain with a frame-latency waitable object (maximum latency 1); `Flush` copies the back buffer to the front buffer and presents it only if the waitable object is already signalled, else leaves it for the next `Flush` (or a 10 ms window timer, should the game stop flushing). The CPU is kept at most 3 flushes ahead of the GPU with event queries, since nothing else blocks (the GPU is not the limit: about 20 us of waiting per flush). Same 60 s run: the city reaches 2011-12-16, as with the stock driver. `vsync = off` in `opensc4.ini`'s `[renderer]` section (or `OPENSC4_RENDERER_VSYNC=off`) presents every `Flush` at once (tearing allowed where supported); without flip-model support (before Windows 8.1) the driver falls back to a blt-model swap chain presenting every `Flush`.

**The game caps its frame rate by simulation speed** (what sc4-disable-fps-limits patches): `cSC4Simulator`'s speed setter (0x00702430, from `SetSimSpeed` and `Init`) stores 30, 20 or 15 frames per second for turtle, rhino and cheetah in the simulator at + 0x84 (the interface pointer's offset; a cheat, message 0x0AC07E24, also sets it). The only reader (0x00703EDD, found with a hardware read watchpoint; 0x00703E70 reads it as + 0x7C from a base 8 bytes further on) turns it into the simulation's time slice per frame: 1000 / cap ms, or 1000 / 30 under a condition at + 0x28 / + 0x2C, minus the time a timer at + 0x58 measured, at least 15 ms, stored at + 0x70. So the cap splits each loop between simulating and drawing. Measured in the test city at cheetah for 60 s (D3D11 driver, the field set after `SetSimSpeed`):

| Cap | Redraws per second | Simulation reached |
|---|---|---|
| 15 (the game's) | 15 | 2011-12-08 |
| 30 | 29 | 2011-12-01 |
| 60 | 57 | 2011-11-29 |
| 255 (sc4-disable-fps-limits) | 57 | 2011-12-02 |

Above about 57 the 15 ms minimum slice is the limit. In this small city the simulation does not use its whole slice, so four times the redraws cost almost nothing; a large city would lose simulation speed (not measured: the test region has only the tutorial cities).

Not changed: the loop never sleeps, so the game keeps a CPU core busy even when paused, as with the stock driver. Lowering that means changing the framework loop (for example waiting when paused), not the driver. Smoother motion while the simulation runs would need the simulation and drawing taken apart, which is far beyond the renderer.

### What the game asks the driver

The game configures itself from `Graphics Rules.sgr` (in the install folder), evaluated against values it collects at startup (0x007CF780): `textureStages`, `DXT`, the driver and card strings, memory, CPU speed and so on. The rules then decide, among other things, whether shadows use a second texture stage (`useSecondStage`, rule "Two stages": at least 2 stages), texture depth (`texturebits`, 16 under "No DXT"), the recommended shadow, effects and traffic levels ("Fast card": card names such as `NVidia*Geforce*`) and dirty-rectangle merging. Two driver calls feed it:

- **`GetVideoModeInfo` for the current mode.** Beyond the size and masks, `sGDMode` carries capabilities (field names from gzcom-dll's `sGDMode.h`): stencil (0x21), texture stage count (0x24), multitexture, texture env combine, NV combine4 and fog coordinate support (0x28-0x2B), DXT support (0x2C) and an initialised flag (0x30). The DirectX driver fills them only in the current mode; on this machine it reports 8 stages, multitexture, combine, fog coordinates, DXT and stencil, no combine4, an alpha mask of 0 for 32-bit modes, and 1 in the unknown bytes 0x22 and 0x2F.
- **`GetDriverInfo`.** Newline-separated: `Maxis 3D GDriver`, then sglDriverName, sglDriverVersion, driverName, driverVersion, cardName and cardVersion (the DirectX driver: `DirectX`, `2.0`, the display driver DLL, its version and device GUID, the adapter name, `Vendor: 10de, Device: 2208, Board: 153510de, Chipset: 00a1`).

Our driver first reported 1 texture stage, no DXT and empty strings (the mode fields had been copied from the OpenGL driver's list without being decoded). The game then left out the second shadow stage and drew shadows on models in a separate pass, which produced the halo, chose 16-bit textures and lower recommended settings. It now reports what the DirectX driver reports, with the real adapter from DXGI and `DirectX` as sglDriverName (what the rules were tuned on), and only the driver name and version lines name OpenSC4. **To check:** run the game with `-writeLog:on` (for example `OPENSC4_GAME_ARGS=-writeLog:on`): it writes `<computer name>-config-log.txt` next to the exe with every collected value and every rule applied. With either driver the logs now differ only in those two lines.

`diagnostics.d3d7_probe` ([d3d7_probe.cpp](../src/core/d3d7_probe.cpp)), used with the stock driver, logs the Direct3D 7 state the DirectX driver actually sets on its draws (render states, texture stage states, material, texture surface format), its `GetDriverInfo` and its mode records: the ground truth when our driver and the stock one disagree.
