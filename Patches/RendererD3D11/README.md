# Spider-Man D3D11 renderer

This Win32 DLL replaces the DirectDraw 7 and Direct3D 7 interfaces used by
`SpideyPC.exe` with a game-specific Direct3D 11 implementation.

The renderer is distributed as the DLL-only `renderer-d3d11.kpatch`. When the
patch runtime loads it, the DLL replaces the executable's
`DirectDrawEnumerateExA` and `DirectDrawCreateEx` import-table entries. It does
not install a `ddraw.dll` proxy and does not depend on dgVoodoo or another
translation layer.

## Current milestone

Implemented:

- all required COM vtable slots for `IDirectDraw7`, `IDirectDrawSurface7`,
  `IDirectDrawClipper`, `IDirect3D7`, and `IDirect3DDevice7`;
- adapter, display-mode, D3D device, Z-buffer, and texture-format enumeration;
- game-compatible legacy display-mode enumeration;
- primary, back-buffer, offscreen, texture, and Z-buffer surface objects;
- CPU surface `Lock` and `Unlock`;
- D3D11 device, swap chain, color/depth clear, and presentation;
- the game's requested 16-bit depth-buffer precision;
- window-message dispatch during windowed presentation;
- conversion of the game's 16-, 24-, and 32-bit surface formats to RGBA8
  textures when the native GPU format is unavailable;
- direct shader-resource uploads for supported RGB565, ARGB1555, and ARGB4444
  textures, retaining their original 16-bit VRAM footprint;
- the game's temporary-surface-to-texture `Blt` upload path;
- transformed/lit `D3DPT_TRIANGLEFAN` submission through a dynamic D3D11
  vertex buffer;
- viewport, Z-buffer, blend, alpha-test, cull, texture-address, and
  texture-filter state translation;
- offscreen-surface upload and full-window blitting for the Bink movie path;
- texture re-upload after a display-mode or D3D11 device reset.

Not implemented yet:

- fog state translation;
- destination/source rectangle scaling for partial `Blt` operations;
- GPU-to-CPU readback when locking a rendered surface;
- game-level validation of every scene, menu, and Bink playback path.

## Build and smoke test

Build the renderer and test as Win32:

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
$env:SKIP_PAUSE = "1"
cmd /c ..\create-patch.bat
& $msbuild .\tests\RendererSmokeTest.vcxproj /p:Configuration=Debug /p:Platform=Win32
.\tests\bin\Debug\RendererSmokeTest.exe .\windows_x86.dll
```

The smoke test loads the patch DLL, verifies the executable import replacement,
creates the DirectDraw/Direct3D compatibility objects, uploads an ARGB4444
texture through the same temporary-surface `Blt` path used by the game, submits
a textured triangle fan, presents it through D3D11, recreates the device at a
second display mode, and draws again with the existing texture.
