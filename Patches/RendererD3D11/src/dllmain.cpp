#include "iat_hook.h"

#include <Windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        return Spidey::InstallDirectDrawHooks() ? TRUE : FALSE;
    }

    return TRUE;
}
