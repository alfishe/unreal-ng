#include "platform/processinfo.h"

#include <cerrno>
#include <signal.h>
#include <unistd.h>

namespace platform
{
    uint32_t CurrentProcessId()
    {
        return static_cast<uint32_t>(getpid());
    }

    bool IsProcessAlive(uint32_t pid)
    {
        if (pid == 0)
            return false;
        // Signal 0 checks existence only; EPERM means it exists under another user
        return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
    }
}  // namespace platform
