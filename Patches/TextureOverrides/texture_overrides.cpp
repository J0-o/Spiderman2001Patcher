#define NOMINMAX

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C"
{
    __declspec(dllexport) volatile LONG SpideyTextureOverrideCount = 0;
    __declspec(dllexport) volatile LONG SpideyTextureOverrideErrorCount = 0;
}

namespace
{
    constexpr std::uintptr_t CreateTextureAddress = 0x0050F6D0;
    constexpr std::uintptr_t CreateIndexed256Address = 0x0050E1A0;
    constexpr std::uintptr_t CreateIndexed16Address = 0x0050E880;
    constexpr std::uintptr_t PreferredTextureFormatAddress = 0x00ADB36C;
    constexpr std::uintptr_t Argb8888FormatFlagsAddress = 0x0056897C;
    constexpr int Argb8888FormatIndex = 3;
    constexpr unsigned LinearArgb8888Type = 0x0907;
    constexpr std::uintptr_t CurrentRegionAddress = 0x006AB8B8;
    constexpr std::uintptr_t RegionsAddress = 0x006B2440;
    constexpr int RegionCount = 40;
    constexpr std::size_t RegionSize = 0x44;
    constexpr std::size_t RegionPsxOffset = 0x18;
    constexpr std::size_t RegionLowResOffset = 0x3B;
    constexpr int TextureCount = 0x400;
    constexpr std::size_t PathCapacity = 512;
    constexpr std::uint64_t MaximumBmpBytes = 64ULL * 1024ULL * 1024ULL;

    using CreateTextureFunction = int(__cdecl*)(
        int,
        unsigned,
        unsigned,
        unsigned,
        const void*,
        unsigned,
        const char*,
        unsigned);

    using CreateIndexedFunction = unsigned(__cdecl*)(
        unsigned,
        unsigned,
        const void*,
        const void*,
        unsigned,
        unsigned,
        unsigned,
        unsigned);

    struct OverrideState
    {
        char* path;
    };

    struct PendingIndexedOverride
    {
        bool active;
        unsigned width;
        unsigned height;
        char path[PathCapacity];
    };

    struct DecodedBmp
    {
        std::uint32_t* pixels;
    };

    char g_gameDirectory[PathCapacity] = {};
    OverrideState g_overrides[TextureCount] = {};
    PendingIndexedOverride g_pending = {};

    std::uint16_t ReadU16(const std::uint8_t* data)
    {
        return static_cast<std::uint16_t>(
            data[0] | (static_cast<std::uint16_t>(data[1]) << 8));
    }

    std::uint32_t ReadU32(const std::uint8_t* data)
    {
        return static_cast<std::uint32_t>(data[0]) |
            (static_cast<std::uint32_t>(data[1]) << 8) |
            (static_cast<std::uint32_t>(data[2]) << 16) |
            (static_cast<std::uint32_t>(data[3]) << 24);
    }

    bool FileExists(const char* path)
    {
        const DWORD attributes = GetFileAttributesA(path);
        return attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    void LogError(const char* path, const char* reason)
    {
        char message[768];
        _snprintf_s(
            message,
            sizeof(message),
            _TRUNCATE,
            "[TextureOverrides] Ignoring '%s': %s\n",
            path ? path : "",
            reason);
        OutputDebugStringA(message);
        InterlockedIncrement(&SpideyTextureOverrideErrorCount);
    }

    bool AppendPath(char* destination, std::size_t capacity, const char* suffix)
    {
        return strcat_s(destination, capacity, suffix) == 0;
    }

    bool BuildRootedPath(char* output, const char* relative)
    {
        if (strcpy_s(output, PathCapacity, g_gameDirectory) != 0 ||
            !AppendPath(output, PathCapacity, "\\TextureOverrides\\") ||
            !AppendPath(output, PathCapacity, relative))
        {
            return false;
        }
        return true;
    }

    void SanitizeBundleName(char* output, const char* input)
    {
        std::size_t i = 0;
        for (; i < 9 && input[i] != '\0'; ++i)
        {
            const unsigned char c = static_cast<unsigned char>(input[i]);
            const bool safe =
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-';
            output[i] = safe ? static_cast<char>(c) : '_';
        }
        output[i] = '\0';
    }

    bool SelectBundleFile(char* output, const char* directory, unsigned offset)
    {
        char relative[PathCapacity];
        _snprintf_s(
            relative,
            sizeof(relative),
            _TRUNCATE,
            "%s\\%08X.bmp",
            directory,
            offset);
        if (!BuildRootedPath(output, relative))
        {
            return false;
        }
        if (FileExists(output))
        {
            return true;
        }

        _snprintf_s(
            relative,
            sizeof(relative),
            _TRUNCATE,
            "%s\\%08X_*.bmp",
            directory,
            offset);
        char pattern[PathCapacity];
        if (!BuildRootedPath(pattern, relative))
        {
            return false;
        }

        WIN32_FIND_DATAA found = {};
        HANDLE search = FindFirstFileA(pattern, &found);
        if (search == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        char selected[MAX_PATH] = {};
        int matches = 0;
        do
        {
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                ++matches;
                if (matches == 1)
                {
                    strcpy_s(selected, found.cFileName);
                }
            }
        } while (FindNextFileA(search, &found));
        FindClose(search);

        if (matches != 1)
        {
            if (matches > 1)
            {
                LogError(pattern, "more than one suffixed override matches");
            }
            return false;
        }

        _snprintf_s(
            relative,
            sizeof(relative),
            _TRUNCATE,
            "%s\\%s",
            directory,
            selected);
        return BuildRootedPath(output, relative);
    }

    bool ResolveBundleOverride(
        char* output,
        const void* sourceData,
        unsigned bytesBeforeData)
    {
        const int regionIndex =
            *reinterpret_cast<const int*>(CurrentRegionAddress);
        if (regionIndex < 0 || regionIndex >= RegionCount || !sourceData)
        {
            return false;
        }

        const auto* region = reinterpret_cast<const std::uint8_t*>(
            RegionsAddress + static_cast<std::uintptr_t>(regionIndex) * RegionSize);
        const auto* base = *reinterpret_cast<std::uint8_t* const*>(
            region + RegionPsxOffset);
        const auto* data = static_cast<const std::uint8_t*>(sourceData);
        if (!base || data < base + bytesBeforeData)
        {
            return false;
        }

        const std::uintptr_t offset =
            static_cast<std::uintptr_t>(data - base - bytesBeforeData);
        if (offset > 0xFFFFFFFFULL)
        {
            return false;
        }

        char bundle[10];
        SanitizeBundleName(bundle, reinterpret_cast<const char*>(region));
        if (bundle[0] == '\0')
        {
            return false;
        }

        char directory[96];
        if (region[RegionLowResOffset] != 0)
        {
            _snprintf_s(
                directory,
                sizeof(directory),
                _TRUNCATE,
                "Bundles\\lowres\\%s",
                bundle);
        }
        else
        {
            _snprintf_s(
                directory,
                sizeof(directory),
                _TRUNCATE,
                "Bundles\\%s",
                bundle);
        }
        return SelectBundleFile(output, directory, static_cast<unsigned>(offset));
    }

    bool NormalizeAssetPath(char* output, const char* source)
    {
        if (!source || source[0] == '\0' || source[0] == '\\' ||
            source[0] == '/' || std::strchr(source, ':'))
        {
            return false;
        }

        std::size_t length = 0;
        const char* segment = source;
        while (*source)
        {
            if (*source == '/' || *source == '\\')
            {
                const std::size_t segmentLength =
                    static_cast<std::size_t>(source - segment);
                if (segmentLength == 0 ||
                    (segmentLength == 1 && segment[0] == '.') ||
                    (segmentLength == 2 && segment[0] == '.' && segment[1] == '.'))
                {
                    return false;
                }
                if (length + 1 >= PathCapacity)
                {
                    return false;
                }
                output[length++] = '\\';
                segment = source + 1;
            }
            else
            {
                if (length + 1 >= PathCapacity)
                {
                    return false;
                }
                output[length++] = *source;
            }
            ++source;
        }

        const std::size_t segmentLength =
            static_cast<std::size_t>(source - segment);
        if (segmentLength == 0 ||
            (segmentLength == 1 && segment[0] == '.') ||
            (segmentLength == 2 && segment[0] == '.' && segment[1] == '.'))
        {
            return false;
        }
        output[length] = '\0';

        char* slash = std::strrchr(output, '\\');
        char* dot = std::strrchr(output, '.');
        if (dot && (!slash || dot > slash))
        {
            strcpy_s(dot, PathCapacity - static_cast<std::size_t>(dot - output), ".bmp");
        }
        else
        {
            if (!AppendPath(output, PathCapacity, ".bmp"))
            {
                return false;
            }
        }
        return true;
    }

    bool ResolveFileOverride(char* output, const char* sourceName)
    {
        char normalized[PathCapacity] = {};
        if (!NormalizeAssetPath(normalized, sourceName))
        {
            return false;
        }

        char relative[PathCapacity] = "Files\\";
        if (!AppendPath(relative, PathCapacity, normalized) ||
            !BuildRootedPath(output, relative))
        {
            return false;
        }
        return FileExists(output);
    }

    bool IsName(const char* name, const char* expected)
    {
        return name && _stricmp(name, expected) == 0;
    }

    bool IsReloadName(const char* name)
    {
        return IsName(name, "RELOADED") || IsName(name, "RESTORED");
    }

    void ClearOverride(int id)
    {
        if (id < 0 || id >= TextureCount || !g_overrides[id].path)
        {
            return;
        }
        HeapFree(GetProcessHeap(), 0, g_overrides[id].path);
        g_overrides[id].path = nullptr;
    }

    void RememberOverride(int id, const char* path)
    {
        if (id < 0 || id >= TextureCount)
        {
            return;
        }

        ClearOverride(id);
        const std::size_t bytes = std::strlen(path) + 1;
        auto* copy = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, bytes));
        if (copy)
        {
            memcpy(copy, path, bytes);
            g_overrides[id].path = copy;
        }
    }

    bool ReadWholeFile(const char* path, std::uint8_t*& bytes, DWORD& size)
    {
        bytes = nullptr;
        size = 0;
        HANDLE file = CreateFileA(
            path,
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        LARGE_INTEGER length = {};
        const bool validLength = GetFileSizeEx(file, &length) &&
            length.QuadPart >= 54 &&
            static_cast<std::uint64_t>(length.QuadPart) <= MaximumBmpBytes;
        if (!validLength)
        {
            CloseHandle(file);
            return false;
        }

        size = static_cast<DWORD>(length.QuadPart);
        bytes = static_cast<std::uint8_t*>(
            HeapAlloc(GetProcessHeap(), 0, size));
        DWORD read = 0;
        const bool ok = bytes && ReadFile(file, bytes, size, &read, nullptr) &&
            read == size;
        CloseHandle(file);
        if (!ok)
        {
            if (bytes)
            {
                HeapFree(GetProcessHeap(), 0, bytes);
            }
            bytes = nullptr;
            size = 0;
        }
        return ok;
    }

    constexpr std::uint32_t PackArgb8888(
        std::uint8_t red,
        std::uint8_t green,
        std::uint8_t blue,
        std::uint8_t alpha)
    {
        return (static_cast<std::uint32_t>(alpha) << 24) |
            (static_cast<std::uint32_t>(red) << 16) |
            (static_cast<std::uint32_t>(green) << 8) |
            blue;
    }

    static_assert(
        PackArgb8888(0x12, 0x34, 0x56, 0x78) == 0x78123456,
        "ARGB8888 packing must produce BGRA bytes on little-endian x86");

    class ScopedPreferredTextureFormat
    {
    public:
        explicit ScopedPreferredTextureFormat(LONG format)
            : m_preferred(reinterpret_cast<volatile LONG*>(
                  PreferredTextureFormatAddress)),
              m_saved(*m_preferred)
        {
            *m_preferred = format;
        }

        ~ScopedPreferredTextureFormat()
        {
            *m_preferred = m_saved;
        }

        ScopedPreferredTextureFormat(const ScopedPreferredTextureFormat&) = delete;
        ScopedPreferredTextureFormat& operator=(
            const ScopedPreferredTextureFormat&) = delete;

    private:
        volatile LONG* m_preferred;
        LONG m_saved;
    };

    bool DecodeBmp(
        const char* path,
        unsigned targetWidth,
        unsigned targetHeight,
        unsigned expectedWidth,
        unsigned expectedHeight,
        DecodedBmp& decoded)
    {
        decoded.pixels = nullptr;
        if (targetWidth == 0 || targetHeight == 0)
        {
            return false;
        }

        std::uint8_t* file = nullptr;
        DWORD fileSize = 0;
        if (!ReadWholeFile(path, file, fileSize))
        {
            return false;
        }

        const std::uint32_t pixelOffset = ReadU32(file + 10);
        const std::uint32_t dibSize = ReadU32(file + 14);
        const std::int32_t width = static_cast<std::int32_t>(ReadU32(file + 18));
        const std::int32_t signedHeight =
            static_cast<std::int32_t>(ReadU32(file + 22));
        const std::uint16_t planes = ReadU16(file + 26);
        const std::uint16_t bitsPerPixel = ReadU16(file + 28);
        const std::uint32_t compression = ReadU32(file + 30);
        const std::uint32_t height = signedHeight < 0
            ? static_cast<std::uint32_t>(-static_cast<std::int64_t>(signedHeight))
            : static_cast<std::uint32_t>(signedHeight);

        bool valid = ReadU16(file) == 0x4D42 && dibSize >= 40 &&
            width > 0 && signedHeight != 0 && planes == 1 &&
            (bitsPerPixel == 24 || bitsPerPixel == 32) && compression == BI_RGB &&
            static_cast<unsigned>(width) <= targetWidth && height <= targetHeight;
        if (expectedWidth != 0 && expectedHeight != 0)
        {
            valid = valid && static_cast<unsigned>(width) == expectedWidth &&
                height == expectedHeight;
        }

        const std::uint64_t rowBytes =
            ((static_cast<std::uint64_t>(width) * bitsPerPixel + 31) / 32) * 4;
        const std::uint64_t sourceBytes = rowBytes * height;
        const std::uint64_t outputPixels =
            static_cast<std::uint64_t>(targetWidth) * targetHeight;
        valid = valid && pixelOffset <= fileSize && sourceBytes <= fileSize - pixelOffset &&
            outputPixels <= MaximumBmpBytes / sizeof(std::uint32_t);
        if (!valid)
        {
            HeapFree(GetProcessHeap(), 0, file);
            return false;
        }

        auto* pixels = static_cast<std::uint32_t*>(HeapAlloc(
            GetProcessHeap(),
            HEAP_ZERO_MEMORY,
            static_cast<SIZE_T>(outputPixels * sizeof(std::uint32_t))));
        if (!pixels)
        {
            HeapFree(GetProcessHeap(), 0, file);
            return false;
        }

        bool alphaIsUsed = false;
        if (bitsPerPixel == 32)
        {
            for (std::uint32_t y = 0; y < height && !alphaIsUsed; ++y)
            {
                const auto* row = file + pixelOffset + y * rowBytes;
                for (std::int32_t x = 0; x < width; ++x)
                {
                    if (row[x * 4 + 3] != 0)
                    {
                        alphaIsUsed = true;
                        break;
                    }
                }
            }
        }

        const unsigned bytesPerPixel = bitsPerPixel / 8;
        for (std::uint32_t y = 0; y < height; ++y)
        {
            const std::uint32_t sourceY = signedHeight < 0 ? y : height - 1 - y;
            const auto* row = file + pixelOffset + sourceY * rowBytes;
            auto* output = pixels + static_cast<std::size_t>(y) * targetWidth;
            for (std::int32_t x = 0; x < width; ++x)
            {
                const auto* source = row + x * bytesPerPixel;
                const std::uint8_t alpha =
                    bitsPerPixel == 32 && alphaIsUsed ? source[3] : 255;
                output[x] = PackArgb8888(
                    source[2], source[1], source[0], alpha);
            }
        }

        HeapFree(GetProcessHeap(), 0, file);
        decoded.pixels = pixels;
        return true;
    }

    void FreeDecoded(DecodedBmp& decoded)
    {
        if (decoded.pixels)
        {
            HeapFree(GetProcessHeap(), 0, decoded.pixels);
            decoded.pixels = nullptr;
        }
    }

    bool ResolveInitialOverride(
        char* path,
        const char* name,
        const void* originalData,
        unsigned targetWidth,
        unsigned targetHeight,
        unsigned& expectedWidth,
        unsigned& expectedHeight)
    {
        if (IsName(name, "PSXPVR"))
        {
            expectedWidth = targetWidth;
            expectedHeight = targetHeight;
            return ResolveBundleOverride(path, originalData, 0x1C);
        }
        if (IsName(name, "PSX256BMP") || IsName(name, "PSX16BMP"))
        {
            const bool found = g_pending.active;
            if (found)
            {
                strcpy_s(path, PathCapacity, g_pending.path);
                expectedWidth = g_pending.width;
                expectedHeight = g_pending.height;
            }
            g_pending.active = false;
            return found;
        }
        return ResolveFileOverride(path, name);
    }

    void MarkPendingIndexed(
        unsigned width,
        unsigned height,
        const void* sourceIndices)
    {
        g_pending.width = width;
        g_pending.height = height;
        g_pending.active = ResolveBundleOverride(
            g_pending.path,
            sourceIndices,
            0x14);
    }
}

extern "C" __declspec(dllexport) unsigned __cdecl CreateIndexed256WithMarker(
    unsigned width,
    unsigned height,
    const void* sourceIndices,
    const void* palette,
    unsigned flags,
    unsigned name,
    unsigned id,
    unsigned checksum)
{
    MarkPendingIndexed(width, height, sourceIndices);
    const auto original =
        reinterpret_cast<CreateIndexedFunction>(CreateIndexed256Address);
    return original(
        width,
        height,
        sourceIndices,
        palette,
        flags,
        name,
        id,
        checksum);
}

extern "C" __declspec(dllexport) unsigned __cdecl CreateIndexed16WithMarker(
    unsigned width,
    unsigned height,
    const void* sourceIndices,
    const void* palette,
    unsigned name,
    unsigned id,
    unsigned checksum,
    unsigned flags)
{
    MarkPendingIndexed(width, height, sourceIndices);
    const auto original =
        reinterpret_cast<CreateIndexedFunction>(CreateIndexed16Address);
    return original(
        width,
        height,
        sourceIndices,
        palette,
        name,
        id,
        checksum,
        flags);
}

extern "C" __declspec(dllexport) int __cdecl CreateTextureWithOverride(
    int id,
    unsigned width,
    unsigned height,
    unsigned type,
    const void* originalData,
    unsigned flags,
    const char* name,
    unsigned checksum)
{
    const auto original =
        reinterpret_cast<CreateTextureFunction>(CreateTextureAddress);

    char path[PathCapacity] = {};
    bool hasOverride = false;
    unsigned expectedWidth = 0;
    unsigned expectedHeight = 0;
    if (IsReloadName(name))
    {
        if (id >= 0 && id < TextureCount && g_overrides[id].path)
        {
            strcpy_s(path, g_overrides[id].path);
            hasOverride = FileExists(path);
        }
    }
    else
    {
        ClearOverride(id);
        hasOverride = ResolveInitialOverride(
            path,
            name,
            originalData,
            width,
            height,
            expectedWidth,
            expectedHeight);
    }

    // Subtextures created by the engine's internal oversized-texture splitter
    // may retain their input pointer. Never give that path temporary memory.
    if (!hasOverride || (flags & 0x200) != 0)
    {
        return original(id, width, height, type, originalData, flags, name, checksum);
    }

    const auto formatFlags = *reinterpret_cast<const volatile LONG*>(
        Argb8888FormatFlagsAddress);
    if ((formatFlags & 1) == 0)
    {
        LogError(path, "the active renderer does not support 32-bit ARGB textures");
        return original(id, width, height, type, originalData, flags, name, checksum);
    }

    DecodedBmp decoded = {};
    if (!DecodeBmp(
            path,
            width,
            height,
            expectedWidth,
            expectedHeight,
            decoded))
    {
        LogError(path, "expected an uncompressed 24/32-bit BMP no larger than the original texture");
        return original(id, width, height, type, originalData, flags, name, checksum);
    }

    const ScopedPreferredTextureFormat preferredFormat(Argb8888FormatIndex);
    const int result = original(
        id,
        width,
        height,
        LinearArgb8888Type,
        decoded.pixels,
        flags,
        name,
        checksum);
    FreeDecoded(decoded);

    if (result != 0)
    {
        RememberOverride(id, path);
        InterlockedIncrement(&SpideyTextureOverrideCount);
    }
    return result;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        const DWORD length = GetModuleFileNameA(
            nullptr,
            g_gameDirectory,
            static_cast<DWORD>(sizeof(g_gameDirectory)));
        if (length == 0 || length >= sizeof(g_gameDirectory))
        {
            g_gameDirectory[0] = '\0';
        }
        else
        {
            char* slash = std::strrchr(g_gameDirectory, '\\');
            if (slash)
            {
                *slash = '\0';
            }
        }
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        for (int i = 0; i < TextureCount; ++i)
        {
            ClearOverride(i);
        }
    }
    return TRUE;
}
