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

The DirectX driver's `QueryInterface` (0x00882A00) also serves four other interfaces; the OpenGL driver's (0x0087D670) serves the same set, so a replacement driver has to provide them. Names come from the Mac build's debug symbols (sc4-ghidra-symbols types, where the DirectX driver is `nSGLDX7::cGDriver`), matched by method count, `this` offset and what the Windows code does. The proxy wraps all four with [interface_counter](../src/core/interface_counter.h), which counts calls by vtable slot without needing the signatures.

| IID | Interface | Where | Methods | 120 s run |
|---|---|---|---|---|
| 0x669565FE | `cIGZGBufferRegionExtension` | the driver object (vtable 0x00AC47EC) | 8 (then 7 more of the class's own) | queried once (0x007C3810, kept in 0x00B4E1F0); NewBufferRegion 2, ReadBufferRegion 2,770, DrawBufferRegion 16,600 |
| 0xE69BFE2A | `cIGZGSnapshotExtension` | object + 4 (0x00AC47DC) | 1: `CopyColorBuffer` | queried 2, CopyColorBuffer 1 |
| 0x87E2B87D | `cIGZGDriverLightingExtension` | object + 8 (0x00AC47AC) | 9 | never queried |
| 0x09CD86F9 | unidentified | separate object held at + 0x1C (vtable 0x00AC4F10) | 9 | queried 225,137 times (0x00593E10, 0x007545C0); slot 3 once per query; slots 6 and 8 407,570 each, 10 404,965, 5 176,675, 7 59,164, 4 15,942 |

- **Buffer regions** (slot: method, Windows DirectX implementation): 3 `BufferRegionEnabled` (true), 4 `NewBufferRegion(buffer)` (0 front, 1 back, 2 depth: creates an off-screen DirectDraw surface, returns a handle), 5 `DeleteBufferRegion`, 6 `ReadBufferRegion` (7 arguments), 7 `DrawBufferRegion`, 8 `IsBufferRegion`, 9 `CanDoPartialRegionWrites` and 10 `CanDoOffsetReads` (both true). Saving and restoring screen areas, probably for the UI.
- **Lighting**: `EnableLighting`, `EnableLight`, `LightModelAmbient`, `LightColor` (two overloads), `LightPosition`, `LightDirection`, `MaterialColor` (two overloads). The Mac order is GCC's; MSVC may order the overloads differently.
- **0x09CD86F9**: slot 3 always returns 0 in the DirectX driver; slots 4 and 5 read fields of 0x30-byte records by index; slot 6 flushes something. The Mac symbols' interfaces do not match it.

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

### gzcom-dll: overloaded virtuals are in the wrong order

MSVC, and clang-cl with it, puts overloaded virtual functions in the vtable in reverse declaration order. gzcom-dll's `cISC43DRender.h` declares `TakeSnapshot(cIGZString&, bool)` before `TakeSnapshot(cIGZBuffer**, bool)`, the order of the exe's slots 22 and 23, so the compiled call to the string overload goes to slot 23, the buffer overload: it would pass a string where a `cIGZBuffer**` is expected. Checked with clang-cl's assembly (`+0x5C` for the string call) against the exe's vtable at 0x00ABAB60 (slot 22 is 0x007C2570, which itself calls slot 23 with a buffer pointer). The autotest calls slot 22 directly. Other overloads in gzcom-dll headers (`StartScreenShake`, `SetBackgroundPicture`, `PickTerrain`, `Pick` here; others elsewhere) may have the same problem; worth reporting upstream once checked.
