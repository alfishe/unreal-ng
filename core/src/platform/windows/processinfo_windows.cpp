#include "platform/processinfo.h"

#include <windows.h>

namespace platform
{
    uint32_t CurrentProcessId()
    {
        return static_cast<uint32_t>(GetCurrentProcessId());
    }

    bool IsProcessAlive(uint32_t pid)
    {
        if (pid == 0)
            return false;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
        if (!process)
            return GetLastError() == ERROR_ACCESS_DENIED;   // exists, owned by someone else
        DWORD code = 0;
        const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
        CloseHandle(process);
        return alive;
    }
}  // namespace platform
