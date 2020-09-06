#include "stdafx.h"

#include "testclient.h"

#include "shell/shell.h"
#include "emulator/emulator.h"
#include "3rdparty/message-center/messagecenter.h"
#include <iostream>
#include <thread>

/// region <TestClient class>
TestClient::TestClient()
{
    _emulator = new Emulator();
}

TestClient::~TestClient()
{
    Stop();
}

void TestClient::Start()
{
    if (!_emulator->Init())
    {
        LOGERROR("Unable to initialize emulator. Exiting...");
        return;
    }

    /// region <Test messaging>
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    string topic("CPU_RESET");
    ObserverCallbackFunc callback = [=](int id, Message* message)
    {
        if (message != nullptr)
        {
            const char* text = (const char*)message->obj;
            LOGINFO("CPU was reset with message '%s'", text);
        }
        else
        {
            LOGINFO("CPU was reset");
        }
    };
    messageCenter.AddObserver(topic, callback);

    topic = "Reg_PC";
    callback = [=](int id, Message* message)
    {
        if (message != nullptr)
        {
            const char* text = (const char*)message->obj;
            LOGINFO("PC: 0x%s", text);
        }
        else
        {
            LOGINFO("PC: ??");
        }
    };
    messageCenter.AddObserver(topic, callback);


    /// endregion </Test messaging>

    _emulator->DebugOn();

/*
	Logger::Mute();
	_emulator->DebugOn();
	_emulator->RunNCPUCycles(3455994 + 100);
	Logger::Unmute();
    _emulator->RunNCPUCycles(500);

	return;
 */

    //endregion Test


    _emulator->Start();
}

void TestClient::Stop()
{
    try
    {
        if (_emulator != nullptr)
        {
            if (_emulator->IsRunning() && !_emulator->IsPaused())
            {
                _emulator->Stop();
            }

            _emulator->Release();
            _emulator = nullptr;
        }
    }
    catch (...)
    {
    }
}

Emulator* TestClient::GetEmulator()
{
    return _emulator;
}

/// endregion </TestClient class>

// Create client instance
TestClient client;
Emulator* g_emulator;

// Create shell instance
Shell shell;

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
    LOGEMPTY();
    LOGINFO("Stopping emulator...");
    client.Stop();
    exit(0);
}
#endif

void registerSignalHandler()
{
#ifdef _WIN32
    SetConsoleCtrlHandler(HandlerRoutine, TRUE);
#endif

#ifdef __linux__
    struct sigaction sigIntHandler;

    sigIntHandler.sa_handler = SignalHandler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);
#endif

#if __APPLE__
    struct sigaction sigIntHandler;

    sigIntHandler.sa_handler = SignalHandler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);
#endif
}

void createNamedPipe()
{
#if __APPLE__
    const char* PIPE_NAME = "/tmp/unreal_pipe";
    struct stat stat_path;
    stat(PIPE_NAME, &stat_path);

    if (!S_ISFIFO(stat_path.st_mode))
    {
        if (mkfifo(PIPE_NAME, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) == 0)
        {

        }
        else
        {
            cout << "Named pipe was not created" << endl;
        }
    }
    else
    {
        // remove(PIPE_NAME);
    }

    int out = open(PIPE_NAME, O_WRONLY);
    if (out != -1)
    {
        dup2(out, 1);
    }
    else
    {
        std::error_code errorCode = std::make_error_code(static_cast<std::errc>(errno));
        std::string errorCategory = errorCode.category().name();
        std::string errorMessage = errorCode.message();

        cout << "Unable to open named pipe: " << errorCategory << ": " << errorMessage << endl;
    }

#endif
}

/// endregion </Platform-dependent handlers>

void run()
{
    client.GetEmulator()->Init();

    client.Start();
}

void runAsShell()
{
    // Create additional pipe to redirect log messages
    //createNamedPipe();
    Logger::RedirectOutputToFile("unreal_log");

    // Share Emulator instance with all shell command handlers
    g_emulator = client.GetEmulator();

    std::thread t([]()
                  {
                      cout << "Client thread started" << endl;

                      client.Start();
                  });

    shell.Init();
    shell.Run();

    Logger::RestoreOutput();
}

int main()
{
    std::cout << "Unreal Speccy NG Emulator\n";

    // Register signal handler
    registerSignalHandler();

    run();
    // runAsShell();
}
