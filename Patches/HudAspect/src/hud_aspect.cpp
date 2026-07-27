#include <Windows.h>

#include <cstdint>

namespace
{
    constexpr std::uintptr_t PanelDisplayAddress = 0x004658C0;
    constexpr std::uintptr_t RenderWidthAddress = 0x00568154;
    constexpr std::uintptr_t RenderHeightAddress = 0x00568158;

    volatile LONG HudDisplayDepth = 0;

    using PanelDisplayFunction = void(__cdecl*)();

    bool GetHudHorizontalTransform(float& screenWidth, float& scale)
    {
        if (HudDisplayDepth <= 0)
        {
            return false;
        }

        const auto width =
            *reinterpret_cast<const std::int32_t*>(RenderWidthAddress);
        const auto height =
            *reinterpret_cast<const std::int32_t*>(RenderHeightAddress);
        if (width <= 0 || height <= 0)
        {
            return false;
        }

        const float fourByThreeWidth =
            static_cast<float>(height) * (4.0f / 3.0f);
        if (fourByThreeWidth >= static_cast<float>(width))
        {
            return false;
        }

        screenWidth = static_cast<float>(width);
        scale = fourByThreeWidth / static_cast<float>(width);
        return true;
    }

    void CorrectHudQuad(float* arguments)
    {
        float screenWidth;
        float horizontalScale;
        if (!GetHudHorizontalTransform(screenWidth, horizontalScale))
        {
            return;
        }

        constexpr int VertexStride = 5;
        constexpr int VertexCount = 4;
        const float anchor = screenWidth * 0.5f;

        for (int vertex = 0; vertex < VertexCount; ++vertex)
        {
            float& x = arguments[vertex * VertexStride];
            x = anchor + (x - anchor) * horizontalScale;
        }
    }

    void CorrectHudRectangle(float* arguments)
    {
        float screenWidth;
        float horizontalScale;
        if (!GetHudHorizontalTransform(screenWidth, horizontalScale))
        {
            return;
        }

        const float anchor = screenWidth * 0.5f;
        arguments[0] =
            anchor + (arguments[0] - anchor) * horizontalScale;
        arguments[2] *= horizontalScale;
    }
}

extern "C" __declspec(dllexport) void __cdecl HudDisplayHook()
{
    InterlockedIncrement(&HudDisplayDepth);
    reinterpret_cast<PanelDisplayFunction>(PanelDisplayAddress)();
    InterlockedDecrement(&HudDisplayDepth);
}

// The runtime reaches this export after PUSHAD/PUSHFD. PUSHAD's saved ESP,
// at [ESP+0x14] on export entry, points to the original return address.
// The quad's first argument follows it.
extern "C" __declspec(dllexport, naked) void HudQuadHook()
{
    __asm
    {
        mov eax, dword ptr [esp + 20]
        add eax, 4
        push eax
        call CorrectHudQuad
        add esp, 4
        ret
    }
}

extern "C" __declspec(dllexport, naked) void HudRectangleHook()
{
    __asm
    {
        mov eax, dword ptr [esp + 20]
        add eax, 4
        push eax
        call CorrectHudRectangle
        add esp, 4
        ret
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
