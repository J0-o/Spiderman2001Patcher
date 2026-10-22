#define NOMINMAX

#include <Windows.h>

#include <cstdint>

namespace
{
    constexpr std::uintptr_t DrawDistanceAddress = 0x0064E560;
    constexpr std::int16_t MaximumDrawDistance = 0x7FFF;
}

extern "C" __declspec(dllexport) void __cdecl DrawDistanceHook()
{
    *reinterpret_cast<volatile std::int16_t*>(DrawDistanceAddress) =
        MaximumDrawDistance;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
