#define NOMINMAX

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>

#pragma comment(lib, "user32.lib")

namespace
{
    constexpr std::size_t BindingCount = 11;
    constexpr std::size_t DeviceNameSize = 128;
    constexpr std::size_t LegacyConfigSize = 252;

    constexpr std::uintptr_t WidthAddress = 0x02E096F8;
    constexpr std::uintptr_t HeightAddress = 0x02E0970C;
    constexpr std::uintptr_t ColorDepthAddress = 0x02E098E4;
    constexpr std::uintptr_t DisplayModeAddress = 0x00562D60;
    constexpr std::uintptr_t LowGraphicsAddress = 0x006B78F8;
    constexpr std::uintptr_t StartupFlagsAddress = 0x02E0988C;
    constexpr std::uintptr_t DeviceNameAddress = 0x02E09710;
    constexpr std::uintptr_t OptionAddresses[] =
    {
        0x006828EC,
        0x006828F0,
        0x006828F4,
        0x006828F8,
    };

    constexpr std::uintptr_t RuntimeOptionAddresses[] =
    {
        0x006B482C,
        0x006B482A,
        0x006B482E,
        0x0061919D,
    };

    using GetBindingFunction = void(__cdecl*)(std::uint32_t, std::uint32_t*);
    using SetBindingFunction = void(__cdecl*)(std::uint32_t, std::uint32_t);

    const auto GetPrimaryBinding =
        reinterpret_cast<GetBindingFunction>(0x0050A400);
    const auto GetSecondaryBinding =
        reinterpret_cast<GetBindingFunction>(0x0050A450);
    const auto SetPrimaryBinding =
        reinterpret_cast<SetBindingFunction>(0x0050A4A0);
    const auto SetSecondaryBinding =
        reinterpret_cast<SetBindingFunction>(0x0050A4E0);

    std::uint32_t FullscreenValue = 1;
    std::uint32_t StretchMoviesValue = 0;
    std::uint32_t MsaaSamplesValue = 0;
    std::uint32_t AnisotropicFilteringValue = 0;
    std::uint32_t VSyncValue = 1;
    std::uint32_t FpsLimitValue = 0;
    std::uint32_t SimulationHzValue = 30;
    std::uint32_t ScriptedSimulationHzValue = 20;
    std::uint32_t ObjectInterpolationHzValue = 60;
    std::uint32_t AnimationInterpolationHzValue = 60;
    std::uint32_t CameraInterpolationHzValue = 60;
#pragma pack(push, 1)
    struct LegacyConfig
    {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t colorDepth;
        std::uint32_t displayMode;
        std::uint32_t lowGraphics;
        std::uint32_t primaryBindings[BindingCount];
        std::uint32_t secondaryBindings[BindingCount];
        char deviceName[DeviceNameSize];
        std::uint32_t options[4];
    };
#pragma pack(pop)

    static_assert(sizeof(LegacyConfig) == LegacyConfigSize);

    struct Config
    {
        std::uint32_t width = 640;
        std::uint32_t height = 480;
        std::uint32_t colorDepth = 16;
        std::uint32_t displayMode = 4;
        std::uint32_t lowGraphics = 0;
        std::uint32_t fullscreen = 1;
        std::uint32_t stretchMovies = 0;
        std::uint32_t msaaSamples = 0;
        std::uint32_t anisotropicFiltering = 0;
        std::uint32_t vsync = 1;
        std::uint32_t fpsLimit = 0;
        std::uint32_t simulationHz = 30;
        std::uint32_t scriptedSimulationHz = 20;
        std::uint32_t objectInterpolationHz = 60;
        std::uint32_t animationInterpolationHz = 60;
        std::uint32_t cameraInterpolationHz = 60;
        std::array<std::uint32_t, BindingCount> primaryBindings{};
        std::array<std::uint32_t, BindingCount> secondaryBindings{};
        std::array<char, DeviceNameSize> deviceName{};
        std::array<std::uint32_t, 4> options{};
    };

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

    Config CaptureGameConfig()
    {
        Config config;
        config.width = GameValue<std::uint32_t>(WidthAddress);
        config.height = GameValue<std::uint32_t>(HeightAddress);
        config.colorDepth = GameValue<std::uint32_t>(ColorDepthAddress);
        config.displayMode = GameValue<std::uint32_t>(DisplayModeAddress);
        config.lowGraphics = GameValue<std::uint32_t>(LowGraphicsAddress);
        config.fullscreen = FullscreenValue;
        config.stretchMovies = StretchMoviesValue;
        config.msaaSamples = MsaaSamplesValue;
        config.anisotropicFiltering = AnisotropicFilteringValue;
        config.vsync = VSyncValue;
        config.fpsLimit = FpsLimitValue;
        config.simulationHz = SimulationHzValue;
        config.scriptedSimulationHz = ScriptedSimulationHzValue;
        config.objectInterpolationHz = ObjectInterpolationHzValue;
        config.animationInterpolationHz = AnimationInterpolationHzValue;
        config.cameraInterpolationHz = CameraInterpolationHzValue;

        for (std::uint32_t index = 0; index < BindingCount; ++index)
        {
            const std::uint32_t action = 1u << index;
            GetPrimaryBinding(action, &config.primaryBindings[index]);
            GetSecondaryBinding(action, &config.secondaryBindings[index]);
        }

        CopyMemory(config.deviceName.data(),
            reinterpret_cast<const void*>(DeviceNameAddress), DeviceNameSize);
        config.deviceName.back() = '\0';

        for (std::size_t index = 0; index < config.options.size(); ++index)
        {
            config.options[index] =
                GameValue<std::uint32_t>(OptionAddresses[index]);
        }
        return config;
    }

    void UseOriginalDefaults(Config& config)
    {
        config.width = 640;
        config.height = 480;
        config.colorDepth = 16;
        config.displayMode = 4;
        config.lowGraphics = 0;
        config.fullscreen = 1;
        config.stretchMovies = 0;
        config.msaaSamples = 0;
        config.anisotropicFiltering = 0;
        config.vsync = 1;
        config.fpsLimit = 0;
        config.simulationHz = 30;
        config.scriptedSimulationHz = 20;
        config.objectInterpolationHz = 60;
        config.animationInterpolationHz = 60;
        config.cameraInterpolationHz = 60;
        config.options[0] = GameValue<std::uint32_t>(0x00550EA4);
        config.options[1] = GameValue<std::uint32_t>(0x00550EA8);
        config.options[2] = GameValue<std::uint32_t>(0x00550EAC);
        config.options[3] = GameValue<std::uint32_t>(0x00550EB0);
        config.deviceName.fill('\0');
    }

    bool ReadLegacyConfig(const std::string& path, Config& config)
    {
        HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        LegacyConfig legacy{};
        DWORD bytesRead = 0;
        const bool read = GetFileSize(file, nullptr) == LegacyConfigSize &&
            ReadFile(file, &legacy, sizeof(legacy), &bytesRead, nullptr) &&
            bytesRead == sizeof(legacy);
        CloseHandle(file);

        if (!read || legacy.width < 320 || legacy.width > 16384 ||
            legacy.height < 200 || legacy.height > 16384)
        {
            return false;
        }

        config.width = legacy.width;
        config.height = legacy.height;
        config.colorDepth = legacy.colorDepth;
        config.displayMode = legacy.displayMode;
        config.lowGraphics = legacy.lowGraphics;
        CopyMemory(config.primaryBindings.data(), legacy.primaryBindings,
            sizeof(legacy.primaryBindings));
        CopyMemory(config.secondaryBindings.data(), legacy.secondaryBindings,
            sizeof(legacy.secondaryBindings));
        CopyMemory(config.deviceName.data(), legacy.deviceName,
            sizeof(legacy.deviceName));
        config.deviceName.back() = '\0';
        CopyMemory(config.options.data(), legacy.options,
            sizeof(legacy.options));
        return true;
    }

    bool ReadInteger(const std::string& path, const char* section,
        const char* key, std::uint32_t& value)
    {
        std::array<char, 32> text{};
        if (GetPrivateProfileStringA(section, key, "", text.data(),
            static_cast<DWORD>(text.size()), path.c_str()) == 0)
        {
            return false;
        }

        char* end = nullptr;
        const unsigned long parsed = std::strtoul(text.data(), &end, 10);
        if (end == text.data() || *end != '\0')
        {
            return false;
        }

        value = static_cast<std::uint32_t>(parsed);
        return true;
    }

    bool ReadTextConfig(const std::string& path, Config& config)
    {
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            return false;
        }

        ReadInteger(path, "Display", "Width", config.width);
        ReadInteger(path, "Display", "Height", config.height);
        ReadInteger(path, "Display", "ColorDepth", config.colorDepth);
        if (!ReadInteger(path, "Display", "Brightness", config.displayMode))
        {
            ReadInteger(path, "Display", "Mode", config.displayMode);
        }
        ReadInteger(path, "Display", "LowGraphics", config.lowGraphics);
        ReadInteger(path, "Display", "Fullscreen", config.fullscreen);
        ReadInteger(path, "Display", "StretchMovies", config.stretchMovies);
        ReadInteger(path, "Display", "MSAA", config.msaaSamples);
        ReadInteger(path, "Display", "AnisotropicFiltering",
            config.anisotropicFiltering);
        ReadInteger(path, "Display", "VSync", config.vsync);
        ReadInteger(path, "Display", "FPSLimit", config.fpsLimit);
        ReadInteger(
            path,
            "Presentation",
            "SimulationHz",
            config.simulationHz);
        ReadInteger(
            path,
            "Presentation",
            "ScriptedSimulationHz",
            config.scriptedSimulationHz);
        ReadInteger(
            path,
            "Presentation",
            "ObjectInterpolationHz",
            config.objectInterpolationHz);
        ReadInteger(
            path,
            "Presentation",
            "AnimationInterpolationHz",
            config.animationInterpolationHz);
        ReadInteger(
            path,
            "Presentation",
            "CameraInterpolationHz",
            config.cameraInterpolationHz);
        const std::array<char, DeviceNameSize> defaultDeviceName =
            config.deviceName;
        GetPrivateProfileStringA("Display", "Device", defaultDeviceName.data(),
            config.deviceName.data(), static_cast<DWORD>(config.deviceName.size()),
            path.c_str());

        for (std::size_t index = 0; index < BindingCount; ++index)
        {
            char key[16]{};
            wsprintfA(key, "Primary%02u", static_cast<unsigned>(index + 1));
            ReadInteger(path, "Controls", key, config.primaryBindings[index]);
            wsprintfA(key, "Secondary%02u", static_cast<unsigned>(index + 1));
            ReadInteger(path, "Controls", key, config.secondaryBindings[index]);
        }

        static constexpr const char* AudioKeys[] =
        {
            "MusicAndSfxVolume",
            "MovieVolume",
            "VoiceVolume",
            "Mono",
        };
        for (std::size_t index = 0; index < config.options.size(); ++index)
        {
            if (!ReadInteger(path, "Audio", AudioKeys[index],
                config.options[index]))
            {
                char oldKey[16]{};
                wsprintfA(oldKey, "Value%u", static_cast<unsigned>(index + 1));
                ReadInteger(path, "Options", oldKey, config.options[index]);
            }
        }

        if (config.width < 320 || config.width > 16384)
        {
            config.width = 640;
        }
        if (config.height < 200 || config.height > 16384)
        {
            config.height = 480;
        }
        if (config.colorDepth != 16 && config.colorDepth != 32)
        {
            config.colorDepth = 16;
        }
        if (config.displayMode > 7)
        {
            config.displayMode = 4;
        }
        config.fullscreen = config.fullscreen == 0 ? 0 : 1;
        config.stretchMovies = config.stretchMovies == 0 ? 0 : 1;
        if (config.msaaSamples != 0 &&
            config.msaaSamples != 2 &&
            config.msaaSamples != 4 &&
            config.msaaSamples != 8)
        {
            config.msaaSamples = 0;
        }
        if (config.anisotropicFiltering != 0 &&
            config.anisotropicFiltering != 2 &&
            config.anisotropicFiltering != 4 &&
            config.anisotropicFiltering != 8 &&
            config.anisotropicFiltering != 16)
        {
            config.anisotropicFiltering = 0;
        }
        config.vsync = config.vsync == 0 ? 0 : 1;
        if (config.fpsLimit > 1000)
        {
            config.fpsLimit = 0;
        }
        if (config.simulationHz == 0 || config.simulationHz > 1000)
        {
            config.simulationHz = 30;
        }
        if (config.scriptedSimulationHz == 0 ||
            config.scriptedSimulationHz > 1000)
        {
            config.scriptedSimulationHz = 20;
        }
        if (config.objectInterpolationHz > 1000)
        {
            config.objectInterpolationHz = 60;
        }
        if (config.animationInterpolationHz > 1000)
        {
            config.animationInterpolationHz = 60;
        }
        if (config.cameraInterpolationHz > 1000)
        {
            config.cameraInterpolationHz = 60;
        }
        if (config.options[0] > 16383)
        {
            config.options[0] = 16383;
        }
        if (config.options[1] > 255)
        {
            config.options[1] = 255;
        }
        if (config.options[2] > 255)
        {
            config.options[2] = 255;
        }
        config.options[3] = config.options[3] == 0 ? 0 : 1;
        return true;
    }

    std::string SafeDeviceName(const Config& config)
    {
        std::string result(config.deviceName.data());
        for (char& character : result)
        {
            if (character == '\r' || character == '\n')
            {
                character = ' ';
            }
        }
        return result;
    }

    bool WriteTextConfig(const std::string& path, const Config& config)
    {
        std::ostringstream text;
        text << "; Spider-Man settings - decimal values unless noted\n"
             << "[Display]\n"
             << "Width=" << config.width << '\n'
             << "Height=" << config.height << '\n'
             << "ColorDepth=" << config.colorDepth << '\n'
             << "Brightness=" << config.displayMode << '\n'
             << "LowGraphics=" << config.lowGraphics << '\n'
             << "Fullscreen=" << config.fullscreen << '\n'
             << "; StretchMovies: 0 preserves aspect ratio; 1 fills the screen\n"
             << "StretchMovies=" << config.stretchMovies << '\n'
             << "; MSAA: 0 disables anti-aliasing; valid values are 0, 2, 4, 8\n"
             << "MSAA=" << config.msaaSamples << '\n'
             << "; AnisotropicFiltering: 0 uses the game's filtering; valid values are 0, 2, 4, 8, 16\n"
             << "AnisotropicFiltering=" << config.anisotropicFiltering << '\n'
             << "; VSync: 0 disables synchronization; 1 synchronizes presentation to the display\n"
             << "VSync=" << config.vsync << '\n'
             << "; FPSLimit: 0 disables the manual limiter; otherwise sets the maximum presentation rate\n"
             << "FPSLimit=" << config.fpsLimit << '\n'
             << "Device=" << SafeDeviceName(config) << "\n\n"
             << "[Presentation]\n"
             << "; Simulation rates must be 1-1000 Hz\n"
             << "SimulationHz=" << config.simulationHz << '\n'
             << "ScriptedSimulationHz=" << config.scriptedSimulationHz << '\n'
             << "; Interpolation rates are 0-1000 Hz; 0 disables that interpolation domain\n"
             << "ObjectInterpolationHz=" << config.objectInterpolationHz << '\n'
             << "AnimationInterpolationHz=" << config.animationInterpolationHz << '\n'
             << "CameraInterpolationHz=" << config.cameraInterpolationHz << "\n\n"
             << "[Controls]\n"
             << "; DirectInput scan codes; 16384 means unbound\n";

        for (std::size_t index = 0; index < BindingCount; ++index)
        {
            const unsigned number = static_cast<unsigned>(index + 1);
            text << "Primary";
            if (number < 10)
            {
                text << '0';
            }
            text << number << '=' << config.primaryBindings[index] << '\n';
            text << "Secondary";
            if (number < 10)
            {
                text << '0';
            }
            text << number << '=' << config.secondaryBindings[index] << '\n';
        }

        text << "\n[Audio]\n"
             << "; MusicAndSfxVolume: 0-16383; Movie/Voice: 0-255\n"
             << "MusicAndSfxVolume=" << config.options[0] << '\n'
             << "MovieVolume=" << config.options[1] << '\n'
             << "VoiceVolume=" << config.options[2] << '\n'
             << "Mono=" << config.options[3] << '\n';

        const std::string contents = text.str();
        HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        DWORD bytesWritten = 0;
        const bool written = WriteFile(file, contents.data(),
            static_cast<DWORD>(contents.size()), &bytesWritten, nullptr) &&
            bytesWritten == contents.size();
        CloseHandle(file);
        return written;
    }

    void ApplyGameConfig(const Config& config)
    {
        GameValue<std::uint32_t>(WidthAddress) = config.width;
        GameValue<std::uint32_t>(HeightAddress) = config.height;
        GameValue<std::uint32_t>(ColorDepthAddress) = config.colorDepth;
        GameValue<std::uint32_t>(DisplayModeAddress) = config.displayMode;
        GameValue<std::uint32_t>(LowGraphicsAddress) = 0;
        FullscreenValue = config.fullscreen;
        StretchMoviesValue = config.stretchMovies;
        MsaaSamplesValue = config.msaaSamples;
        AnisotropicFilteringValue = config.anisotropicFiltering;
        VSyncValue = config.vsync;
        FpsLimitValue = config.fpsLimit;
        SimulationHzValue = config.simulationHz;
        ScriptedSimulationHzValue = config.scriptedSimulationHz;
        ObjectInterpolationHzValue = config.objectInterpolationHz;
        AnimationInterpolationHzValue = config.animationInterpolationHz;
        CameraInterpolationHzValue = config.cameraInterpolationHz;
        GameValue<std::uint32_t>(StartupFlagsAddress) =
            config.fullscreen == 0 ? 1u : 0u;

        for (std::uint32_t index = 0; index < BindingCount; ++index)
        {
            const std::uint32_t action = 1u << index;
            SetPrimaryBinding(action, config.primaryBindings[index]);
            SetSecondaryBinding(action, config.secondaryBindings[index]);
        }

        CopyMemory(reinterpret_cast<void*>(DeviceNameAddress),
            config.deviceName.data(), DeviceNameSize);
        for (std::size_t index = 0; index < config.options.size(); ++index)
        {
            GameValue<std::uint32_t>(OptionAddresses[index]) =
                config.options[index];
        }

        GameValue<std::uint16_t>(RuntimeOptionAddresses[0]) =
            static_cast<std::uint16_t>(config.options[0]);
        GameValue<std::uint16_t>(RuntimeOptionAddresses[1]) =
            static_cast<std::uint16_t>(config.options[1]);
        GameValue<std::uint16_t>(RuntimeOptionAddresses[2]) =
            static_cast<std::uint16_t>(config.options[2]);
        GameValue<std::uint8_t>(RuntimeOptionAddresses[3]) =
            static_cast<std::uint8_t>(config.options[3]);
    }
}

extern "C" __declspec(dllexport) void LoadTextConfig()
{
    const std::string path = ConfigPath();
    Config config = CaptureGameConfig();

    if (ReadLegacyConfig(path, config))
    {
        const std::string backupPath = path + ".binary.bak";
        CopyFileA(path.c_str(), backupPath.c_str(), TRUE);
        WriteTextConfig(path, config);
    }
    else if (!ReadTextConfig(path, config))
    {
        UseOriginalDefaults(config);
        WriteTextConfig(path, config);
    }

    ApplyGameConfig(config);
}

extern "C" __declspec(dllexport) void SaveTextConfig()
{
    WriteTextConfig(ConfigPath(), CaptureGameConfig());
}
