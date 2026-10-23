#define NOMINMAX

#include "include/renderer_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")

extern "C"
{
    __declspec(dllexport) volatile LONG SpideyDrawCount = 0;
    __declspec(dllexport) volatile LONG SpideyPresentStarted = 0;
    __declspec(dllexport) volatile LONG SpideyPresentCompleted = 0;
    __declspec(dllexport) volatile LONG SpideySwapChainPresentCount = 0;
    __declspec(dllexport) volatile LONG SpideyLastPresentResult = S_OK;
    __declspec(dllexport) volatile LONG SpideyDeviceRemovedReason = S_OK;
    __declspec(dllexport) volatile LONG SpideyLastFrameHash = 0;
    __declspec(dllexport) volatile LONG SpideyLastPresentMicroseconds = 0;
    __declspec(dllexport) volatile LONG SpideyMaxPresentMicroseconds = 0;
    __declspec(dllexport) volatile LONG SpideyMsaaSamples = 0;
    __declspec(dllexport) volatile LONG SpideyAnisotropicFiltering = 0;
    __declspec(dllexport) volatile LONG SpideyVSync = 1;
    __declspec(dllexport) volatile LONG SpideyFpsLimit = 0;
    __declspec(dllexport) volatile LONG SpideyLastTextureFormat = DXGI_FORMAT_UNKNOWN;
}

namespace
{
    std::uint32_t CurrentFrameHash = 2166136261U;

    std::string ConfigPath()
    {
        std::array<char, MAX_PATH> path{};
        const DWORD length = GetModuleFileNameA(
            nullptr,
            path.data(),
            static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size())
        {
            return "Spidey.cfg";
        }

        std::string result(path.data(), length);
        const std::size_t separator = result.find_last_of("\\/");
        result.resize(separator == std::string::npos ? 0 : separator + 1);
        result += "Spidey.cfg";
        return result;
    }

    UINT ReadMsaaSamples()
    {
        const UINT samples = GetPrivateProfileIntA(
            "Display",
            "MSAA",
            0,
            ConfigPath().c_str());
        return samples == 2 || samples == 4 || samples == 8
            ? samples
            : 0;
    }

    UINT ReadAnisotropicFiltering()
    {
        const UINT anisotropy = GetPrivateProfileIntA(
            "Display",
            "AnisotropicFiltering",
            0,
            ConfigPath().c_str());
        return anisotropy == 2 ||
               anisotropy == 4 ||
               anisotropy == 8 ||
               anisotropy == 16
            ? anisotropy
            : 0;
    }

    bool ReadVSync()
    {
        return GetPrivateProfileIntA(
            "Display",
            "VSync",
            1,
            ConfigPath().c_str()) != 0;
    }

    UINT ReadFpsLimit()
    {
        const UINT fpsLimit = GetPrivateProfileIntA(
            "Display",
            "FPSLimit",
            0,
            ConfigPath().c_str());
        return fpsLimit <= 1000 ? fpsLimit : 0;
    }

    UINT ChooseMsaaSamples(ID3D11Device* device, UINT requested)
    {
        for (UINT samples = requested; samples >= 2; samples /= 2)
        {
            UINT colorQualityLevels = 0;
            UINT depthQualityLevels = 0;
            if (SUCCEEDED(device->CheckMultisampleQualityLevels(
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    samples,
                    &colorQualityLevels)) &&
                SUCCEEDED(device->CheckMultisampleQualityLevels(
                    DXGI_FORMAT_D16_UNORM,
                    samples,
                    &depthQualityLevels)) &&
                colorQualityLevels != 0 &&
                depthQualityLevels != 0)
            {
                return samples;
            }
        }
        return 1;
    }

    void HashFrameData(const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t index = 0; index < size; ++index)
        {
            CurrentFrameHash ^= bytes[index];
            CurrentFrameHash *= 16777619U;
        }
    }

    constexpr char VertexShaderSource[] = R"(
cbuffer FrameConstants : register(b0)
{
    float2 viewportSize;
    float2 framePadding;
};

struct VertexInput
{
    float4 position : POSITION;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
    float fogDepth : TEXCOORD1;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    float reciprocalW = abs(input.position.w) > 0.000001 ? input.position.w : 1.0;
    float clipW = 1.0 / reciprocalW;
    float2 ndc = float2(
        input.position.x * 2.0 / viewportSize.x - 1.0,
        1.0 - input.position.y * 2.0 / viewportSize.y);

    output.position = float4(
        ndc.x * clipW,
        ndc.y * clipW,
        input.position.z * clipW,
        clipW);
    output.color = input.color;
    output.texcoord = input.texcoord;
    output.fogDepth = input.position.z;
    return output;
}
)";

    constexpr char PixelShaderSource[] = R"(
Texture2D gameTexture : register(t0);
SamplerState gameSampler : register(s0);

cbuffer PixelConstants : register(b0)
{
    uint hasTexture;
    uint alphaTestEnabled;
    uint alphaFunction;
    float alphaReference;
    uint alphaOperation;
    uint fogEnabled;
    float fogStart;
    float fogEnd;
    float4 fogColor;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
    float fogDepth : TEXCOORD1;
};

bool AlphaPasses(float alpha)
{
    if (alphaFunction == 1) return false;
    if (alphaFunction == 2) return alpha < alphaReference;
    if (alphaFunction == 3) return abs(alpha - alphaReference) < (0.5 / 255.0);
    if (alphaFunction == 4) return alpha <= alphaReference;
    if (alphaFunction == 5) return alpha > alphaReference;
    if (alphaFunction == 6) return abs(alpha - alphaReference) >= (0.5 / 255.0);
    if (alphaFunction == 7) return alpha >= alphaReference;
    return true;
}

float4 main(PixelInput input) : SV_TARGET
{
    float4 color = input.color;
    if (hasTexture != 0)
    {
        float4 sampled = gameTexture.Sample(gameSampler, input.texcoord);
        color.rgb *= sampled.rgb;
        if (alphaOperation == 2)
        {
            color.a = sampled.a;
        }
        else if (alphaOperation == 4)
        {
            color.a *= sampled.a;
        }
    }
    if (alphaTestEnabled != 0 && !AlphaPasses(color.a))
    {
        discard;
    }
    if (fogEnabled != 0)
    {
        float fogRange = max(fogEnd - fogStart, 0.000001);
        float visibility = saturate((fogEnd - input.fogDepth) / fogRange);
        color.rgb = lerp(fogColor.rgb, color.rgb, visibility);
    }
    return color;
}
)";

    D3D11_BLEND ToBlend(DWORD blend)
    {
        switch (blend)
        {
        case 1: return D3D11_BLEND_ZERO;
        case 2: return D3D11_BLEND_ONE;
        case 3: return D3D11_BLEND_SRC_COLOR;
        case 4: return D3D11_BLEND_INV_SRC_COLOR;
        case 5: return D3D11_BLEND_SRC_ALPHA;
        case 6: return D3D11_BLEND_INV_SRC_ALPHA;
        case 7: return D3D11_BLEND_DEST_ALPHA;
        case 8: return D3D11_BLEND_INV_DEST_ALPHA;
        case 9: return D3D11_BLEND_DEST_COLOR;
        case 10: return D3D11_BLEND_INV_DEST_COLOR;
        case 11: return D3D11_BLEND_SRC_ALPHA_SAT;
        default: return D3D11_BLEND_ONE;
        }
    }

    float FloatFromDword(DWORD value)
    {
        float result = 0.0F;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    D3D11_COMPARISON_FUNC ToComparison(DWORD comparison)
    {
        if (comparison >= 1 && comparison <= 8)
        {
            return static_cast<D3D11_COMPARISON_FUNC>(comparison);
        }
        return D3D11_COMPARISON_ALWAYS;
    }

    D3D11_TEXTURE_ADDRESS_MODE ToAddressMode(DWORD address)
    {
        switch (address)
        {
        case 1: return D3D11_TEXTURE_ADDRESS_WRAP;
        case 2: return D3D11_TEXTURE_ADDRESS_MIRROR;
        case 3: return D3D11_TEXTURE_ADDRESS_CLAMP;
        case 4: return D3D11_TEXTURE_ADDRESS_BORDER;
        default: return D3D11_TEXTURE_ADDRESS_CLAMP;
        }
    }
}

namespace Spidey
{
    RendererRuntime& RendererRuntime::Instance()
    {
        // The renderer lives for the process lifetime. Deliberately avoid a
        // static destructor: DLL teardown runs under the Windows loader lock,
        // where releasing D3D11/DXGI objects can deadlock.
        static RendererRuntime* runtime = new RendererRuntime();
        return *runtime;
    }

    RendererRuntime::~RendererRuntime()
    {
        ReleaseDevice();
    }

    void RendererRuntime::SetWindow(HWND window, bool fullscreen)
    {
        if (m_window == window && m_fullscreen == fullscreen)
        {
            return;
        }

        ReleaseDevice();
        m_window = window;
        m_fullscreen = fullscreen;
    }

    void RendererRuntime::SetDisplayMode(UINT width, UINT height, UINT bitsPerPixel)
    {
        width = std::max(width, 1U);
        height = std::max(height, 1U);

        if (m_width == width && m_height == height && m_bitsPerPixel == bitsPerPixel)
        {
            return;
        }

        ReleaseDevice();
        m_width = width;
        m_height = height;
        m_bitsPerPixel = bitsPerPixel;
    }

    HRESULT RendererRuntime::EnsureDevice()
    {
        if (m_device)
        {
            return S_OK;
        }

        if (!m_window)
        {
            OutputDebugStringA("[SpideyRenderer] Cannot create D3D11 device without a game window.\n");
            return E_HANDLE;
        }

        DXGI_SWAP_CHAIN_DESC swapChainDesc{};
        swapChainDesc.BufferDesc.Width = m_width;
        swapChainDesc.BufferDesc.Height = m_height;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.OutputWindow = m_window;
        swapChainDesc.Windowed = m_fullscreen ? FALSE : TRUE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr D3D_FEATURE_LEVEL requestedLevels[] =
        {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        D3D_FEATURE_LEVEL createdLevel{};
        UINT flags = 0;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            requestedLevels,
            ARRAYSIZE(requestedLevels),
            D3D11_SDK_VERSION,
            &swapChainDesc,
            &m_swapChain,
            &m_device,
            &createdLevel,
            &m_context);
        if (FAILED(result))
        {
            OutputDebugStringA("[SpideyRenderer] D3D11CreateDeviceAndSwapChain failed.\n");
            ReleaseDevice();
            return result;
        }

        m_msaaSamples = ChooseMsaaSamples(m_device, ReadMsaaSamples());
        m_anisotropicFiltering = ReadAnisotropicFiltering();
        m_vsync = ReadVSync();
        m_fpsLimit = ReadFpsLimit();
        m_nextFrameCounter = 0;
        LARGE_INTEGER performanceFrequency{};
        if (QueryPerformanceFrequency(&performanceFrequency))
        {
            m_frameCounterFrequency = performanceFrequency.QuadPart;
        }
        else
        {
            m_frameCounterFrequency = 0;
            m_fpsLimit = 0;
        }
        InterlockedExchange(
            &SpideyMsaaSamples,
            m_msaaSamples == 1 ? 0 : static_cast<LONG>(m_msaaSamples));
        InterlockedExchange(
            &SpideyAnisotropicFiltering,
            static_cast<LONG>(m_anisotropicFiltering));
        InterlockedExchange(&SpideyVSync, m_vsync ? 1 : 0);
        InterlockedExchange(
            &SpideyFpsLimit,
            static_cast<LONG>(m_fpsLimit));

        result = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&m_backBuffer));
        if (SUCCEEDED(result) && m_msaaSamples > 1)
        {
            D3D11_TEXTURE2D_DESC colorDescription{};
            colorDescription.Width = m_width;
            colorDescription.Height = m_height;
            colorDescription.MipLevels = 1;
            colorDescription.ArraySize = 1;
            colorDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            colorDescription.SampleDesc.Count = m_msaaSamples;
            colorDescription.Usage = D3D11_USAGE_DEFAULT;
            colorDescription.BindFlags = D3D11_BIND_RENDER_TARGET;

            result = m_device->CreateTexture2D(
                &colorDescription,
                nullptr,
                &m_msaaColorTexture);
        }
        if (SUCCEEDED(result))
        {
            ID3D11Texture2D* renderTexture =
                m_msaaColorTexture ? m_msaaColorTexture : m_backBuffer;
            result = m_device->CreateRenderTargetView(
                renderTexture,
                nullptr,
                &m_renderTarget);
        }

        if (FAILED(result))
        {
            OutputDebugStringA("[SpideyRenderer] Failed to create the D3D11 render target.\n");
            ReleaseDevice();
            return result;
        }

        D3D11_TEXTURE2D_DESC depthDescription{};
        depthDescription.Width = m_width;
        depthDescription.Height = m_height;
        depthDescription.MipLevels = 1;
        depthDescription.ArraySize = 1;
        depthDescription.Format = DXGI_FORMAT_D16_UNORM;
        depthDescription.SampleDesc.Count = m_msaaSamples;
        depthDescription.Usage = D3D11_USAGE_DEFAULT;
        depthDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        result = m_device->CreateTexture2D(
            &depthDescription,
            nullptr,
            &m_depthTexture);
        if (SUCCEEDED(result))
        {
            result = m_device->CreateDepthStencilView(
                m_depthTexture,
                nullptr,
                &m_depthView);
        }
        if (FAILED(result))
        {
            OutputDebugStringA("[SpideyRenderer] Failed to create the D3D11 depth target.\n");
            ReleaseDevice();
            return result;
        }

        result = CreatePipelineResources();
        if (FAILED(result))
        {
            ReleaseDevice();
            return result;
        }

        m_context->OMSetRenderTargets(1, &m_renderTarget, m_depthView);

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(m_width);
        viewport.Height = static_cast<float>(m_height);
        viewport.MinDepth = 0.0F;
        viewport.MaxDepth = 1.0F;
        m_context->RSSetViewports(1, &viewport);

        OutputDebugStringA("[SpideyRenderer] D3D11 device and swap chain initialized.\n");
        return S_OK;
    }

    void RendererRuntime::Clear(
        DWORD flags,
        DWORD color,
        float depth,
        BYTE stencil)
    {
        if (FAILED(EnsureDevice()))
        {
            return;
        }

        if (flags & 1)
        {
            const float clearColor[] =
            {
                static_cast<float>((color >> 16) & 0xFF) / 255.0F,
                static_cast<float>((color >> 8) & 0xFF) / 255.0F,
                static_cast<float>(color & 0xFF) / 255.0F,
                static_cast<float>((color >> 24) & 0xFF) / 255.0F,
            };
            m_context->ClearRenderTargetView(m_renderTarget, clearColor);
        }

        UINT depthFlags = 0;
        if (flags & 2)
        {
            depthFlags |= D3D11_CLEAR_DEPTH;
        }
        if (flags & 4)
        {
            depthFlags |= D3D11_CLEAR_STENCIL;
        }
        if (depthFlags)
        {
            m_context->ClearDepthStencilView(
                m_depthView,
                depthFlags,
                depth,
                stencil);
        }
    }

    void RendererRuntime::SetViewport(const D3D11_VIEWPORT& viewport)
    {
        if (SUCCEEDED(EnsureDevice()))
        {
            m_context->RSSetViewports(1, &viewport);
        }
    }

    HRESULT RendererRuntime::DrawTriangleFan(
        const LegacyVertex* vertices,
        DWORD vertexCount,
        ID3D11ShaderResourceView* texture,
        const LegacyPipelineState& state)
    {
        if (!vertices || vertexCount < 3)
        {
            return E_INVALIDARG;
        }

        HRESULT result = EnsureDevice();
        if (FAILED(result))
        {
            return result;
        }

        HashFrameData(vertices, vertexCount * sizeof(*vertices));
        HashFrameData(&state, sizeof(state));
        HashFrameData(&texture, sizeof(texture));

        const UINT expandedCount = (vertexCount - 2) * 3;
        result = EnsureVertexCapacity(expandedCount);
        if (FAILED(result))
        {
            return result;
        }

        std::vector<LegacyVertex> expandedVertices;
        expandedVertices.reserve(expandedCount);
        for (DWORD index = 1; index + 1 < vertexCount; ++index)
        {
            expandedVertices.push_back(vertices[0]);
            expandedVertices.push_back(vertices[index]);
            expandedVertices.push_back(vertices[index + 1]);
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        result = m_context->Map(
            m_vertexBuffer,
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mapped);
        if (FAILED(result))
        {
            return result;
        }
        std::memcpy(
            mapped.pData,
            expandedVertices.data(),
            expandedVertices.size() * sizeof(LegacyVertex));
        m_context->Unmap(m_vertexBuffer, 0);

        result = ApplyPipelineState(state);
        if (FAILED(result))
        {
            return result;
        }

        struct FrameConstants
        {
            float viewportWidth;
            float viewportHeight;
            float padding[2];
        };
        const FrameConstants frame =
        {
            static_cast<float>(m_width),
            static_cast<float>(m_height),
            {},
        };
        m_context->UpdateSubresource(m_frameConstants, 0, nullptr, &frame, 0, 0);

        struct PixelConstants
        {
            UINT hasTexture;
            UINT alphaTestEnabled;
            UINT alphaFunction;
            float alphaReference;
            UINT alphaOperation;
            UINT fogEnabled;
            float fogStart;
            float fogEnd;
            float fogColor[4];
        };
        const DWORD fogColor = state.fogColor;
        const PixelConstants pixel =
        {
            texture ? 1U : 0U,
            state.alphaTestEnable ? 1U : 0U,
            state.alphaFunction,
            static_cast<float>(state.alphaReference & 0xFF) / 255.0F,
            state.alphaOperation,
            state.fogEnable ? 1U : 0U,
            FloatFromDword(state.fogStart),
            FloatFromDword(state.fogEnd),
            {
                static_cast<float>((fogColor >> 16) & 0xFF) / 255.0F,
                static_cast<float>((fogColor >> 8) & 0xFF) / 255.0F,
                static_cast<float>(fogColor & 0xFF) / 255.0F,
                1.0F,
            },
        };
        m_context->UpdateSubresource(m_pixelConstants, 0, nullptr, &pixel, 0, 0);

        const UINT stride = sizeof(LegacyVertex);
        const UINT offset = 0;
        m_context->IASetInputLayout(m_inputLayout);
        m_context->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_vertexShader, nullptr, 0);
        m_context->VSSetConstantBuffers(0, 1, &m_frameConstants);
        m_context->PSSetShader(m_pixelShader, nullptr, 0);
        m_context->PSSetConstantBuffers(0, 1, &m_pixelConstants);
        if (m_boundTexture != texture)
        {
            m_context->PSSetShaderResources(0, 1, &texture);
            m_boundTexture = texture;
        }
        m_context->PSSetSamplers(0, 1, &m_samplerState);
        m_context->Draw(expandedCount, 0);
        InterlockedIncrement(&SpideyDrawCount);

        return S_OK;
    }

    void RendererRuntime::UnbindTexture(ID3D11ShaderResourceView* texture)
    {
        if (m_boundTexture != texture)
        {
            return;
        }

        if (m_context)
        {
            ID3D11ShaderResourceView* noTexture = nullptr;
            m_context->PSSetShaderResources(0, 1, &noTexture);
        }
        m_boundTexture = nullptr;
    }

    void RendererRuntime::LimitFrameRate()
    {
        if (m_fpsLimit == 0 || m_frameCounterFrequency <= 0)
        {
            m_nextFrameCounter = 0;
            return;
        }

        const std::int64_t frameInterval =
            m_frameCounterFrequency / m_fpsLimit;
        if (frameInterval <= 0)
        {
            return;
        }

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        if (m_nextFrameCounter == 0)
        {
            m_nextFrameCounter = now.QuadPart + frameInterval;
            return;
        }

        while (now.QuadPart < m_nextFrameCounter)
        {
            const std::int64_t remaining =
                m_nextFrameCounter - now.QuadPart;
            if (remaining > m_frameCounterFrequency / 500)
            {
                Sleep(1);
            }
            else
            {
                SwitchToThread();
            }
            QueryPerformanceCounter(&now);
        }

        if (now.QuadPart - m_nextFrameCounter > frameInterval * 4)
        {
            m_nextFrameCounter = now.QuadPart + frameInterval;
        }
        else
        {
            m_nextFrameCounter += frameInterval;
        }
    }

    HRESULT RendererRuntime::Present()
    {
        const HRESULT result = EnsureDevice();
        if (FAILED(result))
        {
            return result;
        }

        if (!m_fullscreen)
        {
            MSG message{};
            while (PeekMessageA(&message, m_window, 0, 0, PM_REMOVE))
            {
                if (message.message != WM_SYSKEYDOWN &&
                    message.message != WM_SYSKEYUP)
                {
                    TranslateMessage(&message);
                    DispatchMessageA(&message);
                }
            }
        }

        LimitFrameRate();
        InterlockedIncrement(&SpideyPresentStarted);
        LARGE_INTEGER presentStart{};
        LARGE_INTEGER presentEnd{};
        LARGE_INTEGER performanceFrequency{};
        QueryPerformanceFrequency(&performanceFrequency);
        QueryPerformanceCounter(&presentStart);
        if (m_msaaColorTexture)
        {
            m_context->OMSetRenderTargets(0, nullptr, nullptr);
            m_context->ResolveSubresource(
                m_backBuffer,
                0,
                m_msaaColorTexture,
                0,
                DXGI_FORMAT_R8G8B8A8_UNORM);
        }
        HRESULT presentResult = m_swapChain->Present(m_vsync ? 1 : 0, 0);
        if (m_msaaColorTexture && SUCCEEDED(presentResult))
        {
            m_context->OMSetRenderTargets(
                1,
                &m_renderTarget,
                m_depthView);
        }
        InterlockedIncrement(&SpideySwapChainPresentCount);
        QueryPerformanceCounter(&presentEnd);
        const LONG presentMicroseconds = static_cast<LONG>(
            (presentEnd.QuadPart - presentStart.QuadPart) * 1000000LL /
            performanceFrequency.QuadPart);
        InterlockedExchange(
            &SpideyLastPresentMicroseconds,
            presentMicroseconds);
        LONG maximum = SpideyMaxPresentMicroseconds;
        while (presentMicroseconds > maximum)
        {
            const LONG observed = InterlockedCompareExchange(
                &SpideyMaxPresentMicroseconds,
                presentMicroseconds,
                maximum);
            if (observed == maximum)
            {
                break;
            }
            maximum = observed;
        }
        InterlockedExchange(&SpideyLastPresentResult, presentResult);
        if (FAILED(presentResult))
        {
            InterlockedExchange(
                &SpideyDeviceRemovedReason,
                m_device->GetDeviceRemovedReason());
        }
        InterlockedIncrement(&SpideyPresentCompleted);
        InterlockedExchange(
            &SpideyLastFrameHash,
            static_cast<LONG>(CurrentFrameHash));
        CurrentFrameHash = 2166136261U;
        return presentResult;
    }

    HRESULT RendererRuntime::CreatePipelineResources()
    {
        ID3DBlob* vertexBytecode = nullptr;
        ID3DBlob* pixelBytecode = nullptr;
        ID3DBlob* errors = nullptr;

        HRESULT result = D3DCompile(
            VertexShaderSource,
            sizeof(VertexShaderSource) - 1,
            "SpideyVertexShader",
            nullptr,
            nullptr,
            "main",
            "vs_4_0",
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            &vertexBytecode,
            &errors);
        if (FAILED(result))
        {
            if (errors)
            {
                OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
                errors->Release();
            }
            OutputDebugStringA("[SpideyRenderer] Vertex shader compilation failed.\n");
            return result;
        }
        if (errors)
        {
            errors->Release();
            errors = nullptr;
        }

        result = D3DCompile(
            PixelShaderSource,
            sizeof(PixelShaderSource) - 1,
            "SpideyPixelShader",
            nullptr,
            nullptr,
            "main",
            "ps_4_0",
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            &pixelBytecode,
            &errors);
        if (FAILED(result))
        {
            if (errors)
            {
                OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
                errors->Release();
            }
            vertexBytecode->Release();
            OutputDebugStringA("[SpideyRenderer] Pixel shader compilation failed.\n");
            return result;
        }
        if (errors)
        {
            errors->Release();
        }

        result = m_device->CreateVertexShader(
            vertexBytecode->GetBufferPointer(),
            vertexBytecode->GetBufferSize(),
            nullptr,
            &m_vertexShader);
        if (SUCCEEDED(result))
        {
            result = m_device->CreatePixelShader(
                pixelBytecode->GetBufferPointer(),
                pixelBytecode->GetBufferSize(),
                nullptr,
                &m_pixelShader);
        }

        constexpr D3D11_INPUT_ELEMENT_DESC inputElements[] =
        {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        if (SUCCEEDED(result))
        {
            result = m_device->CreateInputLayout(
                inputElements,
                ARRAYSIZE(inputElements),
                vertexBytecode->GetBufferPointer(),
                vertexBytecode->GetBufferSize(),
                &m_inputLayout);
        }

        vertexBytecode->Release();
        pixelBytecode->Release();
        if (FAILED(result))
        {
            OutputDebugStringA("[SpideyRenderer] Failed to create shader pipeline objects.\n");
            return result;
        }

        D3D11_BUFFER_DESC constantBuffer{};
        constantBuffer.ByteWidth = 16;
        constantBuffer.Usage = D3D11_USAGE_DEFAULT;
        constantBuffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        result = m_device->CreateBuffer(
            &constantBuffer,
            nullptr,
            &m_frameConstants);
        if (SUCCEEDED(result))
        {
            constantBuffer.ByteWidth = 48;
            result = m_device->CreateBuffer(
                &constantBuffer,
                nullptr,
                &m_pixelConstants);
        }
        if (FAILED(result))
        {
            OutputDebugStringA("[SpideyRenderer] Failed to create shader constant buffers.\n");
        }
        return result;
    }

    HRESULT RendererRuntime::EnsureVertexCapacity(UINT vertexCount)
    {
        if (m_vertexBuffer && m_vertexCapacity >= vertexCount)
        {
            return S_OK;
        }

        if (m_vertexBuffer)
        {
            m_vertexBuffer->Release();
            m_vertexBuffer = nullptr;
        }

        m_vertexCapacity = std::max(vertexCount, 256U);
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = m_vertexCapacity * sizeof(LegacyVertex);
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return m_device->CreateBuffer(
            &description,
            nullptr,
            &m_vertexBuffer);
    }

    HRESULT RendererRuntime::ApplyPipelineState(const LegacyPipelineState& state)
    {
        if (m_hasAppliedState &&
            std::memcmp(&m_appliedState, &state, sizeof(state)) == 0)
        {
            return S_OK;
        }

        const std::uint32_t blendKey =
            (state.alphaBlendEnable ? 1U : 0U) |
            ((state.sourceBlend & 0xFU) << 1U) |
            ((state.destinationBlend & 0xFU) << 5U);
        auto blendEntry = m_blendStates.find(blendKey);
        HRESULT result = S_OK;
        if (blendEntry == m_blendStates.end())
        {
            D3D11_BLEND_DESC blend{};
            blend.RenderTarget[0].BlendEnable = state.alphaBlendEnable != 0;
            blend.RenderTarget[0].SrcBlend = ToBlend(state.sourceBlend);
            blend.RenderTarget[0].DestBlend = ToBlend(state.destinationBlend);
            blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            ID3D11BlendState* created = nullptr;
            result = m_device->CreateBlendState(&blend, &created);
            if (SUCCEEDED(result))
            {
                blendEntry = m_blendStates.emplace(blendKey, created).first;
            }
        }
        if (FAILED(result))
        {
            return result;
        }
        m_blendState = blendEntry->second;

        const std::uint32_t depthKey =
            (state.zEnable ? 1U : 0U) |
            ((state.zWriteEnable ? 1U : 0U) << 1U) |
            ((state.zFunction & 0xFU) << 2U);
        auto depthEntry = m_depthStates.find(depthKey);
        if (depthEntry == m_depthStates.end())
        {
            D3D11_DEPTH_STENCIL_DESC depth{};
            depth.DepthEnable = state.zEnable != 0;
            depth.DepthWriteMask =
                state.zWriteEnable
                    ? D3D11_DEPTH_WRITE_MASK_ALL
                    : D3D11_DEPTH_WRITE_MASK_ZERO;
            depth.DepthFunc = ToComparison(state.zFunction);
            ID3D11DepthStencilState* created = nullptr;
            result = m_device->CreateDepthStencilState(&depth, &created);
            if (SUCCEEDED(result))
            {
                depthEntry = m_depthStates.emplace(depthKey, created).first;
            }
        }
        if (FAILED(result))
        {
            return result;
        }
        m_depthState = depthEntry->second;

        const std::uint32_t rasterKey = state.cullMode;
        auto rasterEntry = m_rasterStates.find(rasterKey);
        if (rasterEntry == m_rasterStates.end())
        {
            D3D11_RASTERIZER_DESC raster{};
            raster.FillMode = D3D11_FILL_SOLID;
            raster.FrontCounterClockwise = FALSE;
            raster.DepthClipEnable = TRUE;
            raster.MultisampleEnable = m_msaaSamples > 1;
            if (state.cullMode == 2)
            {
                raster.CullMode = D3D11_CULL_FRONT;
            }
            else if (state.cullMode == 3)
            {
                raster.CullMode = D3D11_CULL_BACK;
            }
            else
            {
                raster.CullMode = D3D11_CULL_NONE;
            }
            ID3D11RasterizerState* created = nullptr;
            result = m_device->CreateRasterizerState(&raster, &created);
            if (SUCCEEDED(result))
            {
                rasterEntry = m_rasterStates.emplace(rasterKey, created).first;
            }
        }
        if (FAILED(result))
        {
            return result;
        }
        m_rasterState = rasterEntry->second;

        const bool linear =
            state.magnificationFilter == 2 || state.minificationFilter == 2;
        const bool anisotropic = linear && m_anisotropicFiltering != 0;
        const D3D11_TEXTURE_ADDRESS_MODE addressU = ToAddressMode(state.addressU);
        const D3D11_TEXTURE_ADDRESS_MODE addressV = ToAddressMode(state.addressV);
        const std::uint32_t samplerKey =
            (linear ? 1U : 0U) |
            (static_cast<std::uint32_t>(addressU) << 1U) |
            (static_cast<std::uint32_t>(addressV) << 5U);
        auto samplerEntry = m_samplerStates.find(samplerKey);
        if (samplerEntry == m_samplerStates.end())
        {
            D3D11_SAMPLER_DESC sampler{};
            sampler.Filter = anisotropic
                ? D3D11_FILTER_ANISOTROPIC
                : linear
                    ? D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT
                    : D3D11_FILTER_MIN_MAG_MIP_POINT;
            sampler.MaxAnisotropy =
                anisotropic ? m_anisotropicFiltering : 1;
            sampler.AddressU = addressU;
            sampler.AddressV = addressV;
            sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.MaxLOD = D3D11_FLOAT32_MAX;
            ID3D11SamplerState* created = nullptr;
            result = m_device->CreateSamplerState(&sampler, &created);
            if (SUCCEEDED(result))
            {
                samplerEntry = m_samplerStates.emplace(samplerKey, created).first;
            }
        }
        if (FAILED(result))
        {
            return result;
        }
        m_samplerState = samplerEntry->second;

        constexpr float blendFactor[] = {0.0F, 0.0F, 0.0F, 0.0F};
        m_context->OMSetBlendState(m_blendState, blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_depthState, 0);
        m_context->RSSetState(m_rasterState);
        m_appliedState = state;
        m_hasAppliedState = true;
        return S_OK;
    }

    void RendererRuntime::ReleaseDevice()
    {
        ++m_deviceGeneration;
        m_boundTexture = nullptr;
        if (m_context)
        {
            m_context->ClearState();
        }
        for (const auto& entry : m_samplerStates)
        {
            entry.second->Release();
        }
        m_samplerStates.clear();
        m_samplerState = nullptr;
        for (const auto& entry : m_rasterStates)
        {
            entry.second->Release();
        }
        m_rasterStates.clear();
        m_rasterState = nullptr;
        for (const auto& entry : m_depthStates)
        {
            entry.second->Release();
        }
        m_depthStates.clear();
        m_depthState = nullptr;
        for (const auto& entry : m_blendStates)
        {
            entry.second->Release();
        }
        m_blendStates.clear();
        m_blendState = nullptr;
        if (m_vertexBuffer)
        {
            m_vertexBuffer->Release();
            m_vertexBuffer = nullptr;
        }
        if (m_pixelConstants)
        {
            m_pixelConstants->Release();
            m_pixelConstants = nullptr;
        }
        if (m_frameConstants)
        {
            m_frameConstants->Release();
            m_frameConstants = nullptr;
        }
        if (m_inputLayout)
        {
            m_inputLayout->Release();
            m_inputLayout = nullptr;
        }
        if (m_pixelShader)
        {
            m_pixelShader->Release();
            m_pixelShader = nullptr;
        }
        if (m_vertexShader)
        {
            m_vertexShader->Release();
            m_vertexShader = nullptr;
        }
        if (m_depthView)
        {
            m_depthView->Release();
            m_depthView = nullptr;
        }
        if (m_depthTexture)
        {
            m_depthTexture->Release();
            m_depthTexture = nullptr;
        }
        if (m_renderTarget)
        {
            m_renderTarget->Release();
            m_renderTarget = nullptr;
        }
        if (m_msaaColorTexture)
        {
            m_msaaColorTexture->Release();
            m_msaaColorTexture = nullptr;
        }
        if (m_backBuffer)
        {
            m_backBuffer->Release();
            m_backBuffer = nullptr;
        }
        if (m_swapChain)
        {
            if (m_fullscreen)
            {
                m_swapChain->SetFullscreenState(FALSE, nullptr);
            }
            m_swapChain->Release();
            m_swapChain = nullptr;
        }
        if (m_context)
        {
            m_context->Release();
            m_context = nullptr;
        }
        if (m_device)
        {
            m_device->Release();
            m_device = nullptr;
        }
        m_vertexCapacity = 0;
        m_hasAppliedState = false;
        m_msaaSamples = 1;
        m_anisotropicFiltering = 0;
        m_vsync = true;
        m_fpsLimit = 0;
        m_frameCounterFrequency = 0;
        m_nextFrameCounter = 0;
        InterlockedExchange(&SpideyMsaaSamples, 0);
        InterlockedExchange(&SpideyAnisotropicFiltering, 0);
        InterlockedExchange(&SpideyVSync, 1);
        InterlockedExchange(&SpideyFpsLimit, 0);
    }
}
