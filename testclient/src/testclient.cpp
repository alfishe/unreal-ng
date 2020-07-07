#include "stdafx.h"

#include <iostream>
#include "testclient.h"
#include "../../core/src/emulator/emulator.h"
#include "../../core/src/3rdparty/message-center/messagecenter.h"

#if __linux__
#include <csignal>
#endif

#if __APPLE__
    #include <csignal>
#endif

TestClient client;

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

int main()
{
    std::cout << "Hello World!\n";

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

	client.Start();
}

TestClient::TestClient()
{
}

TestClient::~TestClient()
{
	Stop();
}

void TestClient::Start()
{
	if (_emulator != nullptr)
	{
		Stop();
	}

	_emulator = new Emulator();
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


/*
	Logger::Mute();
	_emulator->DebugOn();
	_emulator->RunNCPUCycles(3000000 - (4560 * 3) - 1600);
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

