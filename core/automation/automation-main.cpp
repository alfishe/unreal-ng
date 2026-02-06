#include <common/modulelogger.h>
#include <emulator/emulator.h>
#include "automation.h"

Automation automation;

/// region <Platform-dependent handlers>

#if __linux__
#include <csignal>
#endif

#if __APPLE__
#include <csignal>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#if _WIN32
BOOL WINAPI HandlerRoutine(DWORD dwCtrlType)
{
    LOGEMPTY();
    LOGINFO("Stopping emulator...");
    client.Stop();
    exit(0);
}
#endif

#if defined(__linux__) || defined(__APPLE__)
void SignalHandler(int signal)
{
    if (signal == SIGINT)
    {
        std::cout << "Received SIGINT (Ctrl+C)\n";
    }
    else if (signal == SIGTERM)
    {
        std::cout << "Received SIGTERM (Activity Monitor > Quit)\n";
    }

    LOGEMPTY();
    LOGINFO("Stopping automation...");
    automation.stop();
    exit(0);
}
#endif

void registerSignalHandler()
{
    std::signal(SIGINT, SignalHandler);   // Handle Ctrl+C
    std::signal(SIGTERM, SignalHandler);
}

/// endregion </Platform-dependent handlers>

int main(int argc, char *argv[])
{
    // Register signal handler
    registerSignalHandler();

    // Get automation singleton (created on first access)
    Automation& automation = Automation::GetInstance();

#ifdef ENABLE_AUTOMATION
    // Start all automation modules
    automation.start();

    // Keep main execution thread running until explicitly requested to stop (handle Ctrl+C gracefully)
    // Simplest implementation: infinite loop with a sleep to avoid CPU spinning
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Note: Singleton automatically destroyed on program exit
#endif

    return 0;
}
