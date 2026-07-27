#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

extern "C"
{
    __declspec(dllexport) volatile LONG SpideyStoredSfxVolumeAdjustCount = 0;
    __declspec(dllexport) volatile LONG SpideyUpdatedSfxVolumeAdjustCount = 0;
    __declspec(dllexport) volatile LONG SpideyLastSfxAuthoredModifier = 0;
    __declspec(dllexport) volatile LONG SpideyLastSfxRawVolume = 0;
    __declspec(dllexport) volatile LONG SpideyLastSfxAdjustedVolume = 0;
}

namespace
{
    constexpr std::uintptr_t SfxEntriesAddress = 0x00618730;
    constexpr std::uintptr_t SfxVolumeCurveAddress = 0x0054F9F0;
    constexpr std::uintptr_t SetVolumeAddress = 0x005042F0;
    constexpr std::uintptr_t SetPanAddress = 0x00504350;
    constexpr int MaximumVolume = 0x3FFF;
    constexpr int AuthoredVolumeOne = 0x1000;

    struct SfxEntry
    {
        std::int32_t assetIndex;
        std::int32_t field04;
        std::int32_t field08;
        std::int32_t field0C;
        std::int32_t field10;
        std::uint16_t pitch;
        std::int16_t rawVolume;
        std::uint16_t authoredVolume;
        std::uint8_t active;
        std::uint8_t looping;
        std::int32_t field1C;
        std::int32_t convertedPitch;
        std::int32_t flags;
    };

    static_assert(sizeof(SfxEntry) == 0x28);
    static_assert(offsetof(SfxEntry, rawVolume) == 0x16);
    static_assert(offsetof(SfxEntry, authoredVolume) == 0x18);
    static_assert(offsetof(SfxEntry, active) == 0x1A);

    using SetSoundValueFunction = void(__cdecl*)(int, int);

    SfxEntry& Entry(int voice)
    {
        return reinterpret_cast<SfxEntry*>(SfxEntriesAddress)[voice];
    }

    int ClampRawVolume(int volume)
    {
        return std::clamp(volume, -MaximumVolume, MaximumVolume);
    }

    int ApplyAuthoredVolume(int rawVolume, std::uint16_t authoredVolume)
    {
        return ClampRawVolume(
            static_cast<int>(
                static_cast<std::int64_t>(rawVolume) * authoredVolume /
                AuthoredVolumeOne));
    }

    int ConvertRawVolume(int rawVolume)
    {
        int magnitude = rawVolume < 0 ? -rawVolume : rawVolume;
        const int curveIndex = std::min(magnitude >> 5, 255);
        const auto* curve =
            reinterpret_cast<const std::uint8_t*>(SfxVolumeCurveAddress);
        return 255 - curve[curveIndex];
    }

    void RecordAdjustment(int rawVolume, std::uint16_t modifier, int adjusted)
    {
        InterlockedExchange(
            &SpideyLastSfxAuthoredModifier,
            static_cast<LONG>(modifier));
        InterlockedExchange(
            &SpideyLastSfxRawVolume,
            static_cast<LONG>(rawVolume));
        InterlockedExchange(
            &SpideyLastSfxAdjustedVolume,
            static_cast<LONG>(adjusted));
    }

    void SetVolume(int voice, int convertedVolume)
    {
        reinterpret_cast<SetSoundValueFunction>(SetVolumeAddress)(
            voice,
            convertedVolume);
    }

    void SetPan(int voice, int pan)
    {
        reinterpret_cast<SetSoundValueFunction>(SetPanAddress)(voice, pan);
    }
}

void StoredSfxVolume(int voice)
{
    SfxEntry& entry = Entry(voice);
    const int adjusted =
        ApplyAuthoredVolume(entry.rawVolume, entry.authoredVolume);
    SetVolume(voice, ConvertRawVolume(adjusted));
    RecordAdjustment(entry.rawVolume, entry.authoredVolume, adjusted);
    InterlockedIncrement(&SpideyStoredSfxVolumeAdjustCount);
}

void ModifySfxVolume(
    std::uint32_t voiceMask,
    int left,
    int right)
{
    left = ClampRawVolume(left);
    right = ClampRawVolume(right);

    for (int voice = 0; voiceMask != 0; ++voice, voiceMask >>= 1)
    {
        if ((voiceMask & 1U) == 0)
        {
            continue;
        }

        SfxEntry& entry = Entry(voice);
        if (entry.active == 0)
        {
            continue;
        }

        const int combined = left + right;
        const int pan = left == right ? 15 : 31 * left / combined;
        SetPan(voice, pan);

        const int rawVolume = combined >> 1;
        entry.rawVolume = static_cast<std::int16_t>(rawVolume);
        const int adjusted =
            ApplyAuthoredVolume(rawVolume, entry.authoredVolume);
        SetVolume(voice, ConvertRawVolume(adjusted));
        RecordAdjustment(rawVolume, entry.authoredVolume, adjusted);
        InterlockedIncrement(&SpideyUpdatedSfxVolumeAdjustCount);
    }
}

// Detour wrappers enter through JMP after the game has pushed the original
// call arguments. The runtime then PUSHAD/PUSHFDs and CALLs this export.
// PUSHAD's saved ESP is therefore the original argument-list pointer.
extern "C" __declspec(dllexport, naked) void StoredSfxVolumeHook()
{
    __asm
    {
        mov eax, dword ptr [esp + 20]
        push dword ptr [eax]
        call StoredSfxVolume
        add esp, 4
        ret
    }
}

extern "C" __declspec(dllexport, naked) void ModifySfxVolumeHook()
{
    __asm
    {
        mov eax, dword ptr [esp + 20]
        push dword ptr [eax + 8]
        push dword ptr [eax + 4]
        push dword ptr [eax]
        call ModifySfxVolume
        add esp, 12
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
