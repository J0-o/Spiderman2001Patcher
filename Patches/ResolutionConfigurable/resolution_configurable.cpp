#define NOMINMAX

#include <Windows.h>
#include <ddraw.h>

#include <array>
#include <cstdint>
#include <string>

namespace
{
    constexpr std::uintptr_t SceneSurfaceAddress = 0x006B7908;
    constexpr std::uintptr_t MovieSurfaceAddress = 0x00AC0A3C;

    template<typename T>
    T& GameValue(std::uintptr_t address)
    {
        return *reinterpret_cast<T*>(address);
    }

    std::string ConfigPath()
    {
        std::array<char, MAX_PATH> path{};
        const DWORD length = GetModuleFileNameA(nullptr, path.data(),
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

    bool StretchMovies()
    {
        static const bool stretch =
            GetPrivateProfileIntA("Display", "StretchMovies", 0,
                ConfigPath().c_str()) != 0;
        return stretch;
    }
}

extern "C" __declspec(dllexport) HRESULT MovieBltHook()
{
    IDirectDrawSurface7* const scene =
        GameValue<IDirectDrawSurface7*>(SceneSurfaceAddress);
    IDirectDrawSurface7* const movie =
        GameValue<IDirectDrawSurface7*>(MovieSurfaceAddress);
    if (scene == nullptr || movie == nullptr)
    {
        return DDERR_INVALIDOBJECT;
    }

    DDBLTFX bltEffects{};
    bltEffects.dwSize = sizeof(bltEffects);
    if (StretchMovies())
    {
        return scene->Blt(nullptr, movie, nullptr, DDBLT_WAIT, &bltEffects);
    }

    DDSURFACEDESC2 sceneDescription{};
    sceneDescription.dwSize = sizeof(sceneDescription);
    DDSURFACEDESC2 movieDescription{};
    movieDescription.dwSize = sizeof(movieDescription);
    if (FAILED(scene->GetSurfaceDesc(&sceneDescription)) ||
        FAILED(movie->GetSurfaceDesc(&movieDescription)) ||
        sceneDescription.dwWidth == 0 || sceneDescription.dwHeight == 0 ||
        movieDescription.dwWidth == 0 || movieDescription.dwHeight == 0)
    {
        return scene->Blt(nullptr, movie, nullptr, DDBLT_WAIT, &bltEffects);
    }

    const std::uint64_t widthLimitedHeight =
        static_cast<std::uint64_t>(sceneDescription.dwWidth) *
        movieDescription.dwHeight / movieDescription.dwWidth;

    DWORD destinationWidth = sceneDescription.dwWidth;
    DWORD destinationHeight = static_cast<DWORD>(widthLimitedHeight);
    if (destinationHeight > sceneDescription.dwHeight)
    {
        destinationHeight = sceneDescription.dwHeight;
        destinationWidth = static_cast<DWORD>(
            static_cast<std::uint64_t>(sceneDescription.dwHeight) *
            movieDescription.dwWidth / movieDescription.dwHeight);
    }

    RECT destination
    {
        static_cast<LONG>((sceneDescription.dwWidth - destinationWidth) / 2),
        static_cast<LONG>((sceneDescription.dwHeight - destinationHeight) / 2),
        static_cast<LONG>((sceneDescription.dwWidth + destinationWidth) / 2),
        static_cast<LONG>((sceneDescription.dwHeight + destinationHeight) / 2),
    };

    DDBLTFX clearEffects{};
    clearEffects.dwSize = sizeof(clearEffects);
    clearEffects.dwFillColor = 0;
    scene->Blt(nullptr, nullptr, nullptr,
        DDBLT_COLORFILL | DDBLT_WAIT, &clearEffects);

    return scene->Blt(&destination, movie, nullptr, DDBLT_WAIT, &bltEffects);
}
