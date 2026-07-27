#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>
#include <unordered_map>

namespace Spidey
{
    struct LegacyVertex
    {
        float x;
        float y;
        float z;
        float rhw;
        DWORD color;
        float u;
        float v;
    };

    static_assert(sizeof(LegacyVertex) == 28);

    struct LegacyPipelineState
    {
        DWORD zEnable = 1;
        DWORD zWriteEnable = 1;
        DWORD zFunction = 4;
        DWORD alphaBlendEnable = 0;
        DWORD sourceBlend = 2;
        DWORD destinationBlend = 1;
        DWORD alphaTestEnable = 0;
        DWORD alphaReference = 0;
        DWORD alphaFunction = 8;
        DWORD cullMode = 1;
        DWORD fogEnable = 0;
        DWORD fogColor = 0x00FFFFFF;
        DWORD fogStart = 0;
        DWORD fogEnd = 0;
        DWORD addressU = 3;
        DWORD addressV = 3;
        DWORD magnificationFilter = 1;
        DWORD minificationFilter = 1;
        DWORD alphaOperation = 4;
    };

    class RendererRuntime final
    {
    public:
        static RendererRuntime& Instance();

        void SetWindow(HWND window, bool fullscreen);
        void SetDisplayMode(UINT width, UINT height, UINT bitsPerPixel);

        HRESULT EnsureDevice();
        void Clear(DWORD flags, DWORD color, float depth, BYTE stencil);
        void SetViewport(const D3D11_VIEWPORT& viewport);
        HRESULT DrawTriangleFan(
            const LegacyVertex* vertices,
            DWORD vertexCount,
            ID3D11ShaderResourceView* texture,
            const LegacyPipelineState& state);
        void UnbindTexture(ID3D11ShaderResourceView* texture);
        HRESULT Present();

        ID3D11Device* Device() const { return m_device; }
        ID3D11DeviceContext* Context() const { return m_context; }
        std::uint64_t DeviceGeneration() const { return m_deviceGeneration; }
        UINT Width() const { return m_width; }
        UINT Height() const { return m_height; }
        UINT BitsPerPixel() const { return m_bitsPerPixel; }
        HWND Window() const { return m_window; }

    private:
        RendererRuntime() = default;
        ~RendererRuntime();

        RendererRuntime(const RendererRuntime&) = delete;
        RendererRuntime& operator=(const RendererRuntime&) = delete;

        void ReleaseDevice();
        HRESULT CreatePipelineResources();
        HRESULT EnsureVertexCapacity(UINT vertexCount);
        HRESULT ApplyPipelineState(const LegacyPipelineState& state);
        void LimitFrameRate();

        HWND m_window = nullptr;
        bool m_fullscreen = false;
        UINT m_width = 640;
        UINT m_height = 480;
        UINT m_bitsPerPixel = 32;
        ID3D11Device* m_device = nullptr;
        ID3D11DeviceContext* m_context = nullptr;
        IDXGISwapChain* m_swapChain = nullptr;
        ID3D11Texture2D* m_backBuffer = nullptr;
        ID3D11Texture2D* m_msaaColorTexture = nullptr;
        ID3D11RenderTargetView* m_renderTarget = nullptr;
        ID3D11Texture2D* m_depthTexture = nullptr;
        ID3D11DepthStencilView* m_depthView = nullptr;
        UINT m_msaaSamples = 1;
        UINT m_anisotropicFiltering = 0;
        bool m_vsync = true;
        UINT m_fpsLimit = 0;
        std::int64_t m_frameCounterFrequency = 0;
        std::int64_t m_nextFrameCounter = 0;
        ID3D11VertexShader* m_vertexShader = nullptr;
        ID3D11PixelShader* m_pixelShader = nullptr;
        ID3D11InputLayout* m_inputLayout = nullptr;
        ID3D11Buffer* m_frameConstants = nullptr;
        ID3D11Buffer* m_pixelConstants = nullptr;
        ID3D11Buffer* m_vertexBuffer = nullptr;
        UINT m_vertexCapacity = 0;
        ID3D11BlendState* m_blendState = nullptr;
        ID3D11DepthStencilState* m_depthState = nullptr;
        ID3D11RasterizerState* m_rasterState = nullptr;
        ID3D11SamplerState* m_samplerState = nullptr;
        std::unordered_map<std::uint32_t, ID3D11BlendState*> m_blendStates;
        std::unordered_map<std::uint32_t, ID3D11DepthStencilState*> m_depthStates;
        std::unordered_map<std::uint32_t, ID3D11RasterizerState*> m_rasterStates;
        std::unordered_map<std::uint32_t, ID3D11SamplerState*> m_samplerStates;
        ID3D11ShaderResourceView* m_boundTexture = nullptr;
        LegacyPipelineState m_appliedState{};
        bool m_hasAppliedState = false;
        std::uint64_t m_deviceGeneration = 0;
    };
}
