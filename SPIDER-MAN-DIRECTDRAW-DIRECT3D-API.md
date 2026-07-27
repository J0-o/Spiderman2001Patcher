# Spider-Man DirectDraw/Direct3D API Research

## Scope

This is a static Ghidra analysis of:

- File: `exe/SpideyPC.exe`
- Size: 1,507,328 bytes
- SHA-256: `0A11A49F3F63D4BB15650F322D654553FC47234B01885B852A058FF074689DEA`
- Image base: `0x00400000`

The executable uses DirectDraw 7 and Direct3D 7. DirectDraw has only two
normal DLL imports; the rest of the graphics API is called indirectly through
COM vtables.

This inventory covers calls made by `SpideyPC.exe`. It does not claim to cover
DirectDraw calls made internally by third-party DLLs such as `binkw32.dll`.

## High-level initialization

The main graphics initialization is in `FUN_004fedd0` at `0x004FEDD0`:

1. Enumerate all DirectDraw adapters.
2. Create `IDirectDraw7`.
3. Query video-memory and device information.
4. Enumerate display modes.
5. Select exclusive fullscreen or windowed cooperative behavior.
6. Create primary, back-buffer/offscreen, clipper, and optional Z-buffer
   surfaces.
7. Query `IDirect3D7` from `IDirectDraw7`.
8. Enumerate Direct3D devices and Z-buffer formats.
9. Create `IDirect3DDevice7`.
10. Enumerate texture formats and initialize fixed-function render state.

Important global interface pointers identified by their use:

| Address | Interface/role |
|---|---|
| `0x006B7900` | `IDirectDraw7` |
| `0x006B7904` | Primary DirectDraw surface |
| `0x006B7908` | Back-buffer or windowed rendering surface |
| `0x006B790C` | Z-buffer surface |
| `0x006B7914` | `IDirectDrawClipper` |
| `0x006B7918` | `IDirect3D7` |
| `0x006B791C` | `IDirect3DDevice7` |

## Resolution selection and reset

The game has two separate resolution pairs:

| Address pair | Role |
|---|---|
| `0x006B78E4` / `0x006B78E8` | Active physical/rendering width and height |
| `0x00568154` / `0x00568158` | Active internal screen-coordinate width and height |
| `0x02E096F8` / `0x02E0970C` | Width and height loaded from `Spidey.cfg` |

`FUN_00515980` initializes the active pairs to 640x480 before movies and
DirectDraw startup. Changing only its immediates affects that early path.

After the shell and movies initialize, `FUN_00455C90` calls
`FUN_00500250` at `0x00455EBB` with the `Spidey.cfg` pair. That function
overwrites both active resolution pairs and recreates the renderer. It explains
why an early-resolution patch can affect movies while gameplay returns to the
configured or fallback resolution.

The associated gameplay reset sequence is:

- `0x00455EAC`: load selected height for the renderer reset;
- `0x00455EB2`: load selected width for the renderer reset;
- `0x00455EC0` / `0x00455EC6`: rebuild screen bounds;
- `0x00455EDA` / `0x00455EE0`: rebuild the screen center.

If `Spidey.cfg` is absent, `FUN_00515680` defaults its pair to 640x480. A
display-adapter identity mismatch in `FUN_00515520` can also reset the pair to
640x480 and save it. During a failed D3D recreation, `FUN_00500250` restores
the previous active dimensions and retries with a 16-bit color depth; it does
not independently select a lower width or height.

## DLL exports called

| Export | Call site | Purpose |
|---|---:|---|
| `DirectDrawEnumerateExA` | `0x004FEE15` | Enumerates adapters. The flags value is `7`, requesting attached, detached, and non-display devices. |
| `DirectDrawCreateEx` | `0x004FEF7B` | Creates `IDirectDraw7`; the executable contains and passes `IID_IDirectDraw7`. |

A minimal proxy DLL must export these two functions. A generally compatible
`DDRAW.dll` proxy should still forward the complete standard export table so
other DLLs in the process are not broken.

## `IDirectDraw7` methods used

The vtable mapping follows the published DirectDraw 7 ABI.

| Offset | Method | Observed use |
|---:|---|---|
| `0x00` | `QueryInterface` | Requests `IDirect3D7`. |
| `0x08` | `Release` | Graphics shutdown and failed-initialization cleanup. |
| `0x10` | `CreateClipper` | Creates the windowed-mode clipper. |
| `0x18` | `CreateSurface` | Creates primary, back/offscreen, Z-buffer, texture, and temporary surfaces. |
| `0x20` | `EnumDisplayModes` | Discovers supported resolutions and pixel formats. |
| `0x50` | `SetCooperativeLevel` | Selects normal/windowed or exclusive fullscreen operation. |
| `0x54` | `SetDisplayMode` | Sets width, height, and color depth. Some setup paths request 24-bit output. |
| `0x5C` | `GetAvailableVidMem` | Queries available local video memory. |
| `0x6C` | `GetDeviceIdentifier` | Obtains adapter/driver identification. |

No calls to `RestoreDisplayMode`, `WaitForVerticalBlank`, `GetScanLine`, or
mode-test methods were found.

## `IDirectDrawSurface7` methods used

| Offset | Method | Observed use |
|---:|---|---|
| `0x08` | `Release` | Releases render targets, Z-buffer, textures, and temporary surfaces. |
| `0x0C` | `AddAttachedSurface` | Attaches the Z-buffer to the rendering surface. |
| `0x14` | `Blt` | Presents/copies the rendered image, clears/copies surfaces, and uploads textures from a locked temporary surface. |
| `0x20` | `DeleteAttachedSurface` | Detaches the Z-buffer during reset or shutdown. |
| `0x30` | `GetAttachedSurface` | Retrieves the back buffer from a flipping primary surface. |
| `0x64` | `Lock` | Writes texture, video, and Bink frame data into surfaces. |
| `0x6C` | `Restore` | Recovers a lost texture/surface after a failed lock. |
| `0x70` | `SetClipper` | Attaches the windowed-mode clipper to the primary surface. |
| `0x80` | `Unlock` | Completes CPU writes to a locked surface. |

The fullscreen path uses a complex/flipping primary surface and retrieves its
attached back buffer. The windowed path renders to an offscreen surface and
blits it to a clipped primary surface.

The retail texture loader in `FUN_0050f6d0` creates and locks a temporary
offscreen surface, writes the converted texture pixels, creates the final
`DDSCAPS_TEXTURE` surface, and calls `destinationTexture->Blt` from the
temporary surface. A replacement must preserve that surface-to-surface CPU
copy; implementing the call only as presentation leaves the final texture
empty.

The game writes `16` into its display-mode filter's `dwRGBBitCount` but does
not enable `DDSD_PIXELFORMAT` in `dwFlags`. Although native DirectDraw normally
treats that field as inactive, exposing both 16- and 32-bit variants to this
game destabilizes its legacy display-mode path. The renderer therefore
preserves the requested 16-bit game-facing mode while presenting through a
32-bit `DXGI_FORMAT_R8G8B8A8_UNORM` swap chain. Changing the game's own
color-depth behavior belongs in a separate compatibility patch.

## `IDirectDrawClipper` methods used

| Offset | Method | Observed use |
|---:|---|---|
| `0x08` | `Release` | Shutdown/reset cleanup. |
| `0x20` | `SetHWnd` | Associates the clipper with the game window. |

## `IDirect3D7` methods used

`IDirect3D7` is obtained with `IDirectDraw7::QueryInterface` in
`FUN_004fe1b0` at `0x004FE1B0`.

| Offset | Method | Observed use |
|---:|---|---|
| `0x08` | `Release` | Shutdown/reset cleanup. |
| `0x0C` | `EnumDevices` | Enumerates Direct3D 7 hardware/software devices and their capabilities. |
| `0x10` | `CreateDevice` | Creates `IDirect3DDevice7` using the selected device and render target. |
| `0x18` | `EnumZBufferFormats` | Finds Z-buffer formats for the selected device. |

The Z-buffer callback accepts formats with `DDPF_ZBUFFER`, prefers a 16-bit
Z-buffer, and falls back to another enumerated depth if necessary.

No `CreateVertexBuffer` or `EvictManagedTextures` call was found.

## `IDirect3DDevice7` methods used

| Offset | Method | Observed use |
|---:|---|---|
| `0x08` | `Release` | Shutdown/reset cleanup. |
| `0x0C` | `GetCaps` | Reads the selected device's capabilities. |
| `0x10` | `EnumTextureFormats` | Finds compatible RGB/alpha surface formats. |
| `0x14` | `BeginScene` | Begins scene submission. |
| `0x18` | `EndScene` | Ends scene submission. |
| `0x28` | `Clear` | Clears the render target and/or depth buffer. |
| `0x34` | `SetViewport` | Sets a viewport matching the selected resolution. |
| `0x50` | `SetRenderState` | Configures the fixed-function render pipeline. |
| `0x64` | `DrawPrimitive` | Submits the game's geometry. |
| `0x8C` | `SetTexture` | Binds or unbinds the stage-zero texture. |
| `0x94` | `SetTextureStageState` | Configures stage-zero color, alpha, address, and filtering behavior. |

No uses of these device features were found:

- transforms or matrix multiplication;
- materials or lights;
- vertex buffers;
- indexed drawing;
- state blocks;
- user clip planes;
- multiple active texture stages.

### Geometry submission

The observed draw call is equivalent to:

```cpp
device->DrawPrimitive(
    D3DPT_TRIANGLEFAN,
    D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1,
    vertices,
    vertexCount,
    0);
```

The FVF value is `0x144`. Because `D3DFVF_XYZRHW` is used, the game submits
already transformed screen-space vertices. A targeted renderer does not need
to reproduce the Direct3D transform-and-lighting pipeline for this path.

### Render states

`FUN_00502220` at `0x00502220` initializes a broad set of Direct3D 7
fixed-function states. Confirmed state categories include:

- Z testing and Z writes;
- fill and shade mode;
- alpha testing and alpha blending;
- source/destination blend mode;
- culling and depth comparison;
- alpha reference/comparison;
- dithering;
- fog enable, color, mode, start, end, and density;
- specular and edge anti-aliasing flags;
- color keying;
- stencil states;
- texture factor;
- texture-coordinate wrapping;
- clipping, lighting, and color-vertex behavior;
- material-source selections and vertex blending.

Later rendering functions change a smaller subset dynamically, especially
Z-enable, alpha blending, source blend, culling, and alpha test.

### Texture-stage states

Only stage zero was observed. It configures:

- `D3DTSS_COLOROP`, `COLORARG1`, and `COLORARG2`;
- `D3DTSS_ALPHAOP`, `ALPHAARG1`, and `ALPHAARG2`;
- `D3DTSS_ADDRESSU` and `ADDRESSV`;
- magnification, minification, and mip filtering.

Texture-format enumeration accepts matching RGB/alpha bit-mask layouts. The
software/fallback path explicitly constructs a 16-bit ARGB 1:5:5:5 format:

- alpha mask: `0x8000`;
- red mask: `0x7C00`;
- green mask: `0x03E0`;
- blue mask: `0x001F`.

## Video playback interaction

The executable imports these Bink functions that interact with rendered video
data:

- `BinkDDSurfaceType`;
- `BinkCopyToBuffer`.

The game locks a DirectDraw surface, asks Bink to copy a decoded frame into the
buffer, and unlocks the surface. A wrapper therefore needs correct surface
pitch, pixel-format reporting, lock, unlock, and blit behavior for video
playback even though Bink is not itself a DirectDraw COM interface.

## Minimum targeted wrapper surface area

A Spider-Man-specific translation layer needs functional implementations of:

1. the two imported DirectDraw exports;
2. the nine `IDirectDraw7` methods listed above;
3. the nine `IDirectDrawSurface7` methods listed above;
4. the two clipper methods listed above;
5. the four `IDirect3D7` methods listed above;
6. the ten `IDirect3DDevice7` methods listed above;
7. COM identity/reference-count behavior for every returned object.

Methods outside that set can be forwarded to another DirectDraw implementation.
If this is a standalone replacement, every COM vtable slot must still exist
and return a valid result such as `E_NOTIMPL` or the appropriate DirectDraw
error; the vtable cannot be physically shortened.

## 1280x720 patch source mapping

The added `spidey-decomp-master` source confirms the resolution paths observed
in Ghidra:

- `SpideyDX.cpp` initializes both the game and DirectDraw dimensions to
  `640x480` in `RealWinMain`.
- `DXinit.cpp::initDirectDraw7` assigns `640x480` again if both DirectDraw
  dimensions are zero.
- `DXinit.cpp::initDirect3D7` creates its Z buffer and viewport using the
  DirectDraw dimensions.
- `DXinit.cpp::DXINIT_DirectX8` retries Direct3D initialization at 16-bit color
  if the first initialization fails.

The `resolution-1280x720` patch therefore covers both startup constants and
the empty-resolution DirectDraw defaults. The latter map to:

- `0x004FF37C`: `MOV ECX, 640` -> `MOV ECX, 1280`;
- `0x004FF381`: `MOV EAX, 480` -> `MOV EAX, 720`.

Live verification showed these instructions installed as
`B9 00 05 00 00 B8 D0 02 00 00` before DirectDraw initialization.

## Confidence and verification limits

Confidence is high for the interface identities and method-slot mappings:

- the executable embeds `IID_IDirectDraw7`;
- it embeds and requests `IID_IDirect3D7`;
- call arguments and surrounding resource creation match the published DX7
  interfaces;
- all uses of the identified persistent interface globals were traced in
  Ghidra;
- texture/resource creation functions were checked for additional surface
  method calls.

Static analysis can miss a call selected only through unusual runtime data
flow, and it does not inspect calls internal to `binkw32.dll`. Before declaring
a standalone wrapper complete, run an instrumented forwarding proxy and log
every invoked vtable slot through:

- startup in windowed and fullscreen modes;
- gameplay in multiple levels;
- pause/menu transitions;
- Bink video playback;
- Alt-Tab or forced surface loss;
- resolution reset and normal shutdown.

That runtime trace is the final verification step for the static inventory.
