#pragma once

#include <Windows.h>
#include <d3d.h>
#include <d3d11.h>
#include <ddraw.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace Spidey
{
    class DirectDraw7;
    class Direct3D7;

    class DirectDrawClipper final : public IDirectDrawClipper
    {
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;
        HRESULT STDMETHODCALLTYPE GetClipList(LPRECT, LPRGNDATA, LPDWORD) override;
        HRESULT STDMETHODCALLTYPE GetHWnd(HWND* window) override;
        HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW, DWORD) override;
        HRESULT STDMETHODCALLTYPE IsClipListChanged(BOOL*) override;
        HRESULT STDMETHODCALLTYPE SetClipList(LPRGNDATA, DWORD) override;
        HRESULT STDMETHODCALLTYPE SetHWnd(DWORD, HWND window) override;

    private:
        std::atomic<ULONG> m_references{1};
        HWND m_window = nullptr;
    };

    class DirectDrawSurface7 final : public IDirectDrawSurface7
    {
    public:
        DirectDrawSurface7(DirectDraw7* owner, const DDSURFACEDESC2& description);
        ~DirectDrawSurface7();

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;
        HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE7 surface) override;
        HRESULT STDMETHODCALLTYPE AddOverlayDirtyRect(LPRECT) override;
        HRESULT STDMETHODCALLTYPE Blt(LPRECT destination, LPDIRECTDRAWSURFACE7 source, LPRECT sourceRect, DWORD flags, LPDDBLTFX effects) override;
        HRESULT STDMETHODCALLTYPE BltBatch(LPDDBLTBATCH, DWORD, DWORD) override;
        HRESULT STDMETHODCALLTYPE BltFast(DWORD, DWORD, LPDIRECTDRAWSURFACE7, LPRECT, DWORD) override;
        HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD, LPDIRECTDRAWSURFACE7 surface) override;
        HRESULT STDMETHODCALLTYPE EnumAttachedSurfaces(LPVOID, LPDDENUMSURFACESCALLBACK7) override;
        HRESULT STDMETHODCALLTYPE EnumOverlayZOrders(DWORD, LPVOID, LPDDENUMSURFACESCALLBACK7) override;
        HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE7, DWORD) override;
        HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS2 caps, LPDIRECTDRAWSURFACE7* surface) override;
        HRESULT STDMETHODCALLTYPE GetBltStatus(DWORD) override;
        HRESULT STDMETHODCALLTYPE GetCaps(LPDDSCAPS2 caps) override;
        HRESULT STDMETHODCALLTYPE GetClipper(LPDIRECTDRAWCLIPPER* clipper) override;
        HRESULT STDMETHODCALLTYPE GetColorKey(DWORD, LPDDCOLORKEY) override;
        HRESULT STDMETHODCALLTYPE GetDC(HDC*) override;
        HRESULT STDMETHODCALLTYPE GetFlipStatus(DWORD) override;
        HRESULT STDMETHODCALLTYPE GetOverlayPosition(LPLONG, LPLONG) override;
        HRESULT STDMETHODCALLTYPE GetPalette(LPDIRECTDRAWPALETTE*) override;
        HRESULT STDMETHODCALLTYPE GetPixelFormat(LPDDPIXELFORMAT format) override;
        HRESULT STDMETHODCALLTYPE GetSurfaceDesc(LPDDSURFACEDESC2 description) override;
        HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW, LPDDSURFACEDESC2) override;
        HRESULT STDMETHODCALLTYPE IsLost() override;
        HRESULT STDMETHODCALLTYPE Lock(LPRECT, LPDDSURFACEDESC2 description, DWORD, HANDLE) override;
        HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override;
        HRESULT STDMETHODCALLTYPE Restore() override;
        HRESULT STDMETHODCALLTYPE SetClipper(LPDIRECTDRAWCLIPPER clipper) override;
        HRESULT STDMETHODCALLTYPE SetColorKey(DWORD, LPDDCOLORKEY) override;
        HRESULT STDMETHODCALLTYPE SetOverlayPosition(LONG, LONG) override;
        HRESULT STDMETHODCALLTYPE SetPalette(LPDIRECTDRAWPALETTE) override;
        HRESULT STDMETHODCALLTYPE Unlock(LPRECT) override;
        HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD, LPDDOVERLAYFX) override;
        HRESULT STDMETHODCALLTYPE UpdateOverlayDisplay(DWORD) override;
        HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD, LPDIRECTDRAWSURFACE7) override;
        HRESULT STDMETHODCALLTYPE GetDDInterface(LPVOID*) override;
        HRESULT STDMETHODCALLTYPE PageLock(DWORD) override;
        HRESULT STDMETHODCALLTYPE PageUnlock(DWORD) override;
        HRESULT STDMETHODCALLTYPE SetSurfaceDesc(LPDDSURFACEDESC2, DWORD) override;
        HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, LPVOID, DWORD, DWORD) override;
        HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, LPVOID, LPDWORD) override;
        HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override;
        HRESULT STDMETHODCALLTYPE GetUniquenessValue(LPDWORD value) override;
        HRESULT STDMETHODCALLTYPE ChangeUniquenessValue() override;
        HRESULT STDMETHODCALLTYPE SetPriority(DWORD priority) override;
        HRESULT STDMETHODCALLTYPE GetPriority(LPDWORD priority) override;
        HRESULT STDMETHODCALLTYPE SetLOD(DWORD lod) override;
        HRESULT STDMETHODCALLTYPE GetLOD(LPDWORD lod) override;

        const DDSURFACEDESC2& Description() const { return m_description; }
        HRESULT GetTextureView(ID3D11ShaderResourceView** view);
        bool HasCpuData() const { return !m_cpuData.empty(); }

    private:
        bool MatchesCaps(const DDSCAPS2& caps) const;
        void SetBackBuffer(DirectDrawSurface7* surface);
        void ReleaseGpuTexture();

        std::atomic<ULONG> m_references{1};
        DirectDraw7* m_owner = nullptr;
        DDSURFACEDESC2 m_description{};
        DirectDrawSurface7* m_backBuffer = nullptr;
        DirectDrawSurface7* m_zBuffer = nullptr;
        IDirectDrawClipper* m_clipper = nullptr;
        std::vector<std::uint8_t> m_cpuData;
        ID3D11Texture2D* m_gpuTexture = nullptr;
        ID3D11ShaderResourceView* m_textureView = nullptr;
        bool m_textureDirty = true;
        std::uint64_t m_textureGeneration = 0;
        DWORD m_priority = 0;
        DWORD m_lod = 0;
        DWORD m_uniqueness = 1;

        friend class DirectDraw7;
    };

    class Direct3DDevice7 final : public IDirect3DDevice7
    {
    public:
        Direct3DDevice7(Direct3D7* owner, DirectDrawSurface7* renderTarget);
        ~Direct3DDevice7();

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;
        HRESULT STDMETHODCALLTYPE GetCaps(LPD3DDEVICEDESC7 caps) override;
        HRESULT STDMETHODCALLTYPE EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context) override;
        HRESULT STDMETHODCALLTYPE BeginScene() override;
        HRESULT STDMETHODCALLTYPE EndScene() override;
        HRESULT STDMETHODCALLTYPE GetDirect3D(LPDIRECT3D7* direct3D) override;
        HRESULT STDMETHODCALLTYPE SetRenderTarget(LPDIRECTDRAWSURFACE7, DWORD) override;
        HRESULT STDMETHODCALLTYPE GetRenderTarget(LPDIRECTDRAWSURFACE7*) override;
        HRESULT STDMETHODCALLTYPE Clear(DWORD, LPD3DRECT, DWORD flags, D3DCOLOR color, D3DVALUE, DWORD) override;
        HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE, LPD3DMATRIX) override;
        HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE, LPD3DMATRIX) override;
        HRESULT STDMETHODCALLTYPE SetViewport(LPD3DVIEWPORT7 viewport) override;
        HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE, LPD3DMATRIX) override;
        HRESULT STDMETHODCALLTYPE GetViewport(LPD3DVIEWPORT7 viewport) override;
        HRESULT STDMETHODCALLTYPE SetMaterial(LPD3DMATERIAL7) override;
        HRESULT STDMETHODCALLTYPE GetMaterial(LPD3DMATERIAL7) override;
        HRESULT STDMETHODCALLTYPE SetLight(DWORD, LPD3DLIGHT7) override;
        HRESULT STDMETHODCALLTYPE GetLight(DWORD, LPD3DLIGHT7) override;
        HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override;
        HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE state, LPDWORD value) override;
        HRESULT STDMETHODCALLTYPE BeginStateBlock() override;
        HRESULT STDMETHODCALLTYPE EndStateBlock(LPDWORD) override;
        HRESULT STDMETHODCALLTYPE PreLoad(LPDIRECTDRAWSURFACE7) override;
        HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE, DWORD, LPVOID, DWORD, DWORD) override;
        HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE, DWORD, LPVOID, DWORD, LPWORD, DWORD, DWORD) override;
        HRESULT STDMETHODCALLTYPE SetClipStatus(LPD3DCLIPSTATUS) override;
        HRESULT STDMETHODCALLTYPE GetClipStatus(LPD3DCLIPSTATUS) override;
        HRESULT STDMETHODCALLTYPE DrawPrimitiveStrided(D3DPRIMITIVETYPE, DWORD, LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD, DWORD) override;
        HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveStrided(D3DPRIMITIVETYPE, DWORD, LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD, LPWORD, DWORD, DWORD) override;
        HRESULT STDMETHODCALLTYPE DrawPrimitiveVB(D3DPRIMITIVETYPE, LPDIRECT3DVERTEXBUFFER7, DWORD, DWORD, DWORD) override;
        HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveVB(D3DPRIMITIVETYPE, LPDIRECT3DVERTEXBUFFER7, DWORD, DWORD, LPWORD, DWORD, DWORD) override;
        HRESULT STDMETHODCALLTYPE ComputeSphereVisibility(LPD3DVECTOR, LPD3DVALUE, DWORD, DWORD, LPDWORD) override;
        HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage, LPDIRECTDRAWSURFACE7* texture) override;
        HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, LPDIRECTDRAWSURFACE7 texture) override;
        HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE state, LPDWORD value) override;
        HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value) override;
        HRESULT STDMETHODCALLTYPE ValidateDevice(LPDWORD passes) override;
        HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD) override;
        HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD) override;
        HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD) override;
        HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE, LPDWORD) override;
        HRESULT STDMETHODCALLTYPE Load(LPDIRECTDRAWSURFACE7, LPPOINT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD) override;
        HRESULT STDMETHODCALLTYPE LightEnable(DWORD, BOOL) override;
        HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD, BOOL*) override;
        HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD, D3DVALUE*) override;
        HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD, D3DVALUE*) override;
        HRESULT STDMETHODCALLTYPE GetInfo(DWORD, LPVOID, DWORD) override;

    private:
        std::atomic<ULONG> m_references{1};
        Direct3D7* m_owner = nullptr;
        DirectDrawSurface7* m_renderTarget = nullptr;
        DirectDrawSurface7* m_texture = nullptr;
        D3DVIEWPORT7 m_viewport{};
        DWORD m_renderStates[256]{};
        DWORD m_textureStates[32]{};
    };

    class Direct3D7 final : public IDirect3D7
    {
    public:
        explicit Direct3D7(DirectDraw7* owner);
        ~Direct3D7();

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;
        HRESULT STDMETHODCALLTYPE EnumDevices(LPD3DENUMDEVICESCALLBACK7 callback, LPVOID context) override;
        HRESULT STDMETHODCALLTYPE CreateDevice(REFCLSID, LPDIRECTDRAWSURFACE7 renderTarget, LPDIRECT3DDEVICE7* device) override;
        HRESULT STDMETHODCALLTYPE CreateVertexBuffer(LPD3DVERTEXBUFFERDESC, LPDIRECT3DVERTEXBUFFER7*, DWORD) override;
        HRESULT STDMETHODCALLTYPE EnumZBufferFormats(REFCLSID, LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context) override;
        HRESULT STDMETHODCALLTYPE EvictManagedTextures() override;

    private:
        std::atomic<ULONG> m_references{1};
        DirectDraw7* m_owner = nullptr;
    };

    class DirectDraw7 final : public IDirectDraw7
    {
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;
        HRESULT STDMETHODCALLTYPE Compact() override;
        HRESULT STDMETHODCALLTYPE CreateClipper(DWORD, LPDIRECTDRAWCLIPPER* clipper, IUnknown*) override;
        HRESULT STDMETHODCALLTYPE CreatePalette(DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE*, IUnknown*) override;
        HRESULT STDMETHODCALLTYPE CreateSurface(LPDDSURFACEDESC2 description, LPDIRECTDRAWSURFACE7* surface, IUnknown*) override;
        HRESULT STDMETHODCALLTYPE DuplicateSurface(LPDIRECTDRAWSURFACE7, LPDIRECTDRAWSURFACE7*) override;
        HRESULT STDMETHODCALLTYPE EnumDisplayModes(DWORD, LPDDSURFACEDESC2 filter, LPVOID context, LPDDENUMMODESCALLBACK2 callback) override;
        HRESULT STDMETHODCALLTYPE EnumSurfaces(DWORD, LPDDSURFACEDESC2, LPVOID, LPDDENUMSURFACESCALLBACK7) override;
        HRESULT STDMETHODCALLTYPE FlipToGDISurface() override;
        HRESULT STDMETHODCALLTYPE GetCaps(LPDDCAPS, LPDDCAPS) override;
        HRESULT STDMETHODCALLTYPE GetDisplayMode(LPDDSURFACEDESC2 description) override;
        HRESULT STDMETHODCALLTYPE GetFourCCCodes(LPDWORD, LPDWORD) override;
        HRESULT STDMETHODCALLTYPE GetGDISurface(LPDIRECTDRAWSURFACE7*) override;
        HRESULT STDMETHODCALLTYPE GetMonitorFrequency(LPDWORD frequency) override;
        HRESULT STDMETHODCALLTYPE GetScanLine(LPDWORD) override;
        HRESULT STDMETHODCALLTYPE GetVerticalBlankStatus(LPBOOL) override;
        HRESULT STDMETHODCALLTYPE Initialize(GUID*) override;
        HRESULT STDMETHODCALLTYPE RestoreDisplayMode() override;
        HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND window, DWORD flags) override;
        HRESULT STDMETHODCALLTYPE SetDisplayMode(DWORD width, DWORD height, DWORD bitsPerPixel, DWORD, DWORD) override;
        HRESULT STDMETHODCALLTYPE WaitForVerticalBlank(DWORD, HANDLE) override;
        HRESULT STDMETHODCALLTYPE GetAvailableVidMem(LPDDSCAPS2, LPDWORD total, LPDWORD free) override;
        HRESULT STDMETHODCALLTYPE GetSurfaceFromDC(HDC, LPDIRECTDRAWSURFACE7*) override;
        HRESULT STDMETHODCALLTYPE RestoreAllSurfaces() override;
        HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override;
        HRESULT STDMETHODCALLTYPE GetDeviceIdentifier(LPDDDEVICEIDENTIFIER2 identifier, DWORD) override;
        HRESULT STDMETHODCALLTYPE StartModeTest(LPSIZE, DWORD, DWORD) override;
        HRESULT STDMETHODCALLTYPE EvaluateMode(DWORD, DWORD*) override;

    private:
        std::atomic<ULONG> m_references{1};
    };
}

extern "C"
{
    HRESULT WINAPI SpideyDirectDrawCreateEx(GUID* deviceGuid, LPVOID* directDraw, REFIID iid, IUnknown* outer);
    HRESULT WINAPI SpideyDirectDrawEnumerateExA(LPDDENUMCALLBACKEXA callback, LPVOID context, DWORD flags);
}
