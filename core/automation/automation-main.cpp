#include <common/modulelogger.h>
#include <emulator/emulatorcontext.h>
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

int main()
{
    // Register signal handler
    registerSignalHandler();

    automation.start();

    Emulator emulator;
    bool result = emulator.Init();

    if (result)
    {
        emulator.GetContext()->pModuleLogger->SetLoggingLevel(LoggerLevel::LogWarning);

        emulator.Start();
    }
}
