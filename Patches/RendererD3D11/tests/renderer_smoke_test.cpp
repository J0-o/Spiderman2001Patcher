#include <Windows.h>
#include <d3d.h>
#include <ddraw.h>
#include <dxgiformat.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

namespace
{
    struct TextureFormats
    {
        bool hasRgb565;
        bool hasArgb1555;
        bool hasArgb4444;
    };

    BOOL WINAPI EnumerateCallback(
        GUID*,
        LPSTR,
        LPSTR,
        LPVOID context,
        HMONITOR)
    {
        ++*static_cast<int*>(context);
        return TRUE;
    }

    HRESULT WINAPI DeviceCallback(
        LPSTR,
        LPSTR,
        LPD3DDEVICEDESC7 description,
        LPVOID context)
    {
        *static_cast<GUID*>(context) = description->deviceGUID;
        return D3DENUMRET_OK;
    }

    HRESULT WINAPI ZBufferCallback(LPDDPIXELFORMAT format, LPVOID context)
    {
        *static_cast<DDPIXELFORMAT*>(context) = *format;
        return D3DENUMRET_CANCEL;
    }

    HRESULT WINAPI TextureFormatCallback(LPDDPIXELFORMAT format, LPVOID context)
    {
        auto* formats = static_cast<TextureFormats*>(context);
        if (format->dwRGBBitCount == 16 &&
            format->dwRGBAlphaBitMask == 0 &&
            format->dwRBitMask == 0xF800 &&
            format->dwGBitMask == 0x07E0 &&
            format->dwBBitMask == 0x001F)
        {
            formats->hasRgb565 = true;
        }
        if (format->dwRGBBitCount == 16 &&
            format->dwRGBAlphaBitMask == 0x8000 &&
            format->dwRBitMask == 0x7C00 &&
            format->dwGBitMask == 0x03E0 &&
            format->dwBBitMask == 0x001F)
        {
            formats->hasArgb1555 = true;
        }
        if (format->dwRGBBitCount == 16 &&
            format->dwRGBAlphaBitMask == 0xF000 &&
            format->dwRBitMask == 0x0F00 &&
            format->dwGBitMask == 0x00F0 &&
            format->dwBBitMask == 0x000F)
        {
            formats->hasArgb4444 = true;
        }
        return D3DENUMRET_OK;
    }

}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        std::fwprintf(stderr, L"usage: renderer_smoke_test <windows_x86.dll>\n");
        return 2;
    }

    HMODULE renderer = LoadLibraryW(argv[1]);
    if (!renderer)
    {
        std::fwprintf(stderr, L"LoadLibrary failed: %lu\n", GetLastError());
        return 3;
    }

    auto* lastTextureFormat = reinterpret_cast<volatile LONG*>(
        GetProcAddress(renderer, "SpideyLastTextureFormat"));
    if (!lastTextureFormat)
    {
        std::fprintf(stderr, "texture format diagnostic export is missing\n");
        FreeLibrary(renderer);
        return 3;
    }

    int adapterCount = 0;
    HRESULT result = DirectDrawEnumerateExA(
        EnumerateCallback,
        &adapterCount,
        DDENUM_ATTACHEDSECONDARYDEVICES);
    if (FAILED(result) || adapterCount != 1)
    {
        std::fprintf(stderr, "adapter enumeration failed: 0x%08lX, count=%d\n", result, adapterCount);
        return 4;
    }

    IDirectDraw7* directDraw = nullptr;
    result = DirectDrawCreateEx(
        nullptr,
        reinterpret_cast<void**>(&directDraw),
        IID_IDirectDraw7,
        nullptr);
    if (FAILED(result) || !directDraw)
    {
        std::fprintf(stderr, "DirectDrawCreateEx failed: 0x%08lX\n", result);
        return 5;
    }

    HWND window = CreateWindowExW(
        0,
        L"STATIC",
        L"Spider-Man renderer smoke test",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        640,
        480,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (!window)
    {
        std::fprintf(stderr, "CreateWindowEx failed: %lu\n", GetLastError());
        directDraw->Release();
        FreeLibrary(renderer);
        return 6;
    }

    result = directDraw->SetCooperativeLevel(window, DDSCL_NORMAL);
    if (SUCCEEDED(result))
    {
        result = directDraw->SetDisplayMode(640, 480, 32, 0, 0);
    }

    DDSURFACEDESC2 surfaceDescription{};
    surfaceDescription.dwSize = sizeof(surfaceDescription);
    surfaceDescription.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    surfaceDescription.dwWidth = 640;
    surfaceDescription.dwHeight = 480;
    surfaceDescription.ddsCaps.dwCaps =
        DDSCAPS_3DDEVICE | DDSCAPS_OFFSCREENPLAIN;

    IDirectDrawSurface7* renderTarget = nullptr;
    if (SUCCEEDED(result))
    {
        result = directDraw->CreateSurface(
            &surfaceDescription,
            &renderTarget,
            nullptr);
    }

    DDSURFACEDESC2 primaryDescription{};
    primaryDescription.dwSize = sizeof(primaryDescription);
    primaryDescription.dwFlags = DDSD_CAPS;
    primaryDescription.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDrawSurface7* primarySurface = nullptr;
    if (SUCCEEDED(result))
    {
        result = directDraw->CreateSurface(
            &primaryDescription,
            &primarySurface,
            nullptr);
    }

    DDSURFACEDESC2 textureDescription{};
    textureDescription.dwSize = sizeof(textureDescription);
    textureDescription.dwFlags =
        DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    textureDescription.dwWidth = 2;
    textureDescription.dwHeight = 2;
    textureDescription.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
    textureDescription.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    textureDescription.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
    textureDescription.ddpfPixelFormat.dwRGBBitCount = 16;
    textureDescription.ddpfPixelFormat.dwRGBAlphaBitMask = 0xF000;
    textureDescription.ddpfPixelFormat.dwRBitMask = 0x0F00;
    textureDescription.ddpfPixelFormat.dwGBitMask = 0x00F0;
    textureDescription.ddpfPixelFormat.dwBBitMask = 0x000F;

    IDirectDrawSurface7* texture = nullptr;
    if (SUCCEEDED(result))
    {
        result = directDraw->CreateSurface(
            &textureDescription,
            &texture,
            nullptr);
    }

    DDSURFACEDESC2 uploadDescription = textureDescription;
    uploadDescription.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    IDirectDrawSurface7* uploadSurface = nullptr;
    if (SUCCEEDED(result))
    {
        result = directDraw->CreateSurface(
            &uploadDescription,
            &uploadSurface,
            nullptr);
    }
    if (SUCCEEDED(result))
    {
        DDSURFACEDESC2 locked{};
        locked.dwSize = sizeof(locked);
        result = uploadSurface->Lock(nullptr, &locked, DDLOCK_WAIT, nullptr);
        if (SUCCEEDED(result))
        {
            const WORD pixels[] = {0x8F00, 0x40F0, 0x200F, 0xFFFF};
            for (DWORD row = 0; row < 2; ++row)
            {
                std::memcpy(
                    static_cast<BYTE*>(locked.lpSurface) + locked.lPitch * row,
                    pixels + row * 2,
                    2 * sizeof(WORD));
            }
            result = uploadSurface->Unlock(nullptr);
        }
    }
    if (SUCCEEDED(result))
    {
        result = texture->Blt(
            nullptr,
            uploadSurface,
            nullptr,
            DDBLT_WAIT,
            nullptr);
    }

    IDirect3D7* direct3D = nullptr;
    if (SUCCEEDED(result))
    {
        result = directDraw->QueryInterface(
            IID_IDirect3D7,
            reinterpret_cast<void**>(&direct3D));
    }

    GUID deviceGuid{};
    if (SUCCEEDED(result))
    {
        result = direct3D->EnumDevices(DeviceCallback, &deviceGuid);
    }

    TextureFormats textureFormats{};
    IDirect3DDevice7* formatDevice = nullptr;
    if (SUCCEEDED(result))
    {
        result = direct3D->CreateDevice(
            deviceGuid,
            renderTarget,
            &formatDevice);
    }
    if (SUCCEEDED(result))
    {
        result = formatDevice->EnumTextureFormats(
            TextureFormatCallback,
            &textureFormats);
    }
    if (SUCCEEDED(result) &&
        (!textureFormats.hasRgb565 ||
         !textureFormats.hasArgb1555 ||
         !textureFormats.hasArgb4444))
    {
        result = E_FAIL;
    }
    if (formatDevice)
    {
        formatDevice->Release();
    }

    DDPIXELFORMAT zFormat{};
    if (SUCCEEDED(result))
    {
        result = direct3D->EnumZBufferFormats(
            deviceGuid,
            ZBufferCallback,
            &zFormat);
    }
    if (SUCCEEDED(result) &&
        (!(zFormat.dwFlags & DDPF_ZBUFFER) ||
         zFormat.dwZBufferBitDepth != 16))
    {
        result = E_FAIL;
    }

    IDirect3DDevice7* device = nullptr;
    if (SUCCEEDED(result))
    {
        result = direct3D->CreateDevice(
            deviceGuid,
            renderTarget,
            &device);
    }

    D3DDEVICEDESC7 caps{};
    if (SUCCEEDED(result))
    {
        result = device->GetCaps(&caps);
    }

    D3DVIEWPORT7 viewport{};
    viewport.dwWidth = 640;
    viewport.dwHeight = 480;
    viewport.dvMaxZ = 1.0F;
    if (SUCCEEDED(result))
    {
        result = device->SetViewport(&viewport);
    }
    if (SUCCEEDED(result))
    {
        result = device->BeginScene();
    }
    if (SUCCEEDED(result))
    {
        result = device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF204060, 1.0F, 0);
    }
    if (SUCCEEDED(result))
    {
        result = device->SetTexture(0, texture);
    }
    if (SUCCEEDED(result))
    {
        result = device->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    }
    if (SUCCEEDED(result))
    {
        result = device->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    }
    if (SUCCEEDED(result))
    {
        result = device->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTFG_POINT);
    }
    if (SUCCEEDED(result))
    {
        result = device->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTFN_POINT);
    }
    if (SUCCEEDED(result))
    {
        result = device->SetRenderState(D3DRENDERSTATE_ZENABLE, FALSE);
    }

    struct TestVertex
    {
        float x;
        float y;
        float z;
        float rhw;
        DWORD color;
        float u;
        float v;
    };
    const TestVertex vertices[] =
    {
        {100.0F, 80.0F, 0.5F, 1.0F, 0xFFFFFFFF, 0.0F, 0.0F},
        {540.0F, 80.0F, 0.5F, 1.0F, 0xFFFFFFFF, 1.0F, 0.0F},
        {540.0F, 400.0F, 0.5F, 1.0F, 0xFFFFFFFF, 1.0F, 1.0F},
        {100.0F, 400.0F, 0.5F, 1.0F, 0xFFFFFFFF, 0.0F, 1.0F},
    };
    if (SUCCEEDED(result))
    {
        result = device->DrawPrimitive(
            D3DPT_TRIANGLEFAN,
            D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1,
            const_cast<TestVertex*>(vertices),
            ARRAYSIZE(vertices),
            0);
    }
    if (SUCCEEDED(result) &&
        *lastTextureFormat != DXGI_FORMAT_B4G4R4A4_UNORM &&
        *lastTextureFormat != DXGI_FORMAT_R8G8B8A8_UNORM)
    {
        result = E_FAIL;
    }
    if (SUCCEEDED(result))
    {
        result = device->EndScene();
    }
    if (SUCCEEDED(result))
    {
        result = primarySurface->Blt(
            nullptr,
            renderTarget,
            nullptr,
            DDBLT_WAIT,
            nullptr);
    }
    if (SUCCEEDED(result))
    {
        result = directDraw->SetDisplayMode(800, 600, 32, 0, 0);
    }
    if (SUCCEEDED(result))
    {
        viewport.dwWidth = 800;
        viewport.dwHeight = 600;
        result = device->SetViewport(&viewport);
    }
    if (SUCCEEDED(result))
    {
        result = device->BeginScene();
    }
    if (SUCCEEDED(result))
    {
        result = device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF102030, 1.0F, 0);
    }
    if (SUCCEEDED(result))
    {
        result = device->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTFG_LINEAR);
    }
    if (SUCCEEDED(result))
    {
        result = device->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTFN_LINEAR);
    }
    if (SUCCEEDED(result))
    {
        result = device->DrawPrimitive(
            D3DPT_TRIANGLEFAN,
            D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1,
            const_cast<TestVertex*>(vertices),
            ARRAYSIZE(vertices),
            0);
    }
    if (SUCCEEDED(result))
    {
        result = device->EndScene();
    }
    if (SUCCEEDED(result))
    {
        result = primarySurface->Blt(
            nullptr,
            renderTarget,
            nullptr,
            DDBLT_WAIT,
            nullptr);
    }

    IDirectDrawSurface7* flipPrimary = nullptr;
    IDirectDrawSurface7* flipBackBuffer = nullptr;
    if (SUCCEEDED(result))
    {
        result = directDraw->SetCooperativeLevel(
            window,
            DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
    }
    if (SUCCEEDED(result))
    {
        DDSURFACEDESC2 flipDescription{};
        flipDescription.dwSize = sizeof(flipDescription);
        flipDescription.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
        flipDescription.dwBackBufferCount = 1;
        flipDescription.ddsCaps.dwCaps =
            DDSCAPS_3DDEVICE |
            DDSCAPS_PRIMARYSURFACE |
            DDSCAPS_FLIP |
            DDSCAPS_COMPLEX;
        result = directDraw->CreateSurface(
            &flipDescription,
            &flipPrimary,
            nullptr);
    }
    if (SUCCEEDED(result))
    {
        DDSCAPS2 backBufferCaps{};
        backBufferCaps.dwCaps = DDSCAPS_BACKBUFFER;
        result = flipPrimary->GetAttachedSurface(
            &backBufferCaps,
            &flipBackBuffer);
    }
    if (SUCCEEDED(result))
    {
        if (!PostMessageW(window, WM_NULL, 0, 0))
        {
            result = HRESULT_FROM_WIN32(GetLastError());
        }
    }
    if (SUCCEEDED(result))
    {
        result = flipPrimary->Flip(nullptr, DDFLIP_WAIT);
    }
    if (SUCCEEDED(result))
    {
        MSG pending{};
        if (PeekMessageW(
            &pending,
            window,
            WM_NULL,
            WM_NULL,
            PM_NOREMOVE))
        {
            result = E_FAIL;
        }
    }
    if (flipBackBuffer)
    {
        flipBackBuffer->Release();
    }
    if (flipPrimary)
    {
        flipPrimary->Release();
    }
    if (device)
    {
        device->Release();
    }
    if (direct3D)
    {
        direct3D->Release();
    }
    if (renderTarget)
    {
        renderTarget->Release();
    }
    if (texture)
    {
        texture->Release();
    }
    if (uploadSurface)
    {
        uploadSurface->Release();
    }
    if (primarySurface)
    {
        primarySurface->Release();
    }
    const LONG uploadedTextureFormat = *lastTextureFormat;
    directDraw->Release();
    DestroyWindow(window);
    FreeLibrary(renderer);

    if (FAILED(result))
    {
        std::fprintf(stderr, "renderer initialization failed: 0x%08lX\n", result);
        return 7;
    }

    std::printf(
        "renderer smoke test passed: max texture %lux%lu, texture format %ld\n",
        caps.dwMaxTextureWidth,
        caps.dwMaxTextureHeight,
        uploadedTextureFormat);
    return 0;
}
