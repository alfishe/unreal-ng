#include "platform/processinfo.h"

#include <cerrno>
#include <limits>
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
        // Above pid_t's range the cast turns negative, and kill() would ask after a process group (-1: every process)
        if (pid == 0 || pid > static_cast<uint32_t>(std::numeric_limits<pid_t>::max()))
            return false;
        // Signal 0 checks existence only; EPERM means it exists under another user
        return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
    }
}  // namespace platform
