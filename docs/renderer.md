# Renderer (P1)

Notes for replacing the game's graphics driver. Build 641, Steam, 2026-10-05.

## How the game gets its driver

- The game talks to graphics hardware only through `cIGZGDriver`, an 80-method interface (83 vtable slots with `cIGZUnknown`; gzcom-dll's [cIGZGDriver.h](../third_party/gzcom-dll/gzcom-dll/include/cIGZGDriver.h) matches the DirectX driver's vtable at 0x00AC4660 slot for slot).
- The graphics system (`cIGZGraphicSystem2`, service 0xC416025C) creates exactly the three stock drivers between core.dll's director `OnStart` and `PreAppInit`: Software 0x7ACA35C6, DirectX 0xBADB6906 and OpenGL 0xC4554841. It makes one current (DirectX here).
- `cSC4App` keeps the requested driver's class ID at offset 0x65C from the object start (`cISC4App` − 0xC, `cIGZApp` − 0x2C), set from the `-d:` option by 0x0044EED0. 0x0044CE40 passes it to `cIGZGraphicSystem2::SetDefaultDriverClassID`.
- **A new class ID is never instantiated.** With the field and `SetDefaultDriverClassID` set to an unknown ID from `OnStart`, the graphics system still created only the three stock drivers and chose DirectX, and never asked core.dll's director for the class. GZCOM sends a director requests only for classes it registered, so nothing else would reach it either. This is why scvk and SCGL take over the OpenGL ID instead.

## Installing our driver: `SetGDriver`

`cIGZGraphicSystem2::SetGDriver(cIGZGDriver*)` sets the current driver object directly. At `PreAppInit` the drivers exist but the game has not set its video mode or loaded anything; a driver made current there receives `SetVideoMode` and everything after it. No class ID is taken (so no collision with scvk, SCGL or render-services' need for the DirectX driver) and nothing is patched.

`renderer.passthrough` ([gdriver_proxy.cpp](../src/core/gdriver_proxy.cpp), registry kind `api`, phase `PreAppInit`) wraps the current driver this way and forwards every call. With it on, a 120 s autotest runs the city at the same pace as without it (2015-04-03 and 2015-04-05 at the end) and quits cleanly.

Not seen through the proxy: calls made before `PreAppInit` (the driver's `Init`, anything the graphics system does while creating drivers). Code that looks a driver up with `GetGDriverByID` gets the original.

## Other driver interfaces

The DirectX driver's `QueryInterface` (0x00882A00) also serves four other interfaces; the OpenGL driver's (0x0087D670) serves the same set, so a replacement driver has to provide them. Names come from the Mac build's debug symbols (sc4-ghidra-symbols types, where the DirectX driver is `nSGLDX7::cGDriver`), matched by method count, `this` offset and what the Windows code does. The proxy wraps all four with [interface_counter](../src/core/interface_counter.h), which counts calls by vtable slot without needing the signatures.

| IID | Interface | Where | Methods | 60 s run |
|---|---|---|---|---|
| 0x669565FE | `cIGZGBufferRegionExtension` | the driver object (vtable 0x00AC47EC) | 8 (then 7 more of the class's own) | queried once (0x007C3810, kept in 0x00B4E1F0); NewBufferRegion 2, ReadBufferRegion 1,408, DrawBufferRegion 70,070 |
| 0xE69BFE2A | `cIGZGSnapshotExtension` | object + 4 (0x00AC47DC) | 1: `CopyColorBuffer` | queried 2, CopyColorBuffer 1 |
| 0x87E2B87D | `cIGZGDriverLightingExtension` | object + 8 (0x00AC47AC) | 9 | never queried |
| 0x09CD86F9 | unidentified | separate object held at + 0x1C (vtable 0x00AC4F10) | 9 | queried 32,890 times (0x00593E10, 0x007545C0); slots 3 and 4 once per query, slot 5 595,514, slot 6 562,628 |

- **Buffer regions** (slot: method, Windows DirectX implementation): 3 `BufferRegionEnabled` (true), 4 `NewBufferRegion(buffer)` (0 front, 1 back, 2 depth: creates an off-screen DirectDraw surface, returns a handle), 5 `DeleteBufferRegion`, 6 `ReadBufferRegion` (7 arguments), 7 `DrawBufferRegion`, 8 `IsBufferRegion`, 9 `CanDoPartialRegionWrites` and 10 `CanDoOffsetReads` (both true). Saving and restoring screen areas, probably for the UI.
- **Lighting**: `EnableLighting`, `EnableLight`, `LightModelAmbient`, `LightColor` (two overloads), `LightPosition`, `LightDirection`, `MaterialColor` (two overloads). The Mac order is GCC's; MSVC may order the overloads differently.
- **0x09CD86F9**: slot 3 always returns 0 in the DirectX driver; slots 4 and 5 read fields of 0x30-byte records by index; slot 6 flushes something. The Mac symbols' interfaces do not match it.

## render-services

[sc4-render-services](https://github.com/caspervg/sc4-render-services) 0.7.1 runs normally in the autotest, and crashes the game as soon as `renderer.passthrough` is on. Its ImGui service gets the current driver (`GetGDriver`), checks `GetGZCLSID() == 0xBADB6906`, then reads a pointer at offset 0x24C of the `cIGZGDriver` pointer: the DirectX driver's internal Direct3D helper (`cISGLDX7D3DX`, which gives it the DirectDraw and Direct3D 7 device), whose `EndScene` it hooks to draw ImGui. The proxy forwards `GetGZCLSID`, so the check passes, and offset 0x24C of the proxy is not that field ([DX7InterfaceHook.cpp](https://github.com/caspervg/sc4-render-services/blob/v0.7.1/src/DX7InterfaceHook.cpp), `CaptureInterface`). Its other services (draw passes, camera, terrain decals) do not touch the driver.

Fix: the proxy copies the DirectX driver's pointer at 0x24C to the same offset in itself, at install and on every `GetGZCLSID` call (render-services calls it right before reading, on every tick). With that, a 60 s run with render-services and the proxy runs normally and quits cleanly. Not verified yet: that its ImGui overlay actually draws (its log, which would say so, goes to the real `Documents\SimCity 4` or to the debugger). If mirroring causes trouble, the fallback is to report a class ID other than DirectX's, which makes render-services skip its overlay.

Testing it: its installer puts `imgui.dll` in the game's `Apps` folder and `SC4RenderServices.dll` in Plugins. Instead, the DLLs were unpacked from the installer with 7-Zip, `SC4RenderServices.dll` went into the test `Plugins` (with `LogToFile=false`: its log otherwise goes to the real `Documents\SimCity 4` whatever `-UserDir` says, and `ShowDemoPanel=true` so ImGui draws every frame), and `imgui.dll` was loaded with `OPENSC4_TEST_PRELOAD`.

## What the game calls

Making Money Tutorial, 120 s at speed 2, autotest window (no menus, no UI interaction), counted from `PreAppInit` on. 42 of the 80 methods were called:

| Method | Calls | Method | Calls |
|---|---|---|---|
| TexParameter | 13,750,422 | AlphaMultiplier | 861,305 |
| IsDeviceReady | 7,588,383 | TexStageCoord | 693,452 |
| SetTexture | 6,465,102 | MatrixMode | 576,892 |
| InterleavedArrays | 6,298,854 | Disable | 456,264 |
| DrawElements | 6,140,535 | Enable | 336,537 |
| DrawArrays | 4,899,384 | AlphaFunc | 325,113 |
| TexEnv (int) | 4,734,993 | TexEnv (float) | 222,781 |
| TexStage | 4,345,710 | EnableVertexColors | 219,733 |
| ColorMultiplier | 1,296,560 | DepthFunc | 208,296 |
| TexStageMatrix | 1,057,824 | SetCombiner | 170,495 |
| ColorMask | 1,021,161 | DepthMask | 122,924 |
| Flush | 1,018,608 | BlendFunc | 71,603 |
| Clear | 981,999 | LoadIdentity | 36,684 |
| ClearColor | 981,902 | LoadTextureLevel | 3,657 |
| LoadMatrix | 926,824 | SetViewport (both) | 1,290 each |

Rarely called: GetVideoModeInfo (139 by index, 8 current), CreateTexture 102, DeleteTextures 90, GetViewport 3, GetDriverInfo 3, CountVideoModes 2, ClearDepth 2, GetError 2, SetVideoMode 1, PolygonOffset 1.

Never called in this run: the blits (`BitBlt`, `StretchBlt` and their alpha variants), the GL-style texture calls (`GenTextures`, `BindTexture`, `TexImage2D`), stencil, fog, `ShadeModel`, `PixelStore`, the `Get*` queries, the vertex format helpers, `GetTexture`, `Punt`. UI screens and menus may use some of them; the call log of a run with UI interaction will tell.

Observations to follow up: about 8,000 `Clear`/`Flush` pairs per second in a background window, and `TexParameter` on most draws.
