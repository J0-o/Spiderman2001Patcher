#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

extern "C"
{
    __declspec(dllexport) volatile LONG SpideyInterpolatedRenderCount = 0;
    __declspec(dllexport) volatile LONG SpideySimulationCallCount = 0;
    __declspec(dllexport) volatile LONG SpideyLastInterpolatedObjectCount = 0;
    __declspec(dllexport) volatile LONG SpideyLastInterpolatedSkeletonCount = 0;
    __declspec(dllexport) volatile LONG SpideySimulationRate = 30;
    __declspec(dllexport) volatile LONG Spidey20HzReason = 0;
    __declspec(dllexport) volatile LONG SpideyObjectInterpolationRate = 60;
    __declspec(dllexport) volatile LONG SpideyAnimationInterpolationRate = 60;
    __declspec(dllexport) volatile LONG SpideyCameraInterpolationRate = 60;
}

namespace
{
    constexpr std::uintptr_t FrameUpdateAddress = 0x00455400;
    constexpr std::uintptr_t AudioUpdateAddress = 0x00459650;
    constexpr std::uintptr_t RenderAddress = 0x004555A0;
    constexpr std::uintptr_t BodyListUpdateAddress = 0x00460FC0;
    constexpr std::uintptr_t CameraListAddress = 0x0056F3B8;
    constexpr std::uintptr_t MechListAddress = 0x006A9038;
    constexpr std::uintptr_t BaddyListAddress = 0x0056E990;
    constexpr std::uintptr_t LoadCameraAddress = 0x00416A20;
    constexpr std::uintptr_t QuatSlerpAddress = 0x0047CAC0;

    constexpr std::array<std::uintptr_t, 10> ObjectListAddresses =
    {
        0x006A9038,
        0x0060FB94,
        0x0056EFE4,
        0x0060DAB8,
        0x0060DAAC,
        0x0056E89C,
        0x0056E990,
        0x0056E994,
        0x006A903C,
        0x0060DAB0,
    };

    struct Vector3
    {
        std::int32_t x;
        std::int32_t y;
        std::int32_t z;
    };

    struct Angles
    {
        std::int16_t x;
        std::int16_t y;
        std::int16_t z;
    };

    struct Quaternion
    {
        std::int32_t x;
        std::int32_t y;
        std::int32_t z;
        std::int32_t w;
    };

    struct AnimationState
    {
        bool valid;
        std::int16_t frame;
        std::uint16_t animation;
        std::uint8_t loop;
        std::int8_t direction;
        std::int16_t frameFraction;
        std::int16_t frameCount;
    };

    struct ObjectState
    {
        std::uintptr_t object;
        Vector3 position;
        Angles angles;
        AnimationState animation;
    };

    struct CameraState
    {
        std::uintptr_t camera = 0;
        Vector3 position{};
        Quaternion rotation{};
    };

    using VoidFunction = void(__cdecl*)();
    using BodyListUpdateFunction = void(__cdecl*)(std::uintptr_t, int);
    using LoadCameraFunction = void(__fastcall*)(std::uintptr_t);
    using QuatSlerpFunction = void(__cdecl*)(
        Quaternion*,
        const Quaternion*,
        int,
        Quaternion*);

    int NormalSimulationHz = 30;
    int ScriptedSimulationHz = 20;
    int ObjectInterpolationHz = 60;
    int AnimationInterpolationHz = 60;
    int CameraInterpolationHz = 60;
    int ActiveSimulationHz = 0;
    bool PresentationSettingsLoaded = false;
    bool SimulatedThisFrame = false;
    std::int64_t CounterFrequency = 0;
    std::int64_t LastSimulationCounter = 0;
    std::int64_t NextSimulationCounter = 0;
    std::int64_t NextObjectInterpolationCounter = 0;
    std::int64_t NextAnimationInterpolationCounter = 0;
    std::int64_t NextCameraInterpolationCounter = 0;
    int ObjectInterpolationAlpha = 0x1000;
    int AnimationInterpolationAlpha = 0x1000;
    int CameraInterpolationAlpha = 0x1000;
    bool HaveObjectPair = false;
    bool HaveCameraPair = false;
    std::vector<ObjectState> PreviousObjects;
    std::vector<ObjectState> CurrentObjects;
    std::vector<ObjectState> RestoreObjects;
    CameraState PreviousCamera{};
    CameraState CurrentCamera{};
    CameraState RestoreCamera{};
    bool CameraWasApplied = false;

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

    int ReadRate(const char* key, int defaultValue, bool allowZero)
    {
        const int value = GetPrivateProfileIntA(
            "Presentation",
            key,
            defaultValue,
            ConfigPath().c_str());
        if (value < (allowZero ? 0 : 1) || value > 1000)
        {
            return defaultValue;
        }
        return value;
    }

    void LoadPresentationSettings()
    {
        NormalSimulationHz =
            ReadRate("SimulationHz", 30, false);
        ScriptedSimulationHz =
            ReadRate("ScriptedSimulationHz", 20, false);
        ObjectInterpolationHz =
            ReadRate("ObjectInterpolationHz", 60, true);
        AnimationInterpolationHz =
            ReadRate("AnimationInterpolationHz", 60, true);
        CameraInterpolationHz =
            ReadRate("CameraInterpolationHz", 60, true);

        LARGE_INTEGER frequency{};
        CounterFrequency = QueryPerformanceFrequency(&frequency)
            ? frequency.QuadPart
            : 0;

        InterlockedExchange(
            &SpideyObjectInterpolationRate,
            ObjectInterpolationHz);
        InterlockedExchange(
            &SpideyAnimationInterpolationRate,
            AnimationInterpolationHz);
        InterlockedExchange(
            &SpideyCameraInterpolationRate,
            CameraInterpolationHz);
        PresentationSettingsLoaded = true;
    }

    void ResetInterpolationSampling()
    {
        NextObjectInterpolationCounter = 0;
        NextAnimationInterpolationCounter = 0;
        NextCameraInterpolationCounter = 0;
        ObjectInterpolationAlpha = 0x1000;
        AnimationInterpolationAlpha = 0x1000;
        CameraInterpolationAlpha = 0x1000;
    }

    std::int64_t CounterNow()
    {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        return counter.QuadPart;
    }

    std::int64_t RateInterval(int rate)
    {
        return rate > 0 && CounterFrequency > 0
            ? std::max<std::int64_t>(1, CounterFrequency / rate)
            : 0;
    }

    int CurrentSimulationAlpha(std::int64_t now)
    {
        const std::int64_t interval = RateInterval(ActiveSimulationHz);
        if (interval == 0 || LastSimulationCounter == 0)
        {
            return 0x1000;
        }

        const std::int64_t elapsed =
            std::max<std::int64_t>(0, now - LastSimulationCounter);
        return static_cast<int>(
            std::min<std::int64_t>(0x1000, elapsed * 0x1000 / interval));
    }

    int SampleInterpolationAlpha(
        int rate,
        std::int64_t now,
        int simulationAlpha,
        std::int64_t& nextCounter,
        int& sampledAlpha)
    {
        if (rate == 0)
        {
            sampledAlpha = 0x1000;
            nextCounter = 0;
            return sampledAlpha;
        }

        const std::int64_t interval = RateInterval(rate);
        if (interval == 0)
        {
            sampledAlpha = simulationAlpha;
            return sampledAlpha;
        }

        if (nextCounter == 0 || now >= nextCounter)
        {
            sampledAlpha = simulationAlpha;
            if (nextCounter == 0 ||
                now - nextCounter > interval * 4)
            {
                nextCounter = now + interval;
            }
            else
            {
                do
                {
                    nextCounter += interval;
                }
                while (nextCounter <= now);
            }
        }
        return sampledAlpha;
    }

    Vector3& Position(std::uintptr_t object)
    {
        return *reinterpret_cast<Vector3*>(object + 0x08);
    }

    Angles& ObjectAngles(std::uintptr_t object)
    {
        return *reinterpret_cast<Angles*>(object + 0x14);
    }

    std::uintptr_t NextObject(std::uintptr_t object)
    {
        return *reinterpret_cast<std::uintptr_t*>(object + 0x20);
    }

    Quaternion& CameraRotation(std::uintptr_t camera)
    {
        return *reinterpret_cast<Quaternion*>(camera + 0x214);
    }

    template<typename T>
    T& Field(std::uintptr_t object, std::uintptr_t offset)
    {
        return *reinterpret_cast<T*>(object + offset);
    }

    LONG Get20HzReason()
    {
        LONG reason = 0;

        const std::uintptr_t player =
            *reinterpret_cast<std::uintptr_t*>(MechListAddress);
        if (player != 0)
        {
            const bool synthesizedInput =
                Field<std::uint8_t>(player, 0x1AC) != 0;
            const bool cutsceneParserActive =
                Field<std::uint8_t>(player, 0x1B4) != 0;
            const bool cutsceneActionActive =
                Field<std::uintptr_t>(player, 0x1BC) != 0;

            if (synthesizedInput &&
                (cutsceneParserActive || cutsceneActionActive))
            {
                reason |= 1;
            }
        }

        std::uintptr_t baddy =
            *reinterpret_cast<std::uintptr_t*>(BaddyListAddress);
        for (int count = 0; baddy != 0 && count < 16384; ++count)
        {
            if (Field<std::uint16_t>(baddy, 0x38) == 311)
            {
                if (Field<std::int32_t>(baddy, 0x378) == 3)
                {
                    reason |= 2;
                }
                if (Field<std::uint32_t>(baddy, 0x31C) == 6)
                {
                    reason |= 4;
                }
            }
            baddy = NextObject(baddy);
        }

        return reason;
    }

    AnimationState CaptureAnimation(std::uintptr_t object)
    {
        if ((Field<std::uint16_t>(object, 0x04) & 2) == 0)
        {
            return {};
        }

        return
        {
            true,
            Field<std::int16_t>(object, 0x128),
            Field<std::uint16_t>(object, 0x12A),
            Field<std::uint8_t>(object, 0x140),
            Field<std::int8_t>(object, 0x141),
            Field<std::int16_t>(object, 0x146),
            Field<std::int16_t>(object, 0x148),
        };
    }

    ObjectState CaptureObject(std::uintptr_t object)
    {
        return
        {
            object,
            Position(object),
            ObjectAngles(object),
            CaptureAnimation(object),
        };
    }

    void WriteAnimation(
        std::uintptr_t object,
        const AnimationState& animation)
    {
        if (!animation.valid)
        {
            return;
        }
        Field<std::int16_t>(object, 0x128) = animation.frame;
        Field<std::uint16_t>(object, 0x12A) = animation.animation;
        Field<std::uint8_t>(object, 0x140) = animation.loop;
        Field<std::int8_t>(object, 0x141) = animation.direction;
        Field<std::int16_t>(object, 0x146) = animation.frameFraction;
        Field<std::int16_t>(object, 0x148) = animation.frameCount;
    }

    void CaptureObjects(std::vector<ObjectState>& destination)
    {
        destination.clear();
        for (const std::uintptr_t listAddress : ObjectListAddresses)
        {
            std::uintptr_t object =
                *reinterpret_cast<std::uintptr_t*>(listAddress);
            for (int count = 0; object != 0 && count < 16384; ++count)
            {
                destination.push_back(CaptureObject(object));
                object = NextObject(object);
            }
        }

        std::sort(
            destination.begin(),
            destination.end(),
            [](const ObjectState& left, const ObjectState& right)
            {
                return left.object < right.object;
            });
        destination.erase(
            std::unique(
                destination.begin(),
                destination.end(),
                [](const ObjectState& left, const ObjectState& right)
                {
                    return left.object == right.object;
                }),
            destination.end());
    }

    const ObjectState* FindObject(
        const std::vector<ObjectState>& states,
        std::uintptr_t object)
    {
        const auto found = std::lower_bound(
            states.begin(),
            states.end(),
            object,
            [](const ObjectState& state, std::uintptr_t address)
            {
                return state.object < address;
            });
        return found != states.end() && found->object == object
            ? &*found
            : nullptr;
    }

    std::int32_t Interpolate(
        std::int32_t previous,
        std::int32_t current,
        int alpha)
    {
        return previous +
            static_cast<std::int32_t>(
                (static_cast<std::int64_t>(current - previous) * alpha) >>
                12);
    }

    std::int16_t InterpolateAngle(
        std::int16_t previous,
        std::int16_t current,
        int alpha)
    {
        int delta = (static_cast<int>(current) - previous) & 0x0FFF;
        if (delta > 0x0800)
        {
            delta -= 0x1000;
        }
        return static_cast<std::int16_t>(
            (static_cast<int>(previous) + ((delta * alpha) >> 12)) &
            0x0FFF);
    }

    void ApplyAnimationInterpolation(
        std::uintptr_t object,
        const AnimationState& previous,
        const AnimationState& current,
        int alpha)
    {
        if (!previous.valid || !current.valid)
        {
            return;
        }

        if (previous.animation != current.animation)
        {
            WriteAnimation(object, current);
            return;
        }

        std::int64_t previousClock =
            (static_cast<std::int64_t>(previous.frame) * 0x10000) |
            static_cast<std::uint16_t>(previous.frameFraction);
        std::int64_t currentClock =
            (static_cast<std::int64_t>(current.frame) * 0x10000) |
            static_cast<std::uint16_t>(current.frameFraction);
        const std::int64_t loopLength =
            static_cast<std::int64_t>(current.frameCount) << 16;

        if (current.loop == 1 && loopLength > 0)
        {
            if (current.direction >= 0 && currentClock < previousClock)
            {
                currentClock += loopLength;
            }
            else if (current.direction < 0 &&
                     currentClock > previousClock)
            {
                currentClock -= loopLength;
            }
        }

        std::int64_t interpolated =
            previousClock +
            (((currentClock - previousClock) * alpha) >> 12);
        if (current.loop == 1 && loopLength > 0)
        {
            while (interpolated < 0)
            {
                interpolated += loopLength;
            }
            while (interpolated >= loopLength)
            {
                interpolated -= loopLength;
            }
        }

        Field<std::int16_t>(object, 0x128) =
            static_cast<std::int16_t>(interpolated >> 16);
        Field<std::int16_t>(object, 0x146) =
            static_cast<std::int16_t>(
                static_cast<std::uint16_t>(interpolated));
    }

    void ApplyObjectInterpolation(int objectAlpha, int animationAlpha)
    {
        RestoreObjects.clear();
        LONG interpolatedSkeletons = 0;
        if (!HaveObjectPair)
        {
            InterlockedExchange(
                &SpideyLastInterpolatedSkeletonCount,
                0);
            return;
        }

        for (const std::uintptr_t listAddress : ObjectListAddresses)
        {
            std::uintptr_t object =
                *reinterpret_cast<std::uintptr_t*>(listAddress);
            for (int count = 0; object != 0 && count < 16384; ++count)
            {
                const ObjectState* previous =
                    FindObject(PreviousObjects, object);
                const ObjectState* current =
                    FindObject(CurrentObjects, object);
                if (previous && current)
                {
                    RestoreObjects.push_back(CaptureObject(object));
                    const bool animationChanged =
                        previous->animation.valid &&
                        current->animation.valid &&
                        previous->animation.animation !=
                            current->animation.animation;
                    if (animationChanged)
                    {
                        Position(object) = current->position;
                        ObjectAngles(object) = current->angles;
                    }
                    else
                    {
                        Position(object) =
                        {
                            Interpolate(
                                previous->position.x,
                                current->position.x,
                                objectAlpha),
                            Interpolate(
                                previous->position.y,
                                current->position.y,
                                objectAlpha),
                            Interpolate(
                                previous->position.z,
                                current->position.z,
                                objectAlpha),
                        };
                        ObjectAngles(object) =
                        {
                            InterpolateAngle(
                                previous->angles.x,
                                current->angles.x,
                                objectAlpha),
                            InterpolateAngle(
                                previous->angles.y,
                                current->angles.y,
                                objectAlpha),
                            InterpolateAngle(
                                previous->angles.z,
                                current->angles.z,
                                objectAlpha),
                        };
                    }
                    if (previous->animation.valid &&
                        current->animation.valid)
                    {
                        ApplyAnimationInterpolation(
                            object,
                            previous->animation,
                            current->animation,
                            animationAlpha);
                        ++interpolatedSkeletons;
                    }
                }
                object = NextObject(object);
            }
        }

        InterlockedExchange(
            &SpideyLastInterpolatedObjectCount,
            static_cast<LONG>(RestoreObjects.size()));
        InterlockedExchange(
            &SpideyLastInterpolatedSkeletonCount,
            interpolatedSkeletons);
    }

    void RestoreObjectState()
    {
        for (const ObjectState& state : RestoreObjects)
        {
            Position(state.object) = state.position;
            ObjectAngles(state.object) = state.angles;
            WriteAnimation(state.object, state.animation);
        }
        RestoreObjects.clear();
    }

    CameraState CaptureCamera()
    {
        const std::uintptr_t camera =
            *reinterpret_cast<std::uintptr_t*>(CameraListAddress);
        if (camera == 0)
        {
            return {};
        }
        return {camera, Position(camera), CameraRotation(camera)};
    }

    void ApplyCameraInterpolation(int alpha)
    {
        CameraWasApplied = false;
        if (!HaveCameraPair ||
            PreviousCamera.camera == 0 ||
            PreviousCamera.camera != CurrentCamera.camera)
        {
            return;
        }

        const std::uintptr_t camera = CurrentCamera.camera;
        RestoreCamera = CaptureCamera();
        Position(camera) =
        {
            Interpolate(
                PreviousCamera.position.x,
                CurrentCamera.position.x,
                alpha),
            Interpolate(
                PreviousCamera.position.y,
                CurrentCamera.position.y,
                alpha),
            Interpolate(
                PreviousCamera.position.z,
                CurrentCamera.position.z,
                alpha),
        };

        Quaternion previousRotation = PreviousCamera.rotation;
        reinterpret_cast<QuatSlerpFunction>(QuatSlerpAddress)(
            &previousRotation,
            &CurrentCamera.rotation,
            alpha,
            &CameraRotation(camera));
        reinterpret_cast<LoadCameraFunction>(LoadCameraAddress)(camera);
        CameraWasApplied = true;
    }

    void RestoreCameraState()
    {
        if (!CameraWasApplied ||
            RestoreCamera.camera == 0 ||
            *reinterpret_cast<std::uintptr_t*>(CameraListAddress) !=
                RestoreCamera.camera)
        {
            CameraWasApplied = false;
            return;
        }

        Position(RestoreCamera.camera) = RestoreCamera.position;
        CameraRotation(RestoreCamera.camera) = RestoreCamera.rotation;
        reinterpret_cast<LoadCameraFunction>(LoadCameraAddress)(
            RestoreCamera.camera);
        CameraWasApplied = false;
    }
}

extern "C" __declspec(dllexport) void FrameUpdateHook()
{
    if (!PresentationSettingsLoaded)
    {
        LoadPresentationSettings();
    }

    const LONG reason = Get20HzReason();
    const int desiredRate =
        reason != 0 ? ScriptedSimulationHz : NormalSimulationHz;
    const std::int64_t now = CounterNow();
    const std::int64_t interval = RateInterval(desiredRate);

    if (desiredRate != ActiveSimulationHz)
    {
        ActiveSimulationHz = desiredRate;
        LastSimulationCounter = 0;
        NextSimulationCounter = 0;
        ResetInterpolationSampling();
    }

    SimulatedThisFrame =
        interval == 0 ||
        NextSimulationCounter == 0 ||
        now >= NextSimulationCounter;
    InterlockedExchange(&Spidey20HzReason, reason);
    InterlockedExchange(&SpideySimulationRate, desiredRate);

    if (!SimulatedThisFrame)
    {
        return;
    }

    CaptureObjects(PreviousObjects);
    reinterpret_cast<VoidFunction>(FrameUpdateAddress)();
    CaptureObjects(CurrentObjects);
    HaveObjectPair = true;
    LastSimulationCounter = now;
    if (interval != 0)
    {
        if (NextSimulationCounter == 0 ||
            now - NextSimulationCounter > interval * 4)
        {
            NextSimulationCounter = now + interval;
        }
        else
        {
            do
            {
                NextSimulationCounter += interval;
            }
            while (NextSimulationCounter <= now);
        }
    }
    InterlockedIncrement(&SpideySimulationCallCount);
}

extern "C" __declspec(dllexport) void AudioUpdateHook()
{
    if (SimulatedThisFrame)
    {
        reinterpret_cast<VoidFunction>(AudioUpdateAddress)();
    }
}

extern "C" __declspec(dllexport) void CameraUpdateHook()
{
    if (SimulatedThisFrame)
    {
        PreviousCamera = CaptureCamera();
        reinterpret_cast<BodyListUpdateFunction>(BodyListUpdateAddress)(
            CameraListAddress,
            0);
        CurrentCamera = CaptureCamera();
        HaveCameraPair =
            PreviousCamera.camera != 0 &&
            PreviousCamera.camera == CurrentCamera.camera;
    }

    const std::int64_t now = CounterNow();
    const int simulationAlpha = CurrentSimulationAlpha(now);
    const int objectAlpha = SampleInterpolationAlpha(
        ObjectInterpolationHz,
        now,
        simulationAlpha,
        NextObjectInterpolationCounter,
        ObjectInterpolationAlpha);
    const int animationAlpha = SampleInterpolationAlpha(
        AnimationInterpolationHz,
        now,
        simulationAlpha,
        NextAnimationInterpolationCounter,
        AnimationInterpolationAlpha);
    const int cameraAlpha = SampleInterpolationAlpha(
        CameraInterpolationHz,
        now,
        simulationAlpha,
        NextCameraInterpolationCounter,
        CameraInterpolationAlpha);
    ApplyObjectInterpolation(objectAlpha, animationAlpha);
    ApplyCameraInterpolation(cameraAlpha);
    InterlockedIncrement(&SpideyInterpolatedRenderCount);
}

extern "C" __declspec(dllexport) void RenderHook()
{
    reinterpret_cast<VoidFunction>(RenderAddress)();
    RestoreObjectState();
    RestoreCameraState();
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        PreviousObjects.reserve(2048);
        CurrentObjects.reserve(2048);
        RestoreObjects.reserve(2048);
    }
    return TRUE;
}
