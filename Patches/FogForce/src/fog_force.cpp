#include <Windows.h>
#include <d3d.h>

#include <cstdint>
#include <cstring>

namespace
{
    constexpr std::uintptr_t Direct3DDeviceAddress = 0x006B791C;
    constexpr std::uintptr_t FogStartAddress = 0x00568188;
    constexpr std::uintptr_t FogEndAddress = 0x0056818C;
    constexpr std::uintptr_t FogColorAddress = 0x00AC08CC;
    constexpr std::uintptr_t SetFogParamsAddress = 0x00505DB0;
    constexpr std::uintptr_t BeginSceneAddress = 0x00505E00;

    using SetFogParamsFunction =
        void(__cdecl*)(float, float, std::uint32_t);
    using BeginSceneFunction =
        void(__cdecl*)(std::uint32_t, int);

    DWORD FloatBits(float value)
    {
        DWORD result = 0;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    void ForceFogState()
    {
        IDirect3DDevice7* device =
            *reinterpret_cast<IDirect3DDevice7**>(Direct3DDeviceAddress);
        if (!device)
        {
            return;
        }

        const float start =
            *reinterpret_cast<const float*>(FogStartAddress);
        const float end =
            *reinterpret_cast<const float*>(FogEndAddress);
        const DWORD color =
            *reinterpret_cast<const DWORD*>(FogColorAddress) & 0x00FFFFFF;
        if (end <= start)
        {
            return;
        }

        device->SetRenderState(D3DRENDERSTATE_FOGCOLOR, color);
        device->SetRenderState(D3DRENDERSTATE_FOGSTART, FloatBits(start));
        device->SetRenderState(D3DRENDERSTATE_FOGEND, FloatBits(end));
        device->SetRenderState(
            D3DRENDERSTATE_FOGTABLEMODE,
            D3DFOG_LINEAR);
        device->SetRenderState(
            D3DRENDERSTATE_FOGVERTEXMODE,
            D3DFOG_NONE);
        device->SetRenderState(D3DRENDERSTATE_RANGEFOGENABLE, FALSE);
        device->SetRenderState(D3DRENDERSTATE_FOGENABLE, TRUE);
    }
}

extern "C" __declspec(dllexport) void __cdecl FogParamsHook(
    float start,
    float end,
    std::uint32_t color)
{
    reinterpret_cast<SetFogParamsFunction>(SetFogParamsAddress)(
        start,
        end,
        color);
    ForceFogState();
}

extern "C" __declspec(dllexport) void __cdecl FogBeginSceneHook(
    std::uint32_t flags,
    int texture)
{
    reinterpret_cast<BeginSceneFunction>(BeginSceneAddress)(flags, texture);
    ForceFogState();
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
