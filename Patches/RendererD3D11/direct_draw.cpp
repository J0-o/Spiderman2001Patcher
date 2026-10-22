#define NOMINMAX
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700

#include "include/direct_draw.h"

#include "include/renderer_runtime.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace
{
    DDPIXELFORMAT MakeRgb565()
    {
        DDPIXELFORMAT format{};
        format.dwSize = sizeof(format);
        format.dwFlags = DDPF_RGB;
        format.dwRGBBitCount = 16;
        format.dwRBitMask = 0xF800;
        format.dwGBitMask = 0x07E0;
        format.dwBBitMask = 0x001F;
        return format;
    }

    DDPIXELFORMAT MakeArgb4444()
    {
        DDPIXELFORMAT format{};
        format.dwSize = sizeof(format);
        format.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
        format.dwRGBBitCount = 16;
        format.dwRGBAlphaBitMask = 0xF000;
        format.dwRBitMask = 0x0F00;
        format.dwGBitMask = 0x00F0;
        format.dwBBitMask = 0x000F;
        return format;
    }

    DDPIXELFORMAT MakeArgb1555()
    {
        DDPIXELFORMAT format{};
        format.dwSize = sizeof(format);
        format.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
        format.dwRGBBitCount = 16;
        format.dwRGBAlphaBitMask = 0x8000;
        format.dwRBitMask = 0x7C00;
        format.dwGBitMask = 0x03E0;
        format.dwBBitMask = 0x001F;
        return format;
    }

    DDPIXELFORMAT MakeArgb8888()
    {
        DDPIXELFORMAT format{};
        format.dwSize = sizeof(format);
        format.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
        format.dwRGBBitCount = 32;
        format.dwRGBAlphaBitMask = 0xFF000000;
        format.dwRBitMask = 0x00FF0000;
        format.dwGBitMask = 0x0000FF00;
        format.dwBBitMask = 0x000000FF;
        return format;
    }

    DDPIXELFORMAT MakeDepth16()
    {
        DDPIXELFORMAT format{};
        format.dwSize = sizeof(format);
        format.dwFlags = DDPF_ZBUFFER;
        format.dwZBufferBitDepth = 16;
        return format;
    }

    void FillDeviceCaps(D3DDEVICEDESC7& caps)
    {
        std::memset(&caps, 0, sizeof(caps));
        caps.deviceGUID = IID_IDirect3DHALDevice;
        caps.dwDevCaps = D3DDEVCAPS_FLOATTLVERTEX |
                         D3DDEVCAPS_DRAWPRIMTLVERTEX |
                         D3DDEVCAPS_EXECUTESYSTEMMEMORY |
                         D3DDEVCAPS_TEXTUREVIDEOMEMORY;
        caps.dpcTriCaps.dwSize = sizeof(caps.dpcTriCaps);
        caps.dpcTriCaps.dwMiscCaps = D3DPMISCCAPS_CULLNONE |
                                    D3DPMISCCAPS_CULLCW |
                                    D3DPMISCCAPS_CULLCCW;
        caps.dpcTriCaps.dwRasterCaps = D3DPRASTERCAPS_DITHER |
                                      D3DPRASTERCAPS_FOGVERTEX |
                                      D3DPRASTERCAPS_ZTEST;
        caps.dpcTriCaps.dwZCmpCaps = D3DPCMPCAPS_ALWAYS |
                                    D3DPCMPCAPS_LESSEQUAL;
        caps.dpcTriCaps.dwSrcBlendCaps = 0xFFFFFFFF;
        caps.dpcTriCaps.dwDestBlendCaps = 0xFFFFFFFF;
        caps.dpcTriCaps.dwAlphaCmpCaps = 0xFFFFFFFF;
        caps.dpcTriCaps.dwShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB |
                                     D3DPSHADECAPS_ALPHAGOURAUDBLEND |
                                     D3DPSHADECAPS_FOGGOURAUD;
        caps.dpcTriCaps.dwTextureCaps = D3DPTEXTURECAPS_ALPHA |
                                       D3DPTEXTURECAPS_PERSPECTIVE |
                                       D3DPTEXTURECAPS_TRANSPARENCY;
        caps.dpcTriCaps.dwTextureFilterCaps = D3DPTFILTERCAPS_NEAREST |
                                             D3DPTFILTERCAPS_LINEAR;
        caps.dpcTriCaps.dwTextureBlendCaps = 0xFFFFFFFF;
        caps.dpcTriCaps.dwTextureAddressCaps = D3DPTADDRESSCAPS_WRAP |
                                              D3DPTADDRESSCAPS_CLAMP;
        caps.dwDeviceRenderBitDepth = DDBD_16 | DDBD_32;
        caps.dwDeviceZBufferBitDepth = DDBD_16 | DDBD_24;
        caps.dwMaxTextureWidth = 4096;
        caps.dwMaxTextureHeight = 4096;
        caps.dwMaxTextureRepeat = 8192;
        caps.dwMaxTextureAspectRatio = 4096;
        caps.dwMaxAnisotropy = 1;
        caps.dwTextureOpCaps = D3DTEXOPCAPS_DISABLE |
                              D3DTEXOPCAPS_SELECTARG1 |
                              D3DTEXOPCAPS_SELECTARG2 |
                              D3DTEXOPCAPS_MODULATE |
                              D3DTEXOPCAPS_MODULATE2X;
        caps.wMaxTextureBlendStages = 1;
        caps.wMaxSimultaneousTextures = 1;
        caps.dwMaxActiveLights = 0;
        caps.dvMaxVertexW = 1.0e10F;
        caps.deviceGUID = IID_IDirect3DHALDevice;
    }

    HRESULT Unsupported(const char* method)
    {
        OutputDebugStringA(method);
        OutputDebugStringA(" is not implemented by SpideyRenderer.\n");
        return E_NOTIMPL;
    }

    BYTE ExpandChannel(DWORD pixel, DWORD mask, BYTE defaultValue)
    {
        if (!mask)
        {
            return defaultValue;
        }

        DWORD shift = 0;
        while (((mask >> shift) & 1U) == 0)
        {
            ++shift;
        }
        const DWORD maximum = mask >> shift;
        const DWORD value = (pixel & mask) >> shift;
        return static_cast<BYTE>((value * 255U + maximum / 2U) / maximum);
    }
}

namespace Spidey
{
    HRESULT DirectDrawClipper::QueryInterface(REFIID riid, void** object)
    {
        if (!object)
        {
            return E_POINTER;
        }
        *object = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDirectDrawClipper))
        {
            *object = static_cast<IDirectDrawClipper*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG DirectDrawClipper::AddRef()
    {
        return ++m_references;
    }

    ULONG DirectDrawClipper::Release()
    {
        const ULONG references = --m_references;
        if (!references)
        {
            delete this;
        }
        return references;
    }

    HRESULT DirectDrawClipper::GetClipList(LPRECT, LPRGNDATA, LPDWORD)
    {
        return Unsupported("IDirectDrawClipper::GetClipList");
    }

    HRESULT DirectDrawClipper::GetHWnd(HWND* window)
    {
        if (!window)
        {
            return E_POINTER;
        }
        *window = m_window;
        return S_OK;
    }

    HRESULT DirectDrawClipper::Initialize(LPDIRECTDRAW, DWORD)
    {
        return S_OK;
    }

    HRESULT DirectDrawClipper::IsClipListChanged(BOOL* changed)
    {
        if (!changed)
        {
            return E_POINTER;
        }
        *changed = FALSE;
        return S_OK;
    }

    HRESULT DirectDrawClipper::SetClipList(LPRGNDATA, DWORD)
    {
        return Unsupported("IDirectDrawClipper::SetClipList");
    }

    HRESULT DirectDrawClipper::SetHWnd(DWORD, HWND window)
    {
        m_window = window;
        return S_OK;
    }

    DirectDrawSurface7::DirectDrawSurface7(
        DirectDraw7* owner,
        const DDSURFACEDESC2& description)
        : m_owner(owner),
          m_description(description)
    {
        m_owner->AddRef();
        m_description.dwSize = sizeof(m_description);

        if (!(m_description.dwFlags & DDSD_WIDTH))
        {
            m_description.dwWidth = RendererRuntime::Instance().Width();
            m_description.dwFlags |= DDSD_WIDTH;
        }
        if (!(m_description.dwFlags & DDSD_HEIGHT))
        {
            m_description.dwHeight = RendererRuntime::Instance().Height();
            m_description.dwFlags |= DDSD_HEIGHT;
        }
        if (!(m_description.dwFlags & DDSD_PIXELFORMAT))
        {
            m_description.ddpfPixelFormat =
                RendererRuntime::Instance().BitsPerPixel() <= 16
                    ? MakeRgb565()
                    : MakeArgb8888();
            m_description.dwFlags |= DDSD_PIXELFORMAT;
        }
    }

    DirectDrawSurface7::~DirectDrawSurface7()
    {
        ReleaseGpuTexture();
        if (m_backBuffer)
        {
            m_backBuffer->Release();
        }
        if (m_zBuffer)
        {
            m_zBuffer->Release();
        }
        if (m_clipper)
        {
            m_clipper->Release();
        }
        m_owner->Release();
    }

    HRESULT DirectDrawSurface7::QueryInterface(REFIID riid, void** object)
    {
        if (!object)
        {
            return E_POINTER;
        }
        *object = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDirectDrawSurface7))
        {
            *object = static_cast<IDirectDrawSurface7*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG DirectDrawSurface7::AddRef()
    {
        return ++m_references;
    }

    ULONG DirectDrawSurface7::Release()
    {
        const ULONG references = --m_references;
        if (!references)
        {
            delete this;
        }
        return references;
    }

    HRESULT DirectDrawSurface7::AddAttachedSurface(LPDIRECTDRAWSURFACE7 surface)
    {
        auto* attached = static_cast<DirectDrawSurface7*>(surface);
        if (!attached)
        {
            return DDERR_INVALIDPARAMS;
        }

        if (attached->Description().ddsCaps.dwCaps & DDSCAPS_ZBUFFER)
        {
            if (m_zBuffer)
            {
                m_zBuffer->Release();
            }
            m_zBuffer = attached;
            m_zBuffer->AddRef();
            return S_OK;
        }

        return DDERR_CANNOTATTACHSURFACE;
    }

    HRESULT DirectDrawSurface7::AddOverlayDirtyRect(LPRECT)
    {
        return Unsupported("IDirectDrawSurface7::AddOverlayDirtyRect");
    }

    HRESULT DirectDrawSurface7::Blt(
        LPRECT destinationRect,
        LPDIRECTDRAWSURFACE7 source,
        LPRECT,
        DWORD flags,
        LPDDBLTFX effects)
    {
        if (!source && (flags & DDBLT_COLORFILL) && effects)
        {
            RendererRuntime::Instance().Clear(1, effects->dwFillColor, 1.0F, 0);
            return S_OK;
        }

        auto* sourceSurface = static_cast<DirectDrawSurface7*>(source);
        if (sourceSurface &&
            sourceSurface->HasCpuData() &&
            (m_description.ddsCaps.dwCaps & DDSCAPS_TEXTURE))
        {
            if (m_description.dwWidth != sourceSurface->m_description.dwWidth ||
                m_description.dwHeight != sourceSurface->m_description.dwHeight ||
                m_description.ddpfPixelFormat.dwRGBBitCount !=
                    sourceSurface->m_description.ddpfPixelFormat.dwRGBBitCount)
            {
                return DDERR_INVALIDPARAMS;
            }

            m_cpuData = sourceSurface->m_cpuData;
            m_textureDirty = true;
            return DD_OK;
        }

        if (sourceSurface && sourceSurface->HasCpuData())
        {
            ID3D11ShaderResourceView* texture = nullptr;
            HRESULT result = sourceSurface->GetTextureView(&texture);
            if (FAILED(result))
            {
                return result;
            }

            const float left = destinationRect == nullptr
                ? 0.0F
                : static_cast<float>(destinationRect->left);
            const float top = destinationRect == nullptr
                ? 0.0F
                : static_cast<float>(destinationRect->top);
            const float right = destinationRect == nullptr
                ? static_cast<float>(RendererRuntime::Instance().Width())
                : static_cast<float>(destinationRect->right);
            const float bottom = destinationRect == nullptr
                ? static_cast<float>(RendererRuntime::Instance().Height())
                : static_cast<float>(destinationRect->bottom);
            const LegacyVertex vertices[] =
            {
                {left, top, 0.0F, 1.0F, 0xFFFFFFFF, 0.0F, 0.0F},
                {right, top, 0.0F, 1.0F, 0xFFFFFFFF, 1.0F, 0.0F},
                {right, bottom, 0.0F, 1.0F, 0xFFFFFFFF, 1.0F, 1.0F},
                {left, bottom, 0.0F, 1.0F, 0xFFFFFFFF, 0.0F, 1.0F},
            };
            LegacyPipelineState state{};
            state.zEnable = 0;
            state.zWriteEnable = 0;
            state.addressU = 3;
            state.addressV = 3;
            state.magnificationFilter = 2;
            state.minificationFilter = 2;
            result = RendererRuntime::Instance().DrawTriangleFan(
                vertices,
                ARRAYSIZE(vertices),
                texture,
                state);
            if (FAILED(result))
            {
                return result;
            }
        }

        if (sourceSurface &&
            (m_description.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE))
        {
            return RendererRuntime::Instance().Present();
        }

        return S_OK;
    }

    HRESULT DirectDrawSurface7::BltBatch(LPDDBLTBATCH, DWORD, DWORD)
    {
        return Unsupported("IDirectDrawSurface7::BltBatch");
    }

    HRESULT DirectDrawSurface7::BltFast(DWORD, DWORD, LPDIRECTDRAWSURFACE7, LPRECT, DWORD)
    {
        return Unsupported("IDirectDrawSurface7::BltFast");
    }

    HRESULT DirectDrawSurface7::DeleteAttachedSurface(DWORD, LPDIRECTDRAWSURFACE7 surface)
    {
        if (surface == m_zBuffer)
        {
            m_zBuffer->Release();
            m_zBuffer = nullptr;
            return S_OK;
        }
        if (surface == m_backBuffer)
        {
            m_backBuffer->Release();
            m_backBuffer = nullptr;
            return S_OK;
        }
        return DDERR_NOTFOUND;
    }

    HRESULT DirectDrawSurface7::EnumAttachedSurfaces(LPVOID, LPDDENUMSURFACESCALLBACK7)
    {
        return Unsupported("IDirectDrawSurface7::EnumAttachedSurfaces");
    }

    HRESULT DirectDrawSurface7::EnumOverlayZOrders(DWORD, LPVOID, LPDDENUMSURFACESCALLBACK7)
    {
        return Unsupported("IDirectDrawSurface7::EnumOverlayZOrders");
    }

    HRESULT DirectDrawSurface7::Flip(LPDIRECTDRAWSURFACE7, DWORD)
    {
        return RendererRuntime::Instance().Present();
    }

    HRESULT DirectDrawSurface7::GetAttachedSurface(
        LPDDSCAPS2 caps,
        LPDIRECTDRAWSURFACE7* surface)
    {
        if (!caps || !surface)
        {
            return DDERR_INVALIDPARAMS;
        }
        *surface = nullptr;

        DirectDrawSurface7* match = nullptr;
        if (m_backBuffer && m_backBuffer->MatchesCaps(*caps))
        {
            match = m_backBuffer;
        }
        else if (m_zBuffer && m_zBuffer->MatchesCaps(*caps))
        {
            match = m_zBuffer;
        }

        if (!match)
        {
            return DDERR_NOTFOUND;
        }

        match->AddRef();
        *surface = match;
        return S_OK;
    }

    HRESULT DirectDrawSurface7::GetBltStatus(DWORD)
    {
        return DD_OK;
    }

    HRESULT DirectDrawSurface7::GetCaps(LPDDSCAPS2 caps)
    {
        if (!caps)
        {
            return E_POINTER;
        }
        *caps = m_description.ddsCaps;
        return S_OK;
    }

    HRESULT DirectDrawSurface7::GetClipper(LPDIRECTDRAWCLIPPER* clipper)
    {
        if (!clipper)
        {
            return E_POINTER;
        }
        *clipper = m_clipper;
        if (!m_clipper)
        {
            return DDERR_NOCLIPPERATTACHED;
        }
        m_clipper->AddRef();
        return S_OK;
    }

    HRESULT DirectDrawSurface7::GetColorKey(DWORD, LPDDCOLORKEY)
    {
        return DDERR_NOCOLORKEY;
    }

    HRESULT DirectDrawSurface7::GetDC(HDC*)
    {
        return Unsupported("IDirectDrawSurface7::GetDC");
    }

    HRESULT DirectDrawSurface7::GetFlipStatus(DWORD)
    {
        return DD_OK;
    }

    HRESULT DirectDrawSurface7::GetOverlayPosition(LPLONG, LPLONG)
    {
        return Unsupported("IDirectDrawSurface7::GetOverlayPosition");
    }

    HRESULT DirectDrawSurface7::GetPalette(LPDIRECTDRAWPALETTE*)
    {
        return DDERR_NOPALETTEATTACHED;
    }

    HRESULT DirectDrawSurface7::GetPixelFormat(LPDDPIXELFORMAT format)
    {
        if (!format)
        {
            return E_POINTER;
        }
        *format = m_description.ddpfPixelFormat;
        return S_OK;
    }

    HRESULT DirectDrawSurface7::GetSurfaceDesc(LPDDSURFACEDESC2 description)
    {
        if (!description)
        {
            return E_POINTER;
        }
        *description = m_description;
        description->lpSurface = m_cpuData.empty() ? nullptr : m_cpuData.data();
        return S_OK;
    }

    HRESULT DirectDrawSurface7::Initialize(LPDIRECTDRAW, LPDDSURFACEDESC2)
    {
        return DDERR_ALREADYINITIALIZED;
    }

    HRESULT DirectDrawSurface7::IsLost()
    {
        return DD_OK;
    }

    HRESULT DirectDrawSurface7::Lock(
        LPRECT,
        LPDDSURFACEDESC2 description,
        DWORD,
        HANDLE)
    {
        if (!description)
        {
            return DDERR_INVALIDPARAMS;
        }

        const DWORD bits = std::max(m_description.ddpfPixelFormat.dwRGBBitCount, 16UL);
        const DWORD bytesPerPixel = std::max(bits / 8, 2UL);
        const DWORD pitch = m_description.dwWidth * bytesPerPixel;
        const size_t size = static_cast<size_t>(pitch) * m_description.dwHeight;
        if (m_cpuData.size() != size)
        {
            m_cpuData.resize(size);
        }

        *description = m_description;
        description->dwFlags |= DDSD_LPSURFACE | DDSD_PITCH;
        description->lPitch = static_cast<LONG>(pitch);
        description->lpSurface = m_cpuData.data();
        return DD_OK;
    }

    HRESULT DirectDrawSurface7::ReleaseDC(HDC)
    {
        return Unsupported("IDirectDrawSurface7::ReleaseDC");
    }

    HRESULT DirectDrawSurface7::Restore()
    {
        return DD_OK;
    }

    HRESULT DirectDrawSurface7::SetClipper(LPDIRECTDRAWCLIPPER clipper)
    {
        if (m_clipper)
        {
            m_clipper->Release();
        }
        m_clipper = clipper;
        if (m_clipper)
        {
            m_clipper->AddRef();
        }
        return S_OK;
    }

    HRESULT DirectDrawSurface7::SetColorKey(DWORD, LPDDCOLORKEY)
    {
        return S_OK;
    }

    HRESULT DirectDrawSurface7::SetOverlayPosition(LONG, LONG)
    {
        return Unsupported("IDirectDrawSurface7::SetOverlayPosition");
    }

    HRESULT DirectDrawSurface7::SetPalette(LPDIRECTDRAWPALETTE)
    {
        return Unsupported("IDirectDrawSurface7::SetPalette");
    }

    HRESULT DirectDrawSurface7::Unlock(LPRECT)
    {
        m_textureDirty = true;
        return DD_OK;
    }

    HRESULT DirectDrawSurface7::UpdateOverlay(LPRECT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD, LPDDOVERLAYFX)
    {
        return Unsupported("IDirectDrawSurface7::UpdateOverlay");
    }

    HRESULT DirectDrawSurface7::UpdateOverlayDisplay(DWORD)
    {
        return Unsupported("IDirectDrawSurface7::UpdateOverlayDisplay");
    }

    HRESULT DirectDrawSurface7::UpdateOverlayZOrder(DWORD, LPDIRECTDRAWSURFACE7)
    {
        return Unsupported("IDirectDrawSurface7::UpdateOverlayZOrder");
    }

    HRESULT DirectDrawSurface7::GetDDInterface(LPVOID* directDraw)
    {
        if (!directDraw)
        {
            return E_POINTER;
        }
        m_owner->AddRef();
        *directDraw = static_cast<IDirectDraw7*>(m_owner);
        return S_OK;
    }

    HRESULT DirectDrawSurface7::PageLock(DWORD)
    {
        return DD_OK;
    }

    HRESULT DirectDrawSurface7::PageUnlock(DWORD)
    {
        return DD_OK;
    }

    HRESULT DirectDrawSurface7::SetSurfaceDesc(LPDDSURFACEDESC2, DWORD)
    {
        return Unsupported("IDirectDrawSurface7::SetSurfaceDesc");
    }

    HRESULT DirectDrawSurface7::SetPrivateData(REFGUID, LPVOID, DWORD, DWORD)
    {
        return Unsupported("IDirectDrawSurface7::SetPrivateData");
    }

    HRESULT DirectDrawSurface7::GetPrivateData(REFGUID, LPVOID, LPDWORD)
    {
        return Unsupported("IDirectDrawSurface7::GetPrivateData");
    }

    HRESULT DirectDrawSurface7::FreePrivateData(REFGUID)
    {
        return Unsupported("IDirectDrawSurface7::FreePrivateData");
    }

    HRESULT DirectDrawSurface7::GetUniquenessValue(LPDWORD value)
    {
        if (!value)
        {
            return E_POINTER;
        }
        *value = m_uniqueness;
        return S_OK;
    }

    HRESULT DirectDrawSurface7::ChangeUniquenessValue()
    {
        ++m_uniqueness;
        return S_OK;
    }

    HRESULT DirectDrawSurface7::SetPriority(DWORD priority)
    {
        m_priority = priority;
        return S_OK;
    }

    HRESULT DirectDrawSurface7::GetPriority(LPDWORD priority)
    {
        if (!priority)
        {
            return E_POINTER;
        }
        *priority = m_priority;
        return S_OK;
    }

    HRESULT DirectDrawSurface7::SetLOD(DWORD lod)
    {
        m_lod = lod;
        return S_OK;
    }

    HRESULT DirectDrawSurface7::GetLOD(LPDWORD lod)
    {
        if (!lod)
        {
            return E_POINTER;
        }
        *lod = m_lod;
        return S_OK;
    }

    HRESULT DirectDrawSurface7::GetTextureView(ID3D11ShaderResourceView** view)
    {
        if (!view)
        {
            return E_POINTER;
        }
        *view = nullptr;
        if (m_cpuData.empty())
        {
            return DDERR_NOTLOCKED;
        }

        RendererRuntime& runtime = RendererRuntime::Instance();
        HRESULT result = runtime.EnsureDevice();
        if (FAILED(result))
        {
            return result;
        }

        if (!m_textureDirty &&
            m_textureView &&
            m_textureGeneration == runtime.DeviceGeneration())
        {
            *view = m_textureView;
            return S_OK;
        }

        ReleaseGpuTexture();

        const UINT width = m_description.dwWidth;
        const UINT height = m_description.dwHeight;
        const DDPIXELFORMAT& format = m_description.ddpfPixelFormat;
        const UINT bits = std::max<UINT>(format.dwRGBBitCount, 16);
        const UINT bytesPerPixel = std::max<UINT>(bits / 8, 2);
        const UINT sourcePitch = width * bytesPerPixel;
        std::vector<std::uint8_t> rgba(
            static_cast<size_t>(width) * height * 4);

        for (UINT y = 0; y < height; ++y)
        {
            const std::uint8_t* sourceRow =
                m_cpuData.data() + static_cast<size_t>(sourcePitch) * y;
            std::uint8_t* destinationRow =
                rgba.data() + static_cast<size_t>(width) * y * 4;
            for (UINT x = 0; x < width; ++x)
            {
                DWORD pixel = 0;
                std::memcpy(
                    &pixel,
                    sourceRow + static_cast<size_t>(x) * bytesPerPixel,
                    bytesPerPixel);
                destinationRow[x * 4 + 0] =
                    ExpandChannel(pixel, format.dwRBitMask, 0);
                destinationRow[x * 4 + 1] =
                    ExpandChannel(pixel, format.dwGBitMask, 0);
                destinationRow[x * 4 + 2] =
                    ExpandChannel(pixel, format.dwBBitMask, 0);
                destinationRow[x * 4 + 3] =
                    ExpandChannel(pixel, format.dwRGBAlphaBitMask, 255);
            }
        }

        D3D11_TEXTURE2D_DESC textureDescription{};
        textureDescription.Width = width;
        textureDescription.Height = height;
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 1;
        textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage = D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initialData{};
        initialData.pSysMem = rgba.data();
        initialData.SysMemPitch = width * 4;
        result = runtime.Device()->CreateTexture2D(
            &textureDescription,
            &initialData,
            &m_gpuTexture);
        if (SUCCEEDED(result))
        {
            result = runtime.Device()->CreateShaderResourceView(
                m_gpuTexture,
                nullptr,
                &m_textureView);
        }
        if (FAILED(result))
        {
            ReleaseGpuTexture();
            return result;
        }

        m_textureDirty = false;
        m_textureGeneration = runtime.DeviceGeneration();
        *view = m_textureView;
        return S_OK;
    }

    bool DirectDrawSurface7::MatchesCaps(const DDSCAPS2& caps) const
    {
        return (m_description.ddsCaps.dwCaps & caps.dwCaps) == caps.dwCaps;
    }

    void DirectDrawSurface7::SetBackBuffer(DirectDrawSurface7* surface)
    {
        if (m_backBuffer)
        {
            m_backBuffer->Release();
        }
        m_backBuffer = surface;
        if (m_backBuffer)
        {
            m_backBuffer->AddRef();
        }
    }

    void DirectDrawSurface7::ReleaseGpuTexture()
    {
        if (m_textureView)
        {
            RendererRuntime::Instance().UnbindTexture(m_textureView);
            m_textureView->Release();
            m_textureView = nullptr;
        }
        if (m_gpuTexture)
        {
            m_gpuTexture->Release();
            m_gpuTexture = nullptr;
        }
    }

    Direct3DDevice7::Direct3DDevice7(
        Direct3D7* owner,
        DirectDrawSurface7* renderTarget)
        : m_owner(owner),
          m_renderTarget(renderTarget)
    {
        m_owner->AddRef();
        if (m_renderTarget)
        {
            m_renderTarget->AddRef();
        }
    }

    Direct3DDevice7::~Direct3DDevice7()
    {
        if (m_texture)
        {
            m_texture->Release();
        }
        if (m_renderTarget)
        {
            m_renderTarget->Release();
        }
        m_owner->Release();
    }

    HRESULT Direct3DDevice7::QueryInterface(REFIID riid, void** object)
    {
        if (!object)
        {
            return E_POINTER;
        }
        *object = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDirect3DDevice7))
        {
            *object = static_cast<IDirect3DDevice7*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG Direct3DDevice7::AddRef()
    {
        return ++m_references;
    }

    ULONG Direct3DDevice7::Release()
    {
        const ULONG references = --m_references;
        if (!references)
        {
            delete this;
        }
        return references;
    }

    HRESULT Direct3DDevice7::GetCaps(LPD3DDEVICEDESC7 caps)
    {
        if (!caps)
        {
            return E_POINTER;
        }
        FillDeviceCaps(*caps);
        return S_OK;
    }

    HRESULT Direct3DDevice7::EnumTextureFormats(
        LPD3DENUMPIXELFORMATSCALLBACK callback,
        LPVOID context)
    {
        if (!callback)
        {
            return DDERR_INVALIDPARAMS;
        }

        DDPIXELFORMAT formats[] =
        {
            MakeRgb565(),
            MakeArgb1555(),
            MakeArgb4444(),
            MakeArgb8888()
        };
        for (auto& format : formats)
        {
            if (callback(&format, context) == D3DENUMRET_CANCEL)
            {
                break;
            }
        }
        return S_OK;
    }

    HRESULT Direct3DDevice7::BeginScene()
    {
        return RendererRuntime::Instance().EnsureDevice();
    }

    HRESULT Direct3DDevice7::EndScene()
    {
        return S_OK;
    }

    HRESULT Direct3DDevice7::GetDirect3D(LPDIRECT3D7* direct3D)
    {
        if (!direct3D)
        {
            return E_POINTER;
        }
        m_owner->AddRef();
        *direct3D = m_owner;
        return S_OK;
    }

    HRESULT Direct3DDevice7::SetRenderTarget(LPDIRECTDRAWSURFACE7, DWORD)
    {
        return Unsupported("IDirect3DDevice7::SetRenderTarget");
    }

    HRESULT Direct3DDevice7::GetRenderTarget(LPDIRECTDRAWSURFACE7* target)
    {
        if (!target)
        {
            return E_POINTER;
        }
        *target = m_renderTarget;
        if (m_renderTarget)
        {
            m_renderTarget->AddRef();
        }
        return S_OK;
    }

    HRESULT Direct3DDevice7::Clear(
        DWORD,
        LPD3DRECT,
        DWORD flags,
        D3DCOLOR color,
        D3DVALUE depth,
        DWORD stencil)
    {
        RendererRuntime::Instance().Clear(
            flags,
            color,
            depth,
            static_cast<BYTE>(stencil));
        return S_OK;
    }

    HRESULT Direct3DDevice7::SetTransform(D3DTRANSFORMSTATETYPE, LPD3DMATRIX)
    {
        return S_OK;
    }

    HRESULT Direct3DDevice7::GetTransform(D3DTRANSFORMSTATETYPE, LPD3DMATRIX)
    {
        return Unsupported("IDirect3DDevice7::GetTransform");
    }

    HRESULT Direct3DDevice7::SetViewport(LPD3DVIEWPORT7 viewport)
    {
        if (!viewport)
        {
            return E_POINTER;
        }
        m_viewport = *viewport;

        D3D11_VIEWPORT nativeViewport{};
        nativeViewport.TopLeftX = static_cast<float>(viewport->dwX);
        nativeViewport.TopLeftY = static_cast<float>(viewport->dwY);
        nativeViewport.Width = static_cast<float>(viewport->dwWidth);
        nativeViewport.Height = static_cast<float>(viewport->dwHeight);
        nativeViewport.MinDepth = viewport->dvMinZ;
        nativeViewport.MaxDepth = viewport->dvMaxZ;
        RendererRuntime::Instance().SetViewport(nativeViewport);
        return S_OK;
    }

    HRESULT Direct3DDevice7::MultiplyTransform(D3DTRANSFORMSTATETYPE, LPD3DMATRIX)
    {
        return Unsupported("IDirect3DDevice7::MultiplyTransform");
    }

    HRESULT Direct3DDevice7::GetViewport(LPD3DVIEWPORT7 viewport)
    {
        if (!viewport)
        {
            return E_POINTER;
        }
        *viewport = m_viewport;
        return S_OK;
    }

    HRESULT Direct3DDevice7::SetMaterial(LPD3DMATERIAL7) { return S_OK; }
    HRESULT Direct3DDevice7::GetMaterial(LPD3DMATERIAL7) { return Unsupported("IDirect3DDevice7::GetMaterial"); }
    HRESULT Direct3DDevice7::SetLight(DWORD, LPD3DLIGHT7) { return S_OK; }
    HRESULT Direct3DDevice7::GetLight(DWORD, LPD3DLIGHT7) { return Unsupported("IDirect3DDevice7::GetLight"); }

    HRESULT Direct3DDevice7::SetRenderState(D3DRENDERSTATETYPE state, DWORD value)
    {
        const auto index = static_cast<size_t>(state);
        if (index < ARRAYSIZE(m_renderStates))
        {
            m_renderStates[index] = value;
        }
        return S_OK;
    }

    HRESULT Direct3DDevice7::GetRenderState(D3DRENDERSTATETYPE state, LPDWORD value)
    {
        if (!value)
        {
            return E_POINTER;
        }
        const auto index = static_cast<size_t>(state);
        *value = index < ARRAYSIZE(m_renderStates) ? m_renderStates[index] : 0;
        return S_OK;
    }

    HRESULT Direct3DDevice7::BeginStateBlock() { return Unsupported("IDirect3DDevice7::BeginStateBlock"); }
    HRESULT Direct3DDevice7::EndStateBlock(LPDWORD) { return Unsupported("IDirect3DDevice7::EndStateBlock"); }
    HRESULT Direct3DDevice7::PreLoad(LPDIRECTDRAWSURFACE7) { return S_OK; }

    HRESULT Direct3DDevice7::DrawPrimitive(
        D3DPRIMITIVETYPE primitiveType,
        DWORD vertexType,
        LPVOID vertices,
        DWORD vertexCount,
        DWORD)
    {
        if (primitiveType != D3DPT_TRIANGLEFAN ||
            vertexType != (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1))
        {
            return DDERR_UNSUPPORTED;
        }

        ID3D11ShaderResourceView* textureView = nullptr;
        if (m_texture)
        {
            const HRESULT textureResult = m_texture->GetTextureView(&textureView);
            if (FAILED(textureResult))
            {
                return textureResult;
            }
        }

        LegacyPipelineState state{};
        state.zEnable = m_renderStates[D3DRENDERSTATE_ZENABLE];
        state.zWriteEnable = m_renderStates[D3DRENDERSTATE_ZWRITEENABLE];
        state.zFunction = m_renderStates[D3DRENDERSTATE_ZFUNC];
        state.alphaBlendEnable = m_renderStates[D3DRENDERSTATE_ALPHABLENDENABLE];
        state.sourceBlend = m_renderStates[D3DRENDERSTATE_SRCBLEND];
        state.destinationBlend = m_renderStates[D3DRENDERSTATE_DESTBLEND];
        state.alphaTestEnable = m_renderStates[D3DRENDERSTATE_ALPHATESTENABLE];
        state.alphaReference = m_renderStates[D3DRENDERSTATE_ALPHAREF];
        state.alphaFunction = m_renderStates[D3DRENDERSTATE_ALPHAFUNC];
        state.cullMode = m_renderStates[D3DRENDERSTATE_CULLMODE];
        state.fogEnable = m_renderStates[D3DRENDERSTATE_FOGENABLE];
        state.fogColor = m_renderStates[D3DRENDERSTATE_FOGCOLOR];
        state.fogStart = m_renderStates[D3DRENDERSTATE_FOGSTART];
        state.fogEnd = m_renderStates[D3DRENDERSTATE_FOGEND];
        state.addressU = m_textureStates[D3DTSS_ADDRESSU];
        state.addressV = m_textureStates[D3DTSS_ADDRESSV];
        state.magnificationFilter = m_textureStates[D3DTSS_MAGFILTER];
        state.minificationFilter = m_textureStates[D3DTSS_MINFILTER];
        state.alphaOperation = m_textureStates[D3DTSS_ALPHAOP];

        return RendererRuntime::Instance().DrawTriangleFan(
            static_cast<const LegacyVertex*>(vertices),
            vertexCount,
            textureView,
            state);
    }

    HRESULT Direct3DDevice7::DrawIndexedPrimitive(D3DPRIMITIVETYPE, DWORD, LPVOID, DWORD, LPWORD, DWORD, DWORD)
    {
        return Unsupported("IDirect3DDevice7::DrawIndexedPrimitive");
    }
    HRESULT Direct3DDevice7::SetClipStatus(LPD3DCLIPSTATUS) { return Unsupported("IDirect3DDevice7::SetClipStatus"); }
    HRESULT Direct3DDevice7::GetClipStatus(LPD3DCLIPSTATUS) { return Unsupported("IDirect3DDevice7::GetClipStatus"); }
    HRESULT Direct3DDevice7::DrawPrimitiveStrided(D3DPRIMITIVETYPE, DWORD, LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD, DWORD) { return Unsupported("IDirect3DDevice7::DrawPrimitiveStrided"); }
    HRESULT Direct3DDevice7::DrawIndexedPrimitiveStrided(D3DPRIMITIVETYPE, DWORD, LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD, LPWORD, DWORD, DWORD) { return Unsupported("IDirect3DDevice7::DrawIndexedPrimitiveStrided"); }
    HRESULT Direct3DDevice7::DrawPrimitiveVB(D3DPRIMITIVETYPE, LPDIRECT3DVERTEXBUFFER7, DWORD, DWORD, DWORD) { return Unsupported("IDirect3DDevice7::DrawPrimitiveVB"); }
    HRESULT Direct3DDevice7::DrawIndexedPrimitiveVB(D3DPRIMITIVETYPE, LPDIRECT3DVERTEXBUFFER7, DWORD, DWORD, LPWORD, DWORD, DWORD) { return Unsupported("IDirect3DDevice7::DrawIndexedPrimitiveVB"); }
    HRESULT Direct3DDevice7::ComputeSphereVisibility(LPD3DVECTOR, LPD3DVALUE, DWORD, DWORD, LPDWORD) { return Unsupported("IDirect3DDevice7::ComputeSphereVisibility"); }

    HRESULT Direct3DDevice7::GetTexture(DWORD stage, LPDIRECTDRAWSURFACE7* texture)
    {
        if (!texture || stage != 0)
        {
            return DDERR_INVALIDPARAMS;
        }
        *texture = m_texture;
        if (m_texture)
        {
            m_texture->AddRef();
        }
        return S_OK;
    }

    HRESULT Direct3DDevice7::SetTexture(DWORD stage, LPDIRECTDRAWSURFACE7 texture)
    {
        if (stage != 0)
        {
            return DDERR_INVALIDPARAMS;
        }
        if (m_texture)
        {
            m_texture->Release();
        }
        m_texture = static_cast<DirectDrawSurface7*>(texture);
        if (m_texture)
        {
            m_texture->AddRef();
        }
        return S_OK;
    }

    HRESULT Direct3DDevice7::GetTextureStageState(
        DWORD stage,
        D3DTEXTURESTAGESTATETYPE state,
        LPDWORD value)
    {
        if (!value || stage != 0)
        {
            return DDERR_INVALIDPARAMS;
        }
        const auto index = static_cast<size_t>(state);
        *value = index < ARRAYSIZE(m_textureStates) ? m_textureStates[index] : 0;
        return S_OK;
    }

    HRESULT Direct3DDevice7::SetTextureStageState(
        DWORD stage,
        D3DTEXTURESTAGESTATETYPE state,
        DWORD value)
    {
        if (stage != 0)
        {
            return DDERR_INVALIDPARAMS;
        }
        const auto index = static_cast<size_t>(state);
        if (index < ARRAYSIZE(m_textureStates))
        {
            m_textureStates[index] = value;
        }
        return S_OK;
    }

    HRESULT Direct3DDevice7::ValidateDevice(LPDWORD passes)
    {
        if (!passes)
        {
            return E_POINTER;
        }
        *passes = 1;
        return S_OK;
    }

    HRESULT Direct3DDevice7::ApplyStateBlock(DWORD) { return Unsupported("IDirect3DDevice7::ApplyStateBlock"); }
    HRESULT Direct3DDevice7::CaptureStateBlock(DWORD) { return Unsupported("IDirect3DDevice7::CaptureStateBlock"); }
    HRESULT Direct3DDevice7::DeleteStateBlock(DWORD) { return Unsupported("IDirect3DDevice7::DeleteStateBlock"); }
    HRESULT Direct3DDevice7::CreateStateBlock(D3DSTATEBLOCKTYPE, LPDWORD) { return Unsupported("IDirect3DDevice7::CreateStateBlock"); }
    HRESULT Direct3DDevice7::Load(LPDIRECTDRAWSURFACE7, LPPOINT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD) { return Unsupported("IDirect3DDevice7::Load"); }
    HRESULT Direct3DDevice7::LightEnable(DWORD, BOOL) { return S_OK; }
    HRESULT Direct3DDevice7::GetLightEnable(DWORD, BOOL*) { return Unsupported("IDirect3DDevice7::GetLightEnable"); }
    HRESULT Direct3DDevice7::SetClipPlane(DWORD, D3DVALUE*) { return Unsupported("IDirect3DDevice7::SetClipPlane"); }
    HRESULT Direct3DDevice7::GetClipPlane(DWORD, D3DVALUE*) { return Unsupported("IDirect3DDevice7::GetClipPlane"); }
    HRESULT Direct3DDevice7::GetInfo(DWORD, LPVOID, DWORD) { return Unsupported("IDirect3DDevice7::GetInfo"); }

    Direct3D7::Direct3D7(DirectDraw7* owner)
        : m_owner(owner)
    {
        m_owner->AddRef();
    }

    Direct3D7::~Direct3D7()
    {
        m_owner->Release();
    }

    HRESULT Direct3D7::QueryInterface(REFIID riid, void** object)
    {
        if (!object)
        {
            return E_POINTER;
        }
        *object = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDirect3D7))
        {
            *object = static_cast<IDirect3D7*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG Direct3D7::AddRef()
    {
        return ++m_references;
    }

    ULONG Direct3D7::Release()
    {
        const ULONG references = --m_references;
        if (!references)
        {
            delete this;
        }
        return references;
    }

    HRESULT Direct3D7::EnumDevices(
        LPD3DENUMDEVICESCALLBACK7 callback,
        LPVOID context)
    {
        if (!callback)
        {
            return DDERR_INVALIDPARAMS;
        }

        D3DDEVICEDESC7 caps{};
        FillDeviceCaps(caps);
        callback(
            const_cast<char*>("Spider-Man D3D11 HAL"),
            const_cast<char*>("Spider-Man game-specific D3D11 renderer"),
            &caps,
            context);
        return S_OK;
    }

    HRESULT Direct3D7::CreateDevice(
        REFCLSID,
        LPDIRECTDRAWSURFACE7 renderTarget,
        LPDIRECT3DDEVICE7* device)
    {
        if (!renderTarget || !device)
        {
            return DDERR_INVALIDPARAMS;
        }
        *device = nullptr;

        const HRESULT result = RendererRuntime::Instance().EnsureDevice();
        if (FAILED(result))
        {
            return result;
        }

        auto* created = new (std::nothrow) Direct3DDevice7(
            this,
            static_cast<DirectDrawSurface7*>(renderTarget));
        if (!created)
        {
            return E_OUTOFMEMORY;
        }
        *device = created;
        return S_OK;
    }

    HRESULT Direct3D7::CreateVertexBuffer(LPD3DVERTEXBUFFERDESC, LPDIRECT3DVERTEXBUFFER7*, DWORD)
    {
        return Unsupported("IDirect3D7::CreateVertexBuffer");
    }

    HRESULT Direct3D7::EnumZBufferFormats(
        REFCLSID,
        LPD3DENUMPIXELFORMATSCALLBACK callback,
        LPVOID context)
    {
        if (!callback)
        {
            return DDERR_INVALIDPARAMS;
        }
        DDPIXELFORMAT format = MakeDepth16();
        callback(&format, context);
        return S_OK;
    }

    HRESULT Direct3D7::EvictManagedTextures()
    {
        return S_OK;
    }

    HRESULT DirectDraw7::QueryInterface(REFIID riid, void** object)
    {
        if (!object)
        {
            return E_POINTER;
        }
        *object = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDirectDraw7))
        {
            *object = static_cast<IDirectDraw7*>(this);
            AddRef();
            return S_OK;
        }
        if (IsEqualIID(riid, IID_IDirect3D7))
        {
            auto* direct3D = new (std::nothrow) Direct3D7(this);
            if (!direct3D)
            {
                return E_OUTOFMEMORY;
            }
            *object = direct3D;
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG DirectDraw7::AddRef()
    {
        return ++m_references;
    }

    ULONG DirectDraw7::Release()
    {
        const ULONG references = --m_references;
        if (!references)
        {
            delete this;
        }
        return references;
    }

    HRESULT DirectDraw7::Compact() { return DD_OK; }

    HRESULT DirectDraw7::CreateClipper(
        DWORD,
        LPDIRECTDRAWCLIPPER* clipper,
        IUnknown*)
    {
        if (!clipper)
        {
            return DDERR_INVALIDPARAMS;
        }
        *clipper = new (std::nothrow) DirectDrawClipper();
        return *clipper ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT DirectDraw7::CreatePalette(DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE*, IUnknown*)
    {
        return Unsupported("IDirectDraw7::CreatePalette");
    }

    HRESULT DirectDraw7::CreateSurface(
        LPDDSURFACEDESC2 description,
        LPDIRECTDRAWSURFACE7* surface,
        IUnknown*)
    {
        if (!description || !surface)
        {
            return DDERR_INVALIDPARAMS;
        }
        *surface = nullptr;

        if ((description->ddsCaps.dwCaps & DDSCAPS_3DDEVICE) &&
            !(description->ddsCaps.dwCaps & (DDSCAPS_TEXTURE | DDSCAPS_ZBUFFER)) &&
            (description->dwFlags & DDSD_WIDTH) &&
            (description->dwFlags & DDSD_HEIGHT))
        {
            const DWORD bitsPerPixel =
                (description->dwFlags & DDSD_PIXELFORMAT) &&
                description->ddpfPixelFormat.dwRGBBitCount
                    ? description->ddpfPixelFormat.dwRGBBitCount
                    : RendererRuntime::Instance().BitsPerPixel();
            RendererRuntime::Instance().SetDisplayMode(
                description->dwWidth,
                description->dwHeight,
                bitsPerPixel);
        }

        auto* created = new (std::nothrow) DirectDrawSurface7(this, *description);
        if (!created)
        {
            return E_OUTOFMEMORY;
        }

        const DWORD caps = description->ddsCaps.dwCaps;
        if ((caps & DDSCAPS_PRIMARYSURFACE) &&
            (caps & DDSCAPS_COMPLEX) &&
            (caps & DDSCAPS_FLIP))
        {
            DDSURFACEDESC2 backDescription = *description;
            backDescription.ddsCaps.dwCaps =
                DDSCAPS_BACKBUFFER | DDSCAPS_3DDEVICE;
            backDescription.dwFlags &= ~DDSD_BACKBUFFERCOUNT;
            backDescription.dwBackBufferCount = 0;

            auto* backBuffer =
                new (std::nothrow) DirectDrawSurface7(this, backDescription);
            if (!backBuffer)
            {
                created->Release();
                return E_OUTOFMEMORY;
            }
            created->SetBackBuffer(backBuffer);
            backBuffer->Release();
        }

        *surface = created;
        return S_OK;
    }

    HRESULT DirectDraw7::DuplicateSurface(LPDIRECTDRAWSURFACE7, LPDIRECTDRAWSURFACE7*)
    {
        return Unsupported("IDirectDraw7::DuplicateSurface");
    }

    HRESULT DirectDraw7::EnumDisplayModes(
        DWORD,
        LPDDSURFACEDESC2 filter,
        LPVOID context,
        LPDDENUMMODESCALLBACK2 callback)
    {
        if (!callback)
        {
            return DDERR_INVALIDPARAMS;
        }

        constexpr SIZE modes[] =
        {
            {640, 480},
            {800, 600},
            {1024, 768},
            {1280, 720},
            {1280, 1024},
            {1920, 1080},
        };

        const DWORD bits =
            filter && filter->ddpfPixelFormat.dwRGBBitCount
                ? filter->ddpfPixelFormat.dwRGBBitCount
                : 32;

        for (const SIZE& mode : modes)
        {
            DDSURFACEDESC2 description{};
            description.dwSize = sizeof(description);
            description.dwFlags =
                DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS;
            description.dwWidth = static_cast<DWORD>(mode.cx);
            description.dwHeight = static_cast<DWORD>(mode.cy);
            description.ddsCaps.dwCaps = DDSCAPS_3DDEVICE;
            description.ddpfPixelFormat =
                bits <= 16 ? MakeRgb565() : MakeArgb8888();

            if (callback(&description, context) == DDENUMRET_CANCEL)
            {
                break;
            }
        }
        return S_OK;
    }

    HRESULT DirectDraw7::EnumSurfaces(DWORD, LPDDSURFACEDESC2, LPVOID, LPDDENUMSURFACESCALLBACK7)
    {
        return Unsupported("IDirectDraw7::EnumSurfaces");
    }
    HRESULT DirectDraw7::FlipToGDISurface() { return S_OK; }

    HRESULT DirectDraw7::GetCaps(LPDDCAPS driverCaps, LPDDCAPS emulationCaps)
    {
        if (driverCaps)
        {
            const DWORD size = driverCaps->dwSize;
            std::memset(driverCaps, 0, size);
            driverCaps->dwSize = size;
            driverCaps->dwCaps = DDCAPS_3D | DDCAPS_BLT | DDCAPS_BLTCOLORFILL;
            driverCaps->dwVidMemTotal = 256U * 1024U * 1024U;
            driverCaps->dwVidMemFree = 224U * 1024U * 1024U;
        }
        if (emulationCaps)
        {
            const DWORD size = emulationCaps->dwSize;
            std::memset(emulationCaps, 0, size);
            emulationCaps->dwSize = size;
        }
        return S_OK;
    }

    HRESULT DirectDraw7::GetDisplayMode(LPDDSURFACEDESC2 description)
    {
        if (!description)
        {
            return E_POINTER;
        }
        std::memset(description, 0, sizeof(*description));
        description->dwSize = sizeof(*description);
        description->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        description->dwWidth = RendererRuntime::Instance().Width();
        description->dwHeight = RendererRuntime::Instance().Height();
        description->ddpfPixelFormat =
            RendererRuntime::Instance().BitsPerPixel() <= 16
                ? MakeRgb565()
                : MakeArgb8888();
        return S_OK;
    }

    HRESULT DirectDraw7::GetFourCCCodes(LPDWORD count, LPDWORD)
    {
        if (!count)
        {
            return E_POINTER;
        }
        *count = 0;
        return S_OK;
    }
    HRESULT DirectDraw7::GetGDISurface(LPDIRECTDRAWSURFACE7*) { return Unsupported("IDirectDraw7::GetGDISurface"); }

    HRESULT DirectDraw7::GetMonitorFrequency(LPDWORD frequency)
    {
        if (!frequency)
        {
            return E_POINTER;
        }
        *frequency = 60;
        return S_OK;
    }

    HRESULT DirectDraw7::GetScanLine(LPDWORD) { return Unsupported("IDirectDraw7::GetScanLine"); }
    HRESULT DirectDraw7::GetVerticalBlankStatus(LPBOOL) { return Unsupported("IDirectDraw7::GetVerticalBlankStatus"); }
    HRESULT DirectDraw7::Initialize(GUID*) { return DDERR_ALREADYINITIALIZED; }
    HRESULT DirectDraw7::RestoreDisplayMode() { return S_OK; }

    HRESULT DirectDraw7::SetCooperativeLevel(HWND window, DWORD flags)
    {
        if (!window)
        {
            return DDERR_INVALIDPARAMS;
        }
        const bool exclusiveRequested =
            (flags & DDSCL_EXCLUSIVE) != 0 &&
            (flags & DDSCL_FULLSCREEN) != 0;
        const LONG_PTR style = GetWindowLongPtr(window, GWL_STYLE);
        const bool fullscreen =
            exclusiveRequested && (style & WS_CAPTION) == 0;
        RendererRuntime::Instance().SetWindow(window, fullscreen);
        return S_OK;
    }

    HRESULT DirectDraw7::SetDisplayMode(
        DWORD width,
        DWORD height,
        DWORD bitsPerPixel,
        DWORD,
        DWORD)
    {
        RendererRuntime::Instance().SetDisplayMode(width, height, bitsPerPixel);
        return S_OK;
    }

    HRESULT DirectDraw7::WaitForVerticalBlank(DWORD, HANDLE) { return S_OK; }

    HRESULT DirectDraw7::GetAvailableVidMem(
        LPDDSCAPS2,
        LPDWORD total,
        LPDWORD free)
    {
        if (total)
        {
            *total = 256U * 1024U * 1024U;
        }
        if (free)
        {
            *free = 224U * 1024U * 1024U;
        }
        return S_OK;
    }

    HRESULT DirectDraw7::GetSurfaceFromDC(HDC, LPDIRECTDRAWSURFACE7*) { return Unsupported("IDirectDraw7::GetSurfaceFromDC"); }
    HRESULT DirectDraw7::RestoreAllSurfaces() { return S_OK; }
    HRESULT DirectDraw7::TestCooperativeLevel() { return S_OK; }

    HRESULT DirectDraw7::GetDeviceIdentifier(
        LPDDDEVICEIDENTIFIER2 identifier,
        DWORD)
    {
        if (!identifier)
        {
            return E_POINTER;
        }
        std::memset(identifier, 0, sizeof(*identifier));
        strcpy_s(identifier->szDriver, "SpideyD3D11");
        strcpy_s(identifier->szDescription, "Spider-Man D3D11 Renderer");
        identifier->guidDeviceIdentifier =
            {0x4D614E53, 0x4449, 0x4431, {0x31, 0x52, 0x45, 0x4E, 0x44, 0x45, 0x52, 0x00}};
        return S_OK;
    }

    HRESULT DirectDraw7::StartModeTest(LPSIZE, DWORD, DWORD) { return Unsupported("IDirectDraw7::StartModeTest"); }
    HRESULT DirectDraw7::EvaluateMode(DWORD, DWORD*) { return Unsupported("IDirectDraw7::EvaluateMode"); }
}

extern "C" HRESULT WINAPI SpideyDirectDrawCreateEx(
    GUID*,
    LPVOID* directDraw,
    REFIID iid,
    IUnknown* outer)
{
    if (!directDraw)
    {
        return E_POINTER;
    }
    *directDraw = nullptr;
    if (outer)
    {
        return CLASS_E_NOAGGREGATION;
    }
    if (!IsEqualIID(iid, IID_IDirectDraw7))
    {
        return E_NOINTERFACE;
    }

    auto* created = new (std::nothrow) Spidey::DirectDraw7();
    if (!created)
    {
        return E_OUTOFMEMORY;
    }
    *directDraw = static_cast<IDirectDraw7*>(created);
    return S_OK;
}

extern "C" HRESULT WINAPI SpideyDirectDrawEnumerateExA(
    LPDDENUMCALLBACKEXA callback,
    LPVOID context,
    DWORD)
{
    if (!callback)
    {
        return DDERR_INVALIDPARAMS;
    }

    callback(
        nullptr,
        const_cast<char*>("Spider-Man D3D11 Renderer"),
        const_cast<char*>("DISPLAY"),
        context,
        MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY));
    return S_OK;
}
