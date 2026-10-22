#define NOMINMAX

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{
    constexpr std::uintptr_t WidthAddress = 0x02E096F8;
    constexpr std::uintptr_t HeightAddress = 0x02E0970C;
    constexpr std::uintptr_t RenderSetupAddress = 0x00472DC0;
    constexpr std::uintptr_t HorizontalScaleAddress = 0x00654F5C;
    constexpr std::uintptr_t CameraAddress = 0x0056F1B0;
    constexpr std::uintptr_t ViewportAddress = 0x0054D490;
    constexpr std::uintptr_t RenderStateAddress = 0x0056FB2C;

    struct Viewport
    {
        std::uint16_t right;
        std::uint16_t bottom;
        std::uint16_t left;
        std::uint16_t top;
        std::uint16_t hither;
        std::uint16_t yon;
        std::uint16_t tanHalfFov;
        std::uint16_t zoom;
        std::uint16_t centerX;
        std::uint16_t centerY;
    };

    static_assert(offsetof(Viewport, tanHalfFov) == 0x0C);

    using RenderSetupFunction =
        void(__cdecl*)(std::uintptr_t, Viewport*, std::uintptr_t);

    constexpr std::uint16_t AspectCorrectTanHalfFov(
        std::uint16_t tanHalfFov,
        std::uint32_t width,
        std::uint32_t height)
    {
        if (tanHalfFov == 0 || width == 0 || height == 0)
        {
            return tanHalfFov;
        }

        const std::uint64_t numerator =
            static_cast<std::uint64_t>(tanHalfFov) * width * 3;
        const std::uint64_t denominator =
            static_cast<std::uint64_t>(height) * 4;
        const std::uint64_t corrected =
            (numerator + denominator / 2) / denominator;
        return static_cast<std::uint16_t>(std::min<std::uint64_t>(
            corrected, std::numeric_limits<std::uint16_t>::max()));
    }

    constexpr std::uint32_t AspectCorrectHorizontalScale(
        std::uint32_t horizontalScale,
        std::uint32_t width,
        std::uint32_t height)
    {
        if (horizontalScale == 0 || width == 0 || height == 0)
        {
            return horizontalScale;
        }

        const std::uint64_t numerator =
            static_cast<std::uint64_t>(horizontalScale) * height * 4;
        const std::uint64_t denominator =
            static_cast<std::uint64_t>(width) * 3;
        const std::uint64_t corrected =
            (numerator + denominator / 2) / denominator;
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(
            corrected, std::numeric_limits<std::uint32_t>::max()));
    }

    static_assert(AspectCorrectTanHalfFov(2365, 640, 480) == 2365);
    static_assert(AspectCorrectTanHalfFov(2365, 1920, 1080) == 3153);
    static_assert(AspectCorrectHorizontalScale(0x1999, 640, 480) == 0x1999);
    static_assert(AspectCorrectHorizontalScale(0x1999, 1920, 1080) == 0x1333);
}

extern "C" __declspec(dllexport) void __cdecl CameraAspectHook()
{
    const auto renderSetup =
        reinterpret_cast<RenderSetupFunction>(RenderSetupAddress);
    const std::uint32_t width =
        *reinterpret_cast<const std::uint32_t*>(WidthAddress);
    const std::uint32_t height =
        *reinterpret_cast<const std::uint32_t*>(HeightAddress);
    auto& viewport = *reinterpret_cast<Viewport*>(ViewportAddress);
    auto& horizontalScale =
        *reinterpret_cast<std::uint32_t*>(HorizontalScaleAddress);
    const std::uint16_t originalTanHalfFov = viewport.tanHalfFov;
    const std::uint32_t originalHorizontalScale = horizontalScale;
    viewport.tanHalfFov =
        AspectCorrectTanHalfFov(originalTanHalfFov, width, height);
    horizontalScale =
        AspectCorrectHorizontalScale(originalHorizontalScale, width, height);

    const std::uintptr_t renderState =
        *reinterpret_cast<const std::uintptr_t*>(RenderStateAddress);
    const std::uintptr_t orderingTable =
        *reinterpret_cast<const std::uintptr_t*>(renderState + 0x70);
    renderSetup(
        CameraAddress,
        &viewport,
        orderingTable);
    viewport.tanHalfFov = originalTanHalfFov;
    horizontalScale = originalHorizontalScale;
}
