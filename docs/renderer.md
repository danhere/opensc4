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

- **Window.** `SetVideoMode(mode, wndproc, ?, ?)` creates the driver's own ANSI window ("GDriverClass (DirectX)": `WS_OVERLAPPEDWINDOW | WS_VISIBLE` sized to the mode's client area for windowed modes, a popup otherwise), stores the driver in `GWLP_USERDATA` and sends every message to `wndproc`. Mode -1 destroys it. `SetGDriver` calls `SetVideoMode` itself when a mode is already set. Modes are `sGDMode` records of 0x34 bytes (index, width, height, bits per pixel, A/R/G/B masks, a full-screen flag at 0x20); the DirectX driver lists 120 here, every desktop resolution twice (full screen and windowed).
- **Frames.** `Flush` presents: `glFlush` + `SwapBuffers`, or the DirectX driver's blit to the primary surface. The back buffer keeps its contents between frames, which the game relies on: it redraws only what changed. `Clear(mask)` takes 0x4000 colour, 0x1000 depth, 0x2000 stencil and clears the current viewport (a Direct3D 7 clear), with the values from `ClearColor`/`ClearDepth`/`ClearStencil`. `ColorMask` is a no-op in the DirectX driver.
- **Viewport.** GL coordinates, origin bottom left: the DirectX driver clamps to the surface and converts with `top = height - y - h`; `GetViewport` returns `{x, y, x + w, y + h}` in the same coordinates. Direct3D 7 viewports clip, so a viewport also acts as a scissor rectangle (the OpenGL driver sets `glScissor` with it).
- **Matrices.** `MatrixMode` 0 model-view, 1 projection; GL layout. The DirectX driver loads the model-view as the world matrix (view = identity) and rewrites the projection for Direct3D (depth range and half-pixel offset, which is why it reloads it on every viewport change).
- **Vertices.** `InterleavedArrays(format, stride, data)` only records the pointer (stride 0: the format's); `DrawArrays(prim, first, count)` and `DrawElements(prim, count, type, indices)` read from it, with unsigned byte, short or int indices. Formats are bit fields of per-element counts (position, normal, up to 3 colours, 1D-4D texture coordinates), with legacy IDs for common layouts ([vertex_format.h](../src/core/gd/vertex_format.h)). **Colours are D3DCOLOR (BGRA bytes)**: the OpenGL driver swaps red and blue in the caller's array before each draw and back after. Primitives: triangles, strips, fans, points, lines, line strips, quads, quad strips; the DirectX driver turns strips, fans and quads into indexed lists.
- **Colour.** `ColorMultiplier(r, g, b)` and `AlphaMultiplier(a)` switch on lighting with only an ambient light, and leave it on. In the DirectX driver (0x00883240, 0x00883310, 0x008833E0) the lit colour is the ambient material times the multiplier, where `EnableVertexColors(diffuse, specular)` picks the ambient material (vertex colour if `diffuse`, else white); the lit alpha is the diffuse material's, which `EnableVertexColors` sets to the vertex colour whatever its arguments and `AlphaMultiplier(a < 1)` sets to the material (alpha `a`). Vertices without a colour use the material.
- **Textures.** `CreateTexture(format, w, h, levels, hints)` returns a handle (formats RGB5, RGB8, RGBA4, RGB5_A1, RGBA8, DXT1 with alpha, DXT3, DXT5); `LoadTextureLevel(texture, level, x, y, w, h, format, type, rowLength, pixels)` uploads (source formats RGB, RGBA, BGR, BGRA, alpha, luminance, luminance-alpha, DXT1/1A/3/5). `SetTexture(texture, stage)`, `GetTexture(stage)`; the GL-style `GenTextures`/`BindTexture`/`TexImage2D` exist but were not called in our runs. `TexParameter` 0-3 are min filter, mag filter, wrap S, wrap T, per stage.
- **Texture stages.** `TexStage(n)` selects the stage that `Enable(Texture2D)`, `TexEnv`, `TexParameter`, `TexStageCoord`, `TexStageMatrix` and `TexStageCombine` apply to. `TexEnv(int)` sets the GL texture env mode (replace, modulate, decal, blend, combine); `TexEnv(float*)` sets the constant colour (Direct3D's texture factor). `SetCombiner(combiner, stage)` takes a 10-byte `cGDCombiner`: RGB op, three RGB args, alpha op, three alpha args, RGB and alpha scale; an arg's low nibble is the source (texture, previous, constant, primary colour), the high nibble the operand (colour, one minus colour, alpha, one minus alpha); RGB op 0xFF leaves the stage alone. `TexStageCoord(n)`: the low 3 bits pick the coordinate set, 0x10/0x20/0x30 generate coordinates from the camera-space position, normal or reflection vector.
- **State.** `Enable`/`Disable`: 0 alpha test, 1 depth test, 2 stencil test, 3 culling (clockwise faces), 4 blending, 5 texturing on the current stage, 6 fog, 7 nothing. Depth, alpha and stencil functions, stencil ops, blend factors and shade model are GL's lists in GL's order. `PolygonOffset(n)` is a depth bias.
- **Errors.** Methods set an error code that `GetError` returns and clears (DirectX driver: 1 bad enum, 2 bad value, 3 unsupported, 6 failed mode set).
- **Not implemented by either driver:** the six blits (`BitBlt`, `StretchBlt` and their alpha variants) only set an error. `GetBoolean` and `GetFloat` are empty in the DirectX driver; `GetInteger` answers only the unpack row length.
- **Buffer regions** (`cIGZGBufferRegionExtension`): `NewBufferRegion(surface)`, with 0 front, 1 back, 2 depth, returns a handle (from 1) to a surface-sized copy; `ReadBufferRegion(h, x, y, w, h, rx, ry)` copies a rectangle of the surface to (rx, ry) in the region, and `DrawBufferRegion(h, x, y, w, h, rx, ry)` copies (rx, ry, w, h) of the region back to (x, y) of the same surface. Top-left origin. The game uses them to restore parts of the frame instead of redrawing them (about 16,600 draws per 120 s).
- **Snapshots** (`cIGZGSnapshotExtension`): `CopyColorBuffer(x, y, w, h, buffer)` copies part of the front buffer into a 32-bit `cIGZBuffer`, creating one (class 0xC470D325) if `buffer` is null, and returns it.
- **Dynamic vertex buffers** (0x09CD86F9, a separate object; DirectX driver vtable 0x00AC4F10 over Direct3D 7 vertex buffers): slot 3 `GetBuffer(vertexFormat)` returns a handle (always 0 in the DirectX driver), 4 the buffer's format, 5 its capacity in vertices (64 KB worth: 2,048 of the 32-byte format 0xB), 6 `Lock(buffer, count)` reserves `count` vertices after the previous batch, starting over at 0 when they do not fit, starts a new batch and returns a pointer to write them to, 7 `Append(buffer, count)` extends the locked batch, 8 `Unlock`, 9 `Draw(buffer, primitive)` and 10 `DrawIndexed(buffer, primitive, count, uint16 indices)` draw the batch with the current state (indices relative to its first vertex), 11 resets every buffer. The terrain (0x007545C0) and the scene renderer (0x00593E10) draw through it, about 180,000 queries per 90 s shots run; the stock OpenGL driver does not offer it, which is one reason it draws no terrain.
- **Texture transforms.** `TexStageMatrix(m, inputs, outputs, flags)`: the terrain shadow projection uses camera-space coordinates (`TexStageCoord(0x10)`) with (4 inputs, 2 outputs, flag 1). The DirectX driver negates the matrix's third row in that case (and for 3 inputs without flag 1); in our driver the matrix as given matches the stock output better, so Direct3D 7's camera space evidently has the opposite z. The OpenGL driver instead generates only s and t from the eye position.

## D3D11 driver

`renderer.d3d11` ([src/core/gd](../src/core/gd)) is OpenSC4's own driver. The driver ([driver.cpp](../src/core/gd/driver.cpp)) keeps the game's state the way the DirectX driver does and calls a backend interface ([backend.h](../src/core/gd/backend.h)); the D3D11 backend ([d3d11_backend.cpp](../src/core/gd/d3d11_backend.cpp), shaders in [d3d11_shaders.h](../src/core/gd/d3d11_shaders.h)) is the only one so far, and a Vulkan one can sit beside it.

**It takes over the OpenGL driver's class ID (0xC4554841), like scvk and SCGL, and the game selects it with `-d:OpenGL`** (`run-autotest.sh` adds that when `OPENSC4_REPLACE` turns the driver on). The first version was made current at PreAppInit with `SetGDriver`, like the pass-through driver, and drew the UI correctly but no terrain or model textures: the game creates 3D textures through a driver pointer it cached before PreAppInit, so they went to the stock DirectX driver and the handles it then bound on ours were unknown. Created by the graphics system at startup, the driver gets everything. It lists its video modes the way the OpenGL driver does (every desktop resolution, plus a windowed one for each smaller than the desktop; 38 here).

- The back buffer is a texture of our own that keeps its contents; `Flush` copies it to a front-buffer texture and to the swap chain (DXGI blt model) and presents without waiting for vertical sync. Buffer regions and snapshots work on these textures; depth regions and clears go through a small full-screen shader, because Direct3D 11 cannot copy part of a depth buffer.
- Drawing: vertices and indices go through ring buffers (the game's arrays are only valid during the call); fans, quads and quad strips become indexed lists; elements a vertex format lacks come from a stride-0 buffer of defaults. One vertex shader does the transforms, the ambient-only lighting, texture coordinate generation and texture matrices, and per-vertex fog; one pixel shader (and a flat-shaded twin) runs up to 8 texture stages of GL texture environment modes or combiners, the alpha test against the game's byte reference, and fog. Input layouts, samplers, blend, depth-stencil and rasterizer states are cached. Textures are BGRA8, or BC1-3 for DXT data that fills whole blocks; uploads are converted on the CPU ([pixels.cpp](../src/core/gd/pixels.cpp)).
- The dynamic vertex buffers keep CPU copies and draw through the same path.
- `d3d11.dll` and `d3dcompiler_47.dll` are loaded only when the driver starts; `OPENSC4_D3D11_DEBUG=1` turns on the debug layer. The log records the first occurrence of each primitive, vertex format, texture format, upload format, sampler and stage setup (up to 64 of each).

Texture stages follow the DirectX driver's Direct3D setup (0x00884320, 0x00884500) rather than GL's: the first stage that is off, lacks a texture or uses the blend env mode (which that driver does not implement) ends the cascade; filters map as it maps them, including its `LinearMipmapNearest`, which a missing `break` turns into point-filtered with linear mipmaps; textures created as RGB5 or RGB8 sample an alpha of 1 whatever the uploads hold; modulate on a texture without alpha keeps the current alpha.

**Status: renders the city.** Against the stock DirectX driver in the screenshot harness (2026-10-06, Making Money Tutorial), everything matches pixel for pixel except the soft outer ring of tree shadows, where ours is darker by about 15% (ours is close to the stock value times its own shadow factor, as if the soft part were applied twice). The shadows are a 128x128 DXT3 texture with 5 mip levels, projected from camera-space position (`TexStageCoord(0x10)` with a texture matrix), modulated and alpha-blended. Magnified (zoom 5), the stock driver's shadow edge is hard and stepped along shadow texels, as if its alpha were cut at about one half, where ours falls off smoothly as the DXT3 alpha does. The shadow pass redraws the terrain with depth test less-or-equal, no depth writes and no polygon offset. Ruled out so far: the texture matrix (the stock driver's z negation makes it worse), vertex alpha and lighting sources, mipmapping (one level is worse), the stage setup above, depth (the pass writes none), storing DXT textures as A1R5G5B5 or A4R4G4B4 as a Direct3D 7 device without DXT formats would (worse, also for DXT3/DXT5 alone), and the driver class ID the game sees (DirectX's makes no difference). The game also has an alpha-tested shadow texture path (16x16 and 32x32 textures, reference 0.467) that would give such edges; which one the stock driver's frames come from is the next thing to find out. 9 of 20 shots pass `--tolerance 32 --max-differing 1.2`; the others have 1.2-3.5% of pixels off by more than 32, all in those rings. Not done yet: the shadow rings, `PolygonOffset` units (guessed at 256 depth-bias units per step), fog (implemented but not seen in the test city), `TexImage2D` (not called), full-screen modes (shown in a window), and timing (presents without waiting, about 1,000 frames per second here).
