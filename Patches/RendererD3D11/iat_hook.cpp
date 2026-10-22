#define NOMINMAX

#include "include/iat_hook.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

extern "C" HRESULT WINAPI SpideyDirectDrawCreateEx(GUID*, LPVOID*, REFIID, IUnknown*);
extern "C" HRESULT WINAPI SpideyDirectDrawEnumerateExA(LPVOID, LPVOID, DWORD);

namespace
{
    bool ReplaceImport(
        HMODULE module,
        const char* importedDll,
        const char* functionName,
        void* replacement)
    {
        auto* base = reinterpret_cast<std::uint8_t*>(module);
        auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return false;
        }

        auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        {
            return false;
        }

        const IMAGE_DATA_DIRECTORY& importDirectory =
            ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!importDirectory.VirtualAddress)
        {
            return false;
        }

        auto* descriptor =
            reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importDirectory.VirtualAddress);
        for (; descriptor->Name; ++descriptor)
        {
            const char* dllName = reinterpret_cast<const char*>(base + descriptor->Name);
            if (_stricmp(dllName, importedDll) != 0)
            {
                continue;
            }

            auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(
                base + descriptor->OriginalFirstThunk);
            auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(
                base + descriptor->FirstThunk);
            if (!descriptor->OriginalFirstThunk)
            {
                return false;
            }

            for (; names->u1.AddressOfData; ++names, ++addresses)
            {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                {
                    continue;
                }

                auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                    base + names->u1.AddressOfData);
                if (std::strcmp(reinterpret_cast<const char*>(import->Name), functionName) != 0)
                {
                    continue;
                }

                DWORD oldProtection = 0;
                if (!VirtualProtect(
                    &addresses->u1.Function,
                    sizeof(addresses->u1.Function),
                    PAGE_READWRITE,
                    &oldProtection))
                {
                    return false;
                }

                addresses->u1.Function =
                    reinterpret_cast<ULONG_PTR>(replacement);

                DWORD ignored = 0;
                VirtualProtect(
                    &addresses->u1.Function,
                    sizeof(addresses->u1.Function),
                    oldProtection,
                    &ignored);
                FlushInstructionCache(
                    GetCurrentProcess(),
                    &addresses->u1.Function,
                    sizeof(addresses->u1.Function));
                return true;
            }
        }

        return false;
    }
}

namespace Spidey
{
    bool InstallDirectDrawHooks()
    {
        static_assert(sizeof(void*) == 4, "Spider-Man renderer must be built for x86.");

        HMODULE executable = GetModuleHandleW(nullptr);
        const bool createHooked = ReplaceImport(
            executable,
            "ddraw.dll",
            "DirectDrawCreateEx",
            reinterpret_cast<void*>(&SpideyDirectDrawCreateEx));
        const bool enumerateHooked = ReplaceImport(
            executable,
            "ddraw.dll",
            "DirectDrawEnumerateExA",
            reinterpret_cast<void*>(&SpideyDirectDrawEnumerateExA));

        if (!createHooked || !enumerateHooked)
        {
            OutputDebugStringA("[SpideyRenderer] Failed to replace the DirectDraw imports.\n");
            return false;
        }

        OutputDebugStringA("[SpideyRenderer] DirectDraw imports replaced.\n");
        return true;
    }
}
