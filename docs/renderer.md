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
